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

#include "libcmpiutil.h"
#include "device_parsing.h"
#include "misc_util.h"
#include "cs_util.h"
#include "std_association.h"

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

#define DEV_TYPE_COUNT 4
const static int device_types[DEV_TYPE_COUNT] = 
        {VIRT_DEV_NET,
         VIRT_DEV_DISK,
         VIRT_DEV_MEM,
         VIRT_DEV_VCPU,
        };

#define TRACE(l, f, arg...) printf(f "\n", ##arg)

static int get_dom_devices(const char *name,
                           struct inst_list *list,
                           int type,
                           const char *host_cn,
                           const char *ns)
{
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        CMPIStatus s;
        int ret = 0;

        conn = connect_by_classname(_BROKER, host_cn, &s);
        if (conn == NULL)
                goto out;

        dom = virDomainLookupByName(conn, name);
        if (dom == NULL)
                goto out;

        ret = dom_devices(_BROKER, dom, ns, type, list);

        virDomainFree(dom);
 out:
        virConnectClose(conn);

        return ret;
}

static int get_all_devices(const char *name,
                           struct inst_list *list,
                           const char *host_cn,
                           const char *ns)
{
        int i;

        for (i = 0; i < DEV_TYPE_COUNT; i++)
                get_dom_devices(name, list, device_types[i], host_cn, ns);

        return i;
}

static CMPIInstance *host_instance(char *name,
                                   const CMPIObjectPath *ref)
{
        CMPIInstance *inst = NULL;
        virConnectPtr conn = NULL;
        CMPIStatus s;
        CMPIObjectPath *op;
        char *host_class;

        host_class = get_typed_class(CLASSNAME(ref),
                                     "ComputerSystem");
        if (host_class == NULL)
                goto out;

        op = CMNewObjectPath(_BROKER, NAMESPACE(ref), host_class, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op))
                goto out;

        conn = connect_by_classname(_BROKER, host_class, &s);
        if (conn == NULL)
                goto out;

        inst = instance_from_name(_BROKER, conn, name, op);

 out:
        free(host_class);
        virConnectClose(conn);

        return inst;
}


static CMPIInstance *make_ref(const CMPIObjectPath *ref,
                              const CMPIInstance *inst,
                              struct std_assoc_info *info,
                              struct std_assoc *assoc)
{
        CMPIInstance *refinst = NULL;

        refinst = get_typed_instance(_BROKER,
                                     CLASSNAME(ref),
                                     "SystemDevice",
                                     NAMESPACE(ref));

        if (refinst != NULL) {
                CMPIObjectPath *instop;

                instop = CMGetObjectPath(inst, NULL);

                set_reference(assoc, refinst, ref, instop);
        }

        return refinst;
}

static CMPIStatus sys_to_dev(const CMPIObjectPath *ref,
                             struct std_assoc_info *info,
                             struct inst_list *list)
{
        const char *host = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int ret;

        ASSOC_MATCH(info->provider_name, CLASSNAME(ref));

        if (cu_get_str_path(ref, "Name", &host) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing Name");
                goto out;
        }

        ret = get_all_devices(host,
                              list,
                              CLASSNAME(ref),
                              NAMESPACE(ref));

        if (ret >= 0) {
                CMSetStatus(&s, CMPI_RC_OK);
        } else {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get devices");
        }

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
        CMPIInstance *sys;
        CMPIStatus s;

        ASSOC_MATCH(info->provider_name, CLASSNAME(ref));

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

        sys = host_instance(host, ref);

        if (sys == NULL)
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to find DeviceID `%s'", devid);
        else {
                inst_list_add(list, sys);
                CMSetStatus(&s, CMPI_RC_OK);
        }

 out:
        free(dev);
        free(host);

        return s;
}

char* group_component[] = {
        "Xen_ComputerSystem",
        "KVM_ComputerSystem",
        NULL
};

char* part_component[] = {
        "Xen_Processor",
        "Xen_Memory",
        "Xen_NetworkPort",
        "Xen_LogicalDisk",
        "KVM_Processor",
        "KVM_Memory",
        "KVM_NetworkPort",
        "KVM_LogicalDisk",
        NULL
};

char* assoc_classname[] = {
        "Xen_SystemDevice",
        "KVM_SystemDevice",        
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

STDA_AssocMIStub(, Xen_SystemDeviceProvider, _BROKER, libvirt_cim_init(), assoc_handlers);
STDA_AssocMIStub(, KVM_SystemDeviceProvider, _BROKER, libvirt_cim_init(), assoc_handlers);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
