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

#define DISK_XPATH      (xmlChar *)"/domain/devices/disk | "\
        "/domain/devices/filesystem"
#define VCPU_XPATH      (xmlChar *)"/domain/vcpu"
#define NET_XPATH       (xmlChar *)"/domain/devices/interface"
#define EMU_XPATH       (xmlChar *)"/domain/devices/emulator"
#define MEM_XPATH       (xmlChar *)"/domain/memory | /domain/currentMemory"
#define GRAPHICS_XPATH  (xmlChar *)"/domain/devices/graphics"

#define DEFAULT_BRIDGE "xenbr0"
#define DEFAULT_NETWORK "default"

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
        free(dev->source);
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

        if (dev->type == CIM_RES_TYPE_DISK)
                cleanup_disk_device(&dev->dev.disk);
        else if (dev->type == CIM_RES_TYPE_NET)
                cleanup_net_device(&dev->dev.net);
        else if (dev->type == CIM_RES_TYPE_EMU)
                cleanup_emu_device(&dev->dev.emu);
        else if (dev->type == CIM_RES_TYPE_GRAPHICS)
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

static int parse_fs_device(xmlNode *dnode, struct virt_device **vdevs)
{
        struct virt_device *vdev = NULL;
        struct disk_device *ddev = NULL;
        xmlNode *child = NULL;

        vdev = calloc(1, sizeof(*vdev));
        if (vdev == NULL)
                goto err;

        ddev = (&vdev->dev.disk);

        ddev->type = get_attr_value(dnode, "type");
        if (ddev->type == NULL) {
                CU_DEBUG("No type");
                goto err;
        }

        for (child = dnode->children; child != NULL; child = child->next) {
                if (XSTREQ(child->name, "source")) {
                        ddev->source = get_attr_value(child, "dir");
                        if (ddev->source == NULL) {
                                CU_DEBUG("No source dir");
                                goto err;
                        }
                } else if (XSTREQ(child->name, "target")) {
                        ddev->virtual_dev = get_attr_value(child, "dir");
                        if (ddev->virtual_dev == NULL) {
                                CU_DEBUG("No target dir");
                                goto err;
                        }
                }
        }

        if ((ddev->source == NULL) || (ddev->virtual_dev == NULL)) {
                CU_DEBUG("S: %s D: %s", ddev->source, ddev->virtual_dev);
                goto err;
        }

        ddev->disk_type = DISK_FS;

        vdev->type = CIM_RES_TYPE_DISK;
        vdev->id = strdup(ddev->virtual_dev);

        *vdevs = vdev;

        return 1;

 err:
        CU_DEBUG("Error parsing fs");
        cleanup_disk_device(ddev);
        free(vdev);

        return 0;
}

static int parse_block_device(xmlNode *dnode, struct virt_device **vdevs)
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

        vdev->type = CIM_RES_TYPE_DISK;
        vdev->id = strdup(ddev->virtual_dev);

        *vdevs = vdev;

        return 1;

 err:
        cleanup_disk_device(ddev);
        free(vdev);

        return 0;
}

