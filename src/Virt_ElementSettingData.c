/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Kaitlin Rupert <karupert@us.ibm.com>
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
#include <string.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include "libcmpiutil.h"
#include "std_association.h"
#include "misc_util.h"

#include "Virt_VSSD.h"

const static CMPIBroker *_BROKER;

static CMPIStatus vssd_to_sd(const CMPIObjectPath *ref,
                             struct std_assoc_info *info,
                             struct inst_list *list)
{
        CMPIStatus s;
        CMPIInstance *inst;
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        char *host = NULL;

        ASSOC_MATCH(info->provider_name, CLASSNAME(ref));

        if (!parse_instanceid(ref, NULL, &host)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get system name");
                goto out;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        dom = virDomainLookupByName(conn, host);
        if (dom == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "No such system `%s'", host);
                goto out;
        }

        inst = get_vssd_instance(dom, _BROKER, ref);
        if (inst == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Error getting VSSD for `%s'", host);
                goto out;
        }

        inst_list_add(list, inst);

 out:
        virDomainFree(dom);
        virConnectClose(conn);
        free(host);

        return s;
}

static CMPIInstance *make_ref(const CMPIObjectPath *ref,
                              const CMPIInstance *inst,
                              struct std_assoc_info *info,
                              struct std_assoc *assoc)
{
        CMPIInstance *refinst = NULL;
        char *base;
        uint16_t prop_value = 1;

        base = class_base_name(assoc->assoc_class);
        if (base == NULL)
                goto out;

        refinst = get_typed_instance(_BROKER,
                                     base,
                                     NAMESPACE(ref));

        if (refinst != NULL) {
                CMPIObjectPath *instop;

                instop = CMGetObjectPath(inst, NULL);

                set_reference(assoc, refinst, ref, instop);

                /* Set additional properties with values
                 * defined in the "Virtual System Profile."
                 */
                CMSetProperty(refinst, "IsDefault",
                        (CMPIValue *)&prop_value, CMPI_uint16);

                CMSetProperty(refinst, "IsNext",
                        (CMPIValue *)&prop_value, CMPI_uint16);

                CMSetProperty(refinst, "IsMinimum",
                        (CMPIValue *)&prop_value, CMPI_uint16);

                CMSetProperty(refinst, "IsMaximum",
                        (CMPIValue *)&prop_value, CMPI_uint16);
        }

out:
        free(base);

        return refinst;
}

static struct std_assoc vssd_to_sd_fd_bkwd = {
        .source_class = "CIM_VirtualSystemSettingData",
        .source_prop = "ManagedElement",

        .target_class = "CIM_ManagedElement",
        .target_prop = "SettingData",

        .assoc_class = "CIM_ElementSettingData",

        .handler = vssd_to_sd,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &vssd_to_sd_fd_bkwd,
        NULL
};

STDA_AssocMIStub(, Xen_ElementSettingDataProvider, _BROKER, libvirt_cim_init(), handlers);
STDA_AssocMIStub(, KVM_ElementSettingDataProvider, _BROKER, libvirt_cim_init(), handlers);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
