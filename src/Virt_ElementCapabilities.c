/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *  Jay Gagnon <grendel@linux.vnet.ibm.com>
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include <libcmpiutil/libcmpiutil.h>
#include "misc_util.h"
#include <libcmpiutil/std_association.h>

#include "Virt_VirtualSystemManagementCapabilities.h"
#include "Virt_VirtualSystemManagementService.h"
#include "Virt_VSMigrationService.h"
#include "Virt_EnabledLogicalElementCapabilities.h"
#include "Virt_ComputerSystem.h"
#include "Virt_HostSystem.h"
#include "Virt_VSMigrationCapabilities.h"
#include "Virt_AllocationCapabilities.h"
#include "Virt_DevicePool.h"
#include "Virt_ConsoleRedirectionService.h"
#include "Virt_ConsoleRedirectionServiceCapabilities.h"

#include "svpc_types.h"

/* Associate an XXX_Capabilities to the proper XXX_ManagedElement.
 *
 *  -- or --
 *
 * Associate an XXX_ManagedElement to the proper XXX_Capabilities.
 */

const static CMPIBroker *_BROKER;

static CMPIStatus validate_ac_get_rp(const CMPIObjectPath *ref,
                                     CMPIInstance **inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        const char *poolid;

        if (cu_get_str_path(ref, "InstanceID", &poolid) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing InstanceID");
                goto out;
        }       

        s = get_alloc_cap_by_id(_BROKER, ref, poolid, inst);
        if ((inst == NULL) || (s.rc != CMPI_RC_OK))
                goto out;

        s = get_pool_by_name(_BROKER, ref, poolid, inst); 

 out:
        return s;
}

static CMPIStatus validate_caps_get_service_or_rp(const CMPIContext *context,
                                                  const CMPIObjectPath *ref,
                                                  CMPIInstance **inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *_inst = NULL;
        char* classname;

        classname = class_base_name(CLASSNAME(ref));

        if (STREQC(classname, "VirtualSystemManagementCapabilities")) {
                s = get_vsm_cap(_BROKER, ref, &_inst, true);
                if ((s.rc != CMPI_RC_OK) || (_inst == NULL))
                        goto out;

                s = get_vsms(ref, &_inst, _BROKER, context, false);
        } else if (STREQC(classname, "VirtualSystemMigrationCapabilities")) {
                s = get_migration_caps(ref, &_inst, _BROKER, true);
                if ((s.rc != CMPI_RC_OK) || (_inst == NULL))
                        goto out;

                s = get_migration_service(ref, &_inst, _BROKER, context, false);
        } else if (STREQC(classname, "ConsoleRedirectionServiceCapabilities")) {
                s = get_console_rs_caps(_BROKER, ref, &_inst, true);
                if ((s.rc != CMPI_RC_OK) || (_inst == NULL))
                        goto out;

                s = get_console_rs(ref, &_inst, _BROKER, context, false);
        } else if (STREQC(classname, "AllocationCapabilities")) {
                s = validate_ac_get_rp(ref, &_inst);
        } else 
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "Not found");

        
        if ((s.rc != CMPI_RC_OK) || (_inst == NULL))
                goto out;

        *inst = _inst;
 out:
        free(classname);

        return s;
}

static CMPIStatus validate_service_get_caps(const CMPIContext *context,
                                            const CMPIObjectPath *ref,
                                            CMPIInstance **inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
                CMPIInstance *_inst = NULL;
        char* classname;

        classname = class_base_name(CLASSNAME(ref));

        if (STREQC(classname, "VirtualSystemManagementService")) {
                s = get_vsms(ref, &_inst, _BROKER, context, true);
                if ((s.rc != CMPI_RC_OK) || (_inst == NULL))
                        goto out;

                s = get_vsm_cap(_BROKER, ref, &_inst, false);
        } else if (STREQC(classname, "VirtualSystemMigrationService")) {
                s = get_migration_service(ref, &_inst, _BROKER, context, true);
                if ((s.rc != CMPI_RC_OK) || (_inst == NULL))
                        goto out;

                s = get_migration_caps(ref, &_inst, _BROKER, false);
        } else if (STREQC(classname, "ConsoleRedirectionService")) {
                s = get_console_rs(ref, &_inst, _BROKER, context, true);
                if ((s.rc != CMPI_RC_OK) || (_inst == NULL))
                        goto out;
		
                s = get_console_rs_caps(_BROKER, ref, &_inst, false);	
        } else
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "");

        *inst = _inst;

 out:
        free(classname);

        return s;
}

static CMPIStatus sys_to_cap(const CMPIObjectPath *ref,
                             struct std_assoc_info *info,
                             struct inst_list *list)
{
        CMPIInstance *inst = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIObjectPath *vref = NULL;

        if (!STARTS_WITH(CLASSNAME(ref), "Linux_") &&
            !match_hypervisor_prefix(ref, info))
                goto out;