static int parse_disk_device(xmlNode *dnode, struct virt_device **vdevs)
{
        CU_DEBUG("Disk node: %s", dnode->name);

        if (XSTREQ(dnode->name, "disk"))
                return parse_block_device(dnode, vdevs);
        else if (XSTREQ(dnode->name, "filesystem"))
                return parse_fs_device(dnode, vdevs);
        else {
                CU_DEBUG("Unknown disk device: %s", dnode->name);
                return 0;
        }
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
                        ndev->source = get_attr_value(child, "bridge");
                        if (ndev->source != NULL)
                                continue;
                        ndev->source = get_attr_value(child, "network");
                        if (ndev->source != NULL)
                                continue;
                        goto err;
                }
        }

        if (ndev->mac == NULL)
                goto err;

        if (ndev->source == NULL) {
                if (STREQC(ndev->type, "bridge")) {
                        ndev->source = strdup(DEFAULT_BRIDGE);
                        CU_DEBUG("No bridge, taking default of `%s'\n",
                                 ndev->source);
                } else if (STREQC(ndev->type, "network")) {
                        ndev->source = strdup(DEFAULT_NETWORK);
                        CU_DEBUG("No network, taking default of `%s'\n",
                                 ndev->source);
                } else {
                        /* This likely indicates an unsupported
                         * network configuration
                         */
                        CU_DEBUG("No network source, and no known default");
                        goto err;
                }
        }

        vdev->type = CIM_RES_TYPE_NET;
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

        count_str = get_node_content(node);
        if (count_str == NULL)
                count = 1; /* Default to 1 VCPU if non specified */
        else if (sscanf(count_str, "%i", &count) != 1)
                count = 1; /* Default to 1 VCPU if garbage */

        free(count_str);

        list = calloc(1, sizeof(*list));
        if (list == NULL)
                goto err;
        
        list->dev.vcpu.quantity = count;

        list->type = CIM_RES_TYPE_PROC;
        list->id = strdup("proc");

        *vdevs = list;

        return 1;
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

        vdev->type = CIM_RES_TYPE_EMU;

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

        vdev->type = CIM_RES_TYPE_GRAPHICS;

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
        if (type == CIM_RES_TYPE_NET)
                do_real_parse = &parse_net_device;
        else if (type == CIM_RES_TYPE_DISK)
                do_real_parse = &parse_disk_device;
        else if (type == CIM_RES_TYPE_PROC)
                do_real_parse = parse_vcpu_device;
        else if (type == CIM_RES_TYPE_EMU)
                do_real_parse = parse_emu_device;
        else if (type == CIM_RES_TYPE_MEM)
                do_real_parse = parse_mem_device;
        else if (type == CIM_RES_TYPE_GRAPHICS)
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

        if (type == CIM_RES_TYPE_NET)
                xpathstr = NET_XPATH;
        else if (type == CIM_RES_TYPE_DISK)
                xpathstr = DISK_XPATH;
        else if (type == CIM_RES_TYPE_PROC)
                xpathstr = VCPU_XPATH;
        else if (type == CIM_RES_TYPE_EMU)
                xpathstr = EMU_XPATH;
        else if (type == CIM_RES_TYPE_MEM)
                xpathstr = MEM_XPATH;
        else if (type == CIM_RES_TYPE_GRAPHICS)
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

#define DUP_FIELD(d, s, f) do {                         \
                if ((s)->f != NULL)                     \
                        (d)->f = strdup((s)->f);        \
        } while (0);

struct virt_device *virt_device_dup(struct virt_device *_dev)
{
        struct virt_device *dev;

        dev = calloc(1, sizeof(*dev));
        if (dev == NULL)
                return NULL;

        dev->type = _dev->type;
        dev->id = strdup(_dev->id);

        if (dev->type == CIM_RES_TYPE_NET) {
                DUP_FIELD(dev, _dev, dev.net.mac);
                DUP_FIELD(dev, _dev, dev.net.type);
                DUP_FIELD(dev, _dev, dev.net.source);
        } else if (dev->type == CIM_RES_TYPE_DISK) {
                DUP_FIELD(dev, _dev, dev.disk.type);
                DUP_FIELD(dev, _dev, dev.disk.device);
                DUP_FIELD(dev, _dev, dev.disk.driver);
                DUP_FIELD(dev, _dev, dev.disk.source);
                DUP_FIELD(dev, _dev, dev.disk.virtual_dev);
        } else if (dev->type == CIM_RES_TYPE_MEM) {
                dev->dev.mem.size = _dev->dev.mem.size;
                dev->dev.mem.maxsize = _dev->dev.mem.maxsize;
        } else if (dev->type == CIM_RES_TYPE_PROC) {
                dev->dev.vcpu.quantity = _dev->dev.vcpu.quantity;
        } else if (dev->type == CIM_RES_TYPE_EMU) {
                DUP_FIELD(dev, _dev, dev.emu.path);
        } else if (dev->type == CIM_RES_TYPE_GRAPHICS) {
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

        ret = parse_devices(xml, &mdevs, CIM_RES_TYPE_MEM);
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

        mdev->type = CIM_RES_TYPE_MEM;
        mdev->id = strdup("mem");
        *list = mdev;

        cleanup_virt_devices(&mdevs, ret);

        return 1;
}

static int _get_proc_device(const char *xml, struct virt_device **list)
{
        struct virt_device *proc_devs = NULL;
        struct virt_device *proc_dev = NULL;
        int ret;

        ret = parse_devices(xml, &proc_devs, CIM_RES_TYPE_PROC);
        if (ret <= 0)
                return ret;

        proc_dev = malloc(sizeof(*proc_dev));
        if (proc_dev == NULL)
                return 0;

        memset(proc_dev, 0, sizeof(*proc_dev));

        proc_dev->type = CIM_RES_TYPE_PROC;
        proc_dev->id = strdup("proc");
        proc_dev->dev.vcpu.quantity = proc_devs[0].dev.vcpu.quantity;
        *list = proc_dev;

        cleanup_virt_devices(&proc_devs, ret);

        return 1;
};

int get_devices(virDomainPtr dom, struct virt_device **list, int type)
{
        char *xml;
        int ret;

        xml = virDomainGetXMLDesc(dom, 0);
        if (xml == NULL)
                return 0;

        if (type == CIM_RES_TYPE_MEM)
                ret = _get_mem_device(xml, list);
        else if (type == CIM_RES_TYPE_PROC)
                ret = _get_proc_device(xml, list);
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
                else if (XSTREQ(child->name, "init"))
                        STRPROP(dominfo, os_info.lxc.init, child);
        }

