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
        CMPIStatus s;
        CMPIInstance *instance;

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
        CMPIStatus s;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                return s;

        ret = enum_domains(_BROKER, conn, NAMESPACE(ref), list);
        if (ret) {
                CMSetStatus(&s, CMPI_RC_OK);
        } else {
                CMSetStatusWithChars(_BROKER, &s,
                                     CMPI_RC_ERR_FAILED,
                                     "Failed to get domain list");
        }

        CMSetStatus(&s, CMPI_RC_OK);

        return s;
}

static CMPIInstance *make_ref(const CMPIObjectPath *ref,
                              const CMPIInstance *inst,
                              struct std_assoc_info *info,
                              struct std_assoc *assoc)
{
        CMPIInstance *refinst = NULL;

        refinst = get_typed_instance(_BROKER,
                                     "HostedDependency",
                                     NAMESPACE(ref));

        if (refinst != NULL) {
                CMPIObjectPath *instop;

                instop = CMGetObjectPath(inst, NULL);

                set_reference(assoc, refinst, ref, instop);
        }

        return refinst;
}

static struct std_assoc xen_vs_to_host = {
        .source_class = "Xen_ComputerSystem",
        .source_prop = "Antecedent",

        .target_class = "Xen_HostSystem",
        .source_prop = "Dependent",

        .handler = vs_to_host,
        .make_ref = make_ref
};

static struct std_assoc kvm_vs_to_host = {
        .source_class = "KVM_ComputerSystem",
        .source_prop = "Antecedent",

        .target_class = "KVM_HostSystem",
        .source_prop = "Dependent",

        .handler = vs_to_host,
        .make_ref = make_ref
};

static struct std_assoc xen_host_to_vs = {
        .source_class = "Xen_HostSystem",
        .source_prop = "Dependent",

        .target_class = "Xen_ComputerSystem",
        .target_prop = "Antecedent",

        .handler = host_to_vs,
        .make_ref = make_ref
};

static struct std_assoc kvm_host_to_vs = {
        .source_class = "KVM_HostSystem",
        .source_prop = "Dependent",

        .target_class = "KVM_ComputerSystem",
        .target_prop = "Antecedent",

        .handler = host_to_vs,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &xen_vs_to_host,
        &xen_host_to_vs,
        &kvm_vs_to_host,
        &kvm_host_to_vs,
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
