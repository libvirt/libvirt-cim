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
#include <stdint.h>

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_association.h>
#include "misc_util.h"
#include "cs_util.h"
#include "device_parsing.h"

#include "Virt_DevicePool.h"
#include "Virt_Device.h"

#include "svpc_types.h"

const static CMPIBroker *_BROKER;

static CMPIStatus vdev_to_pool(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        uint16_t type;
        const char *id = NULL;
        char *poolid = NULL;
        CMPIInstance *pool = NULL;
        CMPIInstance *inst = NULL;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        s = get_device_by_ref(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        type = res_type_from_device_classname(CLASSNAME(ref));
        if (type == CIM_RES_TYPE_UNKNOWN) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unknown device type");
                goto out;
        }

        if (cu_get_str_path(ref, "DeviceID", &id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing DeviceID");
                goto out;
        }

        poolid = pool_member_of(_BROKER, CLASSNAME(ref), type, id);
        if (poolid == NULL) {
                CU_DEBUG("No pool membership for `%s'", id);
                goto out;
        }

        s = get_pool_by_name(_BROKER, ref, poolid, &pool);
        if (s.rc != CMPI_RC_OK)
                goto out;

        inst_list_add(list, pool);

 out:
        free(poolid);

        return s;

}

static CMPIStatus get_dev_from_pool(const CMPIObjectPath *ref,
                                    const uint16_t type,
                                    const char *_poolid,
                                    struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        char *poolid = NULL;
        struct inst_list tmp;
        int i;

        inst_list_init(&tmp);

        s = enum_devices(_BROKER, ref, NULL, type, &tmp);
        if (s.rc != CMPI_RC_OK) {
                CU_DEBUG("Unable to enum devices in get_dev_from_pool()");
                goto out;
        }

        for (i = 0; i < tmp.cur; i++) {
                CMPIInstance *inst = tmp.list[i];
                const char *cn = NULL;
                const char *dev_id = NULL;

                if (cu_get_str_prop(inst, "CreationClassName", &cn) !=
                   CMPI_RC_OK)
                        continue;
                if (cu_get_str_prop(inst, "DeviceID", &dev_id) != CMPI_RC_OK)
                        continue;

                poolid = pool_member_of(_BROKER, cn, type, dev_id);
                if (poolid && STREQ(poolid, _poolid))
                        inst_list_add(list, inst);
        }

 out:
        free(poolid);
        inst_list_free(&tmp);

        return s;
}

static CMPIStatus get_pools(const CMPIObjectPath *ref,
                            const uint16_t type,
                            const char *poolid,
                            CMPIInstance *pool_inst,
                            struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *pool = NULL;
        bool val;

        if (cu_get_bool_prop(pool_inst, "Primordial", &val) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to determine pool type");
                goto out;
        }

        /* If Primordial is true, the pool is a parent pool. Need to return
           all other pools.  Otherwise, just return the parent pool. */
        if (val) {
                struct inst_list tmp;
                int i;

                inst_list_init(&tmp);

                s = enum_pools(_BROKER, ref, type, &tmp);
                if (s.rc != CMPI_RC_OK) {
                        CU_DEBUG("Unable to enum pools in get_pools()");
                        goto out;
                }

                for (i = 0; i < tmp.cur; i++) {
                        CMPIInstance *inst = tmp.list[i];
                        const char *id = NULL;

                        cu_get_str_prop(inst, "InstanceID", &id);

                        if (!STREQC(id, poolid))
                                inst_list_add(list, inst);
                }

                inst_list_free(&tmp);
        } else {
                pool = parent_device_pool(_BROKER, ref, type, &s);
                if (s.rc != CMPI_RC_OK) {
                        CU_DEBUG("Unable to get parent pool in get_pools()");
                        goto out;
                }

                inst_list_add(list, pool);
        }

 out:
        return s;
}

static CMPIStatus pool_to_vdev_or_pool(const CMPIObjectPath *ref,
                                       struct std_assoc_info *info,
                                       struct inst_list *list)
{
        const char *poolid;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        uint16_t type;
        CMPIInstance *inst = NULL;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        s = get_pool_by_ref(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (cu_get_str_path(ref, "InstanceID", &poolid) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing InstanceID");
                goto out;
        }

        type = res_type_from_pool_id(poolid);
        if (type == CIM_RES_TYPE_UNKNOWN) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid InstanceID or unsupported pool type");
                goto out;
        }

        s = get_dev_from_pool(ref, type, poolid, list);
        if (s.rc != CMPI_RC_OK) {
                CU_DEBUG("Unable to get device from pool");
                goto out;
        }

        s = get_pools(ref, type, poolid, inst, list);

 out:
        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char* pool[] = {
        "Xen_ProcessorPool",
        "Xen_MemoryPool",
        "Xen_NetworkPool",
        "Xen_DiskPool",
        "Xen_GraphicsPool",
        "Xen_InputPool",
        "KVM_ProcessorPool",
        "KVM_MemoryPool",
        "KVM_NetworkPool",
        "KVM_DiskPool",
        "KVM_GraphicsPool",
        "KVM_InputPool",
        "LXC_ProcessorPool",
        "LXC_MemoryPool",
        "LXC_NetworkPool",
        "LXC_DiskPool",
        "LXC_GraphicsPool",
        "LXC_InputPool",
        NULL
};

static char* device[] = {
        "Xen_Processor",
        "Xen_Memory",
        "Xen_NetworkPort",
        "Xen_LogicalDisk",
        "Xen_DisplayController",
        "Xen_PointingDevice",
        "KVM_Processor",
        "KVM_Memory",
        "KVM_NetworkPort",
        "KVM_LogicalDisk",
        "KVM_DisplayController",
        "KVM_PointingDevice",
        "LXC_Processor",
        "LXC_Memory",
        "LXC_NetworkPort",
        "LXC_LogicalDisk",
        "LXC_DisplayController",
        "LXC_PointingDevice",
        NULL
};

static char* device_or_pool[] = {
        "Xen_Processor",
        "Xen_Memory",
        "Xen_NetworkPort",
        "Xen_LogicalDisk",
        "Xen_DisplayController",
        "Xen_PointingDevice",
        "KVM_Processor",
        "KVM_Memory",
        "KVM_NetworkPort",
        "KVM_LogicalDisk",
        "KVM_DisplayController",
        "KVM_PointingDevice",
        "LXC_Processor",
        "LXC_Memory",
        "LXC_NetworkPort",
        "LXC_LogicalDisk",
        "LXC_DisplayController",
        "LXC_PointingDevice",
        "Xen_ProcessorPool",
        "Xen_MemoryPool",
        "Xen_NetworkPool",
        "Xen_DiskPool",
        "Xen_GraphicsPool",
        "Xen_InputPool",
        "KVM_ProcessorPool",
        "KVM_MemoryPool",
        "KVM_NetworkPool",
        "KVM_DiskPool",
        "KVM_GraphicsPool",
        "KVM_InputPool",
        "LXC_ProcessorPool",
        "LXC_MemoryPool",
        "LXC_NetworkPool",
        "LXC_DiskPool",
        "LXC_GraphicsPool",
        "LXC_InputPool",
        NULL
};

static char* assoc_classname[] = {
        "Xen_ElementAllocatedFromPool",
        "KVM_ElementAllocatedFromPool",
        "LXC_ElementAllocatedFromPool",
        NULL
};

static struct std_assoc _vdev_to_pool = {
        .source_class = (char**)&device,
        .source_prop = "Dependent",

        .target_class = (char**)&pool,
        .target_prop = "Antecedent",

        .assoc_class = (char**)&assoc_classname,

        .handler = vdev_to_pool,
        .make_ref = make_ref
};

static struct std_assoc _pool_to_vdev_or_pool = {
        .source_class = (char**)&pool,
        .source_prop = "Antecedent",

        .target_class = (char**)&device_or_pool,
        .target_prop = "Dependent",

        .assoc_class = (char**)&assoc_classname,

        .handler = pool_to_vdev_or_pool,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &_vdev_to_pool,
        &_pool_to_vdev_or_pool,
        NULL
};

STDA_AssocMIStub(, 
                 Virt_ElementAllocatedFromPool, 
                 _BROKER,
                 libvirt_cim_init(),
                 handlers);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
