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

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include "libcmpiutil.h"
#include "std_association.h"
#include "misc_util.h"

#include "Virt_ComputerSystem.h"
#include "Virt_HostSystem.h"

static const CMPIBroker *_BROKER;

static CMPIStatus vs_to_host(const CMPIObjectPath *ref,
                             struct std_assoc_info *info,
                             struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        s = get_host_cs(_BROKER, ref, &instance);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, instance);

        return s;
}

static CMPIStatus host_to_vs(const CMPIObjectPath *ref,
                             struct std_assoc_info *info,
                             struct inst_list *list)
{
        int ret;
        virConnectPtr conn;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if (!match_hypervisor_prefix(ref, info))
                return s;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                return s;

        ret = enum_domains(_BROKER, conn, NAMESPACE(ref), list);
        if (ret) {
                CMSetStatus(&s, CMPI_RC_OK);
        } else {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get domain list");
        }

        CMSetStatus(&s, CMPI_RC_OK);

        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

char* antecedent[] = {
        "Xen_ComputerSystem",
        "KVM_ComputerSystem",       
        NULL
};

char* dependent[] = {
        "Xen_HostSystem",
        "KVM_HostSystem",
        NULL
};

char* assoc_classname[] = {
        "Xen_HostedDependency",
        "KVM_HostedDependency",        
        NULL
};

static struct std_assoc _vs_to_host = {
        .source_class = (char**)&antecedent,
        .source_prop = "Antecedent",

        .target_class = (char**)&dependent,
        .target_prop = "Dependent",

        .assoc_class = (char**)&assoc_classname,

        .handler = vs_to_host,
        .make_ref = make_ref
};

static struct std_assoc _host_to_vs = {
        .source_class = (char**)&dependent,
        .source_prop = "Dependent",

        .target_class = (char**)&antecedent,
        .target_prop = "Antecedent",

        .assoc_class = (char**)&assoc_classname,

        .handler = host_to_vs,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &_vs_to_host,
        &_host_to_vs,
        NULL
};

STDA_AssocMIStub(, Virt_HostedDependencyProvider, _BROKER, libvirt_cim_init(), handlers);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
