/*
 * Copyright IBM Corp. 2008
 *
 * Authors:
 *  Heidi Eckhart <heidieck@linux.vnet.ibm.com>
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

const static CMPIBroker *_BROKER;

static struct reg_prof *get_reg_prof_by_ref(const CMPIObjectPath *ref)
{
        const char* name;
        int i;

        if (cu_get_str_path(ref, "InstanceID", &name) != CMPI_RC_OK)
                return NULL;

        for (i = 0; profiles[i] != NULL; i++) {
                if(STREQ(name, profiles[i]->reg_id))
                        return profiles[i];
        }

        return NULL;
}

static CMPIStatus get_scoping_prof_by_source(const CMPIObjectPath *ref,
                                             struct std_assoc_info *info,
                                             virConnectPtr conn,
                                             struct reg_prof *source,
                                             struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        if (source->scoping_profile != NULL) {
                s = get_profile(_BROKER,
                                ref, 
                                info->properties,
                                pfx_from_conn(conn),
                                source->scoping_profile,
                                &inst);
                if (s.rc != CMPI_RC_OK)
                        goto out;
                inst_list_add(list, inst);
        }

 out:
        return s;
}

static CMPIStatus get_scoping_prof_by_list(const CMPIObjectPath *ref,
                                           struct std_assoc_info *info,
                                           virConnectPtr conn,
                                           struct reg_prof *source,
                                           struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        struct reg_prof *scope;
        int i;

        for (i = 0; profiles[i] != NULL; i++) {
                if (profiles[i]->scoping_profile == NULL)
                        continue;

                scope = profiles[i]->scoping_profile;
                if (!STREQC(scope->reg_id, source->reg_id))
                        continue;

                s = get_profile(_BROKER,
                                ref, 
                                info->properties,
                                pfx_from_conn(conn),
                                profiles[i],
                                &inst);
                if (s.rc != CMPI_RC_OK)
                        goto out;
                        
                inst_list_add(list, inst);
        }

 out:
        return s;
}

static CMPIStatus enum_reg_prof_by_source(const CMPIObjectPath *ref,
                                          struct std_assoc_info *info,
                                          struct reg_prof *source,
                                          struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        // NOTE: Autonomous or scoping profiles are dependent profiles.
	// Return them according to role
	if ((!source->scoping_profile ||
		STREQC(source->reg_name, "System Virtualization")) &&
		info->role && !STREQC(info->role, "Dependent"))
		goto out;

	s = get_scoping_prof_by_source(ref, info, conn, source, list);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = get_scoping_prof_by_list(ref, info, conn, source, list);
        if (s.rc != CMPI_RC_OK)
                goto out;

 out:
        virConnectClose(conn);

        return s;
}

static CMPIStatus prof_to_prof(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        struct reg_prof *source;
        
        if (!match_hypervisor_prefix(ref, info))
                goto out;

        s = get_profile_by_ref(_BROKER, ref, info->properties, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        source = get_reg_prof_by_ref(ref);
        if (source == NULL) {
                cu_statusf(_BROKER, &s, 
                           CMPI_RC_ERR_FAILED,
                           "Can't find RegisteredProfile instance");
                goto out;
        }

        s = enum_reg_prof_by_source(ref, info, source, list);

 out:
        return s;
}

static CMPIInstance *make_ref(const CMPIObjectPath *source_ref,
                              const CMPIInstance *target_inst,
                              struct std_assoc_info *info,
                              struct std_assoc *assoc);

static char* registered_profile[] = {
        "Xen_RegisteredProfile",
        "KVM_RegisteredProfile",
        "LXC_RegisteredProfile",
        NULL
};

static char* assoc_classname[] = {
        "Xen_ReferencedProfile",
        "KVM_ReferencedProfile",
        "LXC_ReferencedProfile",
        NULL
};

static struct std_assoc forward = {
        .source_class = (char**)&registered_profile,
        .source_prop = "Antecedent",

        .target_class = (char**)&registered_profile,
        .target_prop = "Dependent",

        .assoc_class = (char**)&assoc_classname,

        .handler = prof_to_prof,
        .make_ref = make_ref
};

static struct std_assoc backward = {
        .source_class = (char**)&registered_profile,
        .source_prop = "Dependent",

        .target_class = (char**)&registered_profile,
        .target_prop = "Antecedent",

        .assoc_class = (char**)&assoc_classname,

        .handler = prof_to_prof,
        .make_ref = make_ref
};

static struct std_assoc *assoc_handlers[] = {
        &forward,
        &backward,
        NULL
};

static CMPIInstance *make_ref(const CMPIObjectPath *source_ref,
                              const CMPIInstance *target_inst,
                              struct std_assoc_info *info,
                              struct std_assoc *assoc)
{
        CMPIInstance *ref_inst = NULL;
        struct std_assoc *ref_assoc = NULL;
        struct reg_prof *source;
        char* assoc_classname;

        assoc_classname = class_base_name(assoc->assoc_class[0]);

        ref_inst = get_typed_instance(_BROKER,
                                      CLASSNAME(source_ref),
                                      assoc_classname,
                                      NAMESPACE(source_ref));

        source = get_reg_prof_by_ref(source_ref);
        if (source->scoping_profile != NULL)
                ref_assoc = &backward;
        else
                ref_assoc = assoc;

        if (ref_inst != NULL) {
                CMPIObjectPath *target_ref;
                
                target_ref = CMGetObjectPath(target_inst, NULL);

                set_reference(ref_assoc, ref_inst, 
                              source_ref, target_ref);
        }

        free(assoc_classname);

        return ref_inst;
}

STDA_AssocMIStub(,
                 Virt_ReferencedProfile,
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
