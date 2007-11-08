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
#include <sys/stat.h>
#include <uuid/uuid.h>

#include "xmlgen.h"

#ifndef TEST
#include "misc_util.h"
#include "libcmpiutil.h"
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

static char *disk_block_xml(const char *path, const char *vdev)
{
        char *xml;
        int ret;

        ret = asprintf(&xml,
                       "<disk type='block' device='disk'>\n"
                       "  <driver name='phy'/>\n"
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
                       "  <driver name='tap' type='aio'/>\n"
                       "  <source file='%s'/>\n"
                       "  <target dev='%s'/>\n"
                       "</disk>\n",
                       path,
                       vdev);
        if (ret == -1)
                xml = NULL;

        return xml;
}

static char *disk_to_xml(struct disk_device *disk)
{
        struct stat s;

        if (stat(disk->source, &s) < 0)
                return NULL;

        if (S_ISBLK(s.st_mode))
                return disk_block_xml(disk->source, disk->virtual_dev);
        else
                /* If it's not a block device, we assume a file,
                   which should be a reasonable fail-safe */
                return disk_file_xml(disk->source, disk->virtual_dev);
}

static char *net_to_xml(struct net_device *net)
{
        int ret;
        char *xml;

        char *script = "vif-bridge";

        ret = asprintf(&xml,
                       "<interface type='%s'>\n"
                       "  <mac address='%s'/>\n"
                       "  <script path='%s'/>\n"
                       "</interface>\n",
                       net->type,
                       net->mac,
                       script);

        if (ret == -1)
                xml = NULL;

        return xml;
}

static char *proc_to_xml(struct _virVcpuInfo *proc)
{
        return strdup("");
}

static char *mem_to_xml(struct mem_device *mem)
{
        int ret;
        char *xml;

        ret = asprintf(&xml,
                       "<currentMemory>%" PRIu64 "</currentMemory>\n"
                       "<memory>%" PRIu64 "</memory>\n",
                       mem->size,
                       mem->maxsize);


        if (ret == 1)
                xml = NULL;

        return xml;
}

char *device_to_xml(struct virt_device *dev)
{
        switch (dev->type) {
        case VIRT_DEV_NET:
                return net_to_xml(&dev->dev.net);
        case VIRT_DEV_DISK:
                return disk_to_xml(&dev->dev.disk);
        case VIRT_DEV_MEM:
                return mem_to_xml(&dev->dev.mem);
        case VIRT_DEV_VCPU:
                return proc_to_xml(&dev->dev.vcpu);
        default:
                return NULL;
        };
}

static int astrcat(char **dest, char *source)
{
        char *tmp;
        int ret;

        ret = asprintf(&tmp, "%s%s", *dest, source);
        if (ret == -1)
                return 0;

        free(*dest);

        *dest = tmp;

        return 1;
}

static int concat_devxml(char ** xml, struct virt_device *list, int count)
{
        int i;

        for (i = 0; i < count; i++) {
                char *devxml;

                devxml = device_to_xml(&list[i]);
                if (devxml) {
                        astrcat(xml, devxml);
                        free(devxml);
                }
        }

        return count;
}

static char *system_xml(struct domain *domain)
{
        int ret;
        char *xml;

        ret = asprintf(&xml,
                       "<name>%s</name>\n"
                       "<bootloader>%s</bootloader>\n"
                       "<bootloader_args>%s</bootloader_args>\n"
                       "<on_poweroff>%s</on_poweroff>\n"
                       "<on_crash>%s</on_crash>\n",
                       domain->name,
                       domain->bootloader,
                       domain->bootloader_args,
                       vssd_recovery_action_str(domain->on_poweroff),
                       vssd_recovery_action_str(domain->on_crash));
        if (ret == -1)
                xml = NULL;

        return xml;
}

static char *os_xml(struct domain *domain)
{
        struct os_info *os = &domain->os_info;
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

        if (dominfo->uuid) {
                strcpy(uuidstr, dominfo->uuid);
                CU_DEBUG("Using existing UUID: %s");
        } else {
                CU_DEBUG("New UUID");
                uuid_generate(uuid);
                uuid_unparse(uuid, uuidstr);
        }

        concat_devxml(&devxml, dominfo->dev_net, dominfo->dev_net_ct);
        concat_devxml(&devxml, dominfo->dev_disk, dominfo->dev_disk_ct);

        concat_devxml(&sysdevxml, dominfo->dev_mem, dominfo->dev_mem_ct);
        concat_devxml(&sysdevxml, dominfo->dev_vcpu, dominfo->dev_vcpu_ct);

        sysxml = system_xml(dominfo);
        osxml = os_xml(dominfo);

        ret = asprintf(&xml,
                       "<domain type='xen'>\n"
                       "<uuid>%s</uuid>\n"
                       "%s"
                       "%s"
                       "%s"
                       "<devices>\n"
                       "%s"
                       "</devices>\n"
                       "</domain>\n",
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
