/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *  Guolian Yun <yunguol@cn.ibm.com>
 *  Jay Gagnon <grendel@linux.vnet.ibm.com>
 *  Zhengang Li <lizg@cn.ibm.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#include <libcmpiutil/libcmpiutil.h>

#include "device_parsing.h"
#include "xmlgen.h"
#include "../src/svpc_types.h"

#define DISK_XPATH      (xmlChar *)"/domain/devices/disk"
#define VCPU_XPATH      (xmlChar *)"/domain/vcpu"
#define NET_XPATH       (xmlChar *)"/domain/devices/interface"
#define EMU_XPATH       (xmlChar *)"/domain/devices/emulator"
#define MEM_XPATH       (xmlChar *)"/domain/memory | /domain/currentMemory"
#define GRAPHICS_XPATH  (xmlChar *)"/domain/devices/graphics"

#define DEFAULT_BRIDGE "xenbr0"

#define XSTREQ(x, y) (STREQ((char *)x, y))
#define MAX(a,b) (((a)>(b))?(a):(b))

static void cleanup_disk_device(struct disk_device *dev)
{
        free(dev->type);
        free(dev->device);
        free(dev->driver);
        free(dev->source);
        free(dev->virtual_dev);
}

static void cleanup_net_device(struct net_device *dev)
{
        free(dev->type);
        free(dev->mac);
        free(dev->bridge);
}

static void cleanup_emu_device(struct emu_device *dev)
{
        free(dev->path);
}

static void cleanup_graphics_device(struct graphics_device *dev)
{
        free(dev->type);
        free(dev->port);
}

void cleanup_virt_device(struct virt_device *dev)
{
        if (dev == NULL)
                return; /* free()-like semantics */

        if (dev->type == VIRT_DEV_DISK)
                cleanup_disk_device(&dev->dev.disk);
        else if (dev->type == VIRT_DEV_NET)
                cleanup_net_device(&dev->dev.net);
        else if (dev->type == VIRT_DEV_EMU)
                cleanup_emu_device(&dev->dev.emu);
        else if (dev->type == VIRT_DEV_GRAPHICS)
                cleanup_graphics_device(&dev->dev.graphics);

        free(dev->id);

        memset(&dev->dev, 0, sizeof(dev->dev));
}

void cleanup_virt_devices(struct virt_device **_devs, int count)
{
        int i;
        struct virt_device *devs = *_devs;

        for (i = 0; i < count; i++)
                cleanup_virt_device(&devs[i]);

        free(devs);
        *_devs = NULL;
}

static char *get_attr_value(xmlNode *node, char *attrname)
{
        char *buf = NULL;
        char *ret = NULL;

        buf = (char *)xmlGetProp(node, (xmlChar *)attrname);
        if (buf) {
                ret = strdup(buf);
                xmlFree(buf);
        }

        return ret;
}

static char *get_node_content(xmlNode *node)
{
        char *buf = NULL;
        xmlChar *ret = NULL;

        ret = xmlNodeGetContent(node);
        if (ret) {
                buf = strdup((char *)ret);
                xmlFree(ret);
        }

        return buf;
}

static int parse_disk_device(xmlNode *dnode, struct virt_device **vdevs)
{
        struct virt_device *vdev = NULL;
        struct disk_device *ddev = NULL;
        xmlNode * child = NULL;

        vdev = calloc(1, sizeof(*vdev));
        if (vdev == NULL)
                goto err;

        ddev = &(vdev->dev.disk);

        ddev->type = get_attr_value(dnode, "type");
        if (ddev->type == NULL)
                goto err;

        ddev->device = get_attr_value(dnode, "device");
        if (ddev->device == NULL)
                goto err;

        for (child = dnode->children; child != NULL; child = child->next) {
                if (XSTREQ(child->name, "driver")) {
                        ddev->driver = get_attr_value(child, "name");
                        if (ddev->driver == NULL)
                                goto err;
                } else if (XSTREQ(child->name, "source")) {
                        ddev->source = get_attr_value(child, "file");
                        if (ddev->source) {
                                ddev->disk_type = DISK_FILE;
                                continue;
                        }
                        ddev->source = get_attr_value(child, "dev");
                        if (ddev->source) {
                                ddev->disk_type = DISK_PHY;
                                continue;
                        }
                        goto err;
                } else if (XSTREQ(child->name, "target")) {
                        ddev->virtual_dev = get_attr_value(child, "dev");
                        if (ddev->virtual_dev == NULL)
                                goto err;
                }
        }
        if ((ddev->source == NULL) || (ddev->virtual_dev == NULL))
                goto err;

        vdev->type = VIRT_DEV_DISK;
        vdev->id = strdup(ddev->virtual_dev);

        *vdevs = vdev;

        return 1;

 err:
        cleanup_disk_device(ddev);
        free(vdev);

        return 0;
}

