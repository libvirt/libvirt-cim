/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
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
#include <uuid/uuid.h>

#include "xmlgen.h"

#ifndef TEST
#include "misc_util.h"
#include <libcmpiutil/libcmpiutil.h>
#include "cmpimacs.h"
#endif

static char *__tag_attr(struct kv *attrs, int count)
{
        char *result = strdup("");
        char *new = NULL;
        int i;
        int size = 0;

        for (i = 0; i < count; i++) {
                char *tmp;
                int ret;

                ret = asprintf(&new, " %s='%s'",
                               attrs[i].key, attrs[i].val);

                if (ret == -1)
                        goto err;

                size += strlen(new) + 1;
                tmp = realloc(result, size);
                if (tmp == NULL)
                        goto err;

                result = tmp;

                strcat(result, new);
                free(new);
        }

        return result;

 err:
        free(result);
        return NULL;
}

static char *tagify(char *tagname, char *content, struct kv *attrs, int count)
{
        char *result;
        int ret;
        char *opentag;

        if (count)
                opentag = __tag_attr(attrs, count);
        else
                opentag = strdup("");

        if (content)
                ret = asprintf(&result,
                               "<%s%s>%s</%s>",
                               tagname, opentag, content, tagname);
        else
                ret = asprintf(&result,
                               "<%s%s/>", tagname, opentag);
        if (ret == -1)
                result = NULL;

        free(opentag);

        return result;
}

static int astrcat(char **dest, char *source)
{
        char *tmp;
        int ret;

        if (*dest) {
                ret = asprintf(&tmp, "%s%s", *dest, source);
                if (ret == -1)
                        return 0;
        } else {
                tmp = strdup(source);
        }

        free(*dest);

        *dest = tmp;

        return 1;
}

static char *disk_block_xml(const char *path, const char *vdev)
{
        char *xml;
        int ret;

        ret = asprintf(&xml,
                       "<disk type='block' device='disk'>\n"
                       "  <source dev='%s'/>\n"
                       "  <target dev='%s'/>\n"
                       "</disk>\n",
                       path,
                       vdev);
        if (ret == -1)
                xml = NULL;

        return xml;
}

static char *disk_file_xml(const char *path, const char *vdev)
{
        char *xml;
        int ret;

        ret = asprintf(&xml,
                       "<disk type='file' device='disk'>\n"
                       "  <source file='%s'/>\n"
                       "  <target dev='%s'/>\n"
                       "</disk>\n",
                       path,
                       vdev);
        if (ret == -1)
                xml = NULL;

        return xml;
}

static char *disk_fs_xml(const char *path, const char *vdev)
{
        char *xml;
        int ret;

        ret = asprintf(&xml,
                       "<filesystem type='mount'>\n"
                       "  <source dir='%s'/>\n"
                       "  <target dir='%s'/>\n"
                       "</filesystem>\n",
                       path,
                       vdev);
        if (ret == -1)
                xml = NULL;

        return xml;
}

static bool disk_to_xml(char **xml, struct virt_device *dev)
{
        char *_xml = NULL;
        struct disk_device *disk = &dev->dev.disk;

        if (disk->disk_type == DISK_PHY)
                _xml = disk_block_xml(disk->source, disk->virtual_dev);
        else if (disk->disk_type == DISK_FILE)
                /* If it's not a block device, we assume a file,
                   which should be a reasonable fail-safe */
                _xml = disk_file_xml(disk->source, disk->virtual_dev);
        else if (disk->disk_type == DISK_FS)
                _xml = disk_fs_xml(disk->source, disk->virtual_dev);
        else
                return false;

        astrcat(xml, _xml);
        free(_xml);

        return true;
}

static bool bridge_net_to_xml(char **xml, struct virt_device *dev)
{
        int ret;
        char *_xml;
        char *script = "vif-bridge";
        struct net_device *net = &dev->dev.net;

        ret = asprintf(&_xml,
                       "<interface type='%s'>\n"
                       "  <source bridge='%s'/>\n"
                       "  <mac address='%s'/>\n"
                       "  <script path='%s'/>\n"
                       "</interface>\n",
                       net->type,
                       net->source,
                       net->mac,
                       script);

        if (ret == -1)
                return false;
        else
                astrcat(xml, _xml);

        free(_xml);

        return true;
}

