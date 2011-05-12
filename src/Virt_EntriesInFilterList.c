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

#include <strings.h>

#include "acl_parsing.h"
#include "misc_util.h"
#include "Virt_FilterList.h"
#include "Virt_FilterEntry.h"

static const CMPIBroker *_BROKER;

/**
 *  given a filter, find all *direct* children
 */
static CMPIStatus list_to_rule(
        const CMPIObjectPath *reference,
        struct std_assoc_info *info,
        struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;
        struct acl_filter *filter = NULL;
        const char *name = NULL;
        virConnectPtr conn = NULL;
        int i;

        CU_DEBUG("Reference = %s", REF2STR(reference));

        if (cu_get_str_path(reference, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_NOT_FOUND,
                        "Unable to get Name from reference");
                goto out;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        get_filter_by_name(conn, name, &filter);
        if (filter == NULL) {
                CU_DEBUG("Filter '%s' does not exist", name);
                goto out;
        }

        for (i = 0; i < filter->rule_ct; i++) {
                CU_DEBUG("Processing %s", filter->rules[i]->name);

                s = instance_from_rule(_BROKER,
                                        info->context,
                                        reference,
                                        filter->rules[i],
                                        &instance);

                if (instance != NULL) {
                        inst_list_add(list, instance);
                        instance = NULL;
                }
        }

        cleanup_filter(filter);

 out:
        virConnectClose(conn);

        return s;
}

/**
 * given a rule, fine the parent filter
 */
static CMPIStatus rule_to_list(
        const CMPIObjectPath *reference,
        struct std_assoc_info *info,
        struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct acl_filter *filters = NULL;
        CMPIInstance *instance = NULL;
        const char *name = NULL;
        virConnectPtr conn = NULL;
        int count = 0;
        int i, j = 0;

        CU_DEBUG("Reference = %s", REF2STR(reference));

        if (cu_get_str_path(reference, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_NOT_FOUND,
                        "Unable to get Name from reference");
                goto out;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        count = get_filters(conn, &filters);
        if (filters == NULL)
                goto out;

        /* return the filter that contains the rule */
        for (i = 0; i < count; i++) {
                for (j = 0; j < filters[i].rule_ct; j++) {
                        if (STREQC(name, filters[i].rules[j]->name)) {
                                CU_DEBUG("Processing %s,",filters[i].name);

                                s = instance_from_filter(_BROKER,
                                                        info->context,
                                                        reference,
                                                        &filters[i],
                                                        &instance);

                                if (instance != NULL) {
                                        inst_list_add(list, instance);
                                        instance = NULL;
                                }

                        }
                }
        }

 out:
        cleanup_filters(&filters, count);
        virConnectClose(conn);

        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char *group_component[] = {
        "KVM_FilterList",
        NULL
};

static char *part_component[] = {
        "KVM_Hdr8021Filter",
        "KVM_IPHeadersFilter",
        NULL
};

static char *assoc_class_name[] = {
        "KVM_EntriesInFilterList",
        NULL
};

static struct std_assoc _list_to_rule = {
        .source_class = (char **)&group_component,
        .source_prop = "GroupComponent",

        .target_class = (char **)&part_component,
        .target_prop = "PartComponent",

        .assoc_class = (char **)&assoc_class_name,

        .handler = list_to_rule,
        .make_ref = make_ref
};

static struct std_assoc _rule_to_list = {
        .source_class = (char **)&part_component,
        .source_prop = "PartComponent",

        .target_class = (char **)&group_component,
        .target_prop = "GroupComponent",

        .assoc_class = (char **)&assoc_class_name,

        .handler = rule_to_list,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &_list_to_rule,
        &_rule_to_list,
        NULL
};

STDA_AssocMIStub(,
        Virt_EntriesInFilterList,
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
