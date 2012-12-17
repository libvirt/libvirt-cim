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

struct tmp_disk_pool {
        char *tag;
        char *path;
        bool primordial;
};

static void free_diskpool(struct tmp_disk_pool *pools, int count)
{
        int i;

        if (pools == NULL) {
                return;
        }

        for (i = 0; i < count; i++) {
                free(pools[i].tag);
                free(pools[i].path);
        }

        free(pools);
}

/*
 * If fail, *_pools will be freed and set to NULL, and *_count will be set to
 * zero.
 */
static bool get_disk_parent(struct tmp_disk_pool **_pools,
                            int *_count)
{
        struct tmp_disk_pool *pools = NULL;
        int count;

        count = *_count;
        pools = *_pools;

        pools = realloc(pools, (count + 1) * (sizeof(*pools)));
        if (pools == NULL) {
                CU_DEBUG("Failed to alloc new pool");
                pools = *_pools;
                goto fail;
        }

        pools[count].path = NULL;
        pools[count].primordial = true;
        pools[count].tag = strdup("0");
        if (pools[count].tag == NULL) {
                count++;
                goto fail;
        }
        count++;

        *_count = count;
        *_pools = pools;
        return true;

 fail:
        free_diskpool(pools, count);
        /* old pool is invalid, update it */
        *_count = 0;
        *_pools = NULL;
        return false;
}

#if VIR_USE_LIBVIRT_STORAGE
int get_disk_pool(virStoragePoolPtr poolptr, struct virt_pool **pool)
{
        char *xml;
        int ret;

        xml = virStoragePoolGetXMLDesc(poolptr, 0);
        if (xml == NULL)
                return 0;

        CU_DEBUG("pool xml is %s", xml);

        *pool = malloc(sizeof(**pool));
        if (*pool == NULL) {
                ret = 0;
                goto out;
        }

        ret = get_pool_from_xml(xml, *pool, CIM_RES_TYPE_DISK);

 out:
        free(xml);

        return ret;
}

/*
 * return 0 on success, negative on fail, *pools and *_count will be set
 * only on success .
 */
static int get_diskpool_config(virConnectPtr conn,
                               struct tmp_disk_pool **_pools,
                               int *_count)
{
        int count = 0, realcount = 0;
        int i;
        char ** names = NULL;
        struct tmp_disk_pool *pools = NULL;
        int ret = 0;

        count = virConnectNumOfStoragePools(conn);
        if (count < 0) {
                ret = count;
                goto out;
        } else if (count == 0) {
                goto set_parent;
        }

        names = calloc(count, sizeof(char *));
        if (names == NULL) {
                CU_DEBUG("Failed to alloc space for %i pool names", count);
                ret = -1;
                goto out;
        }

        realcount = virConnectListStoragePools(conn, names, count);
        if (realcount < 0) {
                CU_DEBUG("Failed to get storage pools, return %d.", realcount);
                ret = realcount;
                goto free_names;
        }
        if (realcount == 0) {
                CU_DEBUG("Zero pools got, but prelist is %d.", count);
                goto set_parent;
        }

        pools = calloc(realcount, sizeof(*pools));
        if (pools == NULL) {
                CU_DEBUG("Failed to alloc space for %i pool structs",
                         realcount);
                ret = -2;
                goto free_names;
        }

        for (i = 0; i < realcount; i++) {
                pools[i].tag = names[i];
                names[i] = NULL;
                pools[i].primordial = false;
        }

 set_parent:
        if (!get_disk_parent(&pools, &realcount)) {
                CU_DEBUG("Failed in adding parentpool.");
                ret = -4;
                /* pools is already freed in get_disk_parent().*/
                goto free_names;
        }

        /* succeed */
        *_pools = pools;
        *_count = realcount;

 free_names:
        for (i = 0; i < count; i++) {
                free(names[i]);
        }
        free(names);

 out:
        return ret;
}

