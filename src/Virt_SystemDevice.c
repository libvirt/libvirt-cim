/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *  Jay Gagnon <grendel@linux.vnet.ibm.com>
 *  Zhengang Li <lizg@cn.ibm.com>
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include <libcmpiutil/libcmpiutil.h>
#include "device_parsing.h"
#include "misc_util.h"
#include "cs_util.h"
#include <libcmpiutil/std_association.h>

#include "Virt_ComputerSystem.h"
#include "Virt_Device.h"

/* Associate an XXX_ComputerSystem to the proper XXX_LogicalDisk
 * and XXX_NetworkPort instances.
 *
 *  -- or --
 *
 * Associate an XXX_LogicalDevice to the proper XXX_ComputerSystem
 */

const static CMPIBroker *_BROKER;

static CMPIStatus sys_to_dev(const CMPIObjectPath *ref,
                             struct std_assoc_info *info,
                             struct inst_list *list)
{
        const char *host = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        s = get_domain_by_ref(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (cu_get_str_path(ref, "Name", &host) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing Name");
                goto out;
        }

        s = enum_devices(_BROKER, ref, host, CIM_RES_TYPE_ALL, list);

 out:
        return s;
}

static CMPIStatus dev_to_sys(const CMPIObjectPath *ref,
                             struct std_assoc_info *info,
                             struct inst_list *list)
{
        const char *devid = NULL;
        char *host = NULL;
        char *dev = NULL;
        CMPIInstance *inst = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if (!match_hypervisor_prefix(ref, info))
                return s;

        s = get_device_by_ref(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (cu_get_str_path(ref, "DeviceID", &devid) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing DeviceID");
                goto out;
        }

        if (!parse_fq_devid(devid, &host, &dev)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid DeviceID");
                goto out;
        }

        s = get_domain_by_name(_BROKER, ref, host, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        inst_list_add(list, inst);

 out:
        free(dev);
        free(host);

        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char* group_component[] = {
        "Xen_ComputerSystem",
        "KVM_ComputerSystem",
        "LXC_ComputerSystem",
        NULL
};

static char* part_component[] = {
        "Xen_Processor",
        "Xen_Memory",
        "Xen_NetworkPort",
        "Xen_LogicalDisk",
        "Xen_DisplayController",
        "Xen_PointingDevice",
        "KVM_Processor",
        "KVM_Memory",
        "KVM_NetworkPort",
        "KVM_LogicalDisk",
        "KVM_DisplayController",
        "KVM_PointingDevice",
        "LXC_Processor",
        "LXC_Memory",
        "LXC_NetworkPort",
        "LXC_LogicalDisk",
        "LXC_DisplayController",
        "LXC_PointingDevice",
        NULL
};

static char* assoc_classname[] = {
        "Xen_SystemDevice",
        "KVM_SystemDevice",
        "LXC_SystemDevice",
        NULL
};

static struct std_assoc forward = {
        .source_class = (char**)&group_component,
        .source_prop = "GroupComponent",

        .target_class = (char**)&part_component,
        .target_prop = "PartComponent",

        .assoc_class = (char**)&assoc_classname,

        .handler = sys_to_dev,
        .make_ref = make_ref
};

static struct std_assoc backward = {
        .source_class = (char**)&part_component,
        .source_prop = "PartComponent",

        .target_class = (char**)&group_component,
        .target_prop = "GroupComponent",

        .assoc_class = (char**)&assoc_classname,

        .handler = dev_to_sys,
        .make_ref = make_ref
};

static struct std_assoc *assoc_handlers[] = {
        &forward,
        &backward,
        NULL
};

STDA_AssocMIStub(,
                 Virt_SystemDevice,
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
