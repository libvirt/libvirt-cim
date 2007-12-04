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

#include "libcmpiutil.h"
#include "std_association.h"
#include "misc_util.h"
#include "device_parsing.h"

#include "Virt_Device.h"
#include "Virt_RASD.h"
#include "Virt_ComputerSystem.h"
#include "Virt_VSSD.h"
#include "svpc_types.h"

const static CMPIBroker *_BROKER;

static CMPIInstance *find_rasd(struct inst_list *list,
                               const char *devid)
{
        int i;
        CMPIInstance *inst;

        for (i = 0; i < list->cur; i++) {
                const char *id;
                int ret;

                inst = list->list[i];

                ret = cu_get_str_prop(inst, "InstanceID", &id);
                if (ret != CMPI_RC_OK)
                        continue;

                if (STREQ(id, devid))
                        return inst;
        }

        return NULL;
}

static CMPIStatus dev_to_rasd(const CMPIObjectPath *ref,
                              struct std_assoc_info *info,
                              struct inst_list *list)
{
        CMPIStatus s;
        CMPIInstance *rasd;
        struct inst_list rasds;
        const char *id = NULL;
        char *name = NULL;
        char *devid = NULL;
        int ret;

        inst_list_init(&rasds);

        if (cu_get_str_path(ref, "DeviceID", &id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing DeviceID");
                goto out;
        }

        ret = parse_fq_devid(id, &name, &devid);
        if (!ret) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid DeviceID");
                goto out;
        }

        ret = rasds_for_domain(_BROKER,
                               name,
                               device_type_from_classname(CLASSNAME(ref)),
                               ref,
                               &rasds);

        rasd = find_rasd(&rasds, id);
        if (rasd != NULL)
                inst_list_add(list, rasd);

        CMSetStatus(&s, CMPI_RC_OK);

 out:
        free(name);
        free(devid);

        return s;
}

static CMPIInstance *_get_typed_device(const char *id,
                                       int type,
                                       const CMPIObjectPath *ref,
                                       CMPIStatus *s)
{
        virConnectPtr conn = NULL;
        CMPIInstance *dev = NULL;
        const char *typestr;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), s);
        if (conn == NULL)
                goto out;

        if (type == CIM_RASD_TYPE_DISK)
                typestr = "LogicalDisk";
        else if (type == CIM_RASD_TYPE_MEM)
                typestr = "Memory";
        else if (type == CIM_RASD_TYPE_PROC)
                typestr = "Processor";
        else if (type == CIM_RASD_TYPE_NET)
                typestr = "NetworkPort";
        else {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid device type (%i)", type);
                goto out;
        }

        dev = instance_from_devid(_BROKER,
                                  conn,
                                  id,
                                  NAMESPACE(ref),
                                  device_type_from_classname(typestr));
 out:
        virConnectClose(conn);

        return dev;
}

static CMPIStatus rasd_to_dev(const CMPIObjectPath *ref,
                              struct std_assoc_info *info,
                              struct inst_list *list)
{
        CMPIStatus s;
        CMPIInstance *dev = NULL;
        const char *id = NULL;
        uint16_t type;

        ASSOC_MATCH(info->provider_name, CLASSNAME(ref));

        if (cu_get_str_path(ref, "InstanceID", &id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing InstanceID");
                goto out;
        }

        if (rasd_type_from_classname(CLASSNAME(ref), &type) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing ResourceType");
                goto out;
        }

        dev = _get_typed_device(id, type, ref, &s);
        if (dev == NULL)
                goto out;

        inst_list_add(list, dev);

        CMSetStatus(&s, CMPI_RC_OK);

 out:
        return s;
}

static CMPIStatus vs_to_vssd(const CMPIObjectPath *ref,
                             struct std_assoc_info *info,
                             struct inst_list *list)
{
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        const char *name;
        CMPIInstance *vssd;
        CMPIStatus s;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                return s;

        if (cu_get_str_path(ref, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing Name property");
                goto out;
        }

        dom = virDomainLookupByName(conn, name);
        if (dom == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "No such domain `%s'", name);
                goto out;
        }

        vssd = get_vssd_instance(dom, _BROKER, ref);
        if (vssd != NULL)
                inst_list_add(list, vssd);

        CMSetStatus(&s, CMPI_RC_OK);

 out:
        virDomainFree(dom);
        virConnectClose(conn);

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
        virConnectPtr conn = NULL;
        CMPIStatus s;
        CMPIInstance *cs;

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

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        cs = instance_from_name(_BROKER,
                                conn,
                                name,
                                ref);
        if (cs != NULL)
                inst_list_add(list, cs);

        CMSetStatus(&s, CMPI_RC_OK);

 out:
        free(name);
        free(pfx);

        virConnectClose(conn);

        return s;
}

static CMPIInstance *make_ref(const CMPIObjectPath *ref,
                              const CMPIInstance *inst,
                              struct std_assoc_info *info,
                              struct std_assoc *assoc)
{
        CMPIInstance *refinst = NULL;

        refinst = get_typed_instance(_BROKER,
                                     CLASSNAME(ref),
                                     "SettingsDefineState",
                                     NAMESPACE(ref));

        if (refinst != NULL) {
                CMPIObjectPath *instop;

                instop = CMGetObjectPath(inst, NULL);

                set_reference(assoc, refinst, ref, instop);
        }

        return refinst;
}

char* logical_device[] = {
        "Xen_Processor",
        "Xen_Memory",
        "Xen_NetworkPort",
        "Xen_LogicalDisk",
        "KVM_Processor",
        "KVM_Memory",
        "KVM_NetworkPort",
        "KVM_LogicalDisk",
        NULL
};

char* resource_allocation_setting_data[] = {
        "Xen_DiskResourceAllocationSettingData",
        "Xen_MemResourceAllocationSettingData",
        "Xen_NetResourceAllocationSettingData",
        "Xen_ProcResourceAllocationSettingData",
        "KVM_DiskResourceAllocationSettingData",
        "KVM_MemResourceAllocationSettingData",
        "KVM_NetResourceAllocationSettingData",
        "KVM_ProcResourceAllocationSettingData",
        NULL
};

char* computer_system[] = {
        "Xen_ComputerSystem",
        "KVM_ComputerSystem",
        NULL
};

char* virtual_system_setting_data[] = {
        "Xen_VirtualSystemSettingData",
        "KVM_VirtualSystemSettingData",        
        NULL
};

char* assoc_classname[] = {
        "Xen_SettingsDefineState",
        "KVM_SettingsDefineState",        
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

        .handler = vs_to_vssd,
        .make_ref = make_ref
};

static struct std_assoc _vssd_to_vs = {
        .source_class = (char**)&virtual_system_setting_data,
        .source_prop = "SettingData",

        .target_class = (char**)&computer_system,
        .target_prop = "ManagedElement",

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

STDA_AssocMIStub(, Xen_SettingsDefineStateProvider, _BROKER, libvirt_cim_init(), handlers);
STDA_AssocMIStub(, KVM_SettingsDefineStateProvider, _BROKER, libvirt_cim_init(), handlers);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