static bool diskpool_set_capacity(virConnectPtr conn,
                                  CMPIInstance *inst,
                                  struct tmp_disk_pool *_pool)
{
        bool result = false;
        virStoragePoolPtr pool;
        virStoragePoolInfo info;
        uint64_t cap;
        uint64_t res;
        uint16_t type;
        struct virt_pool *pool_vals = NULL;
        const char *pool_str = NULL;
        uint16_t autostart;
        int  start;

        pool = virStoragePoolLookupByName(conn, _pool->tag);
        if (pool == NULL) {
                CU_DEBUG("Failed to lookup storage pool `%s'", _pool->tag);
                goto out;
        }

        if (virStoragePoolGetInfo(pool, &info) == -1) {
                CU_DEBUG("Failed to get info for pool `%s'", _pool->tag);
                goto out;
        }

        cap = info.capacity >> 20;
        res = info.allocation >> 20;

        CMSetProperty(inst, "Capacity",
                      (CMPIValue *)&cap, CMPI_uint64);

        CMSetProperty(inst, "Reserved",
                      (CMPIValue *)&res, CMPI_uint64);

        if (get_disk_pool(pool, &pool_vals) != 0) {
                CU_DEBUG("Error getting pool path for: %s", _pool->tag);
        } else {
                if (pool_vals->pool_info.disk.path != NULL) {
                        pool_str = strdup(pool_vals->pool_info.disk.path);

                        CMSetProperty(inst, "Path",
                                      (CMPIValue *)pool_str, CMPI_chars);
                }
                if (pool_vals->pool_info.disk.host != NULL) {
                        pool_str = strdup(pool_vals->pool_info.disk.host);

                        CMSetProperty(inst, "Host",
                                      (CMPIValue *)pool_str, CMPI_chars);
                }
                if (pool_vals->pool_info.disk.src_dir != NULL) {
                        pool_str = strdup(pool_vals->pool_info.disk.src_dir);

                        CMSetProperty(inst, "SourceDirectory",
                                      (CMPIValue *)pool_str, CMPI_chars);
                }

                type = pool_vals->pool_info.disk.pool_type;
                CMSetProperty(inst, "OtherResourceType", 
                              (CMPIValue *)get_disk_pool_type(type), 
                              CMPI_chars);
        }

        if (virStoragePoolGetAutostart(pool, &start) == -1) {
                CU_DEBUG("Failed to read if %s StoragePool is set for "
                         "Autostart", _pool->tag);
                goto out;
        }

        autostart = start;

        CMSetProperty(inst, "Autostart",
                      (CMPIValue *)&autostart, CMPI_uint16);

        result = true;
 out:
        virStoragePoolFree(pool);
        cleanup_virt_pool(&pool_vals);

        return result;
}

static bool _diskpool_is_member(virConnectPtr conn,
                                const struct tmp_disk_pool *pool,
                                const char *file)
{
        virStorageVolPtr vol = NULL;
        bool result = false;
        virStoragePoolPtr pool_vol = NULL;
        const char *pool_name = NULL;

        vol = virStorageVolLookupByPath(conn, file);
        if (vol == NULL)
                goto out;

        pool_vol = virStoragePoolLookupByVolume(vol);
        if (vol != NULL) {
                pool_name = virStoragePoolGetName(pool_vol);                
                if ((pool_name != NULL) && (STREQC(pool_name, pool->tag)))
                        result = true;
        }
        
 out:
        CU_DEBUG("Image %s in pool %s: %s",
                 file,
                 pool->tag,
                 result ? "YES": "NO");

        virStorageVolFree(vol);
        virStoragePoolFree(pool_vol);

        return result;
}
#else
static bool parse_diskpool_line(struct tmp_disk_pool *pool,
                                const char *line)
{
        int ret;

        ret = sscanf(line, "%as %as", &pool->tag, &pool->path);
        if (ret != 2) {
                free(pool->tag);
                free(pool->path);
                pool->tag = NULL;
                pool->path = NULL;
        }
        pool->primordial = false;

        return (ret == 2);
}

/*
 * return 0 on success, negative on fail, *pools and *_count will be set
 * only on success .
 */
static int get_diskpool_config(virConnectPtr conn,
                               struct tmp_disk_pool **_pools,
                               int *_count)
{
        const char *path = DISK_POOL_CONFIG;
        FILE *config;
        char *line = NULL;
        size_t len = 0;
        int count = 0, ret = 0;
        struct tmp_disk_pool *pools = NULL, *new_pools = NULL, *pool = NULL;

        config = fopen(path, "r");
        if (config == NULL) {
                CU_DEBUG("Failed to open %s: %m", path);
                ret = -1;
                goto out;
        }

        pool = calloc(1, sizeof(*pool));
        if (!pool) {
                CU_DEBUG("Failed to calloc pool");
                ret = -2;
                goto close;
        }

        /* *line will be automatically freed by getline() */
        while (getline(&line, &len, config) > 0) {
                if (parse_diskpool_line(pool, line)) {
                        new_pools = realloc(pools,
                                           (count + 1) * (sizeof(*pools)));
                        if (new_pools == NULL) {
                                CU_DEBUG("Failed to alloc new pool");
                                ret = -3;
                                goto free_pools;
                        }
                        pools = new_pools;
                        pools[count] = *pool;
                        memset(pool, 0, sizeof(*pool));
                        count++;
                }
        }

        if (!get_disk_parent(&pools, &count)) {
                CU_DEBUG("Failed in adding parentpool.");
                ret = -4;
                /* pools is already freed by get_disk_parent() */
                goto clean;
        }

        /* succeed */
        *_pools = pools;
        *_count = count;
        goto clean;

 free_pools:
        free_diskpool(pools, count);
 clean:
        free(line);
        free_diskpool(pool, 1);
 close:
        fclose(config);
 out:
        return ret;
}

static bool diskpool_set_capacity(virConnectPtr conn,
                                  CMPIInstance *inst,
                                  struct tmp_disk_pool *pool)
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

        CMSetProperty(inst, "Path",
                      (CMPIValue *)pool->path, CMPI_chars);

        result = true;
 out:
        return result;
}

static bool _diskpool_is_member(virConnectPtr conn,
                                const struct tmp_disk_pool *pool,
                                const char *file)
{
        return STARTS_WITH(file, pool->path);
}
#endif

static char *_diskpool_member_of(virConnectPtr conn,
                                 const char *file)
{
        struct tmp_disk_pool *pools = NULL;
        int count;
        int i, ret;
        char *pool = NULL;

        ret = get_diskpool_config(conn, &pools, &count);
        if (ret < 0) {
                return NULL;
        }

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

        count = get_devices(dom, &devs, CIM_RES_TYPE_DISK, 0);

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
        virDomainFree(dom);
        virConnectClose(conn);

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

        for (i = 0; i < num; i++)
                free(networks[i]);
        free(networks);

        return network;
}