static int parse_net_device(xmlNode *inode, struct virt_device **vdevs)
{
        struct virt_device *vdev = NULL;
        struct net_device *ndev = NULL;
        xmlNode *child = NULL;

        vdev = calloc(1, sizeof(*vdev));
        if (vdev == NULL)
                goto err;

        ndev = &(vdev->dev.net);

        ndev->type = get_attr_value(inode, "type");
        if (ndev->type == NULL)
                goto err;

        for (child = inode->children; child != NULL; child = child->next) {
                if (XSTREQ(child->name, "mac")) {
                        ndev->mac = get_attr_value(child, "address");
                        if (ndev->mac == NULL)
                                goto err;
                } else if (XSTREQ(child->name, "source")) {
                        ndev->bridge = get_attr_value(child, "bridge");
                        if (ndev->bridge == NULL)
                                goto err;
                }
        }

        if (ndev->mac == NULL)
                goto err;

        if (ndev->bridge == NULL) {
                ndev->bridge = strdup(DEFAULT_BRIDGE);
                printf("No bridge, taking default of `%s'\n", ndev->bridge);
        }

        vdev->type = VIRT_DEV_NET;
        vdev->id = strdup(ndev->mac);

        *vdevs = vdev;

        return 1;
  err:
        cleanup_net_device(ndev);
        free(vdev);

        return 0;
}

static int parse_vcpu_device(xmlNode *node, struct virt_device **vdevs)
{
        struct virt_device *list = NULL;
        char *count_str;
        int count;
        int i;

        count_str = get_node_content(node);
        if (count_str == NULL)
                count = 1; /* Default to 1 VCPU if non specified */
        else if (sscanf(count_str, "%i", &count) != 1)
                count = 1; /* Default to 1 VCPU if garbage */

        free(count_str);

        list = calloc(count, sizeof(*list));
        if (list == NULL)
                goto err;

        for (i = 0; i < count; i++) {
                struct virt_device *vdev = &list[i];
                struct vcpu_device *cdev = &vdev->dev.vcpu;

                cdev->number = i;

                vdev->type = VIRT_DEV_VCPU;
                if (asprintf(&vdev->id, "%i", i) == -1)
                        vdev->id = NULL;
        }

        *vdevs = list;

        return count;
 err:
        free(list);

        return 0;
}

static int parse_emu_device(xmlNode *node, struct virt_device **vdevs)
{
        struct virt_device *vdev = NULL;
        struct emu_device *edev = NULL;

        vdev = calloc(1, sizeof(*vdev));
        if (vdev == NULL)
                goto err;

        edev = &(vdev->dev.emu);

        edev->path = get_node_content(node);
        if (edev->path != NULL)
                goto err;

        vdev->type = VIRT_DEV_EMU;

        *vdevs = vdev;

        return 1;
 err:
        cleanup_emu_device(edev);
        free(vdev);

        return 0;
}

static int parse_mem_device(xmlNode *node, struct virt_device **vdevs)
{
        struct virt_device *vdev = NULL;
        struct mem_device *mdev = NULL;
        char *content = NULL;

        vdev = calloc(1, sizeof(*vdev));
        if (vdev == NULL)
                goto err;

        mdev = &(vdev->dev.mem);

        content = get_node_content(node);

        if (XSTREQ(node->name, "memory"))
                sscanf(content, "%" PRIu64, &mdev->size);
        else if (XSTREQ(node->name, "currentMemory"))
                sscanf(content, "%" PRIu64, &mdev->maxsize);

        free(content);

        *vdevs = vdev;

        return 1;

 err:
        free(content);
        free(vdev);

        return 0;
}

static int parse_graphics_device(xmlNode *node, struct virt_device **vdevs)
{
        struct virt_device *vdev = NULL;
        struct graphics_device *gdev = NULL;

        vdev = calloc(1, sizeof(*vdev));
        if (vdev == NULL)
                goto err;

        gdev = &(vdev->dev.graphics);

        gdev->type = get_attr_value(node, "type");
        gdev->port = get_attr_value(node, "port");

        if ((gdev->type == NULL) || (gdev->port == NULL))
                goto err;

        vdev->type = VIRT_DEV_GRAPHICS;

        *vdevs = vdev;

        return 1;
 err:
        cleanup_graphics_device(gdev);
        free(vdev);

        return 0;
}

