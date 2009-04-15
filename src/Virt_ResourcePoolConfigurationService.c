/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *  Kaitlin Rupert <karupert@us.ibm.com>
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
#include "xmlgen.h"

#include "svpc_types.h"
#include "Virt_HostSystem.h"
#include "Virt_ResourcePoolConfigurationService.h"
#include "Virt_DevicePool.h"
#include "Virt_RASD.h"

const static CMPIBroker *_BROKER;

const char *DEF_POOL_NAME = "libvirt-cim-pool";

/*
 *  * Right now, detect support and use it, if available.
 *   * Later, this can be a configure option if needed
 *    */
#if LIBVIR_VERSION_NUMBER > 4000
# define VIR_USE_LIBVIRT_STORAGE 1
#else
# define VIR_USE_LIBVIRT_STORAGE 0
#endif

static CMPIStatus create_child_pool_parse_args(const CMPIArgs *argsin,
                                               const char **name,
                                               CMPIArray **set,
                                               CMPIArray **parent_arr)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if (cu_get_str_arg(argsin, "ElementName", name) != CMPI_RC_OK) {
                CU_DEBUG("No ElementName string argument");
                *name = strdup(DEF_POOL_NAME);
        }

        if (cu_get_array_arg(argsin, "Settings", set) != CMPI_RC_OK) {
                CU_DEBUG("Failed to get Settings array arg");
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Missing argument `Settings'");
                goto out;
        }

        if (cu_get_array_arg(argsin, "ParentPool", parent_arr) != CMPI_RC_OK)
                CU_DEBUG("No parent pool specified during pool creation");

 out:
        return s;
}

static const char *net_rasd_to_pool(CMPIInstance *inst,
                                    struct virt_pool *pool)
{
        const char *val = NULL;
        const char *msg = NULL;

        /*FIXME:  Need to add validation of addresses if user specified */

        if (cu_get_str_prop(inst, "Address", &val) != CMPI_RC_OK)
                val = "192.168.122.1";

        free(pool->pool_info.net.addr);
        pool->pool_info.net.addr = strdup(val);

        if (cu_get_str_prop(inst, "Netmask", &val) != CMPI_RC_OK)
                val = "255.255.255.0";

        free(pool->pool_info.net.netmask);
        pool->pool_info.net.netmask = strdup(val);

        if (cu_get_str_prop(inst, "IPRangeStart", &val) != CMPI_RC_OK)
                val = "192.168.122.2";

        free(pool->pool_info.net.ip_start);
        pool->pool_info.net.ip_start = strdup(val);

        if (cu_get_str_prop(inst, "IPRangeStart", &val) != CMPI_RC_OK)
                val = "192.168.122.254";

        free(pool->pool_info.net.ip_end);
        pool->pool_info.net.ip_end = strdup(val);

        return msg;

}

#if VIR_USE_LIBVIRT_STORAGE
static const char *disk_rasd_to_pool(CMPIInstance *inst,
                                    struct virt_pool *pool)
{
        const char *val = NULL;
        const char *msg = NULL;
        uint16_t type;

        if (cu_get_u16_prop(inst, "Type", &type) != CMPI_RC_OK)
                return "Missing `Type' property";

        if (type != DISK_POOL_DIR)
                return "Storage pool type not supported";

        pool->pool_info.disk.pool_type = type;

        if (cu_get_str_prop(inst, "Path", &val) != CMPI_RC_OK)
                return "Missing `Path' property";

        pool->pool_info.disk.path = strdup(val);

        return msg;

}

static const char *_delete_pool(virConnectPtr conn,
                                const char *pool_name,
                                uint16_t type)
{
        const char *msg = NULL;

        if (destroy_pool(conn, pool_name, type) == 0)
                msg = "Unable to destroy resource pool";

        return msg;
}
#else
static const char *disk_rasd_to_pool(CMPIInstance *inst,
                                    struct virt_pool *pool)
{
        return "Storage pool creation not supported in this version of libvirt";
}

static const char *_delete_pool(virConnectPtr conn,
                                const char *pool_name,
                                uint16_t type)
{
        return "Storage pool deletion not supported in this version of libvirt";
}
#endif

static const char *rasd_to_vpool(CMPIInstance *inst,
                                 struct virt_pool *pool,
                                 uint16_t type)
{
        pool->type = type;

        if (type == CIM_RES_TYPE_NET) {
                return net_rasd_to_pool(inst, pool);
        } else if (type == CIM_RES_TYPE_DISK) {
                return disk_rasd_to_pool(inst, pool);
        }

        pool->type = CIM_RES_TYPE_UNKNOWN;

        return "Resource type not supported on this platform";
}

static const char *get_pool_properties(CMPIArray *settings,
                                       struct virt_pool *pool)
{
        CMPIObjectPath *op;
        CMPIData item;
        CMPIInstance *inst;
        const char *msg = NULL;
        uint16_t type;
        int count;

        count = CMGetArrayCount(settings, NULL);
        if (count < 1)
                return "No resources specified";

        if (count > 1)
                CU_DEBUG("More than one RASD specified during pool creation");

        item = CMGetArrayElementAt(settings, 0, NULL);
        if (CMIsNullObject(item.value.inst))
                return "Internal array error";

        inst = item.value.inst;

        op = CMGetObjectPath(inst, NULL);
        if (op == NULL)
                return "Unknown resource instance type";

        if (res_type_from_rasd_classname(CLASSNAME(op), &type) != CMPI_RC_OK)
                return "Unable to determine resource type";

        msg = rasd_to_vpool(inst, pool, type);

        return msg;
}

static char *get_pool_id(int res_type,
                         const char *name) 
{
        char *id = NULL;
        const char *pool = NULL;

        if (res_type == CIM_RES_TYPE_NET)
                pool = "NetworkPool";
        else if (res_type == CIM_RES_TYPE_DISK)
                pool = "DiskPool";
        else if (res_type == CIM_RES_TYPE_MEM)
                pool = "MemoryPool";
        else if (res_type == CIM_RES_TYPE_PROC)
                pool = "ProcessorPool";
        else if (res_type == CIM_RES_TYPE_GRAPHICS)
                pool = "GraphicsPool";
        else if (res_type == CIM_RES_TYPE_INPUT)
                pool = "InputPool";
        else
                pool = "Unknown";

        if (asprintf(&id, "%s/%s", pool, name) == -1) {
                return NULL;
        }

        return id;
}

static CMPIInstance *connect_and_create(char *xml,
                                        const CMPIObjectPath *ref,
                                        const char *id,
                                        int res_type,
                                        CMPIStatus *s)
{
        virConnectPtr conn;
        CMPIInstance *inst = NULL;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), s);
        if (conn == NULL) {
                CU_DEBUG("libvirt connection failed");
                return NULL;
        }

        if (define_pool(conn, xml, res_type) == 0) {
                virt_set_status(_BROKER, s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "Unable to create resource pool");
                goto out;
        }

        *s = get_pool_by_name(_BROKER, ref, id, &inst);
        if (s->rc != CMPI_RC_OK) {
                CU_DEBUG("Failed to get new pool instance: %s", id);
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to lookup resulting pool");
        }

 out:
        virConnectClose(conn);

        return inst;
}

static CMPIStatus create_child_pool(CMPIMethodMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference,
                                    const CMPIArgs *argsin,
                                    CMPIArgs *argsout)
{
        uint32_t rc = CIM_SVPC_RETURN_FAILED;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        CMPIArray *set;
        CMPIArray *parent_pools;
        CMPIObjectPath *result;
        struct virt_pool *pool = NULL;
        const char *name = NULL; 
        const char *msg = NULL; 
        char *full_id = NULL;
        char *xml = NULL;

        CU_DEBUG("CreateChildResourcePool");

        s = create_child_pool_parse_args(argsin, &name, &set, &parent_pools);
        if (s.rc != CMPI_RC_OK)
                goto out;

        pool = calloc(1, sizeof(*pool));
        if (pool == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to allocate pool struct");
                goto out;
        }

        msg = get_pool_properties(set, pool);
        if (msg != NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Settings Error: %s", msg);

                goto out;
        }

        full_id = get_pool_id(pool->type, name);
        if (full_id == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to format resulting pool ID");
                goto out;
        }

        s = get_pool_by_name(_BROKER, reference, full_id, &inst);
        if (s.rc == CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Pool with that name already exists");
                goto out;
        }

        pool->id = strdup(name);

        xml = pool_to_xml(pool);
        if (xml == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to generate XML for resource pool");
                goto out;
        }

        CU_DEBUG("Pool XML:\n%s", xml);

        inst = connect_and_create(xml, reference, full_id, pool->type, &s);
        if (inst == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to create resource pool");
                goto out;
        }

        result = CMGetObjectPath(inst, &s);
        if ((result != NULL) && (s.rc == CMPI_RC_OK)) {
                CMSetNameSpace(result, NAMESPACE(reference));
                CMAddArg(argsout, "Pool", &result, CMPI_ref);
        }

        /* FIXME:  Trigger indication here */

        cu_statusf(_BROKER, &s, CMPI_RC_OK, "");
 out:
        cleanup_virt_pool(&pool);

        free(xml);
        free(full_id);

        if (s.rc == CMPI_RC_OK)
                rc = CIM_SVPC_RETURN_COMPLETED;
        CMReturnData(results, &rc, CMPI_uint32);

        return s;
}

static CMPIStatus delete_pool(CMPIMethodMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const CMPIArgs *argsin,
                              CMPIArgs *argsout)
{
        uint32_t rc = CIM_SVPC_RETURN_FAILED;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        CMPIObjectPath *pool = NULL;
        virConnectPtr conn = NULL;
        const char *poolid = NULL;
        const char *msg = NULL;
        char *pool_name = NULL;
        uint16_t type;

        CU_DEBUG("DeleteResourcePool");

        if (cu_get_ref_arg(argsin, "Pool", &pool) != CMPI_RC_OK) {
                CU_DEBUG("Failed to get Pool reference arg");
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Missing argument `Pool'");
                goto out;
        }

        s = get_pool_by_ref(_BROKER, pool, &inst);
        if (s.rc != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Resource pool instance does not exist");
                goto out;
        }

        if (cu_get_str_path(pool, "InstanceID", &poolid) != CMPI_RC_OK) {
                CU_DEBUG("Failed to get InstanceID from pool reference");
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing InstanceID in pool reference");
                goto out;
        }

        pool_name = name_from_pool_id(poolid);
        if (pool_name == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Pool has invalid InstanceID");
                goto out;
        }

        type = res_type_from_pool_classname(CLASSNAME(pool));
        if (type == CIM_RES_TYPE_UNKNOWN) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to determine resource type of pool");
                goto out;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to connect to hypervisor");
                goto out;
        }

        msg = _delete_pool(conn, pool_name, type);
        if (msg != NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Storage pool deletion error: %s", msg);

                goto out;
        }

 out:
        free(pool_name);
        virConnectClose(conn);

        if (s.rc == CMPI_RC_OK)
                rc = CIM_SVPC_RETURN_COMPLETED;
        CMReturnData(results, &rc, CMPI_uint32);

        return s;
}

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
        .handler = create_child_pool,
        .args = {{"ElementName", CMPI_string, true},
                 {"Settings", CMPI_instanceA, false},
                 {"ParentPool", CMPI_refA, true},
                 ARG_END
        }
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
        .handler = delete_pool,
        .args = {{"Pool", CMPI_ref, false},
                 ARG_END
        }
};

static struct method_handler *my_handlers[] = {
        &CreateResourcePool,
        &CreateChildResourcePool,
        &AddResourcesToResourcePool,
        &RemoveResourcesFromResourcePool,
        &DeleteResourcePool,
        NULL,
};

STDIM_MethodMIStub(, 
                   Virt_ResourcePoolConfigurationService,
                   _BROKER, 
                   libvirt_cim_init(),
                   my_handlers);

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

CMPIStatus get_rpcs(const CMPIObjectPath *reference,
                    CMPIInstance **_inst,
                    const CMPIBroker *broker,
                    const CMPIContext *context,
                    bool is_get_inst)
{
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;
        const char *name = NULL;
        const char *ccname = NULL;

        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL) {
                if (is_get_inst)
                        cu_statusf(broker, &s,
                                   CMPI_RC_ERR_NOT_FOUND,
                                   "No such instance");
                goto out;
        }

        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "ResourcePoolConfigurationService",
                                  NAMESPACE(reference));
        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get "
                           "ResourcePoolConfigurationService instance");
                goto out;
        }

        s = get_host_system_properties(&name, 
                                       &ccname, 
                                       reference, 
                                       broker,
                                       context);
        if (s.rc != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get host attributes");
                goto out;
        }

        CMSetProperty(inst, "Name",
                      (CMPIValue *)"RPCS", CMPI_chars);

        CMSetProperty(inst, "SystemName",
                      (CMPIValue *)name, CMPI_chars);

        CMSetProperty(inst, "SystemCreationClassName",
                      (CMPIValue *)ccname, CMPI_chars);

        if (is_get_inst) {
                s = cu_validate_ref(broker, reference, inst);
                if (s.rc != CMPI_RC_OK)
                        goto out;
        }

        *_inst = inst;

 out:
        virConnectClose(conn);

        return s;
}

static CMPIStatus return_rpcs(const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              bool names_only,
                              bool is_get_inst)
{        
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        
        s = get_rpcs(reference, &inst, _BROKER, context, is_get_inst);
        if (s.rc != CMPI_RC_OK || inst == NULL)
                goto out;
        
        if (names_only)
                cu_return_instance_name(results, inst);
        else
                CMReturnInstance(results, inst);
        
 out:
        return s;
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        return return_rpcs(context, results, reference, false, true);
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_rpcs(context, results, reference, true, false);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{
        return return_rpcs(context, results, reference, false, false);
}


STD_InstanceMIStub(,
                   Virt_ResourcePoolConfigurationService,
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

