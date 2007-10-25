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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include "cs_util.h"
#include "libcmpiutil.h"
#include "misc_util.h"
#include "profiles.h"

#include "Virt_RegisteredProfile.h"

const static CMPIBroker *_BROKER;

static bool reg_prof_set_id(CMPIInstance *instance, 
                            struct reg_prof *profile)
{
        char *id;

        if (asprintf(&id, "%s_%s", profile->reg_name, 
                     profile->reg_version) == -1)
                id = NULL;
        
        if(id)
                CMSetProperty(instance, "InstanceID", 
                              (CMPIValue *)id, CMPI_chars);

        return id != NULL;
}

CMPIInstance *reg_prof_instance(const CMPIBroker *broker,
				const CMPIObjectPath *ref, 
				struct reg_prof *profile)
{
        CMPIStatus s;
        CMPIObjectPath *op;
        CMPIInstance *instance = NULL;
        char *classname;

        classname = get_typed_class("RegisteredProfile");
        if (classname == NULL) {
                //TRACE(1, "Can't assemble classname.");
                printf("Can't assemble classname.\n");
                goto out;
        }

        op = CMNewObjectPath(broker, NAMESPACE(ref), classname, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op))
                goto out;

        instance = CMNewInstance(broker, op, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op))
                goto out;

        reg_prof_set_id(instance, profile);

        CMSetProperty(instance, "CreationClassName", 
                      (CMPIValue *)classname, CMPI_chars);

        CMSetProperty(instance, "RegisteredOrganization", 
                      (CMPIValue *)&profile->reg_org, CMPI_uint16);

        CMSetProperty(instance, "RegisteredName", 
                      (CMPIValue *)profile->reg_name, CMPI_chars);

        CMSetProperty(instance, "RegisteredVersion", 
                      (CMPIValue *)profile->reg_version, CMPI_chars);

 out:
        free(classname);

        return instance;
}

static CMPIStatus enum_profs(const CMPIObjectPath *ref,
                             const CMPIResult *results,
                             const char **properties,
                             bool names_only)
{
        int i;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance;

        for (i = 0; profiles[i] != NULL; i++) {
                instance = reg_prof_instance(_BROKER, ref, profiles[i]);
                if (instance == NULL) {
                        CMSetStatusWithChars(_BROKER, &s, CMPI_RC_ERR_FAILED,
                                             "Can't create profile instance.");
                        goto out;
                }
                
                if (properties) {
                        s = CMSetPropertyFilter(instance, properties, NULL);
                        if (s.rc != CMPI_RC_OK) {
                                CMSetStatusWithChars(_BROKER, &s, 
                                                     CMPI_RC_ERR_FAILED,
                                                     "Property filter failed.");
                                goto out;
                        }
                }

                if (names_only)
                        cu_return_instance_name(results, instance);
                else
                        CMReturnInstance(results, instance);
        }

 out:
        return s;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return enum_profs(reference, results, NULL, true);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{
        return enum_profs(reference, results, properties, false);
}

DEFAULT_GI();
DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

/* Avoid a warning in the stub macro below */
CMPIInstanceMI *
Virt_RegisteredProfileProvider_Create_InstanceMI(const CMPIBroker *,
                                                 const CMPIContext *,
                                                 CMPIStatus *rc);

CMInstanceMIStub(, Virt_RegisteredProfileProvider, _BROKER, CMNoHook);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
