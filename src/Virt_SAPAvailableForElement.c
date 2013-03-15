/*
 * Copyright IBM Corp. 2008
 *
 * Authors:
 * Richard Maciel <richardm@br.ibm.com> 
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_association.h>
#include "misc_util.h"

#include "Virt_ComputerSystem.h"
#include "Virt_KVMRedirectionSAP.h"

static const CMPIBroker *_BROKER;

static CMPIStatus sapavail_to_guest(const CMPIObjectPath *ref,
                                    struct std_assoc_info *info,
                                    struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;
        const char *dom_name;

        if (!match_hypervisor_prefix(ref, info))
                goto out;

        s = get_console_sap_by_ref(_BROKER, ref, &instance);
        if (s.rc != CMPI_RC_OK)
                goto out;
        
        s.rc = cu_get_str_path(ref, "SystemName", &dom_name);
        if (s.rc != CMPI_RC_OK)
                goto out;
        
        s = get_domain_by_name(_BROKER, ref, dom_name, &instance);

        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, instance);

 out:
        return s;
}


static CMPIStatus guest_to_sapavail(const CMPIObjectPath *ref,
                                    struct std_assoc_info *info,
                                    struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;
        const char *sap_host_name = NULL;
        const char *dom_host_name = NULL;
        int i;
        struct inst_list temp_list;

        inst_list_init(&temp_list);

        if (!match_hypervisor_prefix(ref, info))
                goto out;
       
        s = get_domain_by_ref(_BROKER, ref, &instance);
        if (s.rc != CMPI_RC_OK) 
                goto out; 

        s.rc = cu_get_str_path(ref, "Name", &dom_host_name);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = enum_console_sap(_BROKER, ref, &temp_list);

        for (i = 0; i < temp_list.cur; i++) {
                s.rc = cu_get_str_prop(temp_list.list[i], 
                                       "SystemName", 
                                       &sap_host_name);
                if (s.rc != CMPI_RC_OK)
                        goto out;

                if (STREQC(dom_host_name, sap_host_name))
                        inst_list_add(list, temp_list.list[i]);
        }

 out:
        inst_list_free(&temp_list);

        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char* managedelem[] = {
        "Xen_ComputerSystem",
        "KVM_ComputerSystem",
        "LXC_ComputerSystem",
        NULL
};

static char* availablesap[] = {
        "Xen_KVMRedirectionSAP",
        "KVM_KVMRedirectionSAP",
        "LXC_KVMRedirectionSAP",
        NULL
};

static char* assoc_classname[] = {
        "Xen_SAPAvailableForElement",
        "KVM_SAPAvailableForElement",
        "LXC_SAPAvailableForElement",
        NULL
};

static struct std_assoc _guest_to_sapavail = {
        .source_class = (char **)&managedelem,
        .source_prop = "ManagedElement", 

        .target_class = (char **)&availablesap,
        .target_prop = "AvailableSAP", 

        .assoc_class = (char **)&assoc_classname,

        .handler = guest_to_sapavail,
        .make_ref = make_ref
};

static struct std_assoc _sapavail_to_guest = {
        .source_class = (char **)&availablesap,
        .source_prop = "AvailableSAP",

        .target_class = (char **)&managedelem,
        .target_prop = "ManagedElement",
        
        .assoc_class = (char **)&assoc_classname,

        .handler = sapavail_to_guest,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &_guest_to_sapavail,
        &_sapavail_to_guest,
        NULL
};

STDA_AssocMIStub(,
                 Virt_SAPAvailableForElement,
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
