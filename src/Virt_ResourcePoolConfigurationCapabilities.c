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
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_instance.h>

#include "misc_util.h"

const static CMPIBroker *_BROKER;

enum {CreateResourcePool              = 2,
      CreateChildResourcePool         = 3,
      DeleteResourcePool              = 4,
      AddResourcesToResourcePool      = 5,
      RemoveResourcesFromResourcePool = 6,
      ChangeParentResourcePool        = 7,
};

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

static CMPIStatus get_rpc_cap(const CMPIObjectPath *reference,
                              CMPIInstance **_inst,
                              bool is_get_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        virConnectPtr conn = NULL;

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        inst = get_typed_instance(_BROKER,
                                  pfx_from_conn(conn),
                                  "ResourcePoolConfigurationCapabilities",
                                  NAMESPACE(reference));
        if (inst == NULL)
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Can't create ResourcePoolConfigurationCapabilities instance");

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)"RPCC", CMPI_chars);

        /* No method currently supported */

        if (is_get_inst) {
                s = cu_validate_ref(_BROKER, reference, inst);
                if (s.rc != CMPI_RC_OK)
                        goto out;
        }
        
        *_inst = inst;

 out:
        virConnectClose(conn);
        
        return s;
}

static CMPIStatus return_rpc_cap(const CMPIObjectPath *reference,
                                 const CMPIResult *results,
                                 bool names_only,
                                 bool is_get_inst)
{
        CMPIStatus s;
        CMPIInstance *inst = NULL;

        s = get_rpc_cap(reference, &inst, is_get_inst);
        if (s.rc != CMPI_RC_OK || inst == NULL)
                goto out;
        
        if (names_only)
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
        return return_rpc_cap(reference, results, true, false);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{

        return return_rpc_cap(reference, results, false, false);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        return return_rpc_cap(reference, results, false, true);
}


STD_InstanceMIStub(,
                   Virt_ResourcePoolConfigurationCapabilities,
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
