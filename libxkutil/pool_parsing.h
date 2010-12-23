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
#ifndef __RES_POOLS_H
#define __RES_POOLS_H

#include <stdint.h>
#include <stdbool.h>
#include <libvirt/libvirt.h>

#include "../src/svpc_types.h"

struct net_pool {
        char *addr;
        char *netmask;
        char *ip_start;
        char *ip_end;
        char *forward_mode;
        char *forward_dev;
};

struct disk_pool {
        enum {DISK_POOL_UNKNOWN, 
              DISK_POOL_DIR, 
              DISK_POOL_FS, 
              DISK_POOL_NETFS, 
              DISK_POOL_DISK, 
              DISK_POOL_ISCSI, 
              DISK_POOL_LOGICAL,
              DISK_POOL_SCSI} pool_type;
        char *path;
        char **device_paths;
        uint16_t device_paths_ct;
        char *host;
        char *src_dir;
        char *adapter;
        char *port_name;
        char *node_name;
};

struct virt_pool {
        uint16_t type;
        union {
                struct net_pool net;
                struct disk_pool disk;
        } pool_info;
        char *id;
};

struct storage_vol {
        enum {VOL_FORMAT_UNKNOWN,
              VOL_FORMAT_RAW,
              VOL_FORMAT_QCOW2} format_type;
        char *vol_name;
        char *path;
        uint16_t alloc;
        uint16_t cap;
        char *cap_units;
};

struct virt_pool_res {
        uint16_t type;
        union {
                struct storage_vol storage_vol;
        } res;
        char *pool_id;
};

void cleanup_virt_pool(struct virt_pool **pool);
void cleanup_virt_pool_res(struct virt_pool_res **res);

int get_pool_from_xml(const char *xml, struct virt_pool *pool, int type);

int define_pool(virConnectPtr conn, const char *xml, int res_type);
int destroy_pool(virConnectPtr conn, const char *name, int res_type);

char *create_resource(virConnectPtr conn, const char *pname,
                      const char *xml, int res_type);

int delete_resource(virConnectPtr conn, const char *rname, int res_type);

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
