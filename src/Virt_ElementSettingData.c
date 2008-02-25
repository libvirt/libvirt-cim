/*
 * Copyright IBM Corp. 2007
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
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
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        char *host = NULL;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        if (!parse_instanceid(ref, NULL, &host)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get system name");
                goto out;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        dom = virDomainLookupByName(conn, host);
        if (dom == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "No such system `%s'", host);
                goto out;
        }

        inst = get_vssd_instance(dom, _BROKER, ref);
        if (inst == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Error getting VSSD for `%s'", host);
                goto out;
        }

        inst_list_add(list, inst);

 out:
        virDomainFree(dom);
        virConnectClose(conn);
        free(host);

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
        NULL
};

static char* resource_allocation_setting_data[] = {
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

static char* assoc_classname[] = {
        "Xen_ElementSettingData",
        "KVM_ElementSettingData",        
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
