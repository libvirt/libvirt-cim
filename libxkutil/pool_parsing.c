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
#include "../src/svpc_types.h"

static void cleanup_net_pool(struct net_pool pool) {
        free(pool.addr);
        free(pool.netmask);
        free(pool.ip_start);
        free(pool.ip_end);
        free(pool.forward_mode);
        free(pool.forward_dev);
}

void cleanup_virt_pool(struct virt_pool **pool)
{
        struct virt_pool *_pool = *pool;

        if ((pool == NULL) || (*pool == NULL))
                return;
 
        if (_pool->type == CIM_RES_TYPE_NET)
                cleanup_net_pool(_pool->pool_info.net);

        free(_pool->id);
        free(_pool);

        *pool = NULL;
}

int define_pool(virConnectPtr conn, const char *xml, int res_type)
{
        int ret = 1;

        if (res_type == CIM_RES_TYPE_NET) {
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
        }

        return ret; 
}

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */

