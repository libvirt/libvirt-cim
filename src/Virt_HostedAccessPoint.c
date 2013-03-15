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

#include "Virt_HostSystem.h"
#include "Virt_KVMRedirectionSAP.h"

static const CMPIBroker *_BROKER;

static CMPIStatus rsap_to_host(const CMPIObjectPath *ref,
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

        s = get_host(_BROKER, info->context, ref, &instance, false);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, instance);

 out:
        return s;
}


static CMPIStatus host_to_rsap(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;
        CMPIObjectPath *vref = NULL;

        if (!STARTS_WITH(CLASSNAME(ref), "Linux_") &&
            !match_hypervisor_prefix(ref, info))
                goto out;

        s = get_host(_BROKER, info->context, ref, &instance, true);
        if (s.rc != CMPI_RC_OK)
                goto out; 

        vref = convert_sblim_hostsystem(_BROKER, ref, info);
        if (vref == NULL)
                goto out;

        s = enum_console_sap(_BROKER, vref, list);

 out:
        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char* antecedent[] = {
        "Xen_HostSystem",
        "KVM_HostSystem",
        "LXC_HostSystem",
        "Linux_ComputerSystem",
        NULL
};

static char* dependent[] = {
        "Xen_KVMRedirectionSAP",
        "KVM_KVMRedirectionSAP",
        "LXC_KVMRedirectionSAP",
        NULL
};

static char* assoc_classname[] = {
        "Xen_HostedAccessPoint",
        "KVM_HostedAccessPoint",
        "LXC_HostedAccessPoint",
        NULL
};

static struct std_assoc _host_to_rsap = {
        .source_class = (char **)&antecedent,
        .source_prop = "Antecedent", 

        .target_class = (char **)&dependent,
        .target_prop = "Dependent", 

        .assoc_class = (char **)&assoc_classname,

        .handler = host_to_rsap,
        .make_ref = make_ref
};

static struct std_assoc _rsap_to_host = {
        .source_class = (char **)&dependent,
        .source_prop = "Dependent",

        .target_class = (char **)&antecedent,
        .target_prop = "Antecedent",
        
        .assoc_class = (char **)&assoc_classname,

        .handler = rsap_to_host,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &_host_to_rsap,
        &_rsap_to_host,
        NULL
};

STDA_AssocMIStub(,
                 Virt_HostedAccessPoint,
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