static bool resize_devlist(struct virt_device **list, int newsize)
{
        struct virt_device *_list;

        _list = realloc(*list, newsize * sizeof(struct virt_device));
        if (_list == NULL)
                return false;

        *list = _list;

        return true;
}

static int do_parse(xmlNodeSet *nsv, int type, struct virt_device **l)
{
        int devidx;
        int lstidx = 0;
        int count = 0;
        struct virt_device *list = NULL;
        xmlNode **dev_nodes = NULL;
        int (*do_real_parse)(xmlNode *, struct virt_device **) = NULL;

        /* point to correct parser function according to type */
        if (type == VIRT_DEV_NET)
                do_real_parse = &parse_net_device;
        else if (type == VIRT_DEV_DISK)
                do_real_parse = &parse_disk_device;
        else if (type == VIRT_DEV_VCPU)
                do_real_parse = parse_vcpu_device;
        else if (type == VIRT_DEV_EMU)
                do_real_parse = parse_emu_device;
        else if (type == VIRT_DEV_MEM)
                do_real_parse = parse_mem_device;
        else if (type == VIRT_DEV_GRAPHICS)
                do_real_parse = parse_graphics_device;
        else
                goto out;

        if (nsv == NULL)
                goto out;

        dev_nodes = nsv->nodeTab;
        count = nsv ? nsv->nodeNr : 0;

        if (count <= 0)
                goto out;

        /* walk thru the array, do real parsing on each node */
        for (devidx = 0; devidx < count; devidx++) {
                struct virt_device *tmp_list = NULL;
                int devices = 0;

                devices = do_real_parse(dev_nodes[devidx], &tmp_list);
                if (devices <= 0)
                        continue;

                if (!resize_devlist(&list, lstidx + devices)) {
                        /* Skip these devices and try again for the
                         * next cycle, which will probably fail, but
                         * what else can you do?
                         */
                        goto end;
                }

                memcpy(&list[lstidx], tmp_list, devices * sizeof(*tmp_list));
                lstidx += devices;
        end:
                free(tmp_list);
        }

  out:
        *l = list;
        return lstidx;
}

/* Dummy function to suppress error message from libxml2 */
static void swallow_err_msg(void *ctx, const char *msg, ...)
{
        /* do nothing, just swallow the message. */
}

static int parse_devices(const char *xml, struct virt_device **_list, int type)
{
        int len = 0;
        int count = 0;

        xmlDoc *xmldoc;
        xmlXPathContext *xpathCtx;
        xmlXPathObject *xpathObj;
        xmlChar *xpathstr;

        if (type == VIRT_DEV_NET)
                xpathstr = NET_XPATH;
        else if (type == VIRT_DEV_DISK)
                xpathstr = DISK_XPATH;
        else if (type == VIRT_DEV_VCPU)
                xpathstr = VCPU_XPATH;
        else if (type == VIRT_DEV_EMU)
                xpathstr = EMU_XPATH;
        else if (type == VIRT_DEV_MEM)
                xpathstr = MEM_XPATH;
        else if (type == VIRT_DEV_GRAPHICS)
                xpathstr = GRAPHICS_XPATH;
        else
                goto err1;

        len = strlen(xml) + 1;

        xmlSetGenericErrorFunc(NULL, swallow_err_msg);
        if ((xmldoc = xmlParseMemory(xml, len)) == NULL)
                goto err1;

        if ((xpathCtx = xmlXPathNewContext(xmldoc)) == NULL)
                goto err2;

        if ((xpathObj = xmlXPathEvalExpression(xpathstr, xpathCtx))
                        == NULL)
                goto err3;

        count = do_parse(xpathObj->nodesetval, type, _list);

        xmlSetGenericErrorFunc(NULL, NULL);
        xmlXPathFreeObject(xpathObj);
  err3:
        xmlXPathFreeContext(xpathCtx);
  err2:
        xmlFreeDoc(xmldoc);
  err1:
        return count;
}

#define DUP_FIELD(d, s, f) ((d)->f = strdup((s)->f))

struct virt_device *virt_device_dup(struct virt_device *_dev)
{
        struct virt_device *dev;

        dev = malloc(sizeof(*dev));
        if (dev == NULL)
                return NULL;

        dev->type = _dev->type;
        dev->id = strdup(_dev->id);

        if (dev->type == VIRT_DEV_NET) {
                DUP_FIELD(dev, _dev, dev.net.mac);
                DUP_FIELD(dev, _dev, dev.net.type);
                DUP_FIELD(dev, _dev, dev.net.bridge);
        } else if (dev->type == VIRT_DEV_DISK) {
                DUP_FIELD(dev, _dev, dev.disk.type);
                DUP_FIELD(dev, _dev, dev.disk.device);
                DUP_FIELD(dev, _dev, dev.disk.driver);
                DUP_FIELD(dev, _dev, dev.disk.source);
                DUP_FIELD(dev, _dev, dev.disk.virtual_dev);
        } else if (dev->type == VIRT_DEV_MEM) {
                dev->dev.mem.size = _dev->dev.mem.size;
                dev->dev.mem.maxsize = _dev->dev.mem.maxsize;
        } else if (dev->type == VIRT_DEV_VCPU) {
                dev->dev.vcpu.number = _dev->dev.vcpu.number;
        } else if (dev->type == VIRT_DEV_EMU) {
                DUP_FIELD(dev, _dev, dev.emu.path);
        } else if (dev->type == VIRT_DEV_GRAPHICS) {
                DUP_FIELD(dev, _dev, dev.graphics.type);
                DUP_FIELD(dev, _dev, dev.graphics.port);
        }

        return dev;
}

static int _get_mem_device(const char *xml, struct virt_device **list)
{
        struct virt_device *mdevs = NULL;
        struct virt_device *mdev = NULL;
        int ret;

        ret = parse_devices(xml, &mdevs, VIRT_DEV_MEM);
        if (ret <= 0)
                return ret;

        mdev = malloc(sizeof(*mdev));
        if (mdev == NULL)
                return 0;

        memset(mdev, 0, sizeof(*mdev));

        /* We could get one or two memory devices back, depending on
         * if there is a currentMemory tag or not.  Coalesce these
         * into a single device to return
         */

        if (ret == 2) {
                mdev->dev.mem.size = MAX(mdevs[0].dev.mem.size,
                                         mdevs[1].dev.mem.size);
                mdev->dev.mem.maxsize = MAX(mdevs[0].dev.mem.maxsize,
                                            mdevs[1].dev.mem.maxsize);
        } else {
                mdev->dev.mem.size = MAX(mdevs[0].dev.mem.size,
                                         mdevs[0].dev.mem.maxsize);
                mdev->dev.mem.maxsize = mdev->dev.mem.size;
        }

        mdev->type = VIRT_DEV_MEM;
        mdev->id = strdup("mem");
        *list = mdev;

        cleanup_virt_devices(&mdevs, ret);

        return 1;
}

int get_devices(virDomainPtr dom, struct virt_device **list, int type)
{
        char *xml;
        int ret;

        xml = virDomainGetXMLDesc(dom, 0);
        if (xml == NULL)
                return 0;

        if (type == VIRT_DEV_MEM)
                ret = _get_mem_device(xml, list);
        else
                ret = parse_devices(xml, list, type);

        free(xml);

        return ret;
}

char *get_fq_devid(char *host, char *_devid)
{
        char *devid;

        if (asprintf(&devid, "%s/%s", host, _devid) == -1)
                return NULL;
        else
                return devid;
}

int parse_fq_devid(const char *devid, char **host, char **device)
{
        int ret;

        ret = sscanf(devid, "%a[^/]/%as", host, device);
        if (ret != 2) {
                free(*host);
                free(*device);

                *host = NULL;
                *device = NULL;

                return 0;
        }

        return 1;
}

#define STRPROP(d, p, n) (d->p = get_node_content(n))

