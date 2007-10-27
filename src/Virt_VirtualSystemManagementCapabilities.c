/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
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
#include <stdlib.h>
#include <unistd.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include "libcmpiutil.h"
#include "misc_util.h"
#include "device_parsing.h"

#include "Virt_VirtualSystemManagementCapabilities.h"

const static CMPIBroker *_BROKER;

enum {ADD_RESOURCES = 1,
      DEFINE_SYSTEM,
      DESTROY_SYSTEM,
      DESTROY_SYS_CONFIG,
      MOD_RESOURCE_SETTINGS,
      MOD_SYS_SETTINGS,
      RM_RESOURCES};
                         

static CMPIStatus set_inst_properties(const CMPIBroker *broker,
                                      CMPIInstance *inst,
                                      const char *classname,
                                      const char *sys_name)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIArray *array;
        uint16_t element;
        char *devid;
        
        CMSetProperty(inst, "CreationClassName",
                      (CMPIValue *)classname, CMPI_chars);

        devid = get_fq_devid((char *)sys_name, "0");
        if (devid == NULL) {
                CMSetStatusWithChars(broker, &s, CMPI_RC_ERR_FAILED,
                                     "Could not get full ID.");
                goto out;
        }
        CMSetProperty(inst, "InstanceID", (CMPIValue *)devid, CMPI_chars);

        array = CMNewArray(broker, 4, CMPI_uint16, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(array))
                goto out;
        
        element = (uint16_t)DEFINE_SYSTEM;
        CMSetArrayElementAt(array, 0, &element, CMPI_uint16);

        element = (uint16_t)DESTROY_SYSTEM;
        CMSetArrayElementAt(array, 1, &element, CMPI_uint16);

        element = (uint16_t)MOD_RESOURCE_SETTINGS;
        CMSetArrayElementAt(array, 2, &element, CMPI_uint16);

        element = (uint16_t)MOD_SYS_SETTINGS;
        CMSetArrayElementAt(array, 3, &element, CMPI_uint16);

        CMSetProperty(inst, "SynchronousMethodsSupported",
                      (CMPIValue *)&array, CMPI_uint16A);
 out:
        return s;
}

CMPIStatus get_vsm_cap(const CMPIBroker *broker,
                       const CMPIObjectPath *ref,
                       CMPIInstance **inst)
{
        CMPIStatus s;
        CMPIObjectPath *op;
        char *classname = NULL;
        char *sys_name = NULL;
        
        sys_name = cu_get_str_path(ref, "Name");
        if (sys_name == NULL) {
                CMSetStatusWithChars(broker, &s,
                                     CMPI_RC_ERR_FAILED,
                                     "Missing key: Name");
                goto out;
        }

        classname = get_typed_class("VirtualSystemManagementCapabilities");
        if (classname == NULL) {
                CMSetStatusWithChars(broker, &s,
                                     CMPI_RC_ERR_FAILED,
                                     "Invalid class");
                goto out;
        }

        op = CMNewObjectPath(broker, NAMESPACE(ref), classname, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op)) {
                CMSetStatusWithChars(broker, &s,
                                     CMPI_RC_ERR_FAILED,
                                     "Cannot get object path for VSMCapabilities");
                goto out;
        }

        *inst = CMNewInstance(broker, op, &s);
        if ((s.rc != CMPI_RC_OK) || (CMIsNullObject(*inst))) {
                CMSetStatusWithChars(broker, &s,
                                     CMPI_RC_ERR_FAILED,
                                     "Failed to instantiate HostSystem");
                goto out;
        }

        s = set_inst_properties(broker, *inst, classname, sys_name);

 out:
        free(classname);
        free(sys_name);

        return s;
}

static CMPIStatus return_vsm_cap(const CMPIObjectPath *ref,
                                 const CMPIResult *results,
                                 int names_only)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        
        s = get_vsm_cap(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
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
        return return_vsm_cap(reference, results, 1);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{

        return return_vsm_cap(reference, results, 0);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        return return_vsm_cap(reference, results, 0);
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

/* Avoid a warning in the stub macro below */
CMPIInstanceMI *
Virt_VirtualSystemManagementCapabilitiesProvider_Create_InstanceMI(const CMPIBroker *,
                                                                   const CMPIContext *,
                                                                   CMPIStatus *rc);

CMInstanceMIStub(, Virt_VirtualSystemManagementCapabilitiesProvider, _BROKER, 
                 CMNoHook);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */