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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_instance.h>

#include "misc_util.h"

const static CMPIBroker *_BROKER;

static CMPIStatus set_properties(const CMPIBroker *broker,
                                 CMPIInstance *inst)
{
        CMPIStatus s;
        uint16_t type = 3;  /* Use live migration as default */
        uint16_t priority = 0;  /* Use default priority */

        CMSetProperty(inst, "MigrationType",
                      (CMPIValue *)&type, CMPI_uint16);

        CMSetProperty(inst, "Priority",
                      (CMPIValue *)&priority, CMPI_uint16);


        CMSetStatus(&s, CMPI_RC_OK);

        return s;
}

static CMPIStatus get_migration_sd(const CMPIObjectPath *ref,
                                   CMPIInstance **_inst,
                                   const CMPIBroker *broker,
                                   bool is_get_inst)
{
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;

        conn = connect_by_classname(broker, CLASSNAME(ref), &s);
        if (conn == NULL) {
                if (is_get_inst)
                        cu_statusf(broker, &s,
                                   CMPI_RC_ERR_NOT_FOUND,
                                   "No such instance");
                goto out;
        }

        inst = get_typed_instance(broker,
                                  CLASSNAME(ref),
                                  "VirtualSystemMigrationSettingData",
                                  NAMESPACE(ref));
        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get instance for %s", CLASSNAME(ref));
                goto out;
        }

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)"MigrationSettingData", CMPI_chars);

        s = set_properties(broker, inst);

        if (s.rc != CMPI_RC_OK)
            goto out;

        if (is_get_inst) {
                s = cu_validate_ref(broker, ref, inst);
                if (s.rc != CMPI_RC_OK)
                        goto out;
        }

        *_inst = inst;

 out:
        virConnectClose(conn);

        return s;
}

static CMPIStatus return_vsmsd(const CMPIObjectPath *ref,
                               const CMPIResult *results,
                               bool name_only,
                               bool is_get_inst)
{
        CMPIInstance *inst = NULL;
        CMPIStatus s;

        s = get_migration_sd(ref, &inst, _BROKER, is_get_inst);

        if ((s.rc == CMPI_RC_OK) && (inst != NULL)) {
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
        return return_vsmsd(ref, results, true, false);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *ref,
                                const char **properties)
{
        return return_vsmsd(ref, results, false, false);
}


static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *ref,
                              const char **properties)
{
        return return_vsmsd(ref, results, false, true);
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(, 
                   Virt_VSMigrationSettingData,
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
