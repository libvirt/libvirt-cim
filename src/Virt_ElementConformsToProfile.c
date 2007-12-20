/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *  Heidi Eckhart <heidieck@linux.vnet.ibm.com>
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

#include <libcmpiutil/libcmpiutil.h>
#include "misc_util.h"
#include "profiles.h"
#include <libcmpiutil/std_association.h>

#include "config.h"

#include "Virt_RegisteredProfile.h"

/* Associate an XXX_RegisteredProfile to the proper XXX_ManagedElement.
 *
 *  -- or --
 *
 * Associate an XXX_ManagedElement to the proper XXX_RegisteredProfile.
 */

const static CMPIBroker *_BROKER;

static CMPIStatus elem_instances(const CMPIObjectPath *ref,
                                 struct std_assoc_info *info,
                                 struct inst_list *list,
                                 struct reg_prof *profile,
                                 virConnectPtr conn)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIObjectPath *op;
        CMPIEnumeration *en  = NULL;
        CMPIData data ;
        char *classname;

        classname = get_typed_class(pfx_from_conn(conn), 
                                    profile->provider_name);
        if (classname == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED, 
                           "Can't assemble classname" );
                goto out;
        }

        op = CMNewObjectPath(_BROKER, CIM_VIRT_NS, classname, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op))
                goto out;
        
        en = CBEnumInstances(_BROKER, info->context , op, NULL, &s);
        if (en == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED, 
                           "Upcall enumInstances to target class failed");
                goto out;
        }

        while (CMHasNext(en, &s)) {
                data = CMGetNext(en, &s);
                if (CMIsNullObject(data.value.inst)) {
                        cu_statusf(_BROKER, &s,
                                   CMPI_RC_ERR_FAILED, 
                                   "Failed to retrieve enumeration entry");
                        goto out;
                }

                inst_list_add(list, data.value.inst);
        }

 out:
        free(classname);
        
        return s;
}

static CMPIStatus prof_to_elem(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;
        const char *id;
        int i;
        
        if (!match_hypervisor_prefix(ref, info))
                return s;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                return s;

        if (cu_get_str_path(ref, "InstanceID", &id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "No InstanceID specified");
                goto out;
        }

        for (i = 0; profiles[i] != NULL; i++) {
                if (STREQ(id, profiles[i]->reg_id)) {
                        s = elem_instances(ref, info, list, 
                                           profiles[i], conn);
                        if ((s.rc != CMPI_RC_OK))
                                goto out;
                        break;
                }
        }
        
 out:
        virConnectClose(conn);

        return s;
}

static CMPIStatus elem_to_prof(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance;
        virConnectPtr conn = NULL;
        char *classname;
        struct reg_prof *candidate;
        int i;
        
        if (!match_hypervisor_prefix(ref, info))
                return s;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                return s;

        classname = class_base_name(CLASSNAME(ref));
        if (classname == NULL) {
                cu_statusf(_BROKER, &s, 
                           CMPI_RC_ERR_FAILED,
                           "Can't get class name");
                goto out;
        }

        for (i = 0; profiles[i] != NULL; i++) {
                candidate = profiles[i];
                if (!STREQC(candidate->provider_name, classname))
                        continue;

                instance = reg_prof_instance(_BROKER, 
                                             "/root/interop", 
                                             NULL,
                                             conn,
                                             candidate);
                if (instance == NULL) {
                        cu_statusf(_BROKER, &s, 
                                   CMPI_RC_ERR_FAILED,
                                   "Can't create profile instance");
                        goto out;
                }
                
                inst_list_add(list, instance);
        }
             
 out: 
        free(classname);
        virConnectClose(conn);

        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

char* conformant_standard[] = {
        "Xen_RegisteredProfile",
        "KVM_RegisteredProfile",        
        NULL
};

char* managed_element[] = {
        "Xen_HostSystem",
        "Xen_ComputerSystem",
        "KVM_HostSystem",
        "KVM_ComputerSystem",
        NULL
};

char* assoc_classname[] = {
        "Xen_ElementConformsToProfile",
        "KVM_ElementConformsToProfile",        
        NULL
};

struct std_assoc forward = {
        .source_class = (char**)&conformant_standard,
        .source_prop = "ConformantStandard",

        .target_class = (char**)&managed_element,
        .target_prop = "ManagedElement",

        .assoc_class = (char**)&assoc_classname,

        .handler = prof_to_elem,
        .make_ref = make_ref
};

struct std_assoc backward = {
        .source_class = (char**)&managed_element,
        .source_prop = "ManagedElement",

        .target_class = (char**)&conformant_standard,
        .target_prop = "ConformantStandard",

        .assoc_class = (char**)&assoc_classname,

        .handler = elem_to_prof,
        .make_ref = make_ref
};

struct std_assoc *assoc_handlers[] = {
        &forward,
        &backward,
        NULL
};

STDA_AssocMIStub(,
                 Virt_ElementConformsToProfile,
                 _BROKER, 
                 libvirt_cim_init(), 
                 assoc_handlers);
/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