static bool network_net_to_xml(char **xml, struct virt_device *dev)
{
        int ret;
        char *_xml;
        struct net_device *net = &dev->dev.net;

        if (net->source == NULL)
                net->source = strdup("default");

        ret = asprintf(&_xml,
                       "<interface type='%s'>\n"
                       "  <mac address='%s'/>\n"
                       "  <source network='%s'/>\n"
                       "</interface>\n",
                       net->type,
                       net->mac,
                       net->source);
        if (ret == -1)
                return false;
        else
                astrcat(xml, _xml);

        free(_xml);

        return true;
}

static bool user_net_to_xml(char **xml, struct virt_device *dev)
{
        int ret;
        char *_xml;
        struct net_device *net = &dev->dev.net;

        ret = asprintf(&_xml,
                       "<interface type='%s'>\n"
                       "  <mac address='%s'/>\n"
                       "</interface>\n",
                       net->type,
                       net->mac);
        if (ret == -1)
                return false;
        else
                astrcat(xml, _xml);

        free(_xml);

        return true;
}

static bool net_to_xml(char **xml, struct virt_device *dev)
{
        if (STREQ(dev->dev.net.type, "network"))
                return network_net_to_xml(xml, dev);
        else if (STREQ(dev->dev.net.type, "bridge"))
                return bridge_net_to_xml(xml, dev);
        else if (STREQ(dev->dev.net.type, "user"))
                return user_net_to_xml(xml, dev);
        else
                return false;
}

static bool vcpu_to_xml(char **xml, struct virt_device *dev)
{
        int ret;
        char *_xml;

        ret = asprintf(&_xml, "<vcpu>%" PRIu64 "</vcpu>\n",
                       dev->dev.vcpu.quantity);
        if (ret == -1)
                return false;
        else
                astrcat(xml, _xml);

        return true;
}

static bool mem_to_xml(char **xml, struct virt_device *dev)
{
        int ret;
        char *_xml;
        struct mem_device *mem = &dev->dev.mem;

        ret = asprintf(&_xml,
                       "<currentMemory>%" PRIu64 "</currentMemory>\n"
                       "<memory>%" PRIu64 "</memory>\n",
                       mem->size,
                       mem->maxsize);


        if (ret == -1)
                return false;
        else
                astrcat(xml, _xml);

        free(_xml);

        return true;
}

static bool emu_to_xml(char **xml, struct virt_device *dev)
{
        int ret;
        char *_xml;
        struct emu_device *emu = &dev->dev.emu;

        ret = asprintf(&_xml,
                       "<emulator>%s</emulator>\n",
                       emu->path);
        if (ret == -1)
                return false;
        else
                astrcat(xml, _xml);

        free(_xml);

        return true;
}

static bool graphics_to_xml(char **xml, struct virt_device *dev)
{
        int ret;
        char *_xml;
        struct graphics_device *graphics = &dev->dev.graphics;

        ret = asprintf(&_xml,
                       "<graphics type='%s' port='%s'/>\n",
                       graphics->type,
                       graphics->port);
        if (ret == -1)
                return false;
        else
                astrcat(xml, _xml);

        free(_xml);

        return true;
}

static bool concat_devxml(char **xml,
                          struct virt_device *list,
                          int count,
                          bool (*func)(char **, struct virt_device *))
{
        char *_xml = NULL;
        int i;

        for (i = 0; i < count; i++) {
                /* Deleted devices are marked as CIM_RES_TYPE_UNKNOWN
                 * and should be skipped
                 */
                if (list[i].type != CIM_RES_TYPE_UNKNOWN)
                        func(&_xml, &list[i]);
        }

        if (_xml != NULL)
                astrcat(xml, _xml);
        free(_xml);

        return true;
}

