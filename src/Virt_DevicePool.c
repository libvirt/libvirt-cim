/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *  Heidi Eckhart <heidieck@linux.vnet.ibm.com>
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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include "misc_util.h"
#include "hostres.h"
#include "device_parsing.h"

#include <libcmpiutil.h>

#include "Virt_DevicePool.h"
#include "svpc_types.h"

static const CMPIBroker *_BROKER;

char *device_pool_names[] = {"ProcessorPool", "MemoryPool", NULL};

static char *netpool_member_of(const CMPIBroker *broker, char *rasd_id)
{
        char *host = NULL;
        char *dev = NULL;
        int ret;
        int i;
        char * result = NULL;
        struct virt_device *devs = NULL;
        int count = 0;
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        CMPIStatus s;

        ret = parse_fq_devid(rasd_id, &host, &dev);
        if (!ret)
                goto out;

        conn = lv_connect(broker, &s);
        if (conn == NULL)
                goto out;

        dom = virDomainLookupByName(conn, host);
        if (dom == NULL)
                goto out;

        count = get_net_devices(dom, &devs);

        for (i = 0; i < count; i++) {
                if (STREQ((devs[i].id), dev)) {
                        ret = asprintf(&result,
                                       "NetworkPool/%s",
                                       devs[i].dev.net.bridge);
                        if (ret == -1)
                                result = NULL;
                        break;
                }
        }

 out:
        if (count > 0)
                cleanup_virt_devices(&devs, count);
        virDomainFree(dom);
        virConnectClose(conn);
        free(host);
        free(dev);

        return result;
}

char *pool_member_of(const CMPIBroker *broker, uint16_t type, char *id)
{
        char *poolid = NULL;

        if (type == CIM_RASD_TYPE_PROC)
                poolid = strdup("ProcessorPool/0");
        else if (type == CIM_RASD_TYPE_MEM)
                poolid = strdup("MemoryPool/0");
        else if (type == CIM_RASD_TYPE_NET)
                poolid = netpool_member_of(broker, id);
        else
                return NULL;

        return poolid;
}

static bool mempool_set_total(CMPIInstance *inst, virConnectPtr conn)
{
        virNodeInfo info;
        int ret;
        uint64_t memory = 0;

        ret = virNodeGetInfo(conn, &info);
        if (ret == 0)
                memory = (uint64_t)info.memory;

        CMSetProperty(inst, "Capacity",
                      (CMPIValue *)&memory, CMPI_uint64);

        return memory != 0;
}

static bool mempool_set_reserved(CMPIInstance *inst, virConnectPtr conn)
{
        uint64_t memory;

        /* NB: This doesn't account for memory to be claimed
         * by ballooning dom0
         */
        memory = allocated_memory(conn);

        CMSetProperty(inst, "Reserved",
                      (CMPIValue *)&memory, CMPI_uint64);

        return memory != 0;
}

static bool procpool_set_total(CMPIInstance *inst, virConnectPtr conn)
{
        virNodeInfo info;
        int ret;
        uint64_t procs = 0;

        CMSetProperty(inst, "Reserved",
                      (CMPIValue *)&procs, CMPI_uint64);

        ret = virNodeGetInfo(conn, &info);
        if (ret == 0)
                procs = (uint64_t)info.cpus;

        CMSetProperty(inst, "Capacity",
                      (CMPIValue *)&procs, CMPI_uint64);

        return procs != 0;
}

static bool set_units(CMPIInstance *inst,
                      const char *units)
{
        CMSetProperty(inst, "AllocationUnits",
                      (CMPIValue *)units, CMPI_chars);

        return true;
}

static CMPIStatus mempool_instance(virConnectPtr conn,
                                   struct inst_list *list,
                                   const char *ns,
                                   const char *_id,
                                   const CMPIBroker *broker)
{
        const char *id = "MemoryPool/0";
        uint16_t type = CIM_RASD_TYPE_MEM;
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if ((_id != NULL) && (!STREQC(_id, "0"))) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "No such memory pool `%s'", id);
                return s;
        }

        inst = get_typed_instance(broker, "MemoryPool", ns);

        mempool_set_total(inst, conn);
        mempool_set_reserved(inst, conn);
        set_units(inst, "KiloBytes");

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)id, CMPI_chars);

        CMSetProperty(inst, "ResourceType",
                      (CMPIValue *)&type, CMPI_uint16);

        inst_list_add(list, inst);

        return s;
}

static CMPIStatus procpool_instance(virConnectPtr conn,
                                    struct inst_list *list,
                                    const char *ns,
                                    const char *_id,
                                    const CMPIBroker *broker)
{
        const char *id = "ProcessorPool/0";
        uint16_t type = CIM_RASD_TYPE_PROC;
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if ((_id != NULL) && (!STREQC(_id, "0"))) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "No such processor pool `%s'", id);
                return s;
        }

        inst = get_typed_instance(broker, "ProcessorPool", ns);

        procpool_set_total(inst, conn);
        set_units(inst, "Processors");

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)id, CMPI_chars);

        CMSetProperty(inst, "ResourceType",
                      (CMPIValue *)&type, CMPI_uint16);

        inst_list_add(list, inst);

        return s;
}

static CMPIStatus _netpool_for_bridge(struct inst_list *list,
                                      const char *ns,
                                      const char *bridge,
                                      const CMPIBroker *broker)
{
        char *id = NULL;
        uint16_t type = CIM_RASD_TYPE_NET;
        CMPIInstance *inst;

        inst = get_typed_instance(broker, "NetworkPool", ns);

        if (asprintf(&id, "NetworkPool/%s", bridge) == -1)
                return (CMPIStatus){CMPI_RC_ERR_FAILED, NULL};

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)id, CMPI_chars);

        CMSetProperty(inst, "ResourceType",
                      (CMPIValue *)&type, CMPI_uint16);

        free(id);

        inst_list_add(list, inst);

        return (CMPIStatus){CMPI_RC_OK, NULL};
}

static CMPIStatus netpool_instance(virConnectPtr conn,
                                   struct inst_list *list,
                                   const char *ns,
                                   const char *id,
                                   const CMPIBroker *broker)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        char **bridges;
        int i;

        if (id != NULL) {
                if (!is_bridge(id)) {
                        cu_statusf(broker, &s,
                                   CMPI_RC_ERR_FAILED,
                                   "No such network pool `%s'", id);
                        goto out;
                }
                return _netpool_for_bridge(list, ns, id, broker);
        }

        bridges = list_bridges();
        if (bridges == NULL)
                return (CMPIStatus){CMPI_RC_ERR_FAILED, NULL};

        for (i = 0; bridges[i]; i++) {
                _netpool_for_bridge(list, ns, bridges[i], broker);
                free(bridges[i]);
        }

        free(bridges);

 out:
        return s;
}

static CMPIStatus _get_pool(const CMPIBroker *broker,
                            virConnectPtr conn,
                            const char *type,
                            const char *id,
                            const char *ns,
                            struct inst_list *list)
{
        if (STARTS_WITH(type, "MemoryPool"))
                return mempool_instance(conn, list, ns, id, broker);
        else if (STARTS_WITH(type, "ProcessorPool"))
                return procpool_instance(conn, list, ns, id, broker);
        else if (STARTS_WITH(type, "NetworkPool"))
                return netpool_instance(conn, list, ns, id, broker);

        return (CMPIStatus){CMPI_RC_ERR_NOT_FOUND, NULL};
}

CMPIStatus get_pool_by_type(const CMPIBroker *broker,
                            virConnectPtr conn,
                            const char *type,
                            const char *ns,
                            struct inst_list *list)
{
        return _get_pool(broker, conn, type, NULL, ns, list);
}

CMPIInstance *get_pool_by_id(const CMPIBroker *broker,
                             virConnectPtr conn,
                             const char *id,
                             const char *ns)
{
        CMPIInstance *inst = NULL;
        CMPIStatus s;
        char *type = NULL;
        char *poolid = NULL;
        int ret;
        struct inst_list list;

        inst_list_init(&list);

        ret = sscanf(id, "%a[^/]/%as", &type, &poolid);
        if (ret != 2)
                goto out;

        s = _get_pool(broker, conn, type, poolid, ns, &list);
        if ((s.rc == CMPI_RC_OK) && (list.cur > 0))
                inst = list.list[0];

 out:
        inst_list_free(&list);

        return inst;
}

static void __return_pool(const CMPIResult *results,
                          struct inst_list *list,
                          bool name_only)
{
        if (name_only)
                cu_return_instance_names(results, list->list);
        else
                cu_return_instances(results, list->list);
}

static CMPIStatus return_pool(const CMPIObjectPath *ref,
                              const CMPIResult *results,
                              bool name_only,
                              bool single_only)
{
        CMPIStatus s;
        char *type;
        virConnectPtr conn;
        struct inst_list list;

        if (!provider_is_responsible(_BROKER, ref, &s))
                return s;

        type = class_base_name(CLASSNAME(ref));
        if (type == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid classname `%s'", CLASSNAME(ref));
                return s;
        }

        inst_list_init(&list);

        conn = lv_connect(_BROKER, &s);
        if (conn == NULL)
                goto out;

        s = get_pool_by_type(_BROKER,
                             conn,
                             type,
                             NAMESPACE(ref),
                             &list);
        if (s.rc == CMPI_RC_OK) {
                __return_pool(results, &list, name_only);
                CMSetStatus(&s, CMPI_RC_OK);
        } else {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Pool type %s not found", type);
        }

 out:
        free(type);
        inst_list_free(&list);

        return s;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_pool(reference, results, true, false);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{
        return return_pool(reference, results, false, false);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        CMPIStatus s;
        CMPIInstance *inst;
        virConnectPtr conn = NULL;
        char *id = NULL;

        id = cu_get_str_path(reference, "InstanceID");
        if (id == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing InstanceID");
                goto out;
        }

        conn = lv_connect(_BROKER, &s);
        if (conn == NULL)
                goto out;

        inst = get_pool_by_id(_BROKER, conn, id, NAMESPACE(reference));
        if (inst) {
                CMReturnInstance(results, inst);
                CMSetStatus(&s, CMPI_RC_OK);
        } else {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "No such instance `%s'", id);
        }


 out:
        free(id);
        virConnectClose(conn);

        return s;
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

/* Avoid a warning in the stub macro below */
CMPIInstanceMI *
Virt_DevicePoolProvider_Create_InstanceMI(const CMPIBroker *,
                                          const CMPIContext *,
                                          CMPIStatus *rc);

CMInstanceMIStub(, Virt_DevicePoolProvider, _BROKER, CMNoHook);


/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
