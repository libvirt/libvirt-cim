/*
 * Copyright IBM Corp. 2007, 2008
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_instance.h>

#include "cs_util.h"
#include "misc_util.h"
#include "profiles.h"

#include "Virt_RegisteredProfile.h"

const static CMPIBroker *_BROKER;

CMPIStatus get_profile(const CMPIBroker *broker,
                       const CMPIObjectPath *reference,
                       const char **properties,
                       const char* pfx,
                       struct reg_prof *profile,
                       CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;

        instance = get_typed_instance(broker,
                                      pfx,
                                      "RegisteredProfile",
                                      CIM_INTEROP_NS);

        if (instance == NULL) {
                cu_statusf(broker, &s, 
                           CMPI_RC_ERR_FAILED,
                           "Can't create RegisteredProfile instance");
                goto out;
        }

        if (properties) {
                const char *keys[] = {"InstanceID", NULL};
                CMSetPropertyFilter(instance, properties, keys);
        }
        
        CMSetProperty(instance, "InstanceID",
                      (CMPIValue *)profile->reg_id, CMPI_chars);

        CMSetProperty(instance, "RegisteredOrganization", 
                      (CMPIValue *)&profile->reg_org, CMPI_uint16);

        CMSetProperty(instance, "RegisteredName", 
                      (CMPIValue *)profile->reg_name, CMPI_chars);

        CMSetProperty(instance, "RegisteredVersion", 
                      (CMPIValue *)profile->reg_version, CMPI_chars);

        *_inst = instance;

 out:

        return s;
}

CMPIStatus get_profile_by_name(const CMPIBroker *broker,
                               const CMPIObjectPath *reference,
                               const char *name,
                               const char **properties,
                               CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;
        int i;
        bool found = false;

        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance");
                goto out;
        }

        for (i = 0; profiles[i] != NULL; i++) {
                if(STREQ(name, profiles[i]->reg_id)) {
                        CMPIInstance *inst = NULL;

                        s = get_profile(broker,
                                        reference, 
                                        properties,
                                        pfx_from_conn(conn),
                                        profiles[i],
                                        &inst);
                        if (s.rc != CMPI_RC_OK)
                                goto out;

                        *_inst = inst;
                        found = true;
                        break;
                }
        }

        if (found == false)
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s)",
                           name);

 out:
        virConnectClose(conn);

        return s;
}

CMPIStatus get_profile_by_ref(const CMPIBroker *broker,
                              const CMPIObjectPath *reference,
                              const char **properties,
                              CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        const char *name = NULL;

        if (cu_get_str_path(reference, "InstanceID", &name) != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "No InstanceID specified");
                goto out;
        }

        s = get_profile_by_name(broker, reference, name, properties, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;
        
        s = cu_validate_ref(broker, reference, inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        *_inst = inst;

 out:
        return s;
}

CMPIStatus enum_profiles(const CMPIBroker *broker,
                         const CMPIObjectPath *reference,
                         const char **properties,
                         struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;
        int i;

        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        for (i = 0; profiles[i] != NULL; i++) {
                CMPIInstance *inst = NULL;

                s = get_profile(broker,
                                reference, 
                                properties,
                                pfx_from_conn(conn),
                                profiles[i],
                                &inst);

                if (s.rc != CMPI_RC_OK)
                        continue;

                inst_list_add(list, inst);
        }

 out:
        virConnectClose(conn);

        return s;
}

static CMPIStatus return_enum_profiles(const CMPIObjectPath *reference,
                                       const CMPIResult *results,
                                       const char **properties,
                                       const bool names_only)
{
        struct inst_list list;
        CMPIStatus s;

        inst_list_init(&list);

        s = enum_profiles(_BROKER, reference, properties, &list);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (names_only)
                cu_return_instance_names(results, &list);
        else
                cu_return_instances(results, &list);

 out:
        inst_list_free(&list);

        return s;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_enum_profiles(reference, results, NULL, true);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{
        return return_enum_profiles(reference, results, properties, false);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        s = get_profile_by_ref(_BROKER, reference, properties, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        CMReturnInstance(results, inst);

 out:
        return s;
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(,
                   Virt_RegisteredProfile,
                   _BROKER,
                   libvirt_cim_init());

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
