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

/* Associate an XXX_Capabilities to the proper XXX_ManagedElement.
 *
 *  -- or --
 *
 * Associate an XXX_ManagedElement to the proper XXX_Capabilities.
 */

const static CMPIBroker *_BROKER;

static CMPIStatus validate_caps_get_service(const CMPIObjectPath *ref,
                                            CMPIInstance **inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *_inst;
        char* classname;

        classname = class_base_name(CLASSNAME(ref));

        if (STREQC(classname, "VirtualSystemManagementCapabilities")) {
                s = get_vsm_cap(_BROKER, ref, &_inst, true);
                if ((s.rc != CMPI_RC_OK) || (_inst == NULL))
                        goto out;

                s = get_vsms(ref, &_inst, _BROKER, false);
        } else if (STREQC(classname, "VirtualSystemMigrationCapabilities")) {
                s = get_migration_caps(ref, &_inst, _BROKER, true);
                if ((s.rc != CMPI_RC_OK) || (_inst == NULL))
                        goto out;

                s = get_migration_service(ref, &_inst, _BROKER, false);
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

static CMPIStatus validate_service_get_caps(const CMPIObjectPath *ref,
                                            CMPIInstance **inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *_inst;
        char* classname;

        classname = class_base_name(CLASSNAME(ref));

        if (STREQC(classname, "VirtualSystemManagementService")) {
                s = get_vsms(ref, &_inst, _BROKER, true);
                if ((s.rc != CMPI_RC_OK) || (_inst == NULL))
                        goto out;

                s = get_vsm_cap(_BROKER, ref, &_inst, false);
        } else if (STREQC(classname, "VirtualSystemMigrationService")) {
                s = get_migration_service(ref, &_inst, _BROKER, true);
                if ((s.rc != CMPI_RC_OK) || (_inst == NULL))
                        goto out;

                s = get_migration_caps(ref, &_inst, _BROKER, false);
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

        if (!match_hypervisor_prefix(ref, info))
                goto out;

        s = get_host(_BROKER, ref, &inst, true);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = get_vsm_cap(_BROKER, ref, &inst, false);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, inst);

        s = get_migration_caps(ref, &inst, _BROKER, false);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, inst);

 out:
        return s;
}

static CMPIStatus cap_to_sys_or_service(const CMPIObjectPath *ref,
                                        struct std_assoc_info *info,
                                        struct inst_list *list)
{
        CMPIInstance *inst = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        
        if (!match_hypervisor_prefix(ref, info))
                goto out;

        s = validate_caps_get_service(ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;
        
        if (inst != NULL)
                inst_list_add(list, inst);

        s = get_host(_BROKER, ref, &inst, false);
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

        s = validate_service_get_caps(ref, &inst);
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

        s = get_domain(_BROKER, ref, &inst);
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
        CMPIInstance *inst;
        virConnectPtr conn;
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

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        inst = instance_from_name(_BROKER, conn, inst_id, ref);
        if (inst)
                inst_list_add(list, inst);
        else
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s)", inst_id);
        
        virConnectClose(conn);

 out:
        return s;
}

static CMPIStatus alloc_to_pool(const CMPIObjectPath *ref,
                                struct std_assoc_info *info,
                                struct inst_list *list)
{
        /* Pool to alloc is more important.  That will be done first. */
        RETURN_UNSUPPORTED();
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

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char* assoc_classname[] = {
        "Xen_ElementCapabilities",
        "KVM_ElementCapabilities",        
        NULL
};

static char* host_system[] = {
        "Xen_HostSystem",
        "KVM_HostSystem",
        NULL
};

static char* host_sys_and_service[] = {
        "Xen_HostSystem",
        "KVM_HostSystem",
        "Xen_VirtualSystemManagementService",
        "KVM_VirtualSystemManagementService",
        "Xen_VirtualSystemMigrationService",
        "KVM_VirtualSystemMigrationService",
        NULL
};

static char* virtual_system_management_capabilities[] = {
        "Xen_VirtualSystemManagementCapabilities",
        "Xen_VirtualSystemMigrationCapabilities",
        "KVM_VirtualSystemManagementCapabilities",
        "KVM_VirtualSystemMigrationCapabilities",
        NULL,
};

static struct std_assoc system_to_vsm_cap = {
        .source_class = (char**)&host_system,
        .source_prop = "ManagedElement",

        .target_class = (char**)&virtual_system_management_capabilities,
        .target_prop = "Capabilities",

        .assoc_class = (char**)&assoc_classname,

        .handler = sys_to_cap,
        .make_ref = make_ref
};

static struct std_assoc vsm_cap_to_sys_or_service = {
        .source_class = (char**)&virtual_system_management_capabilities,
        .source_prop = "Capabilities",

        .target_class = (char**)&host_sys_and_service,
        .target_prop = "ManagedElement",

        .assoc_class = (char**)&assoc_classname,

        .handler = cap_to_sys_or_service,
        .make_ref = make_ref
};

static char* service[] = {
        "Xen_VirtualSystemManagementService",
        "KVM_VirtualSystemManagementService",
        "Xen_VirtualSystemMigrationService",
        "KVM_VirtualSystemMigrationService",
        NULL
};

static struct std_assoc _service_to_cap = {
        .source_class = (char**)&service,
        .source_prop = "ManagedElement",

        .target_class = (char**)&virtual_system_management_capabilities,
        .target_prop = "Capabilities",

        .assoc_class = (char**)&assoc_classname,

        .handler = service_to_cap,
        .make_ref = make_ref
};

static char* computer_system[] = {
        "Xen_ComputerSystem",
        "KVM_ComputerSystem",
        NULL
};

static char* enabled_logical_element_capabilities[] = {
        "Xen_EnabledLogicalElementCapabilities",
        "KVM_EnabledLogicalElementCapabilities",
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
        NULL
};

static char* resource_pool[] = {
        "Xen_ProcessorPool",
        "Xen_MemoryPool",
        "Xen_NetworkPool",
        "Xen_DiskPool",
        "KVM_ProcessorPool",
        "KVM_MemoryPool",
        "KVM_NetworkPool",
        "KVM_DiskPool",
        NULL
};

static struct std_assoc alloc_cap_to_resource_pool = {
        .source_class = (char**)&allocation_capabilities,
        .source_prop = "Capabilities",

        .target_class = (char**)&resource_pool,
        .target_prop = "ManagedElement",

        .assoc_class = (char**)&assoc_classname,

        .handler = alloc_to_pool,
        .make_ref = make_ref
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
        &vsm_cap_to_sys_or_service,
        &_service_to_cap,
        &ele_cap_to_cs,
        &cs_to_ele_cap,
        &alloc_cap_to_resource_pool,
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