        s = get_host(_BROKER, info->context, ref, &inst, true);
        if (s.rc != CMPI_RC_OK)
                goto out;

        vref = convert_sblim_hostsystem(_BROKER, ref, info);
        if (vref == NULL)
                goto out;

        s = get_vsm_cap(_BROKER, vref, &inst, false);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, inst);

        s = get_migration_caps(vref, &inst, _BROKER, false);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, inst);

        s = get_console_rs_caps(_BROKER, vref, &inst, false);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, inst);

        s = enum_alloc_cap_instances(_BROKER, vref, NULL, NULL, list);
        if (s.rc != CMPI_RC_OK) {
                CU_DEBUG("Failed to enum AC: %s",
                         CMGetCharPtr(s.msg));
        }
 out:
        return s;
}

static CMPIStatus cap_to_sys_or_service_or_rp(const CMPIObjectPath *ref,
                                              struct std_assoc_info *info,
                                              struct inst_list *list)
{
        CMPIInstance *inst = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        
        if (!match_hypervisor_prefix(ref, info))
                goto out;

        s = validate_caps_get_service_or_rp(info->context, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;
        
        if (inst != NULL)
                inst_list_add(list, inst);

        s = get_host(_BROKER, info->context, ref, &inst, false);
        if (s.rc != CMPI_RC_OK)
                goto out;

        inst_list_add(list, inst);

 out:
        return s;
}

static CMPIStatus service_to_cap(const CMPIObjectPath *ref,
                                 struct std_assoc_info *info,
                                 struct inst_list *list)
{
        CMPIInstance *inst = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if (!match_hypervisor_prefix(ref, info))
                goto out;

        s = validate_service_get_caps(info->context, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (inst != NULL)
                inst_list_add(list, inst);
 out:
        return s;
}

static CMPIStatus cs_to_cap(const CMPIObjectPath *ref,
                            struct std_assoc_info *info,
                            struct inst_list *list)
{
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        const char *sys_name = NULL;

        if (!match_hypervisor_prefix(ref, info))
                goto out;

        s = get_domain_by_ref(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (cu_get_str_path(ref, "Name", &sys_name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing key: Name");
                goto out;
        }

        s = get_elec_by_name(_BROKER, ref, sys_name, &inst);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, inst);

  out:
        return s;
}

static CMPIStatus cap_to_cs(const CMPIObjectPath *ref,
                            struct std_assoc_info *info,
                            struct inst_list *list)
{
        const char *inst_id;
        CMPIInstance *inst = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if (!match_hypervisor_prefix(ref, info))
                goto out;

