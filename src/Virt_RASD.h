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
#ifndef __VIRT_RASD_H
#define __VIRT_RASD_H

char *rasd_to_xml(CMPIInstance *rasd);

/**
 * Get a list of RASDs for a given domain
 *
 * @param broker The current broker
 * @param name The name of the domain in question
 * @param type The ResourceType of the desired RASDs
 * @param ref A reference used for hypervisor connection and namespace
 *            setting of the resulting instances
 * @param _list The list of instances to populate
 */
int rasds_for_domain(const CMPIBroker *broker,
                     const char *name,
                     const uint16_t type,
                     const CMPIObjectPath *ref,
                     const char **properties,
                     struct inst_list *_list);

CMPIrc rasd_type_from_classname(const char *cn, uint16_t *type);
CMPIrc rasd_classname_from_type(uint16_t type, const char **cn);

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
