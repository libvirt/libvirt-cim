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
#include <stdio.h>
#include <stdlib.h>

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_association.h>
#include "misc_util.h"
#include "device_parsing.h"

#include "Virt_VSSD.h"
#include "Virt_RASD.h"
#include "svpc_types.h"

const static CMPIBroker *_BROKER;

static CMPIStatus vssd_to_rasd(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        char *name = NULL;
        int i = 0;
        int types[] = {
                CIM_RASD_TYPE_PROC,
                CIM_RASD_TYPE_NET,
                CIM_RASD_TYPE_DISK,
                CIM_RASD_TYPE_MEM,
                -1
        };

        if (!match_hypervisor_prefix(ref, info))
                goto out;

        s = get_vssd_by_ref(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (!parse_instanceid(ref, NULL, &name)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get system name");
                goto out;
        }

        for (i = 0; types[i] > 0; i++) {
                rasds_for_domain(_BROKER,
                                 name,
                                 types[i],
                                 ref,
                                 info->properties,
                                 list);
        }

        free(name);

 out:
        return s;
}

static CMPIStatus rasd_to_vssd(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *vssd = NULL;
        const char *id = NULL;
        char *host = NULL;
        char *devid = NULL;
        int ret;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        if (cu_get_str_path(ref, "InstanceID", &id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing InstanceID");
                goto out;
        }

        ret = parse_fq_devid(id, &host, &devid);
        if (!ret) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid InstanceID");
                goto out;
        }

        s = get_vssd_by_name(_BROKER, ref, host, &vssd);
        if (s.rc != CMPI_RC_OK)
                goto out;

        inst_list_add(list, vssd);

 out:
        free(host);
        free(devid);

        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char* group_component[] = {
        "Xen_VirtualSystemSettingData",
        "KVM_VirtualSystemSettingData",
        NULL
};

static char* part_component[] = {
        "Xen_DiskResourceAllocationSettingData",
        "Xen_MemResourceAllocationSettingData",
        "Xen_NetResourceAllocationSettingData",
        "Xen_ProcResourceAllocationSettingData",
        "KVM_DiskResourceAllocationSettingData",
        "KVM_MemResourceAllocationSettingData",
        "KVM_NetResourceAllocationSettingData",
        "KVM_ProcResourceAllocationSettingData",
        NULL
};

static char* assoc_classname[] = {
        "Xen_VirtualSystemSettingDataComponent",
        "KVM_VirtualSystemSettingDataComponent",        
        NULL
};

static struct std_assoc forward = {
        .source_class = (char**)&group_component,
        .source_prop = "GroupComponent",

        .target_class = (char**)&part_component,
        .target_prop = "PartComponent",

        .assoc_class = (char**)&assoc_classname,

        .handler = vssd_to_rasd,
        .make_ref = make_ref
};

static struct std_assoc backward = {
        .source_class = (char**)&part_component,
        .source_prop = "PartComponent",

        .target_class = (char**)&group_component,
        .target_prop = "GroupComponent",

        .assoc_class = (char**)&assoc_classname,

        .handler = rasd_to_vssd,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &forward,
        &backward,
        NULL
};

STDA_AssocMIStub(, 
                 Virt_VSSDComponent,
                 _BROKER,
                 libvirt_cim_init(),
                 handlers);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
