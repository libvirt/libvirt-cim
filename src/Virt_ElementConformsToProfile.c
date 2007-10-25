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

#include "libcmpiutil.h"
#include "misc_util.h"
#include "profiles.h"
#include "std_association.h"

#include "Virt_RegisteredProfile.h"
#include "Virt_ElementConformsToProfile.h"

/* Associate an XXX_RegisteredProfile to the proper XXX_ManagedElement.
 *
 *  -- or --
 *
 * Associate an XXX_ManagedElement to the proper XXX_RegisteredProfile.
 */

const static CMPIBroker *_BROKER;

static CMPIStatus prof_from_ref(struct reg_prof *prof,
                                const CMPIObjectPath *ref)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        char *tmp_str;
        int tmp_int;
        
        memset(prof, 0, sizeof(*prof));

        prof->reg_name = cu_get_str_path(ref, "RegisteredName");
        prof->reg_version = cu_get_str_path(ref, "RegisteredVersion");
        prof->other_reg_org = cu_get_str_path(ref, "OtherRegisteredOrganization");

        tmp_str = cu_get_str_path(ref, "RegisteredOrganization");
        if (tmp_str) {
                sscanf(tmp_str, "%d", &tmp_int);
                prof->reg_org = (uint16_t)tmp_int;
        }

        free(tmp_str);
        return s;

}

static bool compare_profiles(struct reg_prof *target,
                             struct reg_prof *candidate)
{
        if (!STREQC(target->reg_name, candidate->reg_name))
                return false;

        COMPARE_OPT_STR(target, candidate, reg_version);
        COMPARE_OPT_NUM(target, candidate, reg_org);
        //COMPARE_OPT_NUM(target, candidate, ad_types);
        COMPARE_OPT_STR(target, candidate, other_reg_org);
        //COMPARE_OPT_STR(target, candidate, ad_type_descriptions);

        return true;
}

static CMPIInstance *elem_instance(const CMPIObjectPath *ref,
                                   char *provider_name)
{
        CMPIStatus s;
        CMPIObjectPath *op;
        CMPIInstance *instance = NULL;
        char *classname;

        classname = get_typed_class(provider_name);
        if (classname == NULL) {
                //TRACE("Can't assemble classname.");
                printf("Can't assemble classname.\n");
                goto out;
        }

        op = CMNewObjectPath(_BROKER, NAMESPACE(ref), classname, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op))
                goto out;

        instance = CMNewInstance(_BROKER, op, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(instance))
                goto out;

        CMSetProperty(instance, "CreationClassName", (CMPIValue *)classname, 
                      CMPI_chars);

 out:
        free(classname);

        return instance;
}

static CMPIStatus prof_to_elem(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance;
        struct reg_prof target;
        struct reg_prof *candidate;
        int i;

        s = prof_from_ref(&target, ref);

        for (i = 0; profiles[i] != NULL; i++) {
                candidate = profiles[i];
                if (!compare_profiles(&target, candidate))
                        continue;

                instance = elem_instance(ref, candidate->provider_name);
                if (instance == NULL)
                        goto out;

                inst_list_add(list, instance);
        }

 out:
        return s;
}

static CMPIStatus elem_to_prof(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance;
        char *classname;
        char *provider_name;
        struct reg_prof *candidate;
        int i;

        classname = cu_get_str_path(ref, "CreationClassName");
        if (classname == NULL) {
                CMSetStatusWithChars(_BROKER, &s, CMPI_RC_ERR_FAILED,
                                     "Can't get class name from element.");
                goto error1;
        }

        provider_name = class_base_name(classname);
        if (provider_name == NULL) {
                CMSetStatusWithChars(_BROKER, &s, CMPI_RC_ERR_FAILED,
                                     "Can't get provider name.");
                goto error2;
        }

        for (i = 0; profiles[i] != NULL; i++) {
                candidate = profiles[i];
                if (!STREQC(candidate->provider_name, provider_name))
                        continue;

                instance = reg_prof_instance(_BROKER, ref, candidate);
                if (instance == NULL) {
                        CMSetStatusWithChars(_BROKER, &s, CMPI_RC_ERR_FAILED,
                                             "Can't create profile instance.");
                        goto error3;
                }
                
                inst_list_add(list, instance);
        }

 error3:
        free(provider_name);
 error2:                
        free(classname);
 error1:
        return s;
}

static CMPIInstance *make_ref(const CMPIObjectPath *ref,
                              const CMPIInstance *inst,
                              struct std_assoc_info *info,
                              struct std_assoc *assoc)
{
        CMPIInstance *refinst;
        char *base;

        base = class_base_name(assoc->target_class);

        refinst = get_typed_instance(_BROKER,
                                     base,
                                     NAMESPACE(ref));
        if (refinst != NULL) {
                CMPIObjectPath *instop;

                instop = CMGetObjectPath(inst, NULL);

                CMSetProperty(refinst, assoc->source_prop,
                              (CMPIValue *)ref, CMPI_ref);
                CMSetProperty(refinst, assoc->target_prop,
                              (CMPIValue *)instop, CMPI_ref);
        }

        free(base);

        return refinst;
}

struct std_assoc forward = {
        .source_class = "CIM_RegisteredProfile",
        .source_prop = "ConformantStandard",

        .target_class = "CIM_ManagedElement",
        .target_prop = "ManagedElement",

        .assoc_class = NULL,

        .handler = prof_to_elem,
        .make_ref = make_ref
};

struct std_assoc backward = {
        .source_class = "CIM_ManagedElement",
        .source_prop = "ManagedElement",

        .target_class = "CIM_RegisteredProfile",
        .target_prop = "ConformantStandard",

        .assoc_class = NULL,

        .handler = elem_to_prof,
        .make_ref = make_ref
};

struct std_assoc *assoc_handlers[] = {
        &forward,
        &backward,
        NULL
};


STDA_AssocMIStub(, Virt_ElementConformsToProfileProvider, _BROKER, CMNoHook, assoc_handlers);
/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
