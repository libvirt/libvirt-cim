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
#ifndef __VIRT_COMPUTERSYSTEM_H
#define __VIRT_COMPUTERSYSTEM_H

#include "misc_util.h"

/**
 * Return an instance of a Virt_ComputerSystem, based on a name
 *
 * @param broker A pointer to the current broker
 * @param conn The libvirt connection to use
 * @param name The name of the desired domain instance
 * @param ns The namespace to use
 * @returns The instance or NULL on failure
 */
CMPIInstance *instance_from_name(const CMPIBroker *broker,
                                 virConnectPtr conn,
                                 char *name,
                                 const CMPIObjectPath *ns);


/**
 * Get a list of domain instances
 *
 * @param broker A pointer to the current broker
 * @param conn The libvirt connection to use
 * @param op The namespace to use
 * @param instlist A pointer to an initialized inst_list to populate
 * @returns nonzero on success
 */
int enum_domains(const CMPIBroker *broker,
                 virConnectPtr conn,
                 const CMPIObjectPath *op,
                 struct inst_list *instlist);


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
