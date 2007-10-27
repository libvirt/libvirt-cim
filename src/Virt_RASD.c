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
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil.h>
#include "device_parsing.h"
#include "misc_util.h"

#include "Virt_RASD.h"
#include "svpc_types.h"

const static CMPIBroker *_BROKER;

static struct virt_device *_find_dev(struct virt_device *list,
                                    int count,
                                    const char *id)
{
        int i;

        for (i = 0; i < count; i++) {
                struct virt_device *dev = &list[i];

                if (STREQ(dev->id, id))
                        return virt_device_dup(dev);
        }

        return NULL;
}

static int list_devs(virConnectPtr conn,
                     const uint16_t type,
                     const char *host,
                     struct virt_device **list)
{
        virDomainPtr dom;

        dom = virDomainLookupByName(conn, host);
        if (dom == NULL)
                return 0;

        if (type == CIM_RASD_TYPE_DISK)
                return  get_disk_devices(dom, list);
        else if (type == CIM_RASD_TYPE_NET)
                return get_net_devices(dom, list);
        else if (type == CIM_RASD_TYPE_PROC)
                return get_vcpu_devices(dom, list);
        else if (type == CIM_RASD_TYPE_MEM)
                return get_mem_devices(dom, list);
        else
                return 0;
}

static struct virt_device *find_dev(virConnectPtr conn,
                                    const uint16_t type,
                                    const char *host,
                                    const char *devid)
{
        int count = -1;
        struct virt_device *list = NULL;
        struct virt_device *dev = NULL;

        count = list_devs(conn, type, host, &list);
        if (count > 0) {
                dev = _find_dev(list, count, devid);
                cleanup_virt_devices(&list, count);
        }

        return dev;
}

char *rasd_to_xml(CMPIInstance *rasd)
{
        /* FIXME: Remove this */
        return NULL;
}

static CMPIInstance *rasd_from_vdev(const CMPIBroker *broker,
                                    struct virt_device *dev,
                                    const char *host,
                                    const char *ns)
{
        CMPIInstance *inst;
        uint16_t type;
        char *base;
        char *id;

        if (dev->type == VIRT_DEV_DISK) {
                type = CIM_RASD_TYPE_DISK;
                base = "DiskResourceAllocationSettingData";
        } else if (dev->type == VIRT_DEV_NET) {
                type = CIM_RASD_TYPE_NET;
                base = "NetResourceAllocationSettingData";
        } else if (dev->type == VIRT_DEV_VCPU) {
                type = CIM_RASD_TYPE_PROC;
                base = "ProcResourceAllocationSettingData";
        } else if (dev->type == VIRT_DEV_MEM) {
                type = CIM_RASD_TYPE_MEM;
                base = "MemResourceAllocationSettingData";
        } else {
                return NULL;
        }

        inst = get_typed_instance(broker, base, ns);
        if (inst == NULL)
                return inst;

        id = get_fq_devid((char *)host, dev->id);

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)id, CMPI_chars);

        CMSetProperty(inst, "ResourceType",
                      (CMPIValue *)&type, CMPI_uint16);

        if (dev->type == VIRT_DEV_DISK) {
                CMSetProperty(inst,
                              "VirtualDevice",
                              (CMPIValue *)dev->dev.disk.virtual_dev,
                              CMPI_chars);
                CMSetProperty(inst,
                              "Address",
                              (CMPIValue *)dev->dev.disk.source,
                              CMPI_chars);
        } else if (dev->type == VIRT_DEV_NET) {
                CMSetProperty(inst,
                              "NetworkType",
                              (CMPIValue *)dev->dev.disk.type,
                              CMPI_chars);
        } else if (dev->type == VIRT_DEV_MEM) {
                const char *units = "MegaBytes";

                CMSetProperty(inst, "AllocationUnits",
                              (CMPIValue *)units, CMPI_chars);
                CMSetProperty(inst, "VirtualQuantity",
                              (CMPIValue *)&dev->dev.mem.size, CMPI_uint64);
                CMSetProperty(inst, "Reservation",
                              (CMPIValue *)&dev->dev.mem.size, CMPI_uint64);
                CMSetProperty(inst, "Limit",
                              (CMPIValue *)&dev->dev.mem.maxsize, CMPI_uint64);
        }

        /* FIXME: Put the HostResource in place */

        free(id);

        return inst;
}

static CMPIInstance *get_rasd_instance(const CMPIContext *context,
                                       const CMPIObjectPath *ns,
                                       const char *id,
                                       const uint16_t type)
{
        CMPIInstance *inst = NULL;
        CMPIStatus s;
        int ret;
        char *host = NULL;
        char *devid = NULL;
        virConnectPtr conn = NULL;
        struct virt_device *dev;

        ret = parse_fq_devid((char *)id, &host, &devid);
        if (!ret)
                return NULL;

        conn = lv_connect(_BROKER, &s);
        if (conn == NULL)
                goto out;

        dev = find_dev(conn, type, host, devid);
        if (dev)
                inst = rasd_from_vdev(_BROKER, dev, host, NAMESPACE(ns));

 out:
        virConnectClose(conn);
        free(host);
        free(devid);

        return inst;
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst;
        char *id = NULL;
        uint16_t type;

        id = cu_get_str_path(reference, "InstanceID");
        if (id == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing InstanceID");
                goto out;
        }

        if (!cu_get_u16_path(reference, "ResourceType", &type)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing or invalid ResourceType");
                goto out;
        }

        inst = get_rasd_instance(context, reference, id, type);

        if (inst != NULL)
                CMReturnInstance(results, inst);
        else
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unknown instance");
 out:
        return s;
}

int rasds_for_domain(const CMPIBroker *broker,
                     const char *name,
                     const uint16_t type,
                     const char *ns,
                     struct inst_list *_list)
{
        struct virt_device *list;
        int count;
        int i;
        virConnectPtr conn;
        CMPIStatus s;

        conn = lv_connect(_BROKER, &s);
        if (conn == NULL)
                return 0;

        count = list_devs(conn, type, name, &list);

        for (i = 0; i < count; i++) {
                CMPIInstance *inst;

                inst = rasd_from_vdev(broker, &list[i], name, ns);
                if (inst != NULL)
                        inst_list_add(_list, inst);
        }

        if (count > 0)
                cleanup_virt_devices(&list, count);

        virConnectClose(conn);

        return count;
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EI();
DEFAULT_EIN();
DEFAULT_INST_CLEANUP();
DEFAULT_EQ();

/* Avoid a warning in the stub macro below */
CMPIInstanceMI *
Virt_RASDProvider_Create_InstanceMI(const CMPIBroker *,
                                    const CMPIContext *,
                                    CMPIStatus *rc);

CMInstanceMIStub(, Virt_RASDProvider, _BROKER, CMNoHook);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */