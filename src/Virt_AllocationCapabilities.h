/*
 * Copyright IBM Corp. 2008
 *
 * Authors:
 *  Heidi Eckhart <heidieck@linux.vnet.ibm.com>
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
#ifndef __VIRT_ALLOCATIONCAPABILITIES_H
#define __VIRT_ALLOCATIONCAPABILITIES_H

#include "misc_util.h"

/**
 * Return the instance of the AllocationCapabilities instance, 
 * defined by the id
 *
 * @param broker A pointer to the current broker
 * @param ref The reference
 * @param properties list of properties to set
 * @param id The InstanceID of the AllocationCapabilities
 * @param inst The list of instance(s) in case of success
 * @returns The status of this operation
 */
CMPIStatus enum_alloc_cap_instances(const CMPIBroker *broker,
                                    const CMPIObjectPath *ref,
                                    const char **properties,
                                    const char *id,
                                    struct inst_list *list);

CMPIStatus get_alloc_cap_by_id(const CMPIBroker *broker,
                               const CMPIObjectPath *ref,
                               const char *poolid,
                               CMPIInstance **inst);

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
