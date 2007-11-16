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

#include "libcmpiutil.h"
#include "std_association.h"
#include "misc_util.h"

#include "Virt_HostSystem.h"
#include "Virt_VirtualSystemManagementService.h"
#include "Virt_ResourcePoolConfigurationService.h"

const static CMPIBroker *_BROKER;

static CMPIStatus service_to_host(const CMPIObjectPath *ref,
                                  struct std_assoc_info *info,
                                  struct inst_list *list)
{
        CMPIStatus s;
        CMPIInstance *instance;

        ASSOC_MATCH(info->provider_name, CLASSNAME(ref));

        s = get_host_cs(_BROKER, ref, &instance);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, instance);

        return s;
}

static CMPIStatus host_to_service(const CMPIObjectPath *ref,
                                  struct std_assoc_info *info,
                                  struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst;

        ASSOC_MATCH(info->provider_name, CLASSNAME(ref));

        s = rpcs_instance(ref, &inst, _BROKER);
        if (s.rc != CMPI_RC_OK)
                return s;
        if (!CMIsNullObject(inst))
                inst_list_add(list, inst);

        s = get_vsms(ref, &inst, _BROKER);
        if (s.rc != CMPI_RC_OK)
                return s;
        if (!CMIsNullObject(inst))
            inst_list_add(list, inst);

        return s;
}

static CMPIInstance *make_ref(const CMPIObjectPath *ref,
                              const CMPIInstance *inst,
                              struct std_assoc_info *info,
                              struct std_assoc *assoc)
{
        CMPIInstance *refinst = NULL;
        char *base;

        base = class_base_name(assoc->assoc_class);
        if (base == NULL)
                goto out;

        refinst = get_typed_instance(_BROKER,
                                     base,
                                     NAMESPACE(ref));

        if (refinst != NULL) {
                CMPIObjectPath *instop;

                instop = CMGetObjectPath(inst, NULL);

                set_reference(assoc, refinst, ref, instop);
        }

out:
        return refinst;
}

static struct std_assoc xen_host_to_service = {
        .source_class = "Xen_HostSystem",
        .source_prop = "Antecedent",

        .target_class = "CIM_ManagedElement",
        .target_prop = "Dependent",

        .assoc_class = "CIM_HostedService",

        .handler = host_to_service,
        .make_ref = make_ref
};

static struct std_assoc xen_service_to_host = {
        .source_class = "CIM_Service",
        .source_prop = "Dependent",

        .target_class = "CIM_ManagedElement",
        .target_prop = "Antecedent",

        .assoc_class = "CIM_HostedService",

        .handler = service_to_host,
        .make_ref = make_ref
};

static struct std_assoc kvm_host_to_service = {
        .source_class = "KVM_HostSystem",
        .source_prop = "Antecedent",

        .target_class = "CIM_Service",
        .target_prop = "Dependent",

        .assoc_class = "CIM_HostedService",

        .handler = host_to_service,
        .make_ref = make_ref
};

static struct std_assoc kvm_service_to_host = {
        .source_class = "CIM_Service",
        .source_prop = "Dependent",

        .target_class = "KVM_ComputerSystem",
        .target_prop = "Antecedent",

        .assoc_class = "CIM_HostedService",

        .handler = service_to_host,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &xen_host_to_service,
        &xen_service_to_host,
        &kvm_host_to_service,
        &kvm_service_to_host,
        NULL
};

STDA_AssocMIStub(, Xen_HostedServiceProvider, _BROKER, CMNoHook, handlers);
STDA_AssocMIStub(, KVM_HostedServiceProvider, _BROKER, CMNoHook, handlers);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
