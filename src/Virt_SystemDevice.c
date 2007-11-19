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
                           const char *ns)
{
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        CMPIStatus s;
        int ret = 0;

        conn = lv_connect(_BROKER, &s);
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
                           char *ns)
{
        int i;

        for (i = 0; i < DEV_TYPE_COUNT; i++)
                get_dom_devices(name, list, device_types[i], ns);

        return i;
}

static CMPIInstance *host_instance(char *name,
                                   const char *ns)
{
        CMPIInstance *inst = NULL;
        virConnectPtr conn = NULL;
        CMPIStatus s;
        CMPIObjectPath *op;
        char *host_class;

        host_class = get_typed_class("ComputerSystem");
        if (host_class == NULL)
                goto out;

        op = CMNewObjectPath(_BROKER, ns, host_class, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op))
                goto out;

        conn = lv_connect(_BROKER, &s);
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
        char *host = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int ret;

        ASSOC_MATCH(info->provider_name, CLASSNAME(ref));

        host = cu_get_str_path(ref, "Name");
        if (host == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing Name");
                goto out;
        }

        if (info->result_class) {
                int type;

                type = device_type_from_classname(info->result_class);

                ret = get_dom_devices(host, list, type, NAMESPACE(ref));
        } else {
                ret = get_all_devices(host, list, NAMESPACE(ref));
        }

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
        char *devid = NULL;
        char *host = NULL;
        char *dev = NULL;
        CMPIInstance *sys;
        CMPIStatus s;

        ASSOC_MATCH(info->provider_name, CLASSNAME(ref));

        devid = cu_get_str_path(ref, "DeviceID");
        if (devid == NULL) {
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

        sys = host_instance(host,
                            NAMESPACE(ref));

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
        free(devid);

        return s;
}

static struct std_assoc forward = {
        .source_class = "CIM_System",
        .source_prop = "GroupComponent",

        .target_class = "CIM_LogicalDevice",
        .target_prop = "PartComponent",

        .handler = sys_to_dev,
        .make_ref = make_ref
};

static struct std_assoc backward = {
        .source_class = "CIM_LogicalDevice",
        .source_prop = "PartComponent",

        .target_class = "CIM_System",
        .target_prop = "GroupComponent",

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
