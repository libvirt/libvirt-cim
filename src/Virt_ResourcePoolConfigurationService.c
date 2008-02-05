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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */
#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_invokemethod.h>
#include <libcmpiutil/std_instance.h>

#include "misc_util.h"

#include "Virt_HostSystem.h"
#include "Virt_ResourcePoolConfigurationService.h"

const static CMPIBroker *_BROKER;

static CMPIStatus dummy_handler(CMPIMethodMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const CMPIArgs *argsin,
                                CMPIArgs *argsout)
{
        RETURN_UNSUPPORTED();
}

static struct method_handler CreateResourcePool = {
        .name = "CreateResourcePool",
        .handler = dummy_handler,
        .args = { ARG_END },
};

static struct method_handler CreateChildResourcePool = {
        .name = "CreateChildResourcePool",
        .handler = dummy_handler,
        .args = { ARG_END },
};

static struct method_handler AddResourcesToResourcePool = {
        .name = "AddResourcesToPool",
        .handler = dummy_handler,
        .args = { ARG_END }
};

static struct method_handler RemoveResourcesFromResourcePool = {
        .name = "RemoveResourcesFromResourcePool",
        .handler = dummy_handler,
        .args = { ARG_END }
};

static struct method_handler DeleteResourcePool = {
        .name = "DeleteResourcePool",
        .handler = dummy_handler,
        .args = { ARG_END }
};

static struct method_handler *my_handlers[] = {
        &CreateResourcePool,
        &CreateChildResourcePool,
        &AddResourcesToResourcePool,
        &RemoveResourcesFromResourcePool,
        &DeleteResourcePool,
        NULL,
};

STDIM_MethodMIStub(, 
                   Virt_ResourcePoolConfigurationService,
                   _BROKER, 
                   libvirt_cim_init(),
                   my_handlers);

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

CMPIStatus get_rpcs(const CMPIObjectPath *reference,
                    CMPIInstance **_inst,
                    const CMPIBroker *broker,
                    bool is_get_inst)
{
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;
        const char *name = NULL;
        const char *ccname = NULL;

        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL) {
                if (is_get_inst)
                        cu_statusf(broker, &s,
                                   CMPI_RC_ERR_NOT_FOUND,
                                   "No such instance");
                goto out;
        }

        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "ResourcePoolConfigurationService",
                                  NAMESPACE(reference));
        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get "
                           "ResourcePoolConfigurationService instance");
                goto out;
        }

        s = get_host_system_properties(&name, 
                                       &ccname, 
                                       reference, 
                                       broker);
        if (s.rc != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get host attributes");
                goto out;
        }

        CMSetProperty(inst, "Name",
                      (CMPIValue *)"RPCS", CMPI_chars);

        CMSetProperty(inst, "SystemName",
                      (CMPIValue *)name, CMPI_chars);

        CMSetProperty(inst, "SystemCreationClassName",
                      (CMPIValue *)ccname, CMPI_chars);

        if (is_get_inst) {
                s = cu_validate_ref(broker, reference, inst);
                if (s.rc != CMPI_RC_OK)
                        goto out;
        }

        *_inst = inst;

 out:
        virConnectClose(conn);

        return s;
}

static CMPIStatus return_rpcs(const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              bool names_only,
                              bool is_get_inst)
{        
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        
        s = get_rpcs(reference, &inst, _BROKER, is_get_inst);
        if (s.rc != CMPI_RC_OK || inst == NULL)
                goto out;
        
        if (names_only)
                cu_return_instance_name(results, inst);
        else
                CMReturnInstance(results, inst);
        
 out:
        return s;
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        return return_rpcs(results, reference, false, true);
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_rpcs(results, reference, true, false);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{
        return return_rpcs(results, reference, false, false);
}


STD_InstanceMIStub(,
                   Virt_ResourcePoolConfigurationService,
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

