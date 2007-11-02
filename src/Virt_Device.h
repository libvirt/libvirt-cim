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
 * @param dom The domain in question
 * @param ref The namespace
 * @param list A pointer to an array of CMPIInstance objects (should
 *             be NULL initially)
 * @param cur The number of items in the list (0 initially)
 * @param max The size of the list (0 initially)
 * @returns Nonzero on success
 */
int dom_devices(const CMPIBroker *broker,
                virDomainPtr dom,
                const char *ns,
                int type,
                struct inst_list *list);

/**
 * Return a device instance for a given devid
 *
 * @param broker A pointer to the CIM broker
 * @param conn The libvirt connection to use
 * @param devid The device id
 * @param reference the namespace
 * @returns The instance, or NULL if not found
 */
CMPIInstance *instance_from_devid(const CMPIBroker *broker,
                                  virConnectPtr conn,
                                  const char *devid,
                                  const char *ns,
                                  int type);

int device_type_from_classname(const char *classname);

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
