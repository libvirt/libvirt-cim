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
#include <libvirt/libvirt.h>
#include <libcmpiutil/libcmpiutil.h>
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


/**
 * Get the InstanceID of a pool that a given RASD id (for type) is in
 *
 * @param broker The current Broker
 * @param refcn A reference classname to be used for libvirt
 *              connections.  This can be anything as long as the
 *              prefix is correct.
 * @param type The ResourceType of the RASD
 * @param id The InstanceID of the RASD
 */
char *pool_member_of(const CMPIBroker *broker,
                     const char *refcn,
                     uint16_t type,
                     const char *id);

/**
 * Get all device pools on the system for the given connection
 *
 * @param broker The current Broker
 * @param conn The libvirt connection to use
 * @param ns Namespace for the pools
 * @param list Return instances in this struct
 */
CMPIStatus get_all_pools(const CMPIBroker *broker,
                         virConnectPtr conn,
                         const char *ns,
                         struct inst_list *list);
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
