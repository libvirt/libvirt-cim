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
#include <string.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_instance.h>

#include "misc_util.h"
#include "cs_util.h"

#include "Virt_EnabledLogicalElementCapabilities.h"

const static CMPIBroker *_BROKER;

enum {ENABLED = 2,
      DISABLED,
      SHUTDOWN,
      OFFLINE = 6,
      TEST,
      DEFER,
      QUIESCE,
      REBOOT,
      RESET};

static CMPIInstance *_get_elec(const CMPIBroker *broker,
                               const CMPIObjectPath *reference,
                               virConnectPtr conn,
                               const char *name,
                               CMPIStatus *s)
{
        CMPIInstance *inst = NULL;
        CMPIArray *array;
        uint16_t element;
        int edit_name = 0;

        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "EnabledLogicalElementCapabilities",
                                  NAMESPACE(reference));
        if (inst == NULL) {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to init EnabledLogicalElementCapabilities instance");
                goto out;
        }

        CMSetProperty(inst, "InstanceID", (CMPIValue *)name, CMPI_chars);

        array = CMNewArray(broker, 5, CMPI_uint16, s);
        if ((s->rc != CMPI_RC_OK) || CMIsNullObject(array))
                goto out;
        
        element = (uint16_t)ENABLED;
        CMSetArrayElementAt(array, 0, &element, CMPI_uint16);

        element = (uint16_t)DISABLED;
        CMSetArrayElementAt(array, 1, &element, CMPI_uint16);

        element = (uint16_t)QUIESCE;
        CMSetArrayElementAt(array, 2, &element, CMPI_uint16);

        element = (uint16_t)REBOOT;
        CMSetArrayElementAt(array, 3, &element, CMPI_uint16);

        element = (uint16_t)RESET;
        CMSetArrayElementAt(array, 4, &element, CMPI_uint16);

        CMSetProperty(inst, "RequestedStatesSupported",
                      (CMPIValue *)&array, CMPI_uint16A);

        CMSetProperty(inst, "ElementNameEditSupported",
                      (CMPIValue *)&edit_name, CMPI_boolean);

 out:
        return inst;
}

static CMPIStatus return_enum_elec(const CMPIObjectPath *ref,
                                   const CMPIResult *results,
                                   bool names_only)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        virConnectPtr conn = NULL;
        virDomainPtr *list = NULL;
        int count;
        int i;
        const char *name;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        count = get_domain_list(conn, &list);
        if (count <= 0)
                goto out;

        for (i = 0; i < count; i++) {
                name = virDomainGetName(list[i]);
                if (name == NULL) {
                        virt_set_status(_BROKER, &s,
                                        CMPI_RC_ERR_FAILED,
                                        conn,
                                        "Unable to get domain names");
                        goto end;
                }

                inst = _get_elec(_BROKER, ref, conn, name, &s);
                if (s.rc != CMPI_RC_OK)
                        goto end;

                if (names_only)
                        cu_return_instance_name(results, inst);
                else
                        CMReturnInstance(results, inst);

          end:
                virDomainFree(list[i]);
        }

 out:
        free(list);
        virConnectClose(conn);

        return s;
}

CMPIStatus get_elec_by_name(const CMPIBroker *broker,
                            const CMPIObjectPath *reference,
                            const char *name,
                            CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        virConnectPtr conn;
        virDomainPtr dom;
        
        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance");
                goto out;
        }
        
        dom = virDomainLookupByName(conn, name);
        if (dom == NULL) {
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_NOT_FOUND,
                                conn,
                                "No such instance (%s)",
                                name);
                goto out;
        }

        inst = _get_elec(broker, reference, conn, name, &s);
        virDomainFree(dom);
        if (s.rc != CMPI_RC_OK)
                goto out;

        *_inst = inst;
        
 out:
        virConnectClose(conn);

        return s;
}

CMPIStatus get_elec_by_ref(const CMPIBroker *broker,
                           const CMPIObjectPath *reference,
                           CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        const char *name;
        
        if (cu_get_str_path(reference, "InstanceID", &name) != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "No InstanceID specified");
                goto out;
        }
        
        s = get_elec_by_name(broker, reference, name, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;
        
        s = cu_validate_ref(broker, reference, inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        *_inst = inst;
        
 out:
        return s;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_enum_elec(reference, results, true);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{

        return return_enum_elec(reference, results, false);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        s = get_elec_by_ref(_BROKER, reference, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;
        
        CMReturnInstance(results, inst);

 out:
        return s;
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(,
                   Virt_EnabledLogicalElementCapabilities,
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
