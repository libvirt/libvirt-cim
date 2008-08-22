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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <libvirt/libvirt.h>

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_association.h>
#include "misc_util.h"

#include "Virt_HostSystem.h"
#include "Virt_DevicePool.h"

const static CMPIBroker *_BROKER;

static char *bridge_from_netpool(virConnectPtr conn,
                                const char *poolid)
{
        char *netname = NULL;
        char *bridge = NULL;
        virNetworkPtr net = NULL;

        netname = name_from_pool_id(poolid);
        if (netname == NULL) {
                CU_DEBUG("Unable to parse network pool id: %s", poolid);
                goto out;
        }

        net = virNetworkLookupByName(conn, netname);
        if (net == NULL) {
                CU_DEBUG("Unable to find network %s", netname);
                goto out;
        }

        bridge = virNetworkGetBridgeName(net);
 out:
        free(netname);
        virNetworkFree(net);

        return bridge;
}

static CMPIInstance *get_bridge_instance(const CMPIContext *context,
                                         const CMPIObjectPath *ref,
                                         const char *bridge,
                                         CMPIStatus *s)
{
        CMPIObjectPath *path;
        CMPIInstance *inst = NULL;
        const char *cn = "Linux_EthernetPort";
        const char *sys = NULL;
        const char *syscc = NULL;

        *s = get_host_system_properties(&sys, &syscc, ref, _BROKER, context);
        if (s->rc != CMPI_RC_OK)
                goto out;

        path = CMNewObjectPath(_BROKER, "root/cimv2", cn, s);
        if ((path == NULL) || (s->rc != CMPI_RC_OK))
                goto out;

        CMAddKey(path, "CreationClassName", cn, CMPI_chars);
        CMAddKey(path, "SystemName", sys, CMPI_chars);
        CMAddKey(path, "SystemCreationClassName", syscc, CMPI_chars);
        CMAddKey(path, "DeviceID", bridge, CMPI_chars);

        inst = CBGetInstance(_BROKER, context, path, NULL, s);
 out:
        return inst;
}

static CMPIStatus netpool_to_port(const CMPIObjectPath *ref,
                                  struct std_assoc_info *info,
                                  struct inst_list *list)
{
        virConnectPtr conn = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        const char *poolid;
        char *bridge = NULL;
        CMPIInstance *inst;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                return s;

        if (cu_get_str_path(ref, "InstanceID", &poolid) != CMPI_RC_OK) {
                CU_DEBUG("Failed to get InstanceID from NetworkPool");
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing InstanceID in NetworkPool");
                goto out;
        }

        bridge = bridge_from_netpool(conn, poolid);
        if (bridge == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "NetworkPool not found");
                goto out;
        }

        inst = get_bridge_instance(info->context, ref, bridge, &s);
        if (inst != NULL)
                inst_list_add(list, inst);

 out:
        free(bridge);
        virConnectClose(conn);

        return s;
}

static CMPIStatus port_to_netpool(const CMPIObjectPath *ref,
                                  struct std_assoc_info *info,
                                  struct inst_list *list)
{
        CMPIStatus s;
        const char *device;
        virConnectPtr conn = NULL;
        CMPIInstance *inst = NULL;
        char *id = NULL;

        if (cu_get_str_path(ref, "DeviceID", &device) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing DeviceID from EthernetPort");
                return s;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                return s;

        if (asprintf(&id, "NetworkPool/%s", device) == -1) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to format NetworkPool ID");
                goto out;
        }

        s = get_pool_by_name(_BROKER, ref, id, &inst);
        if ((inst != NULL) && (s.rc == CMPI_RC_OK))
                inst_list_add(list, inst);
 out:
        free(id);
        virConnectClose(conn);

        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char *netpool[] = {
        "Xen_NetworkPool",
        "KVM_NetworkPool",
        "LXC_NetworkPool",
        NULL
};

static char *ethernetport[] = {
        "Linux_EthernetPort",
        NULL,
};

static char *assoc_classname[] = {
        "Xen_ConcreteComponent",
        "KVM_ConcreteComponent",
        "LXC_ConcreteComponent",
        NULL
};

static struct std_assoc _netpool_to_port = {
        .source_class = (char **)&netpool,
        .source_prop = "GroupComponent",

        .target_class = (char **)&ethernetport,
        .target_prop = "PartComponent",

        .assoc_class = (char **)&assoc_classname,

        .handler = netpool_to_port,
        .make_ref = make_ref
};

static struct std_assoc _port_to_netpool = {
        .source_class = (char **)&ethernetport,
        .source_prop = "PartComponent",

        .target_class = (char **)&netpool,
        .target_prop = "GroupComponent",

        .assoc_class = (char **)&assoc_classname,

        .handler = port_to_netpool,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &_netpool_to_port,
        &_port_to_netpool,
        NULL
};

STDA_AssocMIStub(,
                 Virt_ConcreteComponent,
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
