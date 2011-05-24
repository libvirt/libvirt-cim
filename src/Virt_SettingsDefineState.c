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

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_association.h>
#include "misc_util.h"
#include "device_parsing.h"

#include "Virt_Device.h"
#include "Virt_RASD.h"
#include "Virt_ComputerSystem.h"
#include "Virt_VSSD.h"
#include "svpc_types.h"

const static CMPIBroker *_BROKER;

static int get_proc_dev_count(const char *name, 
                              const char *cn,
                              uint16_t type,
                              char *host,
                              CMPIStatus *s)
{
        virConnectPtr conn = NULL;
        struct virt_device *list = NULL;
        struct virt_device *dev = NULL;
        int dev_count = -1;
        int count = -1;

        conn = connect_by_classname(_BROKER, cn, s);
        if (conn == NULL) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s)",
                           name);
                goto out;
        }

        dev_count = list_rasds(conn, type, host, &list);
        if (dev_count <= 0) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get list of processors");
                goto out;
        }

        dev = &list[0];
        count = dev->dev.vcpu.quantity;

        cleanup_virt_devices(&list, dev_count);

 out:
        virConnectClose(conn);
        return count;
}

static char *get_rasd_id(const CMPIInstance *inst, uint16_t type)
{
        char *id = NULL;
        int ret;
        const char *tmp;

        if (type == CIM_RES_TYPE_PROC) {
                ret = cu_get_str_prop(inst, "SystemName", &tmp);
                if (ret != CMPI_RC_OK) {
                        CU_DEBUG("No SystemName in device instance");
                        goto out;
                }

                ret = asprintf(&id, "%s/proc", tmp);
                if (ret == -1) {
                        id = NULL;
                        goto out;
                }
        } else {
                ret = cu_get_str_prop(inst, "DeviceID", &tmp);
                if (ret != CMPI_RC_OK) {
                        CU_DEBUG("No DeviceID in device instance");
                        id = NULL;
                        goto out;
                }

                id = strdup(tmp);
        }

 out:
        return id;
}

static char *get_dev_id(const char *host, int devid)
{
        char *id = NULL;

        if (asprintf(&id, "%s/%d", host, devid) == -1)
                id = NULL;

        return id;
}