static char *_netpool_member_of(virConnectPtr conn,
                                const struct net_device *ndev)
{
        virNetworkPtr net = NULL;
        const char *netname;
        char *pool = NULL;

        if (ndev->source == NULL) {
                CU_DEBUG("Unable to determine pool since no network "
                         "source defined");
                goto out;
        }

        if (STREQ(ndev->type, "bridge"))
                net = bridge_to_network(conn, ndev->source);
        else if (STREQ(ndev->type, "network"))
                net = virNetworkLookupByName(conn, ndev->source);
        else {
                CU_DEBUG("Unhandled network type `%s'", ndev->type);
        }

        if (net == NULL)
                goto out;

        netname = virNetworkGetName(net);
        if (netname == NULL)
                goto out;

        if (asprintf(&pool, "NetworkPool/%s", netname) == -1)
                pool = NULL;

        CU_DEBUG("Determined pool: %s (%s, %s)", pool, ndev->source, netname);

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

        count = get_devices(dom, &devs, CIM_RES_TYPE_NET, 0);

        for (i = 0; i < count; i++) {
                if (STREQ((devs[i].id), dev)) {
                        result = _netpool_member_of(conn,
                                                    &devs[i].dev.net);
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
        else if (type == CIM_RES_TYPE_GRAPHICS)
                poolid = strdup("GraphicsPool/0");
        else if (type == CIM_RES_TYPE_INPUT)
                poolid = strdup("InputPool/0");
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
        else if (strstr(classname, "GraphicsPool"))
                return CIM_RES_TYPE_GRAPHICS;
        else if (strstr(classname, "InputPool"))
                return CIM_RES_TYPE_INPUT;
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
        else if (strstr(id, "GraphicsPool"))
                return CIM_RES_TYPE_GRAPHICS;
        else if (strstr(id, "InputPool"))
                return CIM_RES_TYPE_INPUT;
        else
                return CIM_RES_TYPE_UNKNOWN;
}

char *name_from_pool_id(const char *id)
{
        char *s;

        s = strchr(id, '/');
        if (s == NULL)
                return NULL;

        return strdup((char *)s+1);
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

static bool mempool_set_consumed(CMPIInstance *inst, virConnectPtr conn)
{
        uint64_t memory = 0;
        int *domain_ids = NULL;
        int count, i = 0;

        count = virConnectNumOfDomains(conn);
        if (count <= 0)
                goto out;

        domain_ids = calloc(count, sizeof(domain_ids[0]));
        if (domain_ids == NULL)
                goto out;

        if (virConnectListDomains(conn, domain_ids, count) == -1)
                goto out;

        for (i = 0; i < count; i++) {
                virDomainPtr dom = NULL;
                virDomainInfo dom_info;

                dom = virDomainLookupByID(conn, domain_ids[i]);
                if (dom == NULL) {
                        CU_DEBUG("Cannot connect to domain %d: excluding",
                                domain_ids[i]);
                        continue;
                }

                if (virDomainGetInfo(dom, &dom_info) == 0)
                        memory += dom_info.memory;

                virDomainFree(dom);
        }

 out:
        free(domain_ids);

        CMSetProperty(inst, "Reserved",
                      (CMPIValue *)&memory, CMPI_uint64);
        CMSetProperty(inst, "CurrentlyConsumedResource",
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

static void set_params(CMPIInstance *inst,
                       uint16_t type,
                       const char *id,
                       const char *units,
                       const char *caption,
                       bool primordial)
{
        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)id, CMPI_chars);

        CMSetProperty(inst, "PoolID",
                      (CMPIValue *)id, CMPI_chars);

        CMSetProperty(inst, "ResourceType",
                      (CMPIValue *)&type, CMPI_uint16);

        if (units != NULL) {
                CMSetProperty(inst, "AllocationUnits",
                              (CMPIValue *)units, CMPI_chars);

                CMSetProperty(inst, "ConsumedResourceUnits",
                              (CMPIValue *)units, CMPI_chars);
        }

        if (caption != NULL)
                CMSetProperty(inst, "Caption",
                              (CMPIValue *)caption, CMPI_chars);

        CMSetProperty(inst, "Primordial",
                      (CMPIValue *)&primordial, CMPI_boolean);
}

static CMPIStatus mempool_instance(virConnectPtr conn,
                                   struct inst_list *list,
                                   const char *ns,
                                   const char *_id,
                                   const CMPIBroker *broker)
{
        const char *id = "MemoryPool/0";
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
                                  ns,
                                  false);

        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get instance for MemoryPool");
                return s;
        }

        mempool_set_total(inst, conn);
        mempool_set_consumed(inst, conn);

        set_params(inst, CIM_RES_TYPE_MEM, id, "byte*2^10", NULL, true);

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
                                  ns,
                                  false);

        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get instance for ProcessorPool");
                return s;
        }

        procpool_set_total(inst, conn);

        set_params(inst, CIM_RES_TYPE_PROC, id, "Processors", NULL, true);

        inst_list_add(list, inst);

        return s;
}

static CMPIStatus _netpool_for_parent(struct inst_list *list,
                                      const char *ns,
                                      const char *refcn,
                                      const CMPIBroker *broker)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        char *id = NULL;
        CMPIInstance *inst;

        inst = get_typed_instance(broker,
                                  refcn,
                                  "NetworkPool",
                                  ns,
                                  false);
        if (inst == NULL) {
                CU_DEBUG("Unable to get instance: %s:%s_NetworkPool",
                         ns, refcn);
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Error getting pool instance");
                goto out;
        }

        if (asprintf(&id, "NetworkPool/0") == -1) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "");
                goto out;
        }

        set_params(inst, CIM_RES_TYPE_NET, id, NULL, NULL, true);
        free(id);

        inst_list_add(list, inst);
 out:

        return s;
}

static CMPIStatus _netpool_for_network(struct inst_list *list,
                                       const char *ns,
                                       virConnectPtr conn,
                                       const char *netname,
                                       const char *refcn,
                                       const CMPIBroker *broker)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        char *id = NULL;
        char *cap = NULL;
        char *bridge = NULL;
        CMPIInstance *inst;
        virNetworkPtr network = NULL;

        if (STREQC(netname, "0"))
                return _netpool_for_parent(list, ns, refcn, broker);

        CU_DEBUG("Looking up network `%s'", netname);
        network = virNetworkLookupByName(conn, netname);
        if (network == NULL) {
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "No such NetworkPool: %s", netname);
                goto out;
        }

        inst = get_typed_instance(broker,
                                  refcn,
                                  "NetworkPool",
                                  ns,
                                  false);
        if (inst == NULL) {
                CU_DEBUG("Unable to get instance: %s:%s_NetworkPool",
                         ns, refcn);
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Error getting pool instance");
                goto out;
        }

        if (asprintf(&id, "NetworkPool/%s", netname) == -1) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "");
                goto out;
        }

        bridge = virNetworkGetBridgeName(network);
        if (asprintf(&cap, "Bridge: %s", bridge) == -1) {
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "");
                goto out;
        }

        set_params(inst, CIM_RES_TYPE_NET, id, NULL, cap, false);
        free(cap);

        inst_list_add(list, inst);
 out:
        free(bridge);
        free(id);
        virNetworkFree(network);

        return s;
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
        int nets = 0;

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
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_FAILED,
                                conn,
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

        nets++;
        netnames = realloc(netnames, (nets) * (sizeof(*netnames)));
        if (netnames == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to allocate memory for %i net names", nets);
                goto out;
        }

        netnames[nets - 1] = strdup("0");

        for (i = 0; i < nets; i++) {
                _netpool_for_network(list,
                                     ns,
                                     conn,
                                     netnames[i],
                                     pfx_from_conn(conn),
                                     broker);
        }

 out:
        if (netnames != NULL) {
                for (i = 0; i < nets; i++)
                        free(netnames[i]);
                free(netnames);
        }

        return s;
}

static CMPIInstance *diskpool_from_path(struct tmp_disk_pool *pool,
                                        virConnectPtr conn,
                                        const char *ns,
                                        const char *refcn,
                                        const CMPIBroker *broker)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst;
        char *poolid = NULL;

        inst = get_typed_instance(broker, refcn, "DiskPool", ns, false);

        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to init DiskPool instance");
                goto out;
        }

        if (asprintf(&poolid, "DiskPool/%s", pool->tag) == -1)
                return NULL;

        set_params(inst, 
                   CIM_RES_TYPE_DISK, 
                   poolid, 
                   "Megabytes", 
                   pool->tag, 
                   pool->primordial);

        if (!diskpool_set_capacity(conn, inst, pool))
                CU_DEBUG("Failed to set capacity for disk pool: %s",
                         pool->tag);

out:
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
        struct tmp_disk_pool *pools = NULL;
        int count = 0;
        int i, ret;

        ret = get_diskpool_config(conn, &pools, &count);
        if (ret < 0) {
                CU_DEBUG("Failed to get diskpool config, return is %d.", ret);
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get diskpool config, return is %d.",
                           ret);
                return s;
        }
        if ((id == NULL) && (count == 0)) {
                CU_DEBUG("No defined DiskPools");
                free(pools);
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

static CMPIStatus graphicspool_instance(virConnectPtr conn,
                                        struct inst_list *list,
                                        const char *ns,
                                        const char *_id,
                                        const CMPIBroker *broker)
{
        const char *id = "GraphicsPool/0";
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if ((_id != NULL) && (!STREQC(_id, "0"))) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "No such graphics pool `%s'", id);
                return s;
        }

        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "GraphicsPool",
                                  ns,
                                  false);
        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get instance of %s_GraphicsPool", 
                           pfx_from_conn(conn));
                return s;
        }

        set_params(inst, CIM_RES_TYPE_GRAPHICS, id, NULL, NULL, true);

        inst_list_add(list, inst);

        return s;
}

