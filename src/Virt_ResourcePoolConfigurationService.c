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

STDIM_MethodMIStub(, Virt_ResourcePoolConfigurationServiceProvider,
                   _BROKER, libvirt_cim_init(), my_handlers);

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

CMPIStatus rpcs_instance(const CMPIObjectPath *reference,
                         CMPIInstance **_inst,
                         const CMPIBroker *broker)
{
        CMPIInstance *inst;
        CMPIInstance *host;
        CMPIStatus s;
        CMPIData prop;

        s = get_host_cs(broker, reference, &host);
        if (s.rc != CMPI_RC_OK)
                goto out;

        inst = get_typed_instance(broker,
                                  CLASSNAME(reference),
                                  "ResourcePoolConfigurationService",
                                  NAMESPACE(reference));
        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get "
                           "ResourcePoolConfigurationService instance");
                goto out;
        }

        CMSetProperty(inst, "Name",
                      (CMPIValue *)"RPCS", CMPI_chars);

        prop = CMGetProperty(host, "CreationClassName", &s);
        if (s.rc != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get CreationClassName from HostSystem");
                goto out;
        }

        CMSetProperty(inst, "SystemCreationClassName",
                      (CMPIValue *)&prop.value.string, CMPI_string);

        prop = CMGetProperty(host, "Name", NULL);
        if (s.rc != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get Name from HostSystem");
                goto out;
        }

        CMSetProperty(inst, "SystemName",
                      (CMPIValue *)&prop.value.string, CMPI_string);

        *_inst = inst;
 out:
        return s;
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        CMPIInstance *inst;
        CMPIStatus s;
        const char *prop = NULL;

        s = rpcs_instance(reference, &inst, _BROKER);
        if (s.rc != CMPI_RC_OK)
                return s;

        prop = cu_compare_ref(reference, inst);
        if (prop != NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s)", prop);
        } else {
                CMReturnInstance(results, inst);
        }

        return s;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        CMPIInstance *inst;
        CMPIStatus s;

        s = rpcs_instance(reference, &inst, _BROKER);
        if (s.rc == CMPI_RC_OK)
                cu_return_instance_name(results, inst);

        return s;
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{

        CMPIInstance *inst;
        CMPIStatus s;

        s = rpcs_instance(reference, &inst, _BROKER);
        if (s.rc == CMPI_RC_OK)
                CMReturnInstance(results, inst);

        return s;
}


STD_InstanceMIStub(, Virt_ResourcePoolConfigurationServiceProvider,
                   _BROKER, libvirt_cim_init());


/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */

