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
#define __USE_FILE_OFFSET64

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <inttypes.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include "config.h"

#include "misc_util.h"
#include "device_parsing.h"

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_instance.h>

#include "Virt_DevicePool.h"
#include "svpc_types.h"

static const CMPIBroker *_BROKER;

struct disk_pool {
        char *tag;
        char *path;
};

/*
 * Right now, detect support and use it, if available.
 * Later, this can be a configure option if needed
 */
#if LIBVIR_VERSION_NUMBER > 4000
# define VIR_USE_LIBVIRT_STORAGE 1
#else
# define VIR_USE_LIBVIRT_STORAGE 0
#endif

#if VIR_USE_LIBVIRT_STORAGE
static int get_diskpool_config(virConnectPtr conn,
                               struct disk_pool **_pools)
{
        int count = 0;
        int i;
        char ** names = NULL;
        struct disk_pool *pools;

        count = virConnectNumOfStoragePools(conn);
        if (count <= 0)
                goto out;

        names = calloc(count, sizeof(char *));
        if (names == NULL) {
                CU_DEBUG("Failed to alloc space for %i pool names", count);
                goto out;
        }

        pools = calloc(count, sizeof(*pools));
        if (pools == NULL) {
                CU_DEBUG("Failed to alloc space for %i pool structs", count);
                goto out;
        }

        if (virConnectListStoragePools(conn, names, count) == -1) {
                CU_DEBUG("Failed to get storage pools");
                free(pools);
                goto out;
        }

        for (i = 0; i < count; i++)
                pools[i].tag = names[i];

        *_pools = pools;
 out:
        free(names);

        return count;
}

static bool diskpool_set_capacity(virConnectPtr conn,
                                  CMPIInstance *inst,
                                  struct disk_pool *_pool)
{
        bool result = false;
        virStoragePoolPtr pool;
        virStoragePoolInfo info;

        pool = virStoragePoolLookupByName(conn, _pool->tag);
        if (pool == NULL) {
                CU_DEBUG("Failed to lookup storage pool `%s'", _pool->tag);
                goto out;
        }

        if (virStoragePoolGetInfo(pool, &info) == -1) {
                CU_DEBUG("Failed to get info for pool `%s'", _pool->tag);
                goto out;
        }

        CMSetProperty(inst, "Capacity",
                      (CMPIValue *)&info.capacity, CMPI_uint64);

        CMSetProperty(inst, "Reserved",
                      (CMPIValue *)&info.allocation, CMPI_uint64);

        result = true;
 out:
        virStoragePoolFree(pool);

        return result;
}

static bool _diskpool_is_member(virConnectPtr conn,
                                const struct disk_pool *pool,
                                const char *file)
{
        virStorageVolPtr vol = NULL;
        bool result = false;

        vol = virStorageVolLookupByPath(conn, file);
        if (vol != NULL)
                result = true;

        CU_DEBUG("Image %s in pool %s: %s",
                 file,
                 pool->tag,
                 result ? "YES": "NO");

        virStorageVolFree(vol);

        return result;
}
#else
static int parse_diskpool_line(struct disk_pool *pool,
                               const char *line)
{
        int ret;

        ret = sscanf(line, "%as %as", &pool->tag, &pool->path);
        if (ret != 2) {
                free(pool->tag);
                free(pool->path);
        }

        return (ret == 2);
}

static int get_diskpool_config(virConnectPtr conn,
                               struct disk_pool **_pools)
{
        const char *path = DISK_POOL_CONFIG;
        FILE *config;
        char *line = NULL;
        size_t len = 0;
        int count = 0;
        struct disk_pool *pools = NULL;

        config = fopen(path, "r");
        if (config == NULL) {
                CU_DEBUG("Failed to open %s: %m", path);
                return 0;
        }

        while (getline(&line, &len, config) > 0) {
                pools = realloc(pools,
                                (count + 1) * (sizeof(*pools)));
                if (pools == NULL) {
                        CU_DEBUG("Failed to alloc new pool");
                        goto out;
                }

                if (parse_diskpool_line(&pools[count], line))
                        count++;
        }

 out:
        free(line);
        *_pools = pools;
        fclose(config);

        return count;
}

static bool diskpool_set_capacity(virConnectPtr conn,
                                  CMPIInstance *inst,
                                  struct disk_pool *pool)
{
        bool result = false;
        struct statvfs vfs;
        uint64_t cap;
        uint64_t res;

        if (statvfs(pool->path, &vfs) != 0) {
                CU_DEBUG("Failed to statvfs(%s): %m", pool->path);
                goto out;
        }

        cap = (uint64_t) vfs.f_frsize * vfs.f_blocks;
        res = cap - (uint64_t)(vfs.f_frsize * vfs.f_bfree);

        cap >>= 20;
        res >>= 20;

        CMSetProperty(inst, "Capacity",
                      (CMPIValue *)&cap, CMPI_uint64);

        CMSetProperty(inst, "Reserved",
                      (CMPIValue *)&res, CMPI_uint64);

        result = true;
 out:
        return result;
}

static bool _diskpool_is_member(virConnectPtr conn,
                                const struct disk_pool *pool,
                                const char *file)
{
        return STARTS_WITH(file, pool->path);
}
#endif

static void free_diskpool(struct disk_pool *pools, int count)
{
        int i;

        if (pools == NULL)
                return;

        for (i = 0; i < count; i++) {
                free(pools[i].tag);
                free(pools[i].path);
        }

        free(pools);
}

static char *_diskpool_member_of(virConnectPtr conn,
                                 const char *file)
{
        struct disk_pool *pools = NULL;
        int count;
        int i;
        char *pool = NULL;

        count = get_diskpool_config(conn, &pools);
        if (count == 0)
                return NULL;

        for (i = 0; i < count; i++) {
                if (_diskpool_is_member(conn, &pools[i], file)) {
                        int ret;

                        ret = asprintf(&pool, "DiskPool/%s", pools[i].tag);
                        if (ret == -1)
                                pool = NULL;
                        break;
                }
        }

        free_diskpool(pools, count);

        return pool;
}

static char *diskpool_member_of(const CMPIBroker *broker,
                                const char *rasd_id,
                                const char *refcn)
{
        char *host = NULL;
        char *dev = NULL;
        int ret;
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        int count = 0;
        struct virt_device *devs = NULL;
        int i;
        char *pool = NULL;
        CMPIStatus s;

        ret = parse_fq_devid(rasd_id, &host, &dev);
        if (!ret)
                goto out;

        conn = connect_by_classname(broker, refcn, &s);
        if (conn == NULL)
                goto out;

        dom = virDomainLookupByName(conn, host);
        if (dom == NULL)
                goto out;

        count = get_devices(dom, &devs, CIM_RES_TYPE_DISK);

        for (i = 0; i < count; i++) {
                if (STREQ((devs[i].dev.disk.virtual_dev), dev)) {
                        pool = _diskpool_member_of(conn,
                                                   devs[i].dev.disk.source);
                        break;
                }
        }

 out:
        if (count > 0)
                cleanup_virt_devices(&devs, count);

        free(host);
        free(dev);
        virConnectClose(conn);
        virDomainFree(dom);

        return pool;
}

static virNetworkPtr bridge_to_network(virConnectPtr conn,
                                       const char *bridge)
{
        char **networks = NULL;
        virNetworkPtr network = NULL;
        int num;
        int i;

        num = virConnectNumOfNetworks(conn);
        if (num < 0)
                return NULL;

        networks = calloc(num, sizeof(*networks));
        if (networks == NULL)
                return NULL;

        num = virConnectListNetworks(conn, networks, num);

        for (i = 0; i < num; i++) {
                char *_bridge;

                network = virNetworkLookupByName(conn, networks[i]);
                if (network == NULL)
                        continue;

                _bridge = virNetworkGetBridgeName(network);
                CU_DEBUG("Network `%s' has bridge `%s'", networks[i], _bridge);
                if (STREQ(bridge, _bridge)) {
                        i = num;
                } else {
                        virNetworkFree(network);
                        network = NULL;
                }

                free(_bridge);
        }

        free(networks);

        return network;
}

static char *_netpool_member_of(virConnectPtr conn,
                                const char *bridge)
{
        virNetworkPtr net = NULL;
        const char *netname;
        char *pool = NULL;

        net = bridge_to_network(conn, bridge);
        if (net == NULL)
                goto out;

        netname = virNetworkGetName(net);
        if (netname == NULL)
                goto out;

        if (asprintf(&pool, "NetworkPool/%s", netname) == -1)
                pool = NULL;

        CU_DEBUG("Determined pool: %s (%s, %s)", pool, bridge, netname);

 out:
        virNetworkFree(net);

        return pool;
}

static char *netpool_member_of(const CMPIBroker *broker,
                               const char *rasd_id,
                               const char *refcn)
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

        conn = connect_by_classname(broker, refcn, &s);
        if (conn == NULL)
                goto out;

        dom = virDomainLookupByName(conn, host);
        if (dom == NULL)
                goto out;

        count = get_devices(dom, &devs, CIM_RES_TYPE_NET);

        for (i = 0; i < count; i++) {
                if (STREQ((devs[i].id), dev)) {
                        result = _netpool_member_of(conn,
                                                    devs[i].dev.net.source);
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

char *pool_member_of(const CMPIBroker *broker,
                     const char *refcn,
                     uint16_t type,
                     const char *id)
{
        char *poolid = NULL;

        if (type == CIM_RES_TYPE_PROC)
                poolid = strdup("ProcessorPool/0");
        else if (type == CIM_RES_TYPE_MEM)
                poolid = strdup("MemoryPool/0");
        else if (type == CIM_RES_TYPE_NET)
                poolid = netpool_member_of(broker, id, refcn);
        else if (type == CIM_RES_TYPE_DISK)
                poolid = diskpool_member_of(broker, id, refcn);
        else
                return NULL;

        return poolid;
}

uint16_t res_type_from_pool_classname(const char *classname)
{
        if (strstr(classname, "NetworkPool"))
                return CIM_RES_TYPE_NET;
        else if (strstr(classname, "DiskPool"))
                return CIM_RES_TYPE_DISK;
        else if (strstr(classname, "MemoryPool"))
                return CIM_RES_TYPE_MEM;
        else if (strstr(classname, "ProcessorPool"))
                return CIM_RES_TYPE_PROC;
        else
                return CIM_RES_TYPE_UNKNOWN;
}

uint16_t res_type_from_pool_id(const char *id)
{
        if (STARTS_WITH(id, "NetworkPool"))
                return CIM_RES_TYPE_NET;
        else if (STARTS_WITH(id, "DiskPool"))
                return CIM_RES_TYPE_DISK;
        else if (STARTS_WITH(id, "MemoryPool"))
                return CIM_RES_TYPE_MEM;
        else if (STARTS_WITH(id, "ProcessorPool"))
                return CIM_RES_TYPE_PROC;
        else
                return CIM_RES_TYPE_UNKNOWN;
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
        uint16_t type = CIM_RES_TYPE_MEM;
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if ((_id != NULL) && (!STREQC(_id, "0"))) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "No such memory pool `%s'", id);
                return s;
        }

        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "MemoryPool",
                                  ns);

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
        uint16_t type = CIM_RES_TYPE_PROC;
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if ((_id != NULL) && (!STREQC(_id, "0"))) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "No such processor pool `%s'", id);
                return s;
        }

        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "ProcessorPool",
                                  ns);

        procpool_set_total(inst, conn);
        set_units(inst, "Processors");

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)id, CMPI_chars);

        CMSetProperty(inst, "ResourceType",
                      (CMPIValue *)&type, CMPI_uint16);

        inst_list_add(list, inst);

        return s;
}

static CMPIStatus _netpool_for_network(struct inst_list *list,
                                       const char *ns,
                                       virConnectPtr conn,
                                       const char *netname,
                                       const char *refcn,
                                       const CMPIBroker *broker)
{
        char *str = NULL;
        char *bridge = NULL;
        uint16_t type = CIM_RES_TYPE_NET;
        CMPIInstance *inst;
        virNetworkPtr network = NULL;

        CU_DEBUG("Looking up network `%s'", netname);
        network = virNetworkLookupByName(conn, netname);
        if (network == NULL) {
                CMPIStatus s;

                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "No such NetworkPool: %s", netname);
                return s;
        }

        inst = get_typed_instance(broker,
                                  refcn,
                                  "NetworkPool",
                                  ns);

        if (asprintf(&str, "NetworkPool/%s", netname) == -1)
                return (CMPIStatus){CMPI_RC_ERR_FAILED, NULL};

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)str, CMPI_chars);
        free(str);

        bridge = virNetworkGetBridgeName(network);
        if (asprintf(&str, "Bridge: %s", bridge) == -1)
                return (CMPIStatus){CMPI_RC_ERR_FAILED, NULL};

        CMSetProperty(inst, "Caption",
                      (CMPIValue *)str, CMPI_chars);
        free(str);
        free(bridge);

        CMSetProperty(inst, "ResourceType",
                      (CMPIValue *)&type, CMPI_uint16);


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
        char **netnames = NULL;
        int i;
        int nets;

        if (id != NULL) {
                return _netpool_for_network(list,
                                            ns,
                                            conn,
                                            id,
                                            pfx_from_conn(conn),
                                            broker);
        }

        nets = virConnectNumOfNetworks(conn);
        if (nets < 0) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to list networks");
                goto out;
        }

        netnames = calloc(nets, sizeof(*netnames));
        if (netnames == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to allocate memory for %i net names", nets);
                goto out;
        }

        nets = virConnectListNetworks(conn, netnames, nets);

        for (i = 0; i < nets; i++) {
                _netpool_for_network(list,
                                     ns,
                                     conn,
                                     netnames[i],
                                     pfx_from_conn(conn),
                                     broker);
        }

 out:
        free(netnames);

        return s;
}

static CMPIInstance *diskpool_from_path(struct disk_pool *pool,
                                        virConnectPtr conn,
                                        const char *ns,
                                        const char *refcn,
                                        const CMPIBroker *broker)
{
        CMPIInstance *inst;
        char *poolid = NULL;
        const uint16_t type = CIM_RES_TYPE_DISK;

        inst = get_typed_instance(broker, refcn, "DiskPool", ns);

        if (asprintf(&poolid, "DiskPool/%s", pool->tag) == -1)
                return NULL;

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)poolid, CMPI_chars);

        CMSetProperty(inst, "ResourceType",
                      (CMPIValue *)&type, CMPI_uint16);

        CMSetProperty(inst, "AllocationUnits",
                      (CMPIValue *)"Megabytes", CMPI_chars);

        if (!diskpool_set_capacity(conn, inst, pool))
                CU_DEBUG("Failed to set capacity for disk pool: %s",
                         pool->tag);

        free(poolid);

        return inst;
}

static CMPIStatus diskpool_instance(virConnectPtr conn,
                                    struct inst_list *list,
                                    const char *ns,
                                    const char *id,
                                    const CMPIBroker *broker)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct disk_pool *pools = NULL;
        int count = 0;
        int i;

        count = get_diskpool_config(conn, &pools);
        if ((id == NULL) && (count == 0)) {
                CU_DEBUG("No defined DiskPools");
                return s;
        }

        CU_DEBUG("%i DiskPools", count);

        for (i = 0; i < count; i++) {
                CMPIInstance *pool;

                if ((id != NULL) && (!STREQ(id, pools[i].tag)))
                        continue;
                /* Either this matches id, or we're matching all */

                pool = diskpool_from_path(&pools[i],
                                          conn,
                                          ns,
                                          pfx_from_conn(conn),
                                          broker);
                if (pool != NULL)
                        inst_list_add(list, pool);
        }

        free_diskpool(pools, count);

        return s;
}

static CMPIStatus _get_pools(const CMPIBroker *broker,
                             const CMPIObjectPath *reference,
                             const uint16_t type,
                             const char *id,
                             struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn;

        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        if ((type == CIM_RES_TYPE_PROC) || 
            (type == CIM_RES_TYPE_ALL))
                s = procpool_instance(conn,
                                      list,
                                      NAMESPACE(reference),
                                      id,
                                      broker);

        if ((type == CIM_RES_TYPE_MEM) || 
            (type == CIM_RES_TYPE_ALL))
                s = mempool_instance(conn,
                                     list,
                                     NAMESPACE(reference),
                                     id,
                                     broker);

        if ((type == CIM_RES_TYPE_NET) || 
            (type == CIM_RES_TYPE_ALL))
                s = netpool_instance(conn,
                                     list,
                                     NAMESPACE(reference),
                                     id,
                                     broker);

        if ((type == CIM_RES_TYPE_DISK) || 
            (type == CIM_RES_TYPE_ALL))
                s = diskpool_instance(conn,
                                      list,
                                      NAMESPACE(reference),
                                      id,
                                      broker);

        if (type == CIM_RES_TYPE_UNKNOWN)
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance - resource pool type unknown");

        if (id && list->cur == 0)
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s)", id);

 out:
        virConnectClose(conn);
        return s;
}

CMPIStatus get_pool_by_name(const CMPIBroker *broker,
                            const CMPIObjectPath *reference,
                            const char *id,
                            CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;
        struct inst_list list;
        char *poolid = NULL;
        int ret;
        uint16_t type;

        inst_list_init(&list);

        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance");
                goto out;
        }

        type = res_type_from_pool_id(id);

        if (type == CIM_RES_TYPE_UNKNOWN) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s) - resource pool type mismatch",
                           id);
                goto out;
        }

        ret = sscanf(id, "%*[^/]/%as", &poolid);
        if (ret != 1) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s)",
                           id);
                goto out;
        }

        s = _get_pools(broker, reference, type, poolid, &list);
        if (s.rc != CMPI_RC_OK)
                goto out;

        *_inst = list.list[0];

 out:
        free(poolid);
        virConnectClose(conn);
        inst_list_free(&list);

        return s;
}

CMPIStatus get_pool_by_ref(const CMPIBroker *broker,
                           const CMPIObjectPath *reference,
                           CMPIInstance **instance)
{
        CMPIStatus s = {CMPI_RC_OK};
        CMPIInstance *inst = NULL;
        const char *id = NULL;
        uint16_t type_cls;
        uint16_t type_id;

        if (cu_get_str_path(reference, "InstanceID", &id) != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing InstanceID");
                goto out;
        }
        
        type_cls = res_type_from_pool_classname(CLASSNAME(reference));
        type_id = res_type_from_pool_id(id);

        if ((type_cls != type_id) || 
            (type_cls == CIM_RES_TYPE_UNKNOWN)) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s) - resource pool type mismatch",
                           id);
                goto out;
        }

        s = get_pool_by_name(broker, reference, id, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = cu_validate_ref(broker, reference, inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        *instance = inst;
        
 out:
        return s;
}

CMPIStatus enum_pools(const CMPIBroker *broker,
                      const CMPIObjectPath *reference,
                      const uint16_t type,
                      struct inst_list *list)
{
        return _get_pools(broker, reference, type, NULL, list);
}

static CMPIStatus return_pool(const CMPIObjectPath *ref,
                              const CMPIResult *results,
                              bool names_only)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct inst_list list;

        if (!provider_is_responsible(_BROKER, ref, &s))
                goto out;

        inst_list_init(&list);

        s = enum_pools(_BROKER,
                       ref,
                       res_type_from_pool_classname(CLASSNAME(ref)),
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

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_pool(reference, results, true);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{
        return return_pool(reference, results, false);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        CMPIStatus s = {CMPI_RC_OK};
        CMPIInstance *inst = NULL;

        s = get_pool_by_ref(_BROKER, reference, &inst);
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
                   Virt_DevicePool,
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