static int parse_os(struct domain *dominfo, xmlNode *os)
{
        xmlNode *child;

        for (child = os->children; child != NULL; child = child->next) {
                if (XSTREQ(child->name, "type"))
                        STRPROP(dominfo, os_info.pv.type, child);
                else if (XSTREQ(child->name, "kernel"))
                        STRPROP(dominfo, os_info.pv.kernel, child);
                else if (XSTREQ(child->name, "initrd"))
                        STRPROP(dominfo, os_info.pv.initrd, child);
                else if (XSTREQ(child->name, "cmdline"))
                        STRPROP(dominfo, os_info.pv.cmdline, child);
                else if (XSTREQ(child->name, "loader"))
                        STRPROP(dominfo, os_info.fv.loader, child);
                else if (XSTREQ(child->name, "boot"))
                        dominfo->os_info.fv.boot = get_attr_value(child,
                                                                     "dev");
        }

        if ((STREQC(dominfo->os_info.fv.type, "hvm")) &&
            (STREQC(dominfo->typestr, "xen")))
                dominfo->type = DOMAIN_XENFV;
        else if (STREQC(dominfo->typestr, "kvm"))
                dominfo->type = DOMAIN_KVM;
        else if (STREQC(dominfo->os_info.pv.type, "linux"))
                dominfo->type = DOMAIN_XENPV;
        else
                dominfo->type = -1;

        return 1;
}

static void set_action(int *val, xmlNode *child)
{
        const char *action = (char *)xmlNodeGetContent(child);

        if (action == NULL)
                *val = CIM_VSSD_RECOVERY_NONE;
        else if (STREQ(action, "destroy"))
                *val = CIM_VSSD_RECOVERY_NONE;
        else if (STREQ(action, "preserve"))
                *val = CIM_VSSD_RECOVERY_PRESERVE;
        else if (STREQ(action, "restart"))
                *val = CIM_VSSD_RECOVERY_RESTART;
        else
                *val = CIM_VSSD_RECOVERY_NONE;
}

static int parse_domain(xmlNodeSet *nsv, struct domain *dominfo)
{
        xmlNode **nodes = nsv->nodeTab;
        xmlNode *child;

        memset(dominfo, 0, sizeof(*dominfo));

        dominfo->typestr = get_attr_value(nodes[0], "type");

        for (child = nodes[0]->children; child != NULL; child = child->next) {
                if (XSTREQ(child->name, "name"))
                        STRPROP(dominfo, name, child);
                else if (XSTREQ(child->name, "uuid"))
                        STRPROP(dominfo, uuid, child);
                else if (XSTREQ(child->name, "bootloader"))
                        STRPROP(dominfo, bootloader, child);
                else if (XSTREQ(child->name, "bootloader_args"))
                        STRPROP(dominfo, bootloader_args, child);
                else if (XSTREQ(child->name, "os"))
                        parse_os(dominfo, child);
                else if (XSTREQ(child->name, "on_poweroff"))
                        set_action(&dominfo->on_poweroff, child);
                else if (XSTREQ(child->name, "on_reboot"))
                        set_action(&dominfo->on_reboot, child);
                else if (XSTREQ(child->name, "on_crash"))
                        set_action(&dominfo->on_crash, child);
        }

        return 1;
}

static int _get_dominfo(const char *xml, struct domain *dominfo)
{
        int len;
        int ret = 0;

        xmlDoc *xmldoc;
        xmlXPathContext *xpathctx;
        xmlXPathObject *xpathobj;
        const xmlChar *xpathstr = (xmlChar *)"/domain";

        len = strlen(xml) + 1;

        if ((xmldoc = xmlParseMemory(xml, len)) == NULL)
                goto err1;

        if ((xpathctx = xmlXPathNewContext(xmldoc)) == NULL)
                goto err2;

        if ((xpathobj = xmlXPathEvalExpression(xpathstr, xpathctx)) == NULL)
                goto err3;

        ret = parse_domain(xpathobj->nodesetval, dominfo);

        xmlXPathFreeObject(xpathobj);
 err3:
        xmlXPathFreeContext(xpathctx);
 err2:
        xmlFreeDoc(xmldoc);
 err1:
        return ret;
}

int get_dominfo_from_xml(const char *xml, struct domain **dominfo)
{
        int ret;

        *dominfo = malloc(sizeof(**dominfo));
        if (*dominfo == NULL)
                return 0;

        ret = _get_dominfo(xml, *dominfo);
        if (ret == 0)
                goto err;

        parse_devices(xml, &(*dominfo)->dev_emu, VIRT_DEV_EMU);
        parse_devices(xml, &(*dominfo)->dev_graphics, VIRT_DEV_GRAPHICS);

        (*dominfo)->dev_mem_ct = _get_mem_device(xml, &(*dominfo)->dev_mem);
        (*dominfo)->dev_net_ct = parse_devices(xml,
                                               &(*dominfo)->dev_net,
                                               VIRT_DEV_NET);
        (*dominfo)->dev_disk_ct = parse_devices(xml,
                                                &(*dominfo)->dev_disk,
                                                VIRT_DEV_DISK);
        (*dominfo)->dev_vcpu_ct = parse_devices(xml,
                                                &(*dominfo)->dev_vcpu,
                                                VIRT_DEV_VCPU);

        return ret;

 err:
        free(*dominfo);
        *dominfo = NULL;

        return 0;
}