char *device_to_xml(struct virt_device *dev)
{
        char *xml = NULL;
        int type = dev->type;
        bool (*func)(char **, struct virt_device *);

        switch (type) {
        case CIM_RES_TYPE_DISK:
                func = disk_to_xml;
                break;
        case CIM_RES_TYPE_PROC:
                func = vcpu_to_xml;
                break;
        case CIM_RES_TYPE_NET:
                func = net_to_xml;
                break;
        case CIM_RES_TYPE_MEM:
                func = mem_to_xml;
                break;
        case CIM_RES_TYPE_EMU:
                func = emu_to_xml;
                break;
        case CIM_RES_TYPE_GRAPHICS:
                func = graphics_to_xml;
                break;
        default:
                return NULL;
        }

        if (concat_devxml(&xml, dev, 1, func))
                return xml;

        free(xml);

        return NULL;
}

static char *system_xml(struct domain *domain)
{
        int ret;
        char *bl = NULL;
        char *bl_args = NULL;
        char *xml;

        if (domain->bootloader)
                bl = tagify("bootloader",
                            domain->bootloader,
                            NULL,
                            0);
        if (domain->bootloader_args)
                bl_args = tagify("bootloader_args",
                                 domain->bootloader_args,
                                 NULL,
                                 0);

        ret = asprintf(&xml,
                       "<name>%s</name>\n"
                       "%s\n"
                       "%s\n"
                       "<on_poweroff>%s</on_poweroff>\n"
                       "<on_crash>%s</on_crash>\n",
                       domain->name,
                       bl ? bl : "",
                       bl_args ? bl_args : "",
                       vssd_recovery_action_str(domain->on_poweroff),
                       vssd_recovery_action_str(domain->on_crash));
        if (ret == -1)
                xml = NULL;

        free(bl);
        free(bl_args);

        return xml;
}

static char *_xenpv_os_xml(struct domain *domain)
{
        struct pv_os_info *os = &domain->os_info.pv;
        int ret;
        char *xml;
        char *type = NULL;
        char *kernel = NULL;
        char *initrd = NULL;
        char *cmdline = NULL;

        if (os->type == NULL)
                os->type = strdup("linux");

        if (os->kernel == NULL)
                os->kernel = strdup("/dev/null");

        type = tagify("type", os->type, NULL, 0);
        kernel = tagify("kernel", os->kernel, NULL, 0);
        initrd = tagify("initrd", os->initrd, NULL, 0);
        cmdline = tagify("cmdline", os->cmdline, NULL, 0);

        ret = asprintf(&xml,
                       "<os>\n"
                       "  %s\n"
                       "  %s\n"
                       "  %s\n"
                       "  %s\n"
                       "</os>\n",
                       type,
                       kernel,
                       initrd,
                       cmdline);
        if (ret == -1)
                xml = NULL;

        free(type);
        free(kernel);
        free(initrd);
        free(cmdline);

        return xml;
}

static char *_xenfv_os_xml(struct domain *domain)
{
        struct fv_os_info *os = &domain->os_info.fv;
        int ret;
        char *xml;
        char *type;
        char *loader;
        char *boot;
        struct kv bootattr = {"dev", NULL};

        if (os->type == NULL)
                os->type = strdup("hvm");

        if (os->loader == NULL)
                os->loader = strdup("/usr/lib/xen/boot/hvmloader");

        if (os->boot == NULL)
                os->boot = strdup("hd");

        type = tagify("type", os->type, NULL, 0);
        loader = tagify("loader", os->loader, NULL, 0);

        bootattr.val = os->boot;
        boot = tagify("boot", NULL, &bootattr, 1);

        ret = asprintf(&xml,
                       "<os>\n"
                       "  %s\n"
                       "  %s\n"
                       "  %s\n"
                       "</os>\n"
                       "<features>\n"
                       "  <pae/>\n"
                       "  <acpi/>\n"
                       "  <apic/>\n"
                       "</features>\n",
                       type,
                       loader,
                       boot);
        if (ret == -1)
                xml = NULL;

        free(type);
        free(loader);
        free(boot);

        return xml;
}

