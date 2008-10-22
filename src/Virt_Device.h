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
#ifndef __VIRT_DEVICE_H
#define __VIRT_DEVICE_H

#include "misc_util.h"

/**
 * Return a list of devices for a given domain
 *
 * @param broker A pointer to the CIM broker
 * @param reference Defines the libvirt connection to use
 * @param domain The domain id (NULL means for all domains)
 * @param type The device type or CIM_RES_TYPE_ALL to get 
 *             all devices
 * @param list A pointer to an array of CMPIInstance objects
 *             (should be NULL initially)
 */
CMPIStatus enum_devices(const CMPIBroker *broker,
                        const CMPIObjectPath *reference,
                        const char *domain,
                        const uint16_t type,
                        struct inst_list *list);

/**
 * Returns the device instance defined by the reference
 *
 * @param broker A pointer to the CIM broker
 * @param reference The object path identifying the instance
 * @param _inst Contains the pointer to the instance in case 
 *             of success
 * @returns CMPIStatus of the operation
 */
CMPIStatus get_device_by_ref(const CMPIBroker *broker,
                             const CMPIObjectPath *reference,
                             CMPIInstance **_inst);

/**
 * Returns the device instance for a given name and type
 *
 * @param broker A pointer to the CIM broker
 * @param reference The object path containing namespace info
 * @param name The name "<vm>/<resource>"
 * @param type The resource type
 * @param _inst The instance pointer in case of success
 * @returns The result as CMPIStatus
 */
CMPIStatus get_device_by_name(const CMPIBroker *broker,
                              const CMPIObjectPath *reference,
                              const char *name,
                              const uint16_t type,
                              CMPIInstance **_inst);

uint16_t res_type_from_device_classname(const char *classname);

int get_input_dev_caption(const char *type,
                          const char *bus,
                          char **cap);

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
