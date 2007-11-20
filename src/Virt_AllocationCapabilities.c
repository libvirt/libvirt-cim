/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
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
#include "std_instance.h"

#include "misc_util.h"

#include "Virt_AllocationCapabilities.h"
#include "Virt_RASD.h"
#include "Virt_DevicePool.h"

const static CMPIBroker *_BROKER;

CMPIStatus get_alloc_cap(const CMPIBroker *broker,
                         const CMPIObjectPath *ref,
                         CMPIInstance **inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        char *inst_id;
        uint16_t type;
        int ret;

        *inst = get_typed_instance(broker,
                                   CLASSNAME(ref),
                                   "AllocationCapabilities",
                                   NAMESPACE(ref));

        if (rasd_type_from_classname(CLASSNAME(ref), &type) != CMPI_RC_OK) {
                CMSetStatusWithChars(broker, &s, CMPI_RC_ERR_FAILED,
                                     "Could not get ResourceType.");
                goto out;
        }

        ret = asprintf(&inst_id, "%hi/%s", type, "0");
        if (ret == -1) {
                CMSetStatusWithChars(broker, &s, CMPI_RC_ERR_FAILED,
                                     "Could not get InstanceID.");
                goto out;
        }

        CMSetProperty(*inst, "InstanceID", inst_id, CMPI_chars);
        CMSetProperty(*inst, "ResourceType", &type, CMPI_uint16);

 out:
        return s;
}

static CMPIStatus return_alloc_cap(const CMPIObjectPath *ref, 
                                   const CMPIResult *results, 
                                   int names_only)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        s = get_alloc_cap(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (names_only)
                cu_return_instance_name(results, inst);
        else
                CMReturnInstance(results, inst);
 out:
        return s;
}

static CMPIStatus ac_from_pool(const CMPIBroker *broker, 
                               const CMPIObjectPath *ref,
                               CMPIInstance *pool, 
                               CMPIInstance **alloc_cap)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        *alloc_cap = get_typed_instance(broker,
                                        CLASSNAME(ref),
                                        "AllocationCapabilities",
                                        NAMESPACE(ref));
        if (*alloc_cap == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Could not get alloc_cap instance");
                goto out;
        }

        s = cu_copy_prop(broker, pool, *alloc_cap, "InstanceID", NULL);
        if (s.rc != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Error copying InstanceID");
                goto out;
        }
                
        s = cu_copy_prop(broker, pool, *alloc_cap, "ResourceType", NULL);
        if (s.rc != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Error copying InstanceID");
                goto out;
        }
 out:
        return s;
}

static CMPIStatus alloc_cap_instances(const CMPIBroker *broker,
                                      const CMPIObjectPath *ref,
                                      const CMPIResult *results,
                                      bool names_only,
                                      const char **properties)
{
        int i;
        virConnectPtr conn = NULL;
        CMPIInstance *alloc_cap_inst;
        struct inst_list alloc_cap_list;
        struct inst_list device_pool_list;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        CU_DEBUG("In alloc_cap_instances()");

        inst_list_init(&device_pool_list);
        inst_list_init(&alloc_cap_list);

        if (!provider_is_responsible(broker, ref, &s))
                goto out;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Could not connect to hypervisor");
                goto out;
        }

        s = get_all_pools(broker, conn, NAMESPACE(ref), &device_pool_list);
        if (s.rc != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Error fetching device pools");
                goto out;
        }

        for (i = 0; i < device_pool_list.cur; i++) {
                s = ac_from_pool(broker, ref, 
                                 device_pool_list.list[i], 
                                 &alloc_cap_inst);
                if (s.rc != CMPI_RC_OK)
                        goto out;

                inst_list_add(&alloc_cap_list, alloc_cap_inst);
        }

        if (names_only)
                cu_return_instance_names(results, &alloc_cap_list);
        else
                cu_return_instances(results, &alloc_cap_list);

 out:
        virConnectClose(conn);
        inst_list_free(&alloc_cap_list);
        inst_list_free(&device_pool_list);
        return s;
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        return return_alloc_cap(reference, results, 0);
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return alloc_cap_instances(_BROKER, reference, results, true, NULL);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{
        return alloc_cap_instances(_BROKER, reference, results, false, properties);
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(, Virt_AllocationCapabilitiesProvider, _BROKER,
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