int get_dominfo(virDomainPtr dom, struct domain **dominfo)
{
        char *xml;
        int ret;

        xml = virDomainGetXMLDesc(dom, 0);
        if (xml == NULL)
                return 0;

        ret = get_dominfo_from_xml(xml, dominfo);

        free(xml);

        return ret;
}

void cleanup_dominfo(struct domain **dominfo)
{
        struct domain *dom = *dominfo;

        if ((dominfo == NULL) || (*dominfo == NULL))
                return;

        free(dom->name);
        free(dom->uuid);
        free(dom->bootloader);
        free(dom->bootloader_args);

        if (dom->type == DOMAIN_XENPV) {
                free(dom->os_info.pv.type);
                free(dom->os_info.pv.kernel);
                free(dom->os_info.pv.initrd);
                free(dom->os_info.pv.cmdline);
        } else if ((dom->type == DOMAIN_XENFV) ||
                   (dom->type == DOMAIN_KVM)) {
                free(dom->os_info.fv.type);
                free(dom->os_info.fv.loader);
                free(dom->os_info.fv.boot);
        } else {
                CU_DEBUG("Unknown domain type %i", dom->type);
        }

        cleanup_virt_devices(&dom->dev_mem, dom->dev_mem_ct);
        cleanup_virt_devices(&dom->dev_net, dom->dev_net_ct);
        cleanup_virt_devices(&dom->dev_disk, dom->dev_disk_ct);
        cleanup_virt_devices(&dom->dev_vcpu, dom->dev_vcpu_ct);

        free(dom);

        *dominfo = NULL;
}

static int _change_device(virDomainPtr dom,
                          struct virt_device *dev,
                          bool attach)
{
        char *xml = NULL;
        int ret = 0;
        int (*func)(virDomainPtr, char *);

        if (attach)
                func = virDomainAttachDevice;
        else
                func = virDomainDetachDevice;

        xml = device_to_xml(dev);
        if (xml == NULL) {
                CU_DEBUG("Failed to get XML for device `%s'", dev->id);
                goto out;
        }

        if (func(dom, xml) != 0) {
                CU_DEBUG("Failed to dynamically change device:");
                CU_DEBUG("%s", xml);
                goto out;
        }

        ret = 1;
 out:
        free(xml);

        return ret;

}

static int change_memory(virDomainPtr dom,
                         struct virt_device *dev)
{
        CU_DEBUG("Changing memory of %s to %llu/%llu",
                 virDomainGetName(dom),
                 dev->dev.mem.size,
                 dev->dev.mem.maxsize);

        if (virDomainSetMemory(dom, dev->dev.mem.size))
                return 0;

        if (virDomainSetMaxMemory(dom, dev->dev.mem.maxsize))
                return 0;

        return 1;
}

int attach_device(virDomainPtr dom, struct virt_device *dev)
{
        if ((dev->type == VIRT_DEV_NET) ||
            (dev->type == VIRT_DEV_DISK))
                return _change_device(dom, dev, true);

        CU_DEBUG("Unhandled device type %i", dev->type);

        return 0;
}

int detach_device(virDomainPtr dom, struct virt_device *dev)
{
        if ((dev->type == VIRT_DEV_NET) ||
            (dev->type == VIRT_DEV_DISK))
                return _change_device(dom, dev, false);

        CU_DEBUG("Unhandled device type %i", dev->type);

        return 0;
}

int change_device(virDomainPtr dom, struct virt_device *dev)
{
        if (dev->type == VIRT_DEV_MEM)
                return change_memory(dom, dev);

        CU_DEBUG("Unhandled device type %i", dev->type);

        return 0;
}

int disk_type_from_file(const char *path)
{
        struct stat s;

        if (stat(path, &s) < 0)
                return DISK_UNKNOWN;

        if (S_ISBLK(s.st_mode))
                return DISK_PHY;
        else if (S_ISREG(s.st_mode))
                return DISK_FILE;
        else
                return DISK_UNKNOWN;
}

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