static CMPIStatus dev_to_rasd(const CMPIObjectPath *ref,
                              struct std_assoc_info *info,
                              struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        char *id = NULL;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        s = get_device_by_ref(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        id = get_rasd_id(inst,
                         res_type_from_device_classname(CLASSNAME(ref)));
        if (id == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "Unable to get RASD id from DeviceID");
                goto out;
        }

        s = get_rasd_by_name(_BROKER,
                             ref,
                             id,
                             res_type_from_device_classname(CLASSNAME(ref)),
                             NULL,
                             &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        inst_list_add(list, inst);

 out:
        free(id);
        return s;
}

static CMPIStatus rasd_to_dev(const CMPIObjectPath *ref,
                              struct std_assoc_info *info,
                              struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        const char *name = NULL;
        char *host = NULL;
        char *devid = NULL;
        char *id = NULL;
        uint16_t type;
        int count = 1;
        int i; 

        if (!match_hypervisor_prefix(ref, info))
                return s;

        s = get_rasd_by_ref(_BROKER, ref, NULL, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (res_type_from_rasd_classname(CLASSNAME(ref), &type) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing ResourceType");
                goto out;
        }

        if (cu_get_str_path(ref, "InstanceID", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing InstanceID");
                goto out;
        }

        if (type == CIM_RES_TYPE_PROC) {
                if (parse_fq_devid((char *)name, &host, &devid) != 1) {
                        cu_statusf(_BROKER, &s,
                                   CMPI_RC_ERR_NOT_FOUND,
                                   "No such instance (%s)",
                                   name);
                        goto out;
                }

                count = get_proc_dev_count(name, 
                                           CLASSNAME(ref), 
                                           type, 
                                           host, 
                                           &s); 
                if (count <= 0)
                        goto out;
        }

        for (i = 0; i < count; i++) {
                if (type == CIM_RES_TYPE_PROC)
                        id = get_dev_id(host, i); 
                else 
                        id = strdup(name);

                if (id == NULL) {
                        cu_statusf(_BROKER, &s,
                                   CMPI_RC_ERR_NOT_FOUND,
                                   "Unable to get RASD id from DeviceID");
                        goto out;
                }

                s = get_device_by_name(_BROKER, ref, id, type, &inst);
                if (s.rc != CMPI_RC_OK)
                        goto out;

                inst_list_add(list, inst);
                free(id);
        }

 out:
        free(host);
        free(devid);
        return s;
}

static CMPIStatus vs_to_vssd(const CMPIObjectPath *ref,
                             struct std_assoc_info *info,
                             struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst;
        const char *name;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        s = get_domain_by_ref(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (cu_get_str_path(ref, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing Name property");
                goto out;
        }

        s = get_vssd_by_name(_BROKER, ref, name, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        inst_list_add(list, inst);
        
 out:
        return s;
}

static CMPIStatus vssd_to_vs(const CMPIObjectPath *ref,
                             struct std_assoc_info *info,
                             struct inst_list *list)
{
        const char *id = NULL;
        char *pfx = NULL;
        char *name = NULL;
        int ret;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        s = get_vssd_by_ref(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (cu_get_str_path(ref, "InstanceID", &id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing InstanceID");
                goto out;
        }

        ret = sscanf(id, "%a[^:]:%as", &pfx, &name);
        if (ret != 2) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid InstanceID");
                goto out;
        }

        s = get_domain_by_name(_BROKER, ref, name, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        inst_list_add(list, inst);

 out:
        free(name);
        free(pfx);

        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char* logical_device[] = {
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

static char* resource_allocation_setting_data[] = {
        "Xen_DiskResourceAllocationSettingData",
        "Xen_MemResourceAllocationSettingData",
        "Xen_NetResourceAllocationSettingData",
        "Xen_ProcResourceAllocationSettingData",
        "Xen_GraphicsResourceAllocationSettingData",
        "Xen_InputResourceAllocationSettingData",
        "KVM_DiskResourceAllocationSettingData",
        "KVM_MemResourceAllocationSettingData",
        "KVM_NetResourceAllocationSettingData",
        "KVM_ProcResourceAllocationSettingData",
        "KVM_GraphicsResourceAllocationSettingData",
        "KVM_InputResourceAllocationSettingData",
        "LXC_DiskResourceAllocationSettingData",
        "LXC_MemResourceAllocationSettingData",
        "LXC_NetResourceAllocationSettingData",
        "LXC_ProcResourceAllocationSettingData",
        "LXC_GraphicsResourceAllocationSettingData",
        "LXC_InputResourceAllocationSettingData",
        NULL
};

static char* computer_system[] = {
        "Xen_ComputerSystem",
        "KVM_ComputerSystem",
        "LXC_ComputerSystem",
        NULL
};

static char* virtual_system_setting_data[] = {
        "Xen_VirtualSystemSettingData",
        "KVM_VirtualSystemSettingData",
        "LXC_VirtualSystemSettingData",
        NULL
};

static char* assoc_classname[] = {
        "Xen_SettingsDefineState",
        "KVM_SettingsDefineState",
        "LXC_SettingsDefineState",
        NULL
};

static struct std_assoc _dev_to_rasd = {
        .source_class = (char**)&logical_device,
        .source_prop = "ManagedElement",

        .target_class = (char**)&resource_allocation_setting_data,
        .target_prop = "SettingData",

        .assoc_class = (char**)&assoc_classname,

        .handler = dev_to_rasd,
        .make_ref = make_ref
};

static struct std_assoc _rasd_to_dev = {
        .source_class = (char**)&resource_allocation_setting_data,
        .source_prop = "SettingData",

        .target_class = (char**)&logical_device,
        .target_prop = "ManagedElement",

        .assoc_class = (char**)&assoc_classname,

        .handler = rasd_to_dev,
        .make_ref = make_ref
};

static struct std_assoc _vs_to_vssd = {
        .source_class = (char**)&computer_system,
        .source_prop = "ManagedElement",

        .target_class = (char**)&virtual_system_setting_data,
        .target_prop = "SettingData",

        .assoc_class = (char**)&assoc_classname,

        .handler = vs_to_vssd,
        .make_ref = make_ref
};

static struct std_assoc _vssd_to_vs = {
        .source_class = (char**)&virtual_system_setting_data,
        .source_prop = "SettingData",

        .target_class = (char**)&computer_system,
        .target_prop = "ManagedElement",

        .assoc_class = (char**)&assoc_classname,

        .handler = vssd_to_vs,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &_dev_to_rasd,
        &_rasd_to_dev,
        &_vs_to_vssd,
        &_vssd_to_vs,
        NULL
};

STDA_AssocMIStub(,
                 Virt_SettingsDefineState,
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
