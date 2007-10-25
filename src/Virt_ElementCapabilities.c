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
#include "device_parsing.h"

#include "Virt_VirtualSystemManagementCapabilities.h"
#include "Virt_EnabledLogicalElementCapabilities.h"
#include "Virt_ComputerSystem.h"
#include "Virt_HostSystem.h"

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
        CMPIrc host_rc;
        char *host_name = NULL;
        char *sys_name = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        s = get_host_cs(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        host_rc = cu_get_str_prop(inst, "Name", &host_name);
        if (host_rc != CMPI_RC_OK)
                goto out;
        
        sys_name = cu_get_str_path(ref, "Name");
        if (!STREQ(sys_name, host_name)) {
                cu_statusf(_BROKER, &s, CMPI_RC_ERR_FAILED,
                           "System '%s' is not a host system.", sys_name);
                goto out;
        }

        s = get_vsm_cap(_BROKER, ref, &inst);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, inst);
 out:
        free(sys_name);
        return s;
}

static CMPIStatus cap_to_sys(const CMPIObjectPath *ref,
                             struct std_assoc_info *info,
                             struct inst_list *list)
{
        char *inst_id;
        char *host;
        char *device;
        char *host_name;
        CMPIrc host_rc;
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        inst_id = cu_get_str_path(ref, "InstanceID");
        if (inst_id == NULL) {
                CMSetStatusWithChars(_BROKER, &s, CMPI_RC_ERR_FAILED,
                                     "Could not get InstanceID.");
                goto out;
        }

        if (!parse_fq_devid(inst_id, &host, &device)) {
                CMSetStatusWithChars(_BROKER, &s, CMPI_RC_ERR_FAILED,
                                     "Could not get system name.");
                goto out;
        }

        s = get_host_cs(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        host_rc = cu_get_str_prop(inst, "Name", &host_name);
        if (host_rc != CMPI_RC_OK)
                goto out;

        if (!STREQ(host_name, host))
                goto out;

        inst_list_add(list, inst);

 out:
        free(host);
        free(device);
        free(inst_id);
        return s;
}

static CMPIStatus cs_to_cap(const CMPIObjectPath *ref,
                            struct std_assoc_info *info,
                            struct inst_list *list)
{
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        s = get_ele_cap(_BROKER, ref, &inst);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, inst);
        
        return s;
}

static CMPIStatus cap_to_cs(const CMPIObjectPath *ref,
                            struct std_assoc_info *info,
                            struct inst_list *list)
{
        char *inst_id;
        char *host;
        char *device;
        CMPIInstance *inst;
        virConnectPtr conn;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        inst_id = cu_get_str_path(ref, "InstanceID");
        if (inst_id == NULL) {
                CMSetStatusWithChars(_BROKER, &s, CMPI_RC_ERR_FAILED,
                                     "Could not get InstanceID.");
                goto error1;
        }

        if (!parse_fq_devid(inst_id, &host, &device)) {
                CMSetStatusWithChars(_BROKER, &s, CMPI_RC_ERR_FAILED,
                                     "Could not get system name.");
                goto error1;
        }

        conn = lv_connect(_BROKER, &s);
        if (s.rc != CMPI_RC_OK)
                goto error1;

        inst = instance_from_name(_BROKER, conn, host, ref);
        if (inst)
                inst_list_add(list, inst);

        virConnectClose(conn);
 error1:
        free(host);
        free(device);
        free(inst_id);
        return s;
}

static CMPIInstance *make_ref(const CMPIObjectPath *ref,
                              const CMPIInstance *inst,
                              struct std_assoc_info *info,
                              struct std_assoc *assoc)
{
        CMPIInstance *refinst;
        char *base;

        base = class_base_name(assoc->assoc_class);
        if (base == NULL)
                return NULL;

        refinst = get_typed_instance(_BROKER,
                                     base,
                                     NAMESPACE(ref));

        if (refinst != NULL) {
                CMPIObjectPath *instop;

                instop = CMGetObjectPath(inst, NULL);

                CMSetProperty(refinst, assoc->source_prop,
                              (CMPIValue *)&ref, CMPI_ref);
                CMSetProperty(refinst, assoc->target_prop,
                              (CMPIValue *)&instop, CMPI_ref);
        }

        free(base);

        return refinst;
}

struct std_assoc system_to_vsm_cap = {
        .source_class = "CIM_System",
        .source_prop = "ManagedElement",

        .target_class = "CIM_VirtualSystemManagementCapabilities",
        .target_prop = "Capabilities",

        .assoc_class = "CIM_ElementCapabilities",

        .handler = sys_to_cap,
        .make_ref = make_ref
};

struct std_assoc vsm_cap_to_system = {
        .source_class = "CIM_VirtualSystemManagementCapabilities",
        .source_prop = "Capabilities",

        .target_class = "CIM_System",
        .target_prop = "ManagedElement",

        .assoc_class = "CIM_ElementCapabilities",

        .handler = cap_to_sys,
        .make_ref = make_ref
};

struct std_assoc xen_cs_to_ele_cap = {
        .source_class = "Xen_ComputerSystem",
        .source_prop = "ManagedElement",

        .target_class = "CIM_EnabledLogicalElementCapabilities",
        .target_prop = "Capabilities",

        .assoc_class = "CIM_ElementCapabilities",

        .handler = cs_to_cap,
        .make_ref = make_ref
};

struct std_assoc kvm_cs_to_ele_cap = {
        .source_class = "KVM_ComputerSystem",
        .source_prop = "ManagedElement",

        .target_class = "CIM_EnabledLogicalElementCapabilities",
        .target_prop = "Capabilities",

        .assoc_class = "CIM_ElementCapabilities",

        .handler = cs_to_cap,
        .make_ref = make_ref
};

struct std_assoc ele_cap_to_computer_system = {
        .source_class = "CIM_EnabledLogicalElementCapabilities",
        .source_prop = "Capabilities",

        .target_class = "CIM_ComputerSystem",
        .target_prop = "ManagedElement",

        .assoc_class = "CIM_ElementCapabilities",

        .handler = cap_to_cs,
        .make_ref = make_ref
};

struct std_assoc *assoc_handlers[] = {
        &xen_cs_to_ele_cap,
        &kvm_cs_to_ele_cap,
        &system_to_vsm_cap,
        &vsm_cap_to_system,
        &ele_cap_to_computer_system,
        NULL
};


STDA_AssocMIStub(, Xen_ElementCapabilitiesProvider, _BROKER, CMNoHook, assoc_handlers);
STDA_AssocMIStub(, KVM_ElementCapabilitiesProvider, _BROKER, CMNoHook, assoc_handlers);
/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
