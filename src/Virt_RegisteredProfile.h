/*
 * Copyright IBM Corp. 2007, 2008
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
#ifndef __VIRT_REGISTERED_PROFILE_H
#define __VIRT_REGISTERED_PROFILE_H

CMPIStatus enum_profiles(const CMPIBroker *broker,
                         const CMPIObjectPath *reference,
                         const char **properties,
                         struct inst_list *list);

CMPIStatus get_profile(const CMPIBroker *broker,
                       const CMPIObjectPath *reference,
                       const char **properties,
                       const char* pfx,
                       struct reg_prof *profile,
                       CMPIInstance **_inst);

CMPIStatus get_profile_by_name(const CMPIBroker *broker,
                               const CMPIObjectPath *reference,
                               const char *name,
                               const char **properties,
                               CMPIInstance **_inst);

CMPIStatus get_profile_by_ref(const CMPIBroker *broker,
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
