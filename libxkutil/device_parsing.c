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
#include <sys/stat.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#include <libcmpiutil/libcmpiutil.h>

#include "device_parsing.h"
#include "xmlgen.h"
#include "../src/svpc_types.h"

#define DISK_XPATH      (xmlChar *)"/domain/devices/disk"
#define NET_XPATH       (xmlChar *)"/domain/devices/interface"
#define EMU_XPATH       (xmlChar *)"/domain/devices/emulator"
#define GRAPHICS_XPATH  (xmlChar *)"/domain/devices/graphics"

#define DEFAULT_BRIDGE "xenbr0"

#define XSTREQ(x, y) (STREQ((char *)x, y))

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

static bool parse_disk_device(xmlNode *dnode, struct virt_device *vdev)
{
        struct disk_device *ddev = &(vdev->dev.disk);
        xmlNode * child = NULL;

        memset(ddev, 0, sizeof(*ddev));

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

        return true;

 err:
        cleanup_disk_device(ddev);

        return false;
}

static bool parse_net_device(xmlNode *inode, struct virt_device *vdev)
{
        struct net_device *ndev = &(vdev->dev.net);
        xmlNode *child = NULL;

        memset(ndev, 0, sizeof(*ndev));

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

        return true;
  err:
        cleanup_net_device(ndev);

        return false;
}

static bool parse_emu_device(xmlNode *node, struct virt_device *vdev)
{
        struct emu_device *edev = &(vdev->dev.emu);

        edev->path = get_node_content(node);
        if (edev->path != NULL)
                goto err;

        vdev->type = VIRT_DEV_EMU;

        return true;
 err:
        cleanup_emu_device(edev);

        return false;
}

static bool parse_graphics_device(xmlNode *node, struct virt_device *vdev)
{
        struct graphics_device *gdev = &(vdev->dev.graphics);

        gdev->type = get_attr_value(node, "type");
        gdev->port = get_attr_value(node, "port");

        if ((gdev->type == NULL) || (gdev->port == NULL))
                goto err;

        vdev->type = VIRT_DEV_GRAPHICS;

        return true;
 err:
        cleanup_graphics_device(gdev);

        return false;
}

static int do_parse(xmlNodeSet *nsv, int type, struct virt_device **l)
{
        int devidx;
        int lstidx;
        int count = 0;
        struct virt_device *list = NULL;
        xmlNode **dev_nodes = NULL;
        bool (*do_real_parse)(xmlNode *, struct virt_device *) = NULL;

        /* point to correct parser function according to type */
        if (type == VIRT_DEV_NET)
                do_real_parse = &parse_net_device;
        else if (type == VIRT_DEV_DISK)
                do_real_parse = &parse_disk_device;
        else if (type == VIRT_DEV_EMU)
                do_real_parse = parse_emu_device;
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

        list = (struct virt_device *)malloc(count * sizeof(struct virt_device));
        if (list == NULL) {
                count = 0;
                goto out;
        }

        /* walk thru the array, do real parsing on each node */
        lstidx = 0;
        for (devidx = 0; devidx < count; devidx++) {
                if (do_real_parse(dev_nodes[devidx], &list[lstidx]))
                        lstidx++;
        }

        if (lstidx < devidx) {
                list = realloc(list, lstidx * sizeof(struct virt_device));
                count = lstidx;
        }

  out:
        *l = list;
        return count;
}

/* Dummy function to suppress error message from libxml2 */
static void swallow_err_msg(void *ctx, const char *msg, ...)
{
        /* do nothing, just swallow the message. */
}

static int parse_devices(char *xml, struct virt_device **_list, int type)
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
        else if (type == VIRT_DEV_EMU)
                xpathstr = EMU_XPATH;
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
                dev->dev.vcpu.state = _dev->dev.vcpu.state;
                dev->dev.vcpu.cpuTime = _dev->dev.vcpu.cpuTime;
                dev->dev.vcpu.cpu = _dev->dev.vcpu.cpu;
        } else if (dev->type == VIRT_DEV_EMU) {
                DUP_FIELD(dev, _dev, dev.emu.path);
        } else if (dev->type == VIRT_DEV_GRAPHICS) {
                DUP_FIELD(dev, _dev, dev.graphics.type);
                DUP_FIELD(dev, _dev, dev.graphics.port);
        }

        return dev;
}

static int get_emu_device(virDomainPtr dom, struct virt_device **dev)
{
        char *xml;
        int ret;
        struct virt_device *list = NULL;

        xml = virDomainGetXMLDesc(dom, 0);
        if (xml == NULL)
                return 0;

        ret = parse_devices(xml, &list, VIRT_DEV_EMU);
        if (ret == 1)
                *dev = &list[0];
        else
                *dev = NULL;

        free(xml);

        return ret;
}