static char *_kvm_os_xml(struct domain *domain)
{
        struct fv_os_info *os = &domain->os_info.fv;
        int ret;
        char *xml;
        char *type;
        char *boot;
        struct kv bootattr = {"dev", NULL};

        if (os->type == NULL)
                os->type = strdup("hvm");

        if (os->boot == NULL)
                os->boot = strdup("hd");

        type = tagify("type", os->type, NULL, 0);

        bootattr.val = os->boot;
        boot = tagify("boot", NULL, &bootattr, 1);

        ret = asprintf(&xml,
                       "<os>\n"
                       "  %s\n"
                       "  %s\n"
                       "</os>\n",
                       type,
                       boot);
        if (ret == -1)
                xml = NULL;

        free(type);
        free(boot);

        return xml;
}

static char *_lxc_os_xml(struct domain *domain)
{
        struct lxc_os_info *os = &domain->os_info.lxc;
        int ret;
        char *xml = NULL;

        ret = asprintf(&xml,
                       "<os>\n"
                       "  <init>%s</init>\n"
                       "</os>\n",
                       os->init);
        if (ret == -1)
                xml = NULL;

        return xml;
}

static char *os_xml(struct domain *domain)
{
        if (domain->type == DOMAIN_XENPV)
                return _xenpv_os_xml(domain);
        else if (domain->type == DOMAIN_XENFV)
                return _xenfv_os_xml(domain);
        else if (domain->type == DOMAIN_KVM)
                return _kvm_os_xml(domain);
        else if (domain->type == DOMAIN_LXC)
                return _lxc_os_xml(domain);
        else
                return strdup("<!-- unsupported domain type -->\n");
}

char *system_to_xml(struct domain *dominfo)
{
        char *devxml = strdup("");
        char *sysdevxml = strdup("");
        char *sysxml = NULL;
        char *osxml = NULL;
        char *xml = NULL;
        int ret;
        uint8_t uuid[16];
        char uuidstr[37];
        const char *domtype;

        if ((dominfo->type == DOMAIN_XENPV) || (dominfo->type == DOMAIN_XENFV))
                domtype = "xen";
        else if (dominfo->type == DOMAIN_KVM)
                domtype = "kvm";
        else if (dominfo->type == DOMAIN_LXC)
                domtype = "lxc";
        else
                domtype = "unknown";

        if (dominfo->uuid) {
                strcpy(uuidstr, dominfo->uuid);
                CU_DEBUG("Using existing UUID: %s");
        } else {
                CU_DEBUG("New UUID");
                uuid_generate(uuid);
                uuid_unparse(uuid, uuidstr);
        }

        concat_devxml(&devxml,
                      dominfo->dev_net,
                      dominfo->dev_net_ct,
                      net_to_xml);
        concat_devxml(&devxml,
                      dominfo->dev_disk,
                      dominfo->dev_disk_ct,
                      disk_to_xml);

        if (dominfo->dev_emu)
                concat_devxml(&devxml,
                              dominfo->dev_emu,
                              1,
                              emu_to_xml);

        if (dominfo->dev_graphics)
                concat_devxml(&devxml,
                              dominfo->dev_graphics,
                              1,
                              graphics_to_xml);

        concat_devxml(&sysdevxml,
                      dominfo->dev_mem,
                      dominfo->dev_mem_ct,
                      mem_to_xml);
        concat_devxml(&sysdevxml,
                      dominfo->dev_vcpu,
                      dominfo->dev_vcpu_ct,
                      vcpu_to_xml);

        sysxml = system_xml(dominfo);
        osxml = os_xml(dominfo);

        ret = asprintf(&xml,
                       "<domain type='%s'>\n"
                       "<uuid>%s</uuid>\n"
                       "%s"
                       "%s"
                       "%s"
                       "<devices>\n"
                       "%s"
                       "</devices>\n"
                       "</domain>\n",
                       domtype,
                       uuidstr,
                       sysxml,
                       osxml,
                       sysdevxml,
                       devxml);
        if (ret == -1)
                xml = NULL;

        free(devxml);
        free(sysdevxml);
        free(osxml);
        free(sysxml);

        return xml;
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
