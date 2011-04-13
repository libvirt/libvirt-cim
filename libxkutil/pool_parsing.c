/*
 * Copyright IBM Corp. 2009
 *
 * Authors:
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#include <libcmpiutil/libcmpiutil.h>

#include "pool_parsing.h"
#include "device_parsing.h"
#include "../src/svpc_types.h"

/*
 *  * Right now, detect support and use it, if available.
 *   * Later, this can be a configure option if needed
 *    */
#if LIBVIR_VERSION_NUMBER > 4000
# define VIR_USE_LIBVIRT_STORAGE 1
#else
# define VIR_USE_LIBVIRT_STORAGE 0
#endif

static void cleanup_net_pool(struct net_pool pool) {
        free(pool.addr);
        free(pool.netmask);
        free(pool.ip_start);
        free(pool.ip_end);
        free(pool.forward_mode);
        free(pool.forward_dev);
}

static void cleanup_disk_pool(struct disk_pool pool) {
        uint16_t i;

        free(pool.path);
        free(pool.host);
        free(pool.src_dir);
        free(pool.adapter);
        free(pool.port_name);
        free(pool.node_name);

        for (i = 0; i < pool.device_paths_ct; i++) 
                free(pool.device_paths[i]);

        free(pool.device_paths);
}

void cleanup_virt_pool(struct virt_pool **pool)
{
        struct virt_pool *_pool = *pool;

        if ((pool == NULL) || (*pool == NULL))
                return;
 
        if (_pool->type == CIM_RES_TYPE_NET)
                cleanup_net_pool(_pool->pool_info.net);
        else if (_pool->type == CIM_RES_TYPE_DISK)
                cleanup_disk_pool(_pool->pool_info.disk);

        free(_pool->id);
        free(_pool);

        *pool = NULL;
}

static void cleanup_image_res(struct storage_vol vol) 
{
        free(vol.vol_name);
        free(vol.path);
        free(vol.cap_units);
}

void cleanup_virt_pool_res(struct virt_pool_res **res)
{
        struct virt_pool_res *_res = *res;

        if ((res == NULL) || (*res == NULL))
                return;

        if (_res->type == CIM_RES_TYPE_IMAGE)
                cleanup_image_res(_res->res.storage_vol);

        free(_res->pool_id);
        free(_res);

        *res = NULL;
}

static int parse_disk_target(xmlNode *node, struct disk_pool *pool)
{
        xmlNode *child;

        for (child = node->children; child != NULL; child = child->next) {
                if (XSTREQ(child->name, "path")) {
                        STRPROP(pool, path, child);
                        break;
                }
        }

        return 1;
}

static int parse_disk_source(xmlNode *node, struct disk_pool *pool)
{
        xmlNode *child;
        char **dev_paths = NULL;
        unsigned ct = 0;

        for (child = node->children; child != NULL; child = child->next) {
                if (XSTREQ(child->name, "device")) {
                        char **tmp = NULL;

                        tmp = realloc(dev_paths, sizeof(char *) * (ct + 1));
                        if (tmp == NULL) {
                                CU_DEBUG("Could not alloc space for dev path");
                                continue;
                        }
                        dev_paths = tmp;

                        dev_paths[ct] = get_attr_value(child, "path");
                        ct++;
                } else if (XSTREQ(child->name, "host")) {
                        pool->host = get_attr_value(child, "name");
                        if (pool->host == NULL)
                                goto err;
                } else if (XSTREQ(child->name, "dir")) {
                        pool->src_dir = get_attr_value(child, "path");
                        if (pool->src_dir == NULL)
                                goto err;
                } else if (XSTREQ(child->name, "adapter")) {
                        pool->adapter = get_attr_value(child, "name");
                        pool->port_name = get_attr_value(child, "wwpn");
                        pool->node_name = get_attr_value(child, "wwnn");
                }
        }

        pool->device_paths_ct = ct;
        pool->device_paths = dev_paths;

 err:

        return 1;
}

char *get_disk_pool_type(uint16_t type)
{

        switch (type) {
        case DISK_POOL_DIR:
                return "dir";
        case DISK_POOL_FS:
                return "fs";
        case DISK_POOL_NETFS:
                return "netfs";
        case DISK_POOL_DISK:
                return "disk";
        case DISK_POOL_ISCSI:
                return "iscsi";
        case DISK_POOL_LOGICAL:
                return "logical";
        case DISK_POOL_SCSI:
                return "scsi";
        default:
                return NULL;
        }

}

static const char *parse_disk_pool(xmlNodeSet *nsv, struct disk_pool *pool)
{
        xmlNode **nodes = nsv->nodeTab;
        xmlNode *child;
        const char *type_str = NULL;
        const char *name = NULL;
        int type = 0;

        type_str = get_attr_value(nodes[0], "type");

        if (STREQC(type_str, "dir"))
                type = DISK_POOL_DIR;
        else if (STREQC(type_str, "fs"))
                type = DISK_POOL_FS;
        else if (STREQC(type_str, "netfs"))
                type = DISK_POOL_NETFS;
        else if (STREQC(type_str, "disk"))
                type = DISK_POOL_DISK;
        else if (STREQC(type_str, "iscsi"))
                type = DISK_POOL_ISCSI;
        else if (STREQC(type_str, "logical"))
                type = DISK_POOL_LOGICAL;
        else if (STREQC(type_str, "scsi"))
                type = DISK_POOL_SCSI;
        else
                type = DISK_POOL_UNKNOWN;

        pool->pool_type = type;
              
        for (child = nodes[0]->children; child != NULL; child = child->next) {
                if (XSTREQ(child->name, "name")) {
                        name = get_node_content(child);
                } else if (XSTREQ(child->name, "target"))
                        parse_disk_target(child, pool);
                else if (XSTREQ(child->name, "source"))
                        parse_disk_source(child, pool);
        }

        return name;
}

int get_pool_from_xml(const char *xml, struct virt_pool *pool, int type)
{
        int len;
        int ret = 0;
        xmlDoc *xmldoc;
        xmlXPathContext *xpathctx;
        xmlXPathObject *xpathobj;
        const xmlChar *xpathstr = (xmlChar *)"/pool";
        const char *name;

        CU_DEBUG("Pool XML : %s", xml);
	len = strlen(xml) + 1;

        if ((xmldoc = xmlParseMemory(xml, len)) == NULL)
                goto err1;

        if ((xpathctx = xmlXPathNewContext(xmldoc)) == NULL)
                goto err2;

        if ((xpathobj = xmlXPathEvalExpression(xpathstr, xpathctx)) == NULL)
                goto err3;

        /* FIXME: Add support for parsing network pools */
        if (type == CIM_RES_TYPE_NET) {
                ret = 0;
                goto err1;
        }

        memset(pool, 0, sizeof(*pool));

        pool->type = CIM_RES_TYPE_DISK; 
        name = parse_disk_pool(xpathobj->nodesetval, 
                               &(pool)->pool_info.disk);
        if (name == NULL)
                ret = 0;

        pool->id = strdup(name); 

        xmlXPathFreeObject(xpathobj);
 err3:
        xmlXPathFreeContext(xpathctx);
 err2:
        xmlFreeDoc(xmldoc);
 err1:
        return ret;
}

int define_pool(virConnectPtr conn, const char *xml, struct virt_pool *pool)
{
        int ret = 1;

        if (pool->type == CIM_RES_TYPE_NET) {
                virNetworkPtr ptr = virNetworkDefineXML(conn, xml);
                if (ptr == NULL) {
                        CU_DEBUG("Unable to define virtual network");
                        return 0;
                }

                if (virNetworkCreate(ptr) != 0) {
                        CU_DEBUG("Unable to start virtual network");
                        ret = 0;

                        if (virNetworkUndefine(ptr) != 0)
                                CU_DEBUG("Unable to undefine virtual network");
                }

                virNetworkFree(ptr);
        } else if (pool->type == CIM_RES_TYPE_DISK) {
#if VIR_USE_LIBVIRT_STORAGE
                virStoragePoolPtr ptr = virStoragePoolDefineXML(conn, xml, 0);
                if (ptr == NULL) {
                        CU_DEBUG("Unable to define storage pool");
                        return 0;
                }

                if (virStoragePoolCreate(ptr, 0) != 0) {
                        CU_DEBUG("Unable to start storage pool");
                        ret = 0;

                        if (virStoragePoolUndefine(ptr) != 0)
                                CU_DEBUG("Unable to undefine storage pool");
                }

                if (pool->pool_info.disk.autostart == 1) {
                        if (virStoragePoolSetAutostart(ptr, 1) != 0) 
                                CU_DEBUG("Unable to set autostart for pool");
                }

                virStoragePoolFree(ptr);
#endif
        }


        return ret; 
}

