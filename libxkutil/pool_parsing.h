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
#ifndef __RES_POOLS_H_
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
              DISK_POOL_LOGICAL} pool_type;
        char *path;
};

struct virt_pool {
        uint16_t type;
        union {
                struct net_pool net;
                struct disk_pool disk;
        } pool_info;
        char *id;
};

void cleanup_virt_pool(struct virt_pool **pool);

int define_pool(virConnectPtr conn, const char *xml, int res_type);
int destroy_pool(virConnectPtr conn, const char *name, int res_type);


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
