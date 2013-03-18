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
#include <string.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_instance.h>

#include "misc_util.h"

#include "Virt_AllocationCapabilities.h"
#include "Virt_DevicePool.h"
#include "svpc_types.h"

const static CMPIBroker *_BROKER;

static CMPIStatus ac_from_pool(const CMPIBroker *broker, 
                               const CMPIObjectPath *ref,
                               CMPIInstance *pool, 
                               CMPIInstance **alloc_cap)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        *alloc_cap = get_typed_instance(broker,
                                        CLASSNAME(ref),
                                        "AllocationCapabilities",
                                        NAMESPACE(ref),
                                        false);
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

CMPIStatus enum_alloc_cap_instances(const CMPIBroker *broker,
                                    const CMPIObjectPath *ref,
                                    const char **properties,
                                    const char *id,
                                    struct inst_list *list)
{
        CMPIInstance *alloc_cap_inst;
        struct inst_list device_pool_list;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        const char *inst_id;
        int i;

        inst_list_init(&device_pool_list);

        if (!provider_is_responsible(broker, ref, &s))
                goto out;

        s = enum_pools(broker, ref, CIM_RES_TYPE_ALL, &device_pool_list);
        if (s.rc != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Error fetching device pools");
                goto out;
        }

        for (i = 0; i < device_pool_list.cur; i++) {
                if (cu_get_str_prop(device_pool_list.list[i],
                                    "InstanceID", &inst_id) != CMPI_RC_OK) {
                        cu_statusf(broker, &s,
                                   CMPI_RC_ERR_FAILED,
                                   "Error fetching device pool InstanceID");
                        goto out;
                }
                if (id && (!STREQ(inst_id, id))) {
                        inst_id = NULL;
                        continue;
                }

                s = ac_from_pool(broker, ref, 
                                 device_pool_list.list[i], 
                                 &alloc_cap_inst);
                if (s.rc != CMPI_RC_OK)
                        goto out;

                inst_list_add(list, alloc_cap_inst);

                if (id && (STREQ(inst_id, id)))
                        break;
        }

        if (id && !inst_id) {
            cu_statusf(broker, &s,
                       CMPI_RC_ERR_NOT_FOUND,
                       "Instance not found.");
            goto out; 
        }
        
 out:
        inst_list_free(&device_pool_list);

        return s;
}

CMPIStatus get_alloc_cap_by_id(const CMPIBroker *broker,
                               const CMPIObjectPath *ref,
                               const char *poolid,
                               CMPIInstance **inst)
{
        CMPIInstance *pool;
        CMPIStatus s;

        s = get_pool_by_name(broker, ref, poolid, &pool);
        if ((pool == NULL) || (s.rc != CMPI_RC_OK))
                return s;

        s = ac_from_pool(broker, ref, pool, inst);
        if (s.rc != CMPI_RC_OK)
                return s;

        return cu_validate_ref(broker, ref, *inst);
}

static CMPIStatus return_alloc_cap_instances(const CMPIBroker *broker,
                                             const CMPIObjectPath *ref,
                                             const CMPIResult *results,
                                             bool names_only,
                                             const char **properties,
                                             const char *id)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct inst_list list;

        inst_list_init(&list);

        s = enum_alloc_cap_instances(broker,
                                     ref,
                                     properties,
                                     id,
                                     &list);
        if (s.rc != CMPI_RC_OK)
                goto out;
        
        if (names_only)
                cu_return_instance_names(results, &list);
        else
                cu_return_instances(results, &list);
        
 out:        
        inst_list_free(&list);

        return s;
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        const char* id;

        if (cu_get_str_path(reference, "InstanceID", &id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "No InstanceID specified");
                return s;
        }

        return return_alloc_cap_instances(_BROKER,
                                          reference,
                                          results,
                                          false,
                                          properties,
                                          id);
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_alloc_cap_instances(_BROKER, 
                                          reference, 
                                          results, 
                                          true, 
                                          NULL, 
                                          NULL);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{
        return return_alloc_cap_instances(_BROKER, 
                                          reference, 
                                          results, 
                                          false, 
                                          properties, 
                                          NULL);
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(,
                   Virt_AllocationCapabilities,
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
