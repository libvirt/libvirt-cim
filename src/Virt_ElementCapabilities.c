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

#include "libcmpiutil.h"
#include "misc_util.h"
#include "std_association.h"

#include "Virt_VirtualSystemManagementCapabilities.h"
#include "Virt_EnabledLogicalElementCapabilities.h"
#include "Virt_ComputerSystem.h"
#include "Virt_HostSystem.h"
#include "Virt_VSMigrationCapabilities.h"

/* Associate an XXX_Capabilities to the proper XXX_ManagedElement.
 *
 *  -- or --
 *
 * Associate an XXX_ManagedElement to the proper XXX_Capabilities.
 */

const static CMPIBroker *_BROKER;

static CMPIStatus sys_to_cap(const CMPIObjectPath *ref,
                             struct std_assoc_info *info,
                             struct inst_list *list)
{
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        const char *prop;

        s = get_host_cs(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        prop = cu_compare_ref(ref, inst);
        if (prop != NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "No such HostSystem (%s)", prop);
                goto out;
        }

        s = get_vsm_cap(_BROKER, ref, &inst);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, inst);

        s = get_migration_caps(ref, &inst, _BROKER);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, inst);

 out:
        return s;
}

static CMPIStatus cap_to_sys(const CMPIObjectPath *ref,
                             struct std_assoc_info *info,
                             struct inst_list *list)
{
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        s = get_host_cs(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

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

        if (cu_get_str_path(ref, "Name", &sys_name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing key: Name");
                goto out;
        }

        s = get_ele_cap(_BROKER, ref, sys_name, &inst);
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

        if (cu_get_str_path(ref, "InstanceID", &inst_id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not get InstanceID");
                goto error1;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (s.rc != CMPI_RC_OK)
                goto error1;

        inst = instance_from_name(_BROKER, conn, inst_id, ref);
        if (inst)
                inst_list_add(list, inst);

        virConnectClose(conn);
 error1:

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
        int ret;
        const char *inst_id;
        uint16_t type;
        CMPIInstance *inst = NULL;
        CMPIStatus s = {CMPI_RC_OK};

        if (cu_get_str_path(ref, "InstanceID", &inst_id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not get InstanceID");
                goto out;
        }

        inst = get_typed_instance(_BROKER,
                                  CLASSNAME(ref),
                                  "AllocationCapabilities",
                                  NAMESPACE(ref));
        CMSetProperty(inst, "InstanceID", inst_id, CMPI_chars);
        
        ret = cu_get_u16_path(ref, "ResourceType", &type);
        if (ret != 1) {
                cu_statusf(_BROKER, &s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not get ResourceType");
                goto out;
        }
        CMSetProperty(inst, "ResourceType", &type, CMPI_uint16);

        inst_list_add(list, inst);
        
 out:
        return s;
}

static CMPIInstance *make_ref(const CMPIObjectPath *ref,
                              const CMPIInstance *inst,
                              struct std_assoc_info *info,
                              struct std_assoc *assoc)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *refinst = NULL;
        virConnectPtr conn = NULL;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                return NULL;

        refinst = get_typed_instance(_BROKER,
                                     pfx_from_conn(conn),
                                     "ElementCapabilities",
                                     NAMESPACE(ref));

        if (refinst != NULL) {
                CMPIObjectPath *instop;

                instop = CMGetObjectPath(inst, NULL);

                CMSetProperty(refinst, assoc->source_prop,
                              (CMPIValue *)&ref, CMPI_ref);
                CMSetProperty(refinst, assoc->target_prop,
                              (CMPIValue *)&instop, CMPI_ref);
        }

        virConnectClose(conn);

        return refinst;
}

char* assoc_classname[] = {
        "Xen_ElementCapabilities",
        "KVM_ElementCapabilities",        
        NULL
};

char* host_system[] = {
        "Xen_HostSystem",
        "KVM_HostSystem",
        NULL
};

char* virtual_system_management_capabilities[] = {
        "Xen_VirtualSystemManagementCapabilities",
        "Xen_VirtualSystemMigrationCapabilities",
        "KVM_VirtualSystemManagementCapabilities",
        "KVM_VirtualSystemMigrationCapabilities",
        NULL,
};

struct std_assoc system_to_vsm_cap = {
        .source_class = (char**)&host_system,
        .source_prop = "ManagedElement",

        .target_class = (char**)&virtual_system_management_capabilities,
        .target_prop = "Capabilities",

        .assoc_class = (char**)&assoc_classname,

        .handler = sys_to_cap,
        .make_ref = make_ref
};

struct std_assoc vsm_cap_to_system = {
        .source_class = (char**)&virtual_system_management_capabilities,
        .source_prop = "Capabilities",

        .target_class = (char**)&host_system,
        .target_prop = "ManagedElement",

        .assoc_class = (char**)&assoc_classname,

        .handler = cap_to_sys,
        .make_ref = make_ref
};

char* computer_system[] = {
        "Xen_ComputerSystem",
        "KVM_ComputerSystem",
        NULL
};

char* enabled_logical_element_capabilities[] = {
        "Xen_EnabledLogicalElementCapabilities",
        "KVM_EnabledLogicalElementCapabilities",
        NULL
};

struct std_assoc ele_cap_to_cs = {
        .source_class = (char**)&enabled_logical_element_capabilities,
        .source_prop = "Capabilities",

        .target_class = (char**)&computer_system,
        .target_prop = "ManagedElement",

        .assoc_class = (char**)&assoc_classname,

        .handler = cap_to_cs,
        .make_ref = make_ref
};

struct std_assoc cs_to_ele_cap = {
        .source_class = (char**)&computer_system,
        .source_prop = "ManagedElement",

        .target_class = (char**)&enabled_logical_element_capabilities,
        .target_prop = "Capabilities",

        .assoc_class = (char**)&assoc_classname,

        .handler = cs_to_cap,
        .make_ref = make_ref
};

char* allocation_capabilities[] = {
        "Xen_AllocationCapabilities",
        "KVM_AllocationCapabilities",
        NULL
};

char* resource_pool[] = {
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

struct std_assoc alloc_cap_to_resource_pool = {
        .source_class = (char**)&allocation_capabilities,
        .source_prop = "Capabilities",

        .target_class = (char**)&resource_pool,
        .target_prop = "ManagedElement",

        .assoc_class = (char**)&assoc_classname,

        .handler = alloc_to_pool,
        .make_ref = make_ref
};

struct std_assoc resource_pool_to_alloc_cap = {
        .source_class = (char**)&resource_pool,
        .source_prop = "ManagedElement",

        .target_class = (char**)&allocation_capabilities,
        .target_prop = "Capabilities",

        .assoc_class = (char**)&assoc_classname,

        .handler = pool_to_alloc,
        .make_ref = make_ref
};

struct std_assoc *assoc_handlers[] = {
        &system_to_vsm_cap,
        &vsm_cap_to_system,
        &ele_cap_to_cs,
        &cs_to_ele_cap,
        &alloc_cap_to_resource_pool,
        &resource_pool_to_alloc_cap,
        NULL
};

STDA_AssocMIStub(, Xen_ElementCapabilitiesProvider, _BROKER, libvirt_cim_init(), assoc_handlers);
STDA_AssocMIStub(, KVM_ElementCapabilitiesProvider, _BROKER, libvirt_cim_init(), assoc_handlers);
/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
