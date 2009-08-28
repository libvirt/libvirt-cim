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
#include  "device_parsing.h"

/**
 * Get a list of domain instances
 *
 * @param broker A pointer to the current broker
 * @param reference The object path containing namespace and prefix info
 * @param instlist A pointer to an initialized inst_list to populate
 * @returns CMPIStatus
 */
CMPIStatus enum_domains(const CMPIBroker *broker,
                        const CMPIObjectPath *reference,
                        struct inst_list *instlist);

/**
 * Get domain instance specified by the client given domain 
 * object path
 *
 * @param broker A pointer to the current broker
 * @param ref The client given object path
 * @param _inst In case of success the pointer to the instance
 * @returns CMPIStatus
 */
CMPIStatus get_domain_by_ref(const CMPIBroker *broker,
                             const CMPIObjectPath *reference,
                             CMPIInstance **_inst);

/**
 * Get domain instance specified by the domain name
 *
 * @param broker A pointer to the current broker
 * @param ref The object path containing namespace and prefix info
 * @param name The domain name
 * @param _inst In case of success the pointer to the instance
 * @returns CMPIStatus
 */
CMPIStatus get_domain_by_name(const CMPIBroker *broker,
                              const CMPIObjectPath *reference,
                              const char *name,
                              CMPIInstance **_inst);

/**
 * Create a domain instance from the domain structure. Note that the instance
 * doesn't necessarily represents an existing domain (can represent a deleted
 * one, for instance)
 *
 * @param broker A pointer to the current broker
 * @param namespace The namespace to used by the domain instance
 * @param prefix The virtualization prefix (i.e. KVM, Xen, LXC)
 * @param dominfo A pointer to the struct domain used to fill the instance
 * @param _inst In case of success the pointer to the instance
 * @returns CMPIStatus
 */
CMPIStatus instance_from_dominfo(const CMPIBroker *broker,
                                 const char *namespace,
                                 const char *prefix,
                                 struct domain *dominfo,
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
