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

#include "libcmpiutil.h"
#include "std_instance.h"

#include "cs_util.h"
#include "misc_util.h"
#include "profiles.h"

#include "Virt_RegisteredProfile.h"

const static CMPIBroker *_BROKER;

CMPIInstance *reg_prof_instance(const CMPIBroker *broker,
                                const char *namespace,
                                const char **properties,
				struct reg_prof *profile)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIObjectPath *op;
        CMPIInstance *instance = NULL;
        char *classname;

        classname = get_typed_class("RegisteredProfile");
        if (classname == NULL) {
                goto out;
        }

        op = CMNewObjectPath(broker, namespace, classname, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op))
                goto out;

        instance = CMNewInstance(broker, op, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op))
                goto out;

        if (properties) {
                s = CMSetPropertyFilter(instance, properties, NULL);
                if (s.rc != CMPI_RC_OK) {
                        goto out;
                }
        }
        
        CMSetProperty(instance, "InstanceID",
                      (CMPIValue *)profile->reg_id, CMPI_chars);

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
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance;
        int i;

        for (i = 0; profiles[i] != NULL; i++) {
                instance = reg_prof_instance(_BROKER, 
                                             NAMESPACE(ref), 
                                             properties, 
                                             profiles[i]);
                if (instance == NULL) {
                        CMSetStatusWithChars(_BROKER, &s, CMPI_RC_ERR_FAILED,
                                             "Can't create profile instance.");
                        goto out;
                }

                if (names_only)
                        cu_return_instance_name(results, instance);
                else
                        CMReturnInstance(results, instance);
        }

 out:
        return s;
}

static CMPIStatus get_prof(const CMPIObjectPath *ref,
                           const CMPIResult *results,
                           const char **properties)
{       
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;
        char* id;
        int i;

        id = cu_get_str_path(ref, "InstanceID");
        if (id == NULL) {
                CMSetStatusWithChars(_BROKER, &s,
                                     CMPI_RC_ERR_FAILED,
                                     "No InstanceID specified");
                return s;
        }

        for (i = 0; profiles[i] != NULL; i++) {
                if(STREQ(id, profiles[i]->reg_id)) {
                        instance = reg_prof_instance(_BROKER, 
                                                     NAMESPACE(ref), 
                                                     properties,
                                                     profiles[i]);
                        break;
                }
        }

        if(instance)
                CMReturnInstance(results, instance);
        else
                CMSetStatus(&s, CMPI_RC_ERR_NOT_FOUND);
                

        free(id);

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

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        return get_prof(reference, results, properties);
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(, Virt_RegisteredProfileProvider, _BROKER,
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
