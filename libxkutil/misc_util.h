/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *  Heidi Eckhart <heidieck@linux.vnet.ibm.com>
 *  Jay Gagnon <grendel@linux.vnet.ibm.com>
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
#ifndef __MISC_UTIL_H
#define __MISC_UTIL_H

#include <libvirt/libvirt.h>
#include <stdbool.h>
#include <stdint.h>

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_association.h>

/* Check if the provider is reponsible for the given class:
 * e.g. Xen is running on the system and KVM_... is asked for,
 * the provider is not responsible for the request -> 
 * return false
 * status is set in error case
 */
bool provider_is_responsible(const CMPIBroker *broker,
                             const CMPIObjectPath *reference,
                             CMPIStatus *status);

/* Establish a libvirt connection to the appropriate
 * hypervisor, as determined from the prefix of classname
 */
virConnectPtr connect_by_classname(const CMPIBroker *broker,
                                   const char *classname,
                                   CMPIStatus *s);

/* Establish a libvirt connection to the appropriate hypervisor,
 * as determined by the state of the system, or the value of the
 * HYPURI environment variable, if set.
 */
virConnectPtr lv_connect(const CMPIBroker *broker, CMPIStatus *s);

/* Free count virDomainPtr objects from list */
void free_domain_list(virDomainPtr *list, int count);

/*
 * Xen_ComputerSystem
 *  ^       ^
 *  |       |
 *  |       +------ Base name
 *  |
 *  +-------------- Prefix name
 */
char *class_prefix_name(const char *classname);
char *class_base_name(const char *classname);

/* Returns a class prefix based on the URI reported by conn */
const char *pfx_from_conn(virConnectPtr conn);

/* Returns "%s_%s" % (prefix($refcn), new_base) */
char *get_typed_class(const char *refcn, const char *new_base);
CMPIInstance *get_typed_instance(const CMPIBroker *broker,
                                 const char *refcn,
                                 const char *base,
                                 const char *namespace);

/* Parse an OrgID:LocID string into its constituent parts */
int parse_instance_id(char *iid, char **orgid, char **locid);

const char *get_key_from_ref_arg(const CMPIArgs *args, char *arg, char *key);

bool domain_exists(virConnectPtr conn, const char *name);
bool domain_online(virDomainPtr dom);

uint64_t allocated_memory(virConnectPtr conn);

char *association_prefix(const char *provider_name);
bool match_pn_to_cn(const char *pn, const char *cn);

int parse_id(const char *id, char **pfx, char **name);
bool parse_instanceid(const CMPIObjectPath *ref, char **pfx, char **name);

bool libvirt_cim_init(void);

#define ASSOC_MATCH(pn, cn)                            \
        if (!match_pn_to_cn((pn), (cn))) {             \
                return (CMPIStatus){CMPI_RC_OK, NULL}; \
        }

#endif

bool match_hypervisor_prefix(const CMPIObjectPath *reference,
                             struct std_assoc_info *info);

CMPIInstance *make_reference(const CMPIBroker *broker,
                             const CMPIObjectPath *source_ref,
                             const CMPIInstance *target_inst,
                             struct std_assoc_info *info,
                             struct std_assoc *assoc);


#define LIBVIRT_CIM_DEFAULT_MAKEREF()                                   \
        static CMPIInstance* make_ref(const CMPIObjectPath *source_ref, \
                                      const CMPIInstance *target_inst,  \
                                      struct std_assoc_info *info,      \
                                      struct std_assoc *assoc)          \
        {                                                               \
                return make_reference(_BROKER,                          \
                                      source_ref,                       \
                                      target_inst,                      \
                                      info,                             \
                                      assoc);                           \
        }

#define REF2STR(r) CMGetCharPtr(CMObjectPathToString(r, NULL))

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
