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
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
#ifndef __VIRT_RASD_H
#define __VIRT_RASD_H

#include "device_parsing.h"

#define VIRT_DISK_TYPE_DISK  0
#define VIRT_DISK_TYPE_CDROM 1
#define VIRT_DISK_TYPE_FLOPPY 2
#define VIRT_DISK_TYPE_FS 3
#define VIRT_DISK_TYPE_LUN 4

char *rasd_to_xml(CMPIInstance *rasd);

/**
 * Get a list of RASDs for a given domain
 *
 * @param broker The current broker
 * @param ref Defines the libvirt connection to use
 * @param domain The domain id (NULL means for all domains)
 * @param type The ResourceType of the desired RASDs
 * @param properties The properties to filter for
 * @param _list The list of instances to populate
 */
CMPIStatus enum_rasds(const CMPIBroker *broker,
                      const CMPIObjectPath *ref,
                      const char *domain,
                      const uint16_t type,
                      const char **properties,
                      struct inst_list *_list);

CMPIrc res_type_from_rasd_classname(const char *cn, uint16_t *type);
CMPIrc rasd_classname_from_type(uint16_t type, const char **cn);

CMPIrc pool_rasd_classname_from_type(uint16_t type, const char **classname);

CMPIStatus get_rasd_by_name(const CMPIBroker *broker,
                            const CMPIObjectPath *reference,
                            const char *name,
                            const uint16_t type,
                            const char **properties,
                            CMPIInstance **_inst);

CMPIStatus get_rasd_by_ref(const CMPIBroker *broker,
                           const CMPIObjectPath *reference,
                           const char **properties,
                           CMPIInstance **_inst);

int list_rasds(virConnectPtr conn,
               const uint16_t type,
               const char *host,
               struct virt_device **list);

CMPIInstance *rasd_from_vdev(const CMPIBroker *broker,
                             struct virt_device *dev,
                             const char *host,
                             const CMPIObjectPath *ref,
                             const char **properties);

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
