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
 * Get the resource type of a given pool from the pool's classname
 *
 * @param classname The classname of the pool
 * Returns the resource type
 */
uint16_t res_type_from_pool_classname(const char *classname);

/**
 * Get the resource type of a given pool from the pool's InstanceID
 *
 * @param id The InstanceID of the pool
 * Returns the resource type
 */
uint16_t res_type_from_pool_id(const char *id);

/**
 * Get all device pools on the system for the given type
 * 
 *
 * @param broker The current Broker
 * @param reference Defines the libvirt connection to use
 * @param type The device pool type or CIM_RES_TYPE_ALL
 *             to get all resource pools
 * @param list The list of returned instances
 */
CMPIStatus enum_pools(const CMPIBroker *broker,
                      const CMPIObjectPath *reference,
                      const uint16_t type,
                      struct inst_list *list);

/**
 * Get a device pools instance for the given reference 
 *
 * @param broker The current Broker
 * @param reference The reference passed to the CIMOM 
 * @param instance Return corresponding instance 
 */
CMPIStatus get_pool_by_ref(const CMPIBroker *broker,
                           const CMPIObjectPath *reference,
                           CMPIInstance **instance);

/**
 * Get device pool instance specified by the id
 *
 * @param broker A pointer to the current broker
 * @param ref The object path containing namespace and prefix info
 * @param name The device pool id
 * @param _inst In case of success the pointer to the instance
 * @returns CMPIStatus
 */
CMPIStatus get_pool_by_name(const CMPIBroker *broker,
                            const CMPIObjectPath *reference,
                            const char *id,
                            CMPIInstance **_inst);

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
