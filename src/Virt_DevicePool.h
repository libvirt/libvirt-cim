/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
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
#ifndef __VIRT_DEVICE_POOL_H
#define __VIRT_DEVICE_POOL_H

#include <cmpidt.h>
#include <libvirt.h>
#include <libcmpiutil.h>
#include <stdint.h>

extern char *device_pool_names[];

CMPIStatus get_pool_by_type(const CMPIBroker *broker,
                            virConnectPtr conn,
                            const char *type,
                            const char *ns,
                            struct inst_list *list);

CMPIInstance *get_pool_by_id(const CMPIBroker *broker,
                             virConnectPtr conn,
                             const char *id,
                             const char *ns);

char *pool_member_of(const CMPIBroker *broker, uint16_t type, char *id);

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
