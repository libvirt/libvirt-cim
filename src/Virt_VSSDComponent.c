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

#include "libcmpiutil.h"
#include "std_association.h"
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
        CMPIStatus s;
        char *id = NULL;
        char *pfx = NULL;
        char *name = NULL;
        int ret;
        int i = 0;
        int types[] = {
                CIM_RASD_TYPE_PROC,
                CIM_RASD_TYPE_NET,
                CIM_RASD_TYPE_DISK,
                CIM_RASD_TYPE_MEM,
                -1
        };

        ASSOC_MATCH(info->provider_name, CLASSNAME(ref));

        id = cu_get_str_path(ref, "InstanceID");
        if (id == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing InstanceID");
                goto out;
        }

        ret = sscanf(id, "%a[^:]:%as", &pfx, &name);
        if (ret != 2) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid InstanceID");
                goto out;
        }

        for (i = 0; types[i] > 0; i++) {
                rasds_for_domain(_BROKER,
                                 name,
                                 types[i],
                                 NAMESPACE(ref),
                                 list);
        }

        CMSetStatus(&s, CMPI_RC_OK);

 out:
        free(id);
        free(pfx);
        free(name);

        return s;
}

static CMPIStatus vssd_for_name(const char *host,
                                const CMPIObjectPath *ref,
                                CMPIInstance **inst)
{
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        CMPIStatus s;

        conn = lv_connect(_BROKER, &s);
        if (conn == NULL)
                goto out;

        dom = virDomainLookupByName(conn, host);
        if (dom == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "No such system `%s'", host);
                goto out;
        }

        *inst = get_vssd_instance(dom, _BROKER, ref);
        if (*inst == NULL)
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Error getting VSSD for `%s'", host);
        else
                CMSetStatus(&s, CMPI_RC_OK);

 out:
        virDomainFree(dom);
        virConnectClose(conn);

        return s;
}

static CMPIStatus rasd_to_vssd(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s;
        CMPIInstance *vssd = NULL;
        char *id = NULL;
        char *host = NULL;
        char *devid = NULL;
        int ret;

        ASSOC_MATCH(info->provider_name, CLASSNAME(ref));

        id = cu_get_str_path(ref, "InstanceID");
        if (id == NULL) {
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

        s = vssd_for_name(host, ref, &vssd);
        if (vssd)
                inst_list_add(list, vssd);

 out:
        free(id);
        free(host);
        free(devid);

        return s;
}

static CMPIInstance *make_ref(const CMPIObjectPath *ref,
                              const CMPIInstance *inst,
                              struct std_assoc_info *info,
                              struct std_assoc *assoc)
{
        CMPIInstance *refinst = NULL;

        refinst = get_typed_instance(_BROKER,
                                     "VirtualSystemSettingDataComponent",
                                     NAMESPACE(ref));

        if (refinst != NULL) {
                CMPIObjectPath *instop;

                instop = CMGetObjectPath(inst, NULL);

                set_reference(assoc, refinst, ref, instop);
        }

        return refinst;
}

static struct std_assoc forward = {
        .source_class = "CIM_VirtualSystemSettingData",
        .source_prop = "GroupComponent",

        .target_class = "CIM_ResourceAllocationSettingData",
        .target_prop = "PartComponent",

        .handler = vssd_to_rasd,
        .make_ref = make_ref
};

static struct std_assoc backward = {
        .source_class = "CIM_ResourceAllocationSettingData",
        .source_prop = "PartComponent",

        .target_class = "CIM_VirtualSystemSettingData",
        .target_prop = "GroupComponent",

        .handler = rasd_to_vssd,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &forward,
        &backward,
        NULL
};

STDA_AssocMIStub(, Xen_VSSDComponentProvider, _BROKER, CMNoHook, handlers);
STDA_AssocMIStub(, KVM_VSSDComponentProvider, _BROKER, CMNoHook, handlers);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
