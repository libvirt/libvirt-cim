/*
 * Copyright IBM Corp. 2008
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
#include "svpc_types.h"

#include "Virt_ComputerSystem.h"
#include "Virt_ConsoleRedirectionService.h"
#include "Virt_Device.h"

const static CMPIBroker *_BROKER;

static CMPIStatus service_to_cs(const CMPIObjectPath *ref,
                                struct std_assoc_info *info,
                                struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;
        const char *host = NULL;
        int i;
        int num_of_domains;
        
        if (!match_hypervisor_prefix(ref, info))
                goto out;

        s = get_console_rs(ref, &instance, _BROKER, info->context, true);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = enum_domains(_BROKER, ref, list);
        if (s.rc != CMPI_RC_OK)
                goto out;
       
        num_of_domains = list->cur;
 
        /*
         * For each domain, insert its video and pointer devices into
         * the list
         */
        for (i = 0; i < num_of_domains; i++) {
                s.rc = cu_get_str_prop(list->list[i], "Name", &host);
                if (s.rc != CMPI_RC_OK) 
                        goto out;

                s = enum_devices(_BROKER, 
                                 ref, 
                                 host, 
                                 CIM_RES_TYPE_INPUT, 
                                 list);
                if (s.rc != CMPI_RC_OK)
                        goto out;

                s = enum_devices(_BROKER, 
                                 ref,
                                 host,
                                 CIM_RES_TYPE_GRAPHICS,
                                 list);
                if (s.rc != CMPI_RC_OK)
                        goto out;
        }

 out:
        return s;
}

static CMPIStatus validate_cs_or_dev_ref(const CMPIContext *context,
                                         const CMPIObjectPath *ref)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        char* classname;
                                  
        classname = class_base_name(CLASSNAME(ref));

        if (STREQC(classname, "ComputerSystem")) {
                s = get_domain_by_ref(_BROKER, ref, &inst);
        } else if ((STREQC(classname, "PointingDevice"))  || 
                   (STREQC(classname, "DisplayController"))) {
                s = get_device_by_ref(_BROKER, ref, &inst);        
        }

        free(classname);

        return s;
}

static CMPIStatus cs_to_service(const CMPIObjectPath *ref,
                                struct std_assoc_info *info,
                                struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        if (!match_hypervisor_prefix(ref, info))
                return s;
        
        s = validate_cs_or_dev_ref(info->context, ref);
        if (s.rc != CMPI_RC_OK)
                return s;

        s = get_console_rs(ref, &inst, _BROKER, info->context, false);
        if (s.rc != CMPI_RC_OK)
                return s;
        if (!CMIsNullObject(inst))
                inst_list_add(list, inst);

        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char* affected_ele[] = {  
        "Xen_ComputerSystem",
        "KVM_ComputerSystem",
        "LXC_ComputerSystem",
        "Xen_PointingDevice",
        "KVM_PointingDevice",
        "LXC_PointingDevice",
        "Xen_DisplayController",
        "KVM_DisplayController",
        "LXC_DisplayController",
        NULL
};

static char* affecting_ele[] = {
        "Xen_ConsoleRedirectionService",
        "KVM_ConsoleRedirectionService",
        "LXC_ConsoleRedirectionService",
        NULL
};

static char* assoc_classname[] = {
        "Xen_ServiceAffectsElement",
        "KVM_ServiceAffectsElement",
        "LXC_ServiceAffectsElement",
        NULL
};

static struct std_assoc _cs_to_service = {
        .source_class = (char**)&affected_ele,
        .source_prop = "AffectedElement",

        .target_class = (char**)&affecting_ele,
        .target_prop = "AffectingElement",

        .assoc_class = (char**)&assoc_classname,

        .handler = cs_to_service,
        .make_ref = make_ref
};

static struct std_assoc _service_to_cs = {
        .source_class = (char**)&affecting_ele,
        .source_prop = "AffectingElement",
        
        .target_class = (char**)&affected_ele,
        .target_prop = "AffectedElement",

        .assoc_class = (char**)&assoc_classname,
        
        .handler = service_to_cs,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &_cs_to_service,
        &_service_to_cs,
        NULL
};

STDA_AssocMIStub(, 
                 Virt_ServiceAffectsElement,
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
