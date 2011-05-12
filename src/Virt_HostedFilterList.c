/*
 * Copyright IBM Corp. 2011
 *
 * Authors:
 *  Chip Vincent <cvincent@us.ibm.com>
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
#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_association.h>

#include "misc_util.h"
#include "Virt_HostSystem.h"
#include "Virt_FilterList.h"

static const CMPIBroker *_BROKER;

static CMPIStatus host_to_list(
        const CMPIObjectPath *reference,
        struct std_assoc_info *info,
        struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;

        /* validate host reference */
        s = get_host(_BROKER, info->context, reference, &instance, false);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = cu_validate_ref(_BROKER, reference, instance);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = enum_filter_lists(_BROKER, info->context, reference, list);

 out:
        return s;
}

static CMPIStatus list_to_host(
        const CMPIObjectPath *reference,
        struct std_assoc_info *info,
        struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;

        /* validate filter reference */
        s = get_filter_by_ref(_BROKER, info->context, reference, &instance);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = get_host(_BROKER, info->context, reference, &instance, false);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (instance != NULL)
                inst_list_add(list, instance);

 out:
        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char *antecedent[] = {
        "KVM_HostSystem",
        NULL
};

static char *dependent[] = {
        "KVM_FilterList",
        NULL
};

static char *assoc_class_name[] = {
        "KVM_HostedFilterList",
        NULL
};

static struct std_assoc _host_to_list = {
        .source_class = (char **)&antecedent,
        .source_prop = "Antecedent",

        .target_class = (char **)&dependent,
        .target_prop = "Dependent",

        .assoc_class = (char **)&assoc_class_name,

        .handler = host_to_list,
        .make_ref = make_ref
};

static struct std_assoc _list_to_host = {
        .source_class = (char **)&dependent,
        .source_prop = "Dependent",

        .target_class = (char **)&antecedent,
        .target_prop = "Antecedent",

        .assoc_class = (char **)&assoc_class_name,

        .handler = list_to_host,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &_host_to_list,
        &_list_to_host,
        NULL
};

STDA_AssocMIStub(,
        Virt_HostedFilterList,
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
