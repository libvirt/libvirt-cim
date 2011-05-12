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
#include <libcmpiutil/std_instance.h>

/* FIXME: This seems to be needed to compile STREQC, which suggests
 * libcmpiutil.h needs to add the include since the marco is defined there.
 */
#include <string.h>
#include <strings.h>

#include "acl_parsing.h"
#include "misc_util.h"
#include "Virt_FilterList.h"

static const CMPIBroker *_BROKER;

/**
 *  given a filter, find all *direct* filter_refs
 */
static CMPIStatus parent_to_child(
        const CMPIObjectPath *reference,
        struct std_assoc_info *info,
        struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct acl_filter *parent_filter = NULL;
        struct acl_filter *child_filter = NULL;
        CMPIInstance *instance = NULL;
        const char * name = NULL;
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

        get_filter_by_name(conn, name, &parent_filter);
        if (parent_filter == NULL)
                goto out;

        for (i = 0; i < parent_filter->ref_ct; i++) {
                get_filter_by_name(conn, parent_filter->refs[i],
                                        &child_filter);
                if (child_filter == NULL)
                        continue;

                CU_DEBUG("Processing %s,", child_filter->name);

                s = instance_from_filter(_BROKER,
                                        info->context,
                                        reference,
                                        child_filter,
                                        &instance);

                if (instance != NULL) {
                        CU_DEBUG("Adding instance to inst_list");
                        inst_list_add(list, instance);
                }

                cleanup_filter(child_filter);

                child_filter = NULL;
                instance = NULL;
        }

        cleanup_filter(parent_filter);

 out:
        virConnectClose(conn);

        return s;
}

/**
 *  given a filter, find all the other filters that reference it
 */
static CMPIStatus child_to_parent(
        const CMPIObjectPath *reference,
        struct std_assoc_info *info,
        struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct acl_filter *_list = NULL;
        CMPIInstance *instance = NULL;
        const char *name = NULL;
        virConnectPtr conn = NULL;
        int count, i, j;

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

        /* TODO: Ensure the referenced filter exists */

        count = get_filters(conn, &_list);
        if (_list == NULL)
                goto out;

        /* return any filter that has name in refs */
        for (i = 0; i < count; i++) {
                for (j = 0; j < _list[i].ref_ct; j++) {
                        if (STREQC(name, _list[i].refs[j])) {
                                CU_DEBUG("Processing %s,", _list[i].name);

                                s = instance_from_filter(_BROKER,
                                                        info->context,
                                                        reference,
                                                        &_list[i],
                                                        &instance);

                                if (instance != NULL)
                                        inst_list_add(list, instance);

                                instance = NULL;
                        }

                }

                cleanup_filter(&_list[i]);
        }

        free(_list);

 out:
        virConnectClose(conn);

        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char *antecedent[] = {
        "KVM_FilterList",
        NULL
};

static char *dependent[] = {
        "KVM_FilterList",
        NULL
};

static char *assoc_class_name[] = {
        "KVM_NestedFilterList",
        NULL
};

static struct std_assoc _list_to_filter_ref = {
        .source_class = (char **)&antecedent,
        .source_prop = "Antecedent",

        .target_class = (char **)&dependent,
        .target_prop = "Dependent",

        .assoc_class = (char **)&assoc_class_name,

        .handler = parent_to_child,
        .make_ref = make_ref
};

static struct std_assoc _filter_ref_to_list = {
        .source_class = (char **)&dependent,
        .source_prop = "Dependent",

        .target_class = (char **)&antecedent,
        .target_prop = "Antecedent",

        .assoc_class = (char **)&assoc_class_name,

        .handler = child_to_parent,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &_list_to_filter_ref,
        &_filter_ref_to_list,
        NULL
};

STDA_AssocMIStub(,
        Virt_NestedFilterList,
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