        if ((STREQC(dominfo->os_info.fv.type, "hvm")) &&
            (STREQC(dominfo->typestr, "xen")))
                dominfo->type = DOMAIN_XENFV;
        else if ((STREQC(dominfo->typestr, "kvm")) ||
                 (STREQC(dominfo->typestr, "qemu")))
                dominfo->type = DOMAIN_KVM;
        else if (STREQC(dominfo->typestr, "lxc"))
                dominfo->type = DOMAIN_LXC;
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

        parse_devices(xml, &(*dominfo)->dev_emu, CIM_RES_TYPE_EMU);
        parse_devices(xml, &(*dominfo)->dev_graphics, CIM_RES_TYPE_GRAPHICS);

        (*dominfo)->dev_mem_ct = _get_mem_device(xml, &(*dominfo)->dev_mem);
        (*dominfo)->dev_net_ct = parse_devices(xml,
                                               &(*dominfo)->dev_net,
                                               CIM_RES_TYPE_NET);
        (*dominfo)->dev_disk_ct = parse_devices(xml,
                                                &(*dominfo)->dev_disk,
                                                CIM_RES_TYPE_DISK);
        (*dominfo)->dev_vcpu_ct = parse_devices(xml,
                                                &(*dominfo)->dev_vcpu,
                                                CIM_RES_TYPE_PROC);

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
        } else if (dom->type == DOMAIN_LXC) {
                free(dom->os_info.lxc.init);
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
#if LIBVIR_VERSION_NUMBER >= 4000
        int (*func)(virDomainPtr, const char *);
#else
        int (*func)(virDomainPtr, char *);
#endif

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

static int change_vcpus(virDomainPtr dom, int delta)
{
        int ret;
        virDomainInfo info;

        ret = virDomainGetInfo(dom, &info);
        if (ret == -1) {
                CU_DEBUG("Failed to get domain info for %s",
                         virDomainGetName(dom));
                return 0;
        }

        ret = virDomainSetVcpus(dom, info.nrVirtCpu + delta);
        if (ret == -1) {
                CU_DEBUG("Failed to set domain vcpus to %i",
                         info.nrVirtCpu + delta);
                return 0;
        }

        CU_DEBUG("Changed %s vcpus from %i to %i",
                 virDomainGetName(dom),
                 info.nrVirtCpu,
                 info.nrVirtCpu + delta);

        return 1;
}

int attach_device(virDomainPtr dom, struct virt_device *dev)
{
        if ((dev->type == CIM_RES_TYPE_NET) ||
            (dev->type == CIM_RES_TYPE_DISK))
                return _change_device(dom, dev, true);
        else if (dev->type == CIM_RES_TYPE_PROC)
                return change_vcpus(dom, 1);

        CU_DEBUG("Unhandled device type %i", dev->type);

        return 0;
}

int detach_device(virDomainPtr dom, struct virt_device *dev)
{
        if ((dev->type == CIM_RES_TYPE_NET) ||
            (dev->type == CIM_RES_TYPE_DISK))
                return _change_device(dom, dev, false);
        else if (dev->type == CIM_RES_TYPE_PROC)
                return change_vcpus(dom, -1);

        CU_DEBUG("Unhandled device type %i", dev->type);

        return 0;
}

int change_device(virDomainPtr dom, struct virt_device *dev)
{
        if (dev->type == CIM_RES_TYPE_MEM)
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
