/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Richard Maciel <richardm@br.ibm.com> 
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

#include "Virt_ConsoleRedirectionService.h"
#include "Virt_KVMRedirectionSAP.h"

const static CMPIBroker *_BROKER;


static CMPIStatus service_to_rsap(const CMPIObjectPath *ref,
                                  struct std_assoc_info *info,
                                  struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;
        char* classname;

        if (!match_hypervisor_prefix(ref, info))
                goto out;

        classname = class_base_name(CLASSNAME(ref));

        s = get_console_rs(ref, &instance, _BROKER, info->context, true);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = enum_console_sap(_BROKER, ref, list); 

 out:
        return s;
}

static CMPIStatus rsap_to_service(const CMPIObjectPath *ref,
                                  struct std_assoc_info *info,
                                  struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;

        if (!match_hypervisor_prefix(ref, info))
                goto out;

        s = get_console_sap_by_ref(_BROKER, ref, &instance);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = get_console_rs(ref, &instance, _BROKER, info->context, false);
        if (s.rc != CMPI_RC_OK)
                goto out;

        inst_list_add(list, instance);

 out:
        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char* antecedent[] = {
        "Xen_ConsoleRedirectionService",
        "KVM_ConsoleRedirectionService",
        "LXC_ConsoleRedirectionService",
        NULL
};

static char* dependent[] = {
        "Xen_KVMRedirectionSAP",
        "KVM_KVMRedirectionSAP",
        "LXC_KVMRedirectionSAP",
        NULL
};

static char* assoc_classname[] = {
        "Xen_ServiceAccessBySAP",
        "KVM_ServiceAccessBySAP",
        "LXC_ServiceAccessBySAP",
        NULL
};

static struct std_assoc _service_to_rsap = { 
        .source_class = (char**)&antecedent,
        .source_prop = "Antecedent",

        .target_class = (char**)&dependent,
        .target_prop = "Dependent",

        .assoc_class = (char**)&assoc_classname,

        .handler = service_to_rsap,
        .make_ref = make_ref
};

static struct std_assoc _rsap_to_service = { 
        .source_class = (char**)&dependent,
        .source_prop = "Dependent",
        
        .target_class = (char**)&antecedent,
        .target_prop = "Antecedent",

        .assoc_class = (char**)&assoc_classname,
        
        .handler = rsap_to_service,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &_service_to_rsap,
        &_rsap_to_service,
        NULL
};

STDA_AssocMIStub(, 
                 Virt_ServiceAccessBySAP,
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
