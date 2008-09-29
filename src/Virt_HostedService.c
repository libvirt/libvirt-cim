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

#include "Virt_HostSystem.h"
#include "Virt_VirtualSystemManagementService.h"
#include "Virt_ResourcePoolConfigurationService.h"
#include "Virt_VSMigrationService.h"
#include "Virt_ConsoleRedirectionService.h"

const static CMPIBroker *_BROKER;

static CMPIStatus validate_service_ref(const CMPIContext *context,
                                       const CMPIObjectPath *ref)
{      
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        char* classname;

        classname = class_base_name(CLASSNAME(ref));

        if (STREQC(classname, "VirtualSystemManagementService")) {
                s = get_vsms(ref, &inst, _BROKER, context, true);
        } else if (STREQC(classname, "ResourcePoolConfigurationService")) {
                s = get_rpcs(ref, &inst, _BROKER, context, true);
        } else if (STREQC(classname, "VirtualSystemMigrationService")) {
                s = get_migration_service(ref, &inst, _BROKER, context, true);
        } else if (STREQC(classname, "ConsoleRedirectionService")) {
                s = get_console_rs(ref, &inst, _BROKER, context, true);
        }
        
        free(classname);

        return s;
}

static CMPIStatus service_to_host(const CMPIObjectPath *ref,
                                  struct std_assoc_info *info,
                                  struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        s = validate_service_ref(info->context, ref);
        if (s.rc != CMPI_RC_OK)
                return s;

        s = get_host(_BROKER, info->context, ref, &instance, false);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, instance);

        return s;
}

static CMPIStatus host_to_service(const CMPIObjectPath *ref,
                                  struct std_assoc_info *info,
                                  struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        s = get_host(_BROKER, info->context, ref, &inst, true);
        if (s.rc != CMPI_RC_OK)
                return s;

        s = get_rpcs(ref, &inst, _BROKER, info->context, false);
        if (s.rc != CMPI_RC_OK)
                return s;
        if (!CMIsNullObject(inst))
                inst_list_add(list, inst);

        s = get_vsms(ref, &inst, _BROKER, info->context, false);
        if (s.rc != CMPI_RC_OK)
                return s;
        if (!CMIsNullObject(inst))
            inst_list_add(list, inst);

        s = get_migration_service(ref, &inst, _BROKER, info->context, false);
        if (s.rc != CMPI_RC_OK)
                return s;
        if (!CMIsNullObject(inst))
                inst_list_add(list, inst);

        s = get_console_rs(ref, &inst, _BROKER, info->context, false);
        if (s.rc != CMPI_RC_OK)
                return s;
        if (!CMIsNullObject(inst))
                inst_list_add(list, inst);

        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char* antecedent[] = {  
        "Xen_HostSystem",
        "KVM_HostSystem",
        "LXC_HostSystem",
        NULL
};

static char* dependent[] = {
        "Xen_ResourcePoolConfigurationService",
        "Xen_VirtualSystemManagementService",
        "Xen_VirtualSystemMigrationService",
        "Xen_ConsoleRedirectionService",
        "KVM_ResourcePoolConfigurationService",
        "KVM_VirtualSystemManagementService",
        "KVM_VirtualSystemMigrationService",
        "KVM_ConsoleRedirectionService",
        "LXC_ResourcePoolConfigurationService",
        "LXC_VirtualSystemManagementService",
        "LXC_VirtualSystemMigrationService",
        "LXC_ConsoleRedirectionService",
        NULL
};

static char* assoc_classname[] = {
        "Xen_HostedService",
        "KVM_HostedService",
        "LXC_HostedService",
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

STDA_AssocMIStub(, 
                 Virt_HostedService,
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