        s = get_elec_by_ref(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (cu_get_str_path(ref, "InstanceID", &inst_id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not get InstanceID");
                goto out;
        }

        s = get_domain_by_name(_BROKER, ref, inst_id, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        inst_list_add(list, inst);

 out:
        return s;
}

static CMPIStatus pool_to_alloc(const CMPIObjectPath *ref,
                                struct std_assoc_info *info,
                                struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        const char *inst_id;

        if (!match_hypervisor_prefix(ref, info))
                goto out;

        if (cu_get_str_path(ref, "InstanceID", &inst_id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not get InstanceID");
                goto out;
        }

        s = enum_alloc_cap_instances(_BROKER,
                                     ref,
                                     NULL,
                                     inst_id,
                                     list);
        
 out:
        return s;
}

static CMPIInstance *make_ref_default(const CMPIObjectPath *source_ref,
                                      const CMPIInstance *target_inst,
                                      struct std_assoc_info *info,
                                      struct std_assoc *assoc)
{
        CMPIInstance *ref_inst = NULL;
        CMPIArray *array = NULL;
        CMPIStatus s;
        uint16_t val = CIM_EC_CHAR_DEFAULT;

        ref_inst = make_reference(_BROKER,
                                  source_ref,
                                  target_inst,
                                  info,
                                  assoc);

        array = CMNewArray(_BROKER, 1, CMPI_uint16, &s);
        if ((array == NULL) || (s.rc != CMPI_RC_OK)) {
                CU_DEBUG("Unable to allocate Characteristics array");
                goto out;
        }

        CMSetArrayElementAt(array, 0, &val, CMPI_uint16);

        CMSetProperty(ref_inst, "Characteristics",
                      (CMPIValue *)&array, CMPI_uint16A);

 out:
        return ref_inst;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char* assoc_classname[] = {
        "Xen_ElementCapabilities",
        "KVM_ElementCapabilities",
        "LXC_ElementCapabilities",
        NULL
};

static char* host_system[] = {
        "Xen_HostSystem",
        "KVM_HostSystem",
        "LXC_HostSystem",
        "Linux_ComputerSystem",
        NULL
};

static char* host_sys_and_service_and_rp[] = {
        "Xen_HostSystem",
        "Xen_VirtualSystemManagementService",
        "Xen_VirtualSystemMigrationService",
        "Xen_ConsoleRedirectionService",
        "KVM_HostSystem",
        "KVM_VirtualSystemManagementService",
        "KVM_VirtualSystemMigrationService",
        "KVM_ConsoleRedirectionService",
        "LXC_HostSystem",
        "LXC_VirtualSystemManagementService",
        "LXC_VirtualSystemMigrationService",
        "LXC_ConsoleRedirectionService",
        "Linux_ComputerSystem",
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

static char *host_caps[] = {
        "Xen_VirtualSystemManagementCapabilities",
        "Xen_VirtualSystemMigrationCapabilities",
        "Xen_ConsoleRedirectionServiceCapabilities",
        "KVM_VirtualSystemManagementCapabilities",
        "KVM_VirtualSystemMigrationCapabilities",
        "KVM_ConsoleRedirectionServiceCapabilities",
        "LXC_VirtualSystemManagementCapabilities",
        "LXC_VirtualSystemMigrationCapabilities",
        "LXC_ConsoleRedirectionServiceCapabilities",
        "Xen_AllocationCapabilities",
        "KVM_AllocationCapabilities",
        "LXC_AllocationCapabilities",
        NULL,
};

static struct std_assoc system_to_vsm_cap = {
        .source_class = (char**)&host_system,
        .source_prop = "ManagedElement",

        .target_class = (char**)&host_caps,
        .target_prop = "Capabilities",

        .assoc_class = (char**)&assoc_classname,

        .handler = sys_to_cap,
        .make_ref = make_ref_default
};

static struct std_assoc vsm_cap_to_sys_or_service_or_rp = {
        .source_class = (char**)&host_caps,
        .source_prop = "Capabilities",

        .target_class = (char**)&host_sys_and_service_and_rp,
        .target_prop = "ManagedElement",

        .assoc_class = (char**)&assoc_classname,

        .handler = cap_to_sys_or_service_or_rp,
        .make_ref = make_ref
};

static char* service[] = {
        "Xen_VirtualSystemManagementService",
        "KVM_VirtualSystemManagementService",
        "LXC_VirtualSystemManagementService",
        "Xen_VirtualSystemMigrationService",
        "KVM_VirtualSystemMigrationService",
        "LXC_VirtualSystemMigrationService",
        "Xen_ConsoleRedirectionService",
        "KVM_ConsoleRedirectionService",
        "LXC_ConsoleRedirectionService",
        NULL
};

static struct std_assoc _service_to_cap = {
        .source_class = (char**)&service,
        .source_prop = "ManagedElement",

        .target_class = (char**)&host_caps,
        .target_prop = "Capabilities",

        .assoc_class = (char**)&assoc_classname,

        .handler = service_to_cap,
        .make_ref = make_ref
};

static char* computer_system[] = {
        "Xen_ComputerSystem",
        "KVM_ComputerSystem",
        "LXC_ComputerSystem",
        NULL
};

static char* enabled_logical_element_capabilities[] = {
        "Xen_EnabledLogicalElementCapabilities",
        "KVM_EnabledLogicalElementCapabilities",
        "LXC_EnabledLogicalElementCapabilities",
        NULL
};

static struct std_assoc ele_cap_to_cs = {
        .source_class = (char**)&enabled_logical_element_capabilities,
        .source_prop = "Capabilities",

        .target_class = (char**)&computer_system,
        .target_prop = "ManagedElement",

        .assoc_class = (char**)&assoc_classname,

        .handler = cap_to_cs,
        .make_ref = make_ref
};

static struct std_assoc cs_to_ele_cap = {
        .source_class = (char**)&computer_system,
        .source_prop = "ManagedElement",

        .target_class = (char**)&enabled_logical_element_capabilities,
        .target_prop = "Capabilities",

        .assoc_class = (char**)&assoc_classname,

        .handler = cs_to_cap,
        .make_ref = make_ref
};

static char* allocation_capabilities[] = {
        "Xen_AllocationCapabilities",
        "KVM_AllocationCapabilities",
        "LXC_AllocationCapabilities",
        NULL
};

static char* resource_pool[] = {
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

static struct std_assoc resource_pool_to_alloc_cap = {
        .source_class = (char**)&resource_pool,
        .source_prop = "ManagedElement",

        .target_class = (char**)&allocation_capabilities,
        .target_prop = "Capabilities",

        .assoc_class = (char**)&assoc_classname,

        .handler = pool_to_alloc,
        .make_ref = make_ref
};

static struct std_assoc *assoc_handlers[] = {
        &system_to_vsm_cap,
        &vsm_cap_to_sys_or_service_or_rp,
        &_service_to_cap,
        &ele_cap_to_cs,
        &cs_to_ele_cap,
        &resource_pool_to_alloc_cap,
        NULL
};

STDA_AssocMIStub(, 
                 Virt_ElementCapabilities,
                 _BROKER,
                 libvirt_cim_init(), 
                 assoc_handlers);
/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