static int get_graphics_device(virDomainPtr dom, struct virt_device **dev)
{
        char *xml;
        int ret;
        struct virt_device *list = NULL;

        xml = virDomainGetXMLDesc(dom, 0);
        if (xml == NULL)
                return 0;

        ret = parse_devices(xml, &list, VIRT_DEV_GRAPHICS);
        if (ret == 1)
                *dev = &list[0];
        else
                *dev = NULL;

        free(xml);

        return ret;
}

int get_disk_devices(virDomainPtr dom, struct virt_device **list)
{
        char *xml;
        int ret;

        xml = virDomainGetXMLDesc(dom, 0);
        if (xml == NULL)
                return 0;

        ret = parse_devices(xml, list, VIRT_DEV_DISK);

        free(xml);

        return ret;
}

int get_net_devices(virDomainPtr dom, struct virt_device **list)
{
        char *xml;
        int ret;

        xml = virDomainGetXMLDesc(dom, 0);
        if (xml == NULL)
                return 0;

        ret = parse_devices(xml, list, VIRT_DEV_NET);

        free(xml);

        return ret;
}

int get_mem_devices(virDomainPtr dom, struct virt_device **list)
{
        int rc, ret;
        uint64_t mem_size, mem_maxsize;
        virDomainInfo dom_info;
        struct virt_device *ret_list = NULL;

        rc = virDomainGetInfo(dom, &dom_info);
        if (rc == -1) {
                ret = -1;
                goto out;
        }

        mem_size = (uint64_t)dom_info.memory;
        mem_maxsize = (uint64_t)dom_info.maxMem;
        if (mem_size > mem_maxsize) {
                ret = -1;
                goto out;
        }

        ret_list = malloc(sizeof(struct virt_device));
        if (ret_list == NULL) {
                ret = -1;
                free (ret_list);
                goto out;
        }

        ret_list->type = VIRT_DEV_MEM;
        ret_list->dev.mem.size = mem_size;
        ret_list->dev.mem.maxsize = mem_maxsize;
        ret_list->id = strdup("mem");

        ret = 1;
        *list = ret_list;
 out:
        return ret;
}

int get_vcpu_devices(virDomainPtr dom, struct virt_device **list)
{
        int i, rc, ret, num_filled, num_vcpus;
        virDomainInfo dom_info;
        virVcpuInfoPtr vcpu_info = NULL;
        struct virt_device *ret_list = NULL;

        rc = virDomainGetInfo(dom, &dom_info);
        if (rc == -1) {
                ret = -1;
                goto out1;
        }

        num_vcpus = dom_info.nrVirtCpu;
        vcpu_info = calloc(num_vcpus, sizeof(virVcpuInfo));
        num_filled = virDomainGetVcpus(dom, vcpu_info, num_vcpus, NULL, 0);
        if (num_vcpus != num_filled) {
                ret = -1;
                goto out2;
        }

        ret_list = calloc(num_vcpus, sizeof(struct virt_device));
        for (i = 0; i < num_vcpus; i++) {
                ret_list[i].type = VIRT_DEV_VCPU;
                ret_list[i].dev.vcpu = vcpu_info[i];
                if (asprintf(&ret_list[i].id, "%d", 
                             vcpu_info[i].number) == -1) {
                        ret = -1;
                        free(ret_list);
                        goto out2;
                }
        }

        ret = num_vcpus;
        *list = ret_list;
 out2:
        free(vcpu_info);
 out1:
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
        else if ((STREQC(dominfo->os_info.fv.type, "hvm")) &&
                 (STREQC(dominfo->typestr, "kvm")))
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

int get_dominfo(virDomainPtr dom, struct domain **dominfo)
{
        char *xml;
        int ret;

        *dominfo = malloc(sizeof(**dominfo));
        if (*dominfo == NULL)
                return 0;

        xml = virDomainGetXMLDesc(dom, 0);
        if (xml == NULL) {
                free(*dominfo);
                *dominfo = NULL;
                return 0;
        }

        ret = _get_dominfo(xml, *dominfo);
        if (ret == 0) {
                free(*dominfo);
                *dominfo = NULL;
                goto out;
        }

        get_emu_device(dom, &(*dominfo)->dev_emu);
        get_graphics_device(dom, &(*dominfo)->dev_graphics);

        (*dominfo)->dev_mem_ct = get_mem_devices(dom, &(*dominfo)->dev_mem);
        (*dominfo)->dev_net_ct = get_net_devices(dom, &(*dominfo)->dev_net);
        (*dominfo)->dev_disk_ct = get_disk_devices(dom, &(*dominfo)->dev_disk);
        (*dominfo)->dev_vcpu_ct = get_vcpu_devices(dom, &(*dominfo)->dev_vcpu);

out:
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
        } else if (dom->type == DOMAIN_XENFV) {
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
