/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include "libcmpiutil.h"
#include "misc_util.h"
#include "std_instance.h"

#include "Virt_VSMigrationCapabilities.h"

const static CMPIBroker *_BROKER;

#define SVPC_MIG_MVSTH   2
#define SVPC_MIG_MVSTS   3
#define SVPC_MIG_CVSIMTH 4
#define SVPC_MIG_CVSIMTS 5

static CMPIStatus set_method_properties(const CMPIBroker *broker,
                                        CMPIInstance *inst)
{
        CMPIArray *array;
        CMPIStatus s;
        uint16_t val;

        array = CMNewArray(broker, 2, CMPI_INTEGER, &s);
        if (s.rc != CMPI_RC_OK)
                return s;

        val = SVPC_MIG_MVSTH;
        CMSetArrayElementAt(array, 0, (CMPIValue *)&val, CMPI_uint16);

        val = SVPC_MIG_MVSTS;
        CMSetArrayElementAt(array, 1, (CMPIValue *)&val, CMPI_uint16);

        CMSetProperty(inst, "AsynchronousMethodsSupported",
                      (CMPIValue *)&array, CMPI_ARRAY);


        array = CMNewArray(broker, 2, CMPI_INTEGER, &s);
        if (s.rc != CMPI_RC_OK)
                return s;

        val = SVPC_MIG_CVSIMTH;
        CMSetArrayElementAt(array, 0, (CMPIValue *)&val, CMPI_uint16);

        val = SVPC_MIG_CVSIMTS;
        CMSetArrayElementAt(array, 1, (CMPIValue *)&val, CMPI_uint16);

        CMSetProperty(inst, "SynchronousMethodsSupported",
                      (CMPIValue *)&array, CMPI_ARRAY);

        CMSetStatus(&s, CMPI_RC_OK);

        return s;
}

CMPIStatus get_migration_caps(const CMPIObjectPath *ref,
                              CMPIInstance **_inst,
                              const CMPIBroker *broker)
{
        CMPIInstance *inst;
        CMPIStatus s;

        inst = get_typed_instance(broker,
                                  CLASSNAME(ref),
                                  "VirtualSystemMigrationCapabilities",
                                  NAMESPACE(ref));
        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get instance for %s", CLASSNAME(ref));
                return s;
        }

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)"MigrationCapabilities", CMPI_chars);

        s = set_method_properties(broker, inst);

        if (s.rc == CMPI_RC_OK)
                *_inst = inst;

        return s;
}

static CMPIStatus return_vsmc(const CMPIObjectPath *ref,
                              const CMPIResult *results,
                              bool name_only)
{
        CMPIInstance *inst;
        CMPIStatus s;

        s = get_migration_caps(ref, &inst, _BROKER);

        if (s.rc == CMPI_RC_OK) {
                if (name_only)
                        cu_return_instance_name(results, inst);
                else
                        CMReturnInstance(results, inst);
        }

        return s;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *ref)
{
        return return_vsmc(ref, results, true);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *ref,
                                const char **properties)
{

        return return_vsmc(ref, results, false);
}


static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *ref,
                              const char **properties)
{
        CMPIInstance *inst;
        CMPIStatus s;
        const char *prop;

        s = get_migration_caps(ref, &inst, _BROKER);
        if (s.rc != CMPI_RC_OK)
                return s;

        prop = cu_compare_ref(ref, inst);
        if (prop != NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s)", prop);
        } else {
                CMReturnInstance(results, inst);
        }

        return s;
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(, Virt_VSMC,
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