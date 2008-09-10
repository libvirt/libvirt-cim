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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include <libcmpiutil/libcmpiutil.h>
#include "misc_util.h"
#include "profiles.h"
#include <libcmpiutil/std_association.h>

#include "config.h"

#include "Virt_RegisteredProfile.h"

/* Associate an XXX_RegisteredProfile to the proper XXX_ManagedElement.
 *
 *  -- or --
 *
 * Associate an XXX_ManagedElement to the proper XXX_RegisteredProfile.
 */

const static CMPIBroker *_BROKER;

static CMPIStatus elem_instances(const CMPIObjectPath *ref,
                                 struct std_assoc_info *info,
                                 struct inst_list *list,
                                 virConnectPtr conn,
                                 const char *class)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIObjectPath *op;
        CMPIEnumeration *en  = NULL;
        CMPIData data ;
        char *classname;

        if (class == NULL)
                return s;

        classname = get_typed_class(pfx_from_conn(conn), 
                                    class);
        if (classname == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED, 
                           "Can't assemble classname" );
                goto out;
        }

        op = CMNewObjectPath(_BROKER, CIM_VIRT_NS, classname, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op))
                goto out;
        
        en = CBEnumInstances(_BROKER, info->context , op, info->properties, &s);
        if (en == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED, 
                           "Upcall enumInstances to target class failed");
                goto out;
        }

        while (CMHasNext(en, &s)) {
                data = CMGetNext(en, &s);
                if (CMIsNullObject(data.value.inst)) {
                        cu_statusf(_BROKER, &s,
                                   CMPI_RC_ERR_FAILED, 
                                   "Failed to retrieve enumeration entry");
                        goto out;
                }

                inst_list_add(list, data.value.inst);
        }

 out:
        free(classname);
        
        return s;
}

static CMPIStatus prof_to_elem(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;
        virConnectPtr conn = NULL;
        const char *id;
        int i;
        
        if (!match_hypervisor_prefix(ref, info))
                return s;

        s = get_profile_by_ref(_BROKER, ref, info->properties, &instance);
        if (s.rc != CMPI_RC_OK)
                goto out;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                return s;

        if (cu_get_str_path(ref, "InstanceID", &id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "No InstanceID specified");
                goto out;
        }

        for (i = 0; profiles[i] != NULL; i++) {
                if (STREQC(id, profiles[i]->reg_id)) {
                        s = elem_instances(ref,
                                           info,
                                           list, 
                                           conn,
                                           profiles[i]->scoping_class);
                        s = elem_instances(ref,
                                           info,
                                           list, 
                                           conn,
                                           profiles[i]->central_class);
                        break;
                }
        }
        
 out:
        virConnectClose(conn);

        return s;
}

static CMPIStatus elem_to_prof(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;
        virConnectPtr conn = NULL;
        char *classname;
        int i;
        
        if (!match_hypervisor_prefix(ref, info))
                return s;

        instance = CBGetInstance(_BROKER,
                                 info->context,
                                 ref,
                                 NULL,
                                 &s);
        if (s.rc != CMPI_RC_OK)
                return s;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                return s;

        classname = class_base_name(CLASSNAME(ref));
        if (classname == NULL) {
                cu_statusf(_BROKER, &s, 
                           CMPI_RC_ERR_FAILED,
                           "Can't get class name");
                goto out;
        }

        for (i = 0; profiles[i] != NULL; i++) {

                if ((profiles[i]->scoping_class == NULL) || 
                   (!STREQC(profiles[i]->scoping_class, classname))) {

                   if ((profiles[i]->central_class == NULL) || 
                      (!STREQC(profiles[i]->central_class, classname)))
                        continue;
                }

                s = get_profile(_BROKER,
                                ref, 
                                info->properties,
                                pfx_from_conn(conn),
                                profiles[i],
                                &instance);
                if (s.rc != CMPI_RC_OK)
                        goto out;

                inst_list_add(list, instance);
        }
             
 out: 
        free(classname);
        virConnectClose(conn);

        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char* conformant_standard[] = {
        "Xen_RegisteredProfile",
        "KVM_RegisteredProfile",
        "LXC_RegisteredProfile",
        NULL
};

static char* managed_element[] = {
        "Xen_HostSystem",
        "Xen_ComputerSystem",
        "Xen_DiskPool",
        "Xen_MemoryPool",
        "Xen_NetworkPool",
        "Xen_ProcessorPool",
        "Xen_VirtualSystemMigrationService",
        "KVM_HostSystem",
        "KVM_ComputerSystem",
        "KVM_DiskPool",
        "KVM_MemoryPool",
        "KVM_NetworkPool",
        "KVM_ProcessorPool",
        "KVM_VirtualSystemMigrationService",
        "LXC_HostSystem",
        "LXC_ComputerSystem",
        "LXC_DiskPool",
        "LXC_MemoryPool",
        "LXC_NetworkPool",
        "LXC_ProcessorPool",
        "LXC_VirtualSystemMigrationService",
        NULL
};

static char* assoc_classname[] = {
        "Xen_ElementConformsToProfile",
        "KVM_ElementConformsToProfile",
        "LXC_ElementConformsToProfile",
        NULL
};

static struct std_assoc forward = {
        .source_class = (char**)&conformant_standard,
        .source_prop = "ConformantStandard",

        .target_class = (char**)&managed_element,
        .target_prop = "ManagedElement",

        .assoc_class = (char**)&assoc_classname,

        .handler = prof_to_elem,
        .make_ref = make_ref
};

static struct std_assoc backward = {
        .source_class = (char**)&managed_element,
        .source_prop = "ManagedElement",

        .target_class = (char**)&conformant_standard,
        .target_prop = "ConformantStandard",

        .assoc_class = (char**)&assoc_classname,

        .handler = elem_to_prof,
        .make_ref = make_ref
};

static struct std_assoc *assoc_handlers[] = {
        &forward,
        &backward,
        NULL
};

STDA_AssocMIStub(,
                 Virt_ElementConformsToProfile,
                 _BROKER, 
                 libvirt_cim_init(), 
                 assoc_handlers);
/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