int destroy_pool(virConnectPtr conn, const char *name, int res_type)
{
        int ret = 0;

        if (res_type == CIM_RES_TYPE_NET) {

                virNetworkPtr ptr = virNetworkLookupByName(conn, name);
                if (ptr == NULL) {
                        CU_DEBUG("Virtual network %s is not defined", name);
                        return ret;
                }

                if (virNetworkDestroy(ptr) != 0) {
                        CU_DEBUG("Unable to destroy virtual network");
                        goto err1;
                }

                if (virNetworkUndefine(ptr) != 0) {
                        CU_DEBUG("Unable to undefine virtual network");
                        goto err1;
                }

                ret = 1;

 err1:
                virNetworkFree(ptr);

        } else if (res_type == CIM_RES_TYPE_DISK) {
#if VIR_USE_LIBVIRT_STORAGE
                virStoragePoolPtr ptr = virStoragePoolLookupByName(conn, name);
                if (ptr == NULL) {
                        CU_DEBUG("Storage pool %s is not defined", name);
                        return 0;
                }

                if (virStoragePoolDestroy(ptr) != 0) {
                        CU_DEBUG("Unable to destroy storage pool");
                        goto err2;
                }

                if (virStoragePoolUndefine(ptr) != 0) {
                        CU_DEBUG("Unable to undefine storage pool");
                        goto err2;
                }

                ret = 1;

 err2:
                virStoragePoolFree(ptr);
#endif
        }

        return ret;
}

#if VIR_USE_LIBVIRT_STORAGE
char *create_resource(virConnectPtr conn,
                      const char *pname,
                      const char *xml,
                      int res_type)
{
        char *path = NULL;
        virStoragePoolPtr ptr = NULL;
        virStorageVolPtr vptr = NULL;

        if (res_type == CIM_RES_TYPE_IMAGE) {
                ptr = virStoragePoolLookupByName(conn, pname);
                if (ptr == NULL) {
                        CU_DEBUG("Storage pool %s is not defined", pname);
                        goto out;
                }

                vptr = virStorageVolCreateXML(ptr, xml, 0);
                if (vptr == NULL) {
                        CU_DEBUG("Unable to create storage volume in %s", 
                                 pname);
                        goto out;
                }

                if ((virStoragePoolRefresh(ptr, 0)) == -1)
                        CU_DEBUG("Unable to refresh storage pool");

                path = virStorageVolGetPath(vptr);
                if (path == NULL) {
                        CU_DEBUG("Unable to get storage volume path");
                        goto out;
                }

        }

 out:
        virStoragePoolFree(ptr);
        virStorageVolFree(vptr);

        return path;
}

int delete_resource(virConnectPtr conn,
                    const char *rname,
                    int res_type)
{
        int ret = 0;
        virStorageVolPtr ptr;
        virStoragePoolPtr pool_ptr;

        if (res_type == CIM_RES_TYPE_IMAGE) {
                ptr = virStorageVolLookupByPath(conn, rname);
                if (ptr == NULL) {
                        CU_DEBUG("Storage volume %s is not defined", rname);
                        goto out;
                }

                ret = virStorageVolDelete(ptr, 0);
                if (ret != 0) {
                        CU_DEBUG("Unable to delete storage volume %s", rname);
                } else {
                        pool_ptr = virStoragePoolLookupByVolume(ptr);
                        if (pool_ptr == NULL) {
                                CU_DEBUG("Unable to get storage pool "
                                         "from volume");
                        } else {
                                ret = virStoragePoolRefresh(pool_ptr, 0);
                                if (ret != 0) 
                                        CU_DEBUG("Unable to refresh storage "
                                                 "pool");
                        }
                        ret = 1;
                }

                virStorageVolFree(ptr);
        }

 out:

        return ret;
}
#else
char *create_resource(virConnectPtr conn,
                      const char *pname,
                      const char *xml,
                      int res_type)
{
          CU_DEBUG("Creating resources within libvirt pools not supported");
          return NULL;
}

int delete_resource(virConnectPtr conn,
                    const char *rname,
                    int res_type)
{
          CU_DEBUG("Deleting resources within libvirt pools not supported");
          return 0;
}
#endif

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */

