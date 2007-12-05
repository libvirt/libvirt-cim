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
#include "Virt_VSMigrationService.h"

const static CMPIBroker *_BROKER;

static CMPIStatus service_to_host(const CMPIObjectPath *ref,
                                  struct std_assoc_info *info,
                                  struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance;

        if (!match_hypervisor_prefix(ref, info))
                return s;

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

        if (!match_hypervisor_prefix(ref, info))
                return s;

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

        s = get_migration_service(ref, &inst, _BROKER);
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
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *refinst = NULL;
        virConnectPtr conn = NULL;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                return NULL;

        refinst = get_typed_instance(_BROKER,
                                     pfx_from_conn(conn),
                                     "HostedService",
                                     NAMESPACE(ref));

        if (refinst != NULL) {
                CMPIObjectPath *instop;

                instop = CMGetObjectPath(inst, NULL);

                set_reference(assoc, refinst, ref, instop);
        }

        virConnectClose(conn);

        return refinst;
}

char* antecedent[] = {  
        "Xen_HostSystem",
        "KVM_HostSystem",
        NULL
};

char* dependent[] = {
        "Xen_ResourcePoolConfigurationService",
        "Xen_VirtualSystemManagementService",
        "Xen_VirtualSystemMigrationService",
        "KVM_ResourcePoolConfigurationService",
        "KVM_VirtualSystemManagementService",
        "KVM_VirtualSystemMigrationService",
        NULL
};

char* assoc_classname[] = {
        "Xen_HostedService",
        "KVM_HostedService",        
        NULL
};

static struct std_assoc _host_to_service = {
        .source_class = (char**)&antecedent,
        .source_prop = "Antecedent",

        .target_class = (char**)&dependent,
        .target_prop = "Dependent",

        .assoc_class = (char**)&assoc_classname,

        .handler = host_to_service,
        .make_ref = make_ref
};

static struct std_assoc _service_to_host = {
        .source_class = (char**)&dependent,
        .source_prop = "Dependent",
        
        .target_class = (char**)&antecedent,
        .target_prop = "Antecedent",

        .assoc_class = (char**)&assoc_classname,
        
        .handler = service_to_host,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &_host_to_service,
        &_service_to_host,
        NULL
};

STDA_AssocMIStub(, Virt_HostedServiceProvider, _BROKER, libvirt_cim_init(), handlers);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
