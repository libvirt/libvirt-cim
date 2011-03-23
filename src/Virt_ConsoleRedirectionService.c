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
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include "misc_util.h"
#include "svpc_types.h"

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_invokemethod.h>
#include <libcmpiutil/std_instance.h>

#include "Virt_HostSystem.h"
#include "Virt_ConsoleRedirectionService.h"

#define MAX_SAP_SESSIONS 65535

const static CMPIBroker *_BROKER;

static CMPIStatus set_inst_properties(const CMPIBroker *broker,
                                      const CMPIContext *context,
                                      const CMPIObjectPath *ref,
                                      CMPIInstance *inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIArray *array;
        const char *name = NULL;
        const char *ccname = NULL;
        uint16_t prop_val;

        s = get_host_system_properties(&name, &ccname, ref, broker, context);
        if (s.rc != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get host attributes");
                goto out;
        }

        CMSetProperty(inst, "ElementName",
                      (CMPIValue *)"ConsoleRedirectionService", CMPI_chars);

        CMSetProperty(inst, "Name",
                      (CMPIValue *)"ConsoleRedirectionService", CMPI_chars);

        if (name != NULL)
                CMSetProperty(inst, "SystemName",
                              (CMPIValue *)name, CMPI_chars);

        if (ccname != NULL)
                CMSetProperty(inst, "SystemCreationClassName",
                              (CMPIValue *)ccname, CMPI_chars);

        array = CMNewArray(broker, 1, CMPI_uint16, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(array))
                goto out;

        prop_val = (uint16_t)CIM_CRS_SERVICE_TYPE;
        CMSetArrayElementAt(array, 0, &prop_val, CMPI_uint16);

        CMSetProperty(inst, "RedirectionServiceType",
                      (CMPIValue *)&array, CMPI_uint16A);

        prop_val = (uint16_t)MAX_SAP_SESSIONS;
        CMSetProperty(inst, "MaxCurrentEnabledSAPs",
                      (CMPIValue *)&prop_val, CMPI_uint16);

        prop_val = (uint16_t)CIM_CRS_SHARING_MODE;
        CMSetProperty(inst, "SharingMode",
                      (CMPIValue *)&prop_val, CMPI_uint16);

        prop_val = (uint16_t)CIM_CRS_ENABLED_STATE;
        CMSetProperty(inst, "EnabledState",
                      (CMPIValue *)&prop_val, CMPI_uint16);

        prop_val = (uint16_t)CIM_CRS_REQUESTED_STATE;
        CMSetProperty(inst, "RequestedState",
                      (CMPIValue *)&prop_val, CMPI_uint16);

 out:
        return s;
}

CMPIStatus get_console_rs(const CMPIObjectPath *reference,
                          CMPIInstance **_inst,
                          const CMPIBroker *broker,
                          const CMPIContext *context,
                          bool is_get_inst)
{ 
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;
        CMPIInstance *inst = NULL;

        *_inst = NULL;
        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL) {
                if (is_get_inst)
                        cu_statusf(broker, &s,
                                   CMPI_RC_ERR_NOT_FOUND,
                                   "No such instance");

                return s;
        }

        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "ConsoleRedirectionService",
                                  NAMESPACE(reference));

        if (inst == NULL) {
                CU_DEBUG("Failed to get typed instance");
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to create instance");
                goto out;
        }

        s = set_inst_properties(broker, context, reference, inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (is_get_inst) {
                s = cu_validate_ref(broker, reference, inst);
                if (s.rc != CMPI_RC_OK)
                        goto out;
        }

 out:
        virConnectClose(conn);
        *_inst = inst;

        return s;
}

static CMPIStatus return_rs(const CMPIContext *context,
                            const CMPIObjectPath *reference,
                            const CMPIResult *results,
                            bool name_only,
                            bool is_get_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst;

        s = get_console_rs(reference, &inst, _BROKER, context, is_get_inst);
        if (s.rc != CMPI_RC_OK || inst == NULL)
                goto out;

        if (name_only)
                cu_return_instance_name(results, inst);
        else
                CMReturnInstance(results, inst);
 out:
        return s;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_rs(context, reference, results, true, false);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{

        return return_rs(context, reference, results, false, false);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *ref,
                              const char **properties)
{
        return return_rs(context, ref, results, false, true);
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(, 
                   Virt_ConsoleRedirectionService, 
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
