/*
 * Copyright IBM Corp. 2008
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
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <stdbool.h>
#include <stdint.h>

#include <libvirt/libvirt.h>

#include "misc_util.h"

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_instance.h>

#include "Virt_VirtualSystemSnapshotService.h"
#include "Virt_VirtualSystemSnapshotServiceCapabilities.h"

const static CMPIBroker *_BROKER;

enum { CREATE_SNAPSHOT = 2,
       DESTROY_SNAPSHOT,
       APPLY_SNAPSHOT,
};

static CMPIStatus set_inst_properties(const CMPIBroker *broker,
                                      CMPIInstance *inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIArray *array;
        uint16_t element;

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)"SnapshotCapabilities", CMPI_chars);

        array = CMNewArray(broker, 2, CMPI_uint16, &s);
        if ((s.rc != CMPI_RC_OK) || (array == NULL))
                goto out;

        element = (uint16_t)CREATE_SNAPSHOT;
        CMSetArrayElementAt(array, 0, &element, CMPI_uint16);

        element = (uint16_t)APPLY_SNAPSHOT;
        CMSetArrayElementAt(array, 1, &element, CMPI_uint16);

        /* There is a typo in the mof - the attribute name in the mof is:
           AynchronousMethodsSupported, not AsynchronousMethodsSupported.
           Making a note incase this changes later. */
        CMSetProperty(inst, "AynchronousMethodsSupported",
                      (CMPIValue *)&array, CMPI_uint16A);
     
        array = NULL;
        array = CMNewArray(broker, 1, CMPI_uint16, &s);
        if ((s.rc != CMPI_RC_OK) || (array == NULL))
                goto out;

        element = (uint16_t)DESTROY_SNAPSHOT;
        CMSetArrayElementAt(array, 0, &element, CMPI_uint16);

        CMSetProperty(inst, "SynchronousMethodsSupported",
                      (CMPIValue *)&array, CMPI_uint16A);

        array = CMNewArray(broker, 2, CMPI_uint16, &s);
        if ((s.rc != CMPI_RC_OK) || (array == NULL))
                goto out;

        element = (uint16_t)VIR_VSSS_SNAPSHOT_MEM;
        CMSetArrayElementAt(array, 0, &element, CMPI_uint16);

        element = (uint16_t)VIR_VSSS_SNAPSHOT_MEMT;
        CMSetArrayElementAt(array, 1, &element, CMPI_uint16);

        CMSetProperty(inst, "SnapshotTypesSupported",
                      (CMPIValue *)&array, CMPI_uint16A);

 out:
        return s;
}

CMPIStatus get_vss_cap(const CMPIBroker *broker,
                       const CMPIObjectPath *ref,
                       CMPIInstance **_inst,
                       bool is_get_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
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
                                  pfx_from_conn(conn),
                                  "VirtualSystemSnapshotServiceCapabilities",
                                  NAMESPACE(ref),
                                  false);
        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Can't create instance for %s", CLASSNAME(ref));
                goto out;
        }

        s = set_inst_properties(broker, inst);

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

static CMPIStatus return_vss_cap(const CMPIObjectPath *ref,
                                 const CMPIResult *results,
                                 bool names_only,
                                 bool is_get_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        s = get_vss_cap(_BROKER, ref, &inst, is_get_inst);
        if ((s.rc != CMPI_RC_OK) || (inst == NULL))
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
        return return_vss_cap(reference, results, true, false);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{

        return return_vss_cap(reference, results, false, false);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        return return_vss_cap(reference, results, false, true);
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(,
                   Virt_VirtualSystemSnapshotServiceCapabilities,
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