static CMPIStatus inputpool_instance(virConnectPtr conn,
                                     struct inst_list *list,
                                     const char *ns,
                                     const char *_id,
                                     const CMPIBroker *broker)
{
        const char *id = "InputPool/0";
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if ((_id != NULL) && (!STREQC(_id, "0"))) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "No such input pool `%s'", id);
                return s;
        }

        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "InputPool",
                                  ns,
                                  false);
        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get instance of %s_InputPool", 
                           pfx_from_conn(conn));
                return s;
        }

        set_params(inst, CIM_RES_TYPE_INPUT, id, NULL, NULL, true);

        inst_list_add(list, inst);

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

        if ((type == CIM_RES_TYPE_GRAPHICS) || 
            (type == CIM_RES_TYPE_ALL))
                s = graphicspool_instance(conn,
                                          list,
                                          NAMESPACE(reference),
                                          id,
                                          broker);

        if ((type == CIM_RES_TYPE_INPUT) || 
            (type == CIM_RES_TYPE_ALL))
                s = inputpool_instance(conn,
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

        ret = sscanf(id, "%*[^/]/%a[^\n]", &poolid);
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

        if (list.cur == 0) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s)",
                           id);
                goto out;
        }

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

CMPIInstance *parent_device_pool(const CMPIBroker *broker,
                                 const CMPIObjectPath *reference,
                                 uint16_t type,
                                 CMPIStatus *s)
{
        CMPIInstance *inst = NULL;
        const char *id = NULL;

        if (type == CIM_RES_TYPE_MEM) {
                id = "MemoryPool/0";
        } else if (type == CIM_RES_TYPE_PROC) {
                id = "ProcessorPool/0";
        } else if (type == CIM_RES_TYPE_DISK) {
                id = "DiskPool/0";
        } else if (type == CIM_RES_TYPE_NET) {
                id = "NetworkPool/0";
        } else if (type == CIM_RES_TYPE_GRAPHICS) {
                id = "GraphicsPool/0";
        } else if (type == CIM_RES_TYPE_INPUT) {
                id = "InputPool/0";
        } else {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "No such device type `%s'", type);
                goto out;
        }

        *s = get_pool_by_name(broker, reference, id, &inst);
        if (inst == NULL) {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_FAILED,
                           "No default pool found for type %hi", type);
        }

 out:

        return inst;
}

CMPIInstance *default_device_pool(const CMPIBroker *broker,
                                  const CMPIObjectPath *reference,
                                  uint16_t type,
                                  CMPIStatus *s)
{
        CMPIInstance *inst = NULL;
        struct inst_list list;
        bool val;

        if ((type == CIM_RES_TYPE_DISK) || (type == CIM_RES_TYPE_NET)) {
                int i = 0;
                CMPIrc rc;

                inst_list_init(&list);

                *s = enum_pools(broker, reference, type, &list);
                if ((s->rc != CMPI_RC_OK) || (list.cur <= 0)) {
                        CU_DEBUG("Unable to enum pools to get parent pool");
                        goto out;
                }

                for (i = 0; i < list.cur; i++) {
                        rc = cu_get_bool_prop(list.list[i], 
                                              "Primordial", 
                                              &val);
                        if ((rc != CMPI_RC_OK) || (val))
                                continue;

                        inst = list.list[i];
                        break;
                }

                if (inst == NULL) {
                        cu_statusf(broker, s,
                                   CMPI_RC_ERR_FAILED,
                                   "No default pool found for type %hi", type);
                }
        } else {
                inst = parent_device_pool(broker, reference, type, s);
        }

 out:
        inst_list_free(&list);

        return inst;
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
