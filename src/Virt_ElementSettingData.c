/*
 * Copyright IBM Corp. 2007-2014
 *
 * Authors:
 *  Kaitlin Rupert <karupert@us.ibm.com>
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
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_association.h>
#include "misc_util.h"

#include "Virt_VSSD.h"
#include "Virt_RASD.h"

const static CMPIBroker *_BROKER;

static CMPIStatus vssd_to_vssd(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        /* Special association case: 
         * VSSD instance is pointing to itself
         */
        s = get_vssd_by_ref(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        inst_list_add(list, inst);

 out:
        return s;
}

static CMPIStatus rasd_to_rasd(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        /* Special association case: 
         * RASD instance is pointing to itself
         */
        s = get_rasd_by_ref(_BROKER, ref, info->properties, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;
        
        inst_list_add(list, inst);
        
 out:
        return s;
}

static CMPIInstance *make_ref(const CMPIObjectPath *source_ref,
                              const CMPIInstance *target_inst,
                              struct std_assoc_info *info,
                              struct std_assoc *assoc)
{
        CMPIInstance *refinst = NULL;
        uint16_t prop_value = 1;

        refinst = make_reference(_BROKER, 
                                 source_ref, 
                                 target_inst, 
                                 info,
                                 assoc);

        if (refinst != NULL) {
                /* Set additional properties with values
                 * defined in the "Virtual System Profile."
                 */
                CMSetProperty(refinst, "IsDefault",
                              (CMPIValue *)&prop_value, CMPI_uint16);
                
                CMSetProperty(refinst, "IsNext",
                              (CMPIValue *)&prop_value, CMPI_uint16);

                CMSetProperty(refinst, "IsMinimum",
                              (CMPIValue *)&prop_value, CMPI_uint16);
                
                CMSetProperty(refinst, "IsMaximum",
                              (CMPIValue *)&prop_value, CMPI_uint16);
        }
        
        return refinst;
}

static char* virtual_system_setting_data[] = {
        "Xen_VirtualSystemSettingData",
        "KVM_VirtualSystemSettingData",
        "LXC_VirtualSystemSettingData",
        NULL
};

static char* resource_allocation_setting_data[] = {
        "Xen_DiskResourceAllocationSettingData",
        "Xen_MemResourceAllocationSettingData",
        "Xen_NetResourceAllocationSettingData",
        "Xen_ProcResourceAllocationSettingData",
        "Xen_GraphicsResourceAllocationSettingData",
        "Xen_ConsoleResourceAllocationSettingData",
        "Xen_InputResourceAllocationSettingData",
        "KVM_DiskResourceAllocationSettingData",
        "KVM_MemResourceAllocationSettingData",
        "KVM_NetResourceAllocationSettingData",
        "KVM_ProcResourceAllocationSettingData",
        "KVM_GraphicsResourceAllocationSettingData",
        "KVM_ConsoleResourceAllocationSettingData",
        "KVM_InputResourceAllocationSettingData",
        "KVM_ControllerResourceAllocationSettingData",
        "LXC_DiskResourceAllocationSettingData",
        "LXC_MemResourceAllocationSettingData",
        "LXC_NetResourceAllocationSettingData",
        "LXC_ProcResourceAllocationSettingData",
        "LXC_GraphicsResourceAllocationSettingData",
        "LXC_ConsoleResourceAllocationSettingData",
        "LXC_InputResourceAllocationSettingData",
        NULL
};

static char* assoc_classname[] = {
        "Xen_ElementSettingData",
        "KVM_ElementSettingData",
        "LXC_ElementSettingData",
        NULL
};

static struct std_assoc _vssd_to_vssd = {
        .source_class = (char**)&virtual_system_setting_data,
        .source_prop = "ManagedElement",

        .target_class = (char**)&virtual_system_setting_data,
        .target_prop = "SettingData",

        .assoc_class = (char**)&assoc_classname,

        .handler = vssd_to_vssd,
        .make_ref = make_ref
};

static struct std_assoc _rasd_to_rasd = {
        .source_class = (char**)&resource_allocation_setting_data,
        .source_prop = "ManagedElement",

        .target_class = (char**)&resource_allocation_setting_data,
        .target_prop = "SettingData",

        .assoc_class = (char**)&assoc_classname,

        .handler = rasd_to_rasd,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &_vssd_to_vssd,
        &_rasd_to_rasd,
        NULL
};

STDA_AssocMIStub(, 
                 Virt_ElementSettingData, 
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
