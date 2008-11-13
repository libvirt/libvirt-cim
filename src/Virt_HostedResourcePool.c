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
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include<cmpidt.h>
#include<cmpift.h>
#include<cmpimacs.h>

#include <libvirt/libvirt.h>

#include <libcmpiutil/libcmpiutil.h>
#include "misc_util.h"
#include <libcmpiutil/std_association.h>

#include <config.h>

#include "Virt_HostSystem.h"
#include "Virt_DevicePool.h"
#include "svpc_types.h"

static const CMPIBroker *_BROKER;

static CMPIStatus pool_to_sys(const CMPIObjectPath *ref,
                              struct std_assoc_info *info,
                              struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        if (!match_hypervisor_prefix(ref, info))
                goto out;

        s = get_pool_by_ref(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = get_host(_BROKER, info->context, ref, &inst, false);
        if (s.rc != CMPI_RC_OK)
                goto out;

        inst_list_add(list, inst);

 out:
        return s;
}

static CMPIStatus sys_to_pool(const CMPIObjectPath *ref,
                              struct std_assoc_info *info,
                              struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        CMPIObjectPath *virtref = NULL;

        if (!STARTS_WITH(CLASSNAME(ref), "Linux_") &&
            !match_hypervisor_prefix(ref, info))
                goto out;

        virtref = convert_sblim_hostsystem(_BROKER, ref, info);
        if (virtref == NULL)
                goto out;

        s = get_host(_BROKER, info->context, ref, &inst, true);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = enum_pools(_BROKER, virtref, CIM_RES_TYPE_ALL, list);

 out:
        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char* group_component[] = {
        "Xen_HostSystem",
        "KVM_HostSystem",
        "LXC_HostSystem",
        "Linux_ComputerSystem",
        NULL
};

static char* part_component[] = {
        "Xen_ProcessorPool",
        "Xen_MemoryPool",
        "Xen_NetworkPool",
        "Xen_DiskPool",
        "KVM_ProcessorPool",
        "KVM_MemoryPool",
        "KVM_NetworkPool",
        "KVM_DiskPool",
        "LXC_ProcessorPool",
        "LXC_MemoryPool",
        "LXC_NetworkPool",
        "LXC_DiskPool",
        NULL
};

static char* assoc_classname[] = {
        "Xen_HostedResourcePool",
        "KVM_HostedResourcePool",
        "LXC_HostedResourcePool",
        NULL
};

static struct std_assoc forward = {
        .source_class = (char**)&part_component,
        .source_prop = "PartComponent",

        .target_class = (char**)&group_component,
        .target_prop = "GroupComponent",

        .assoc_class = (char**)&assoc_classname,

        .handler = pool_to_sys,
        .make_ref = make_ref
};

static struct std_assoc backward = {
        .source_class = (char**)&group_component,
        .source_prop = "GroupComponent",

        .target_class = (char**)&part_component,
        .target_prop = "PartComponent",

        .assoc_class = (char**)&assoc_classname,

        .handler = sys_to_pool,
        .make_ref = make_ref
};

static struct std_assoc *assoc_handlers[] = {
        &forward,
        &backward,
        NULL
};

STDA_AssocMIStub(,
                 Virt_HostedResourcePool,
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
