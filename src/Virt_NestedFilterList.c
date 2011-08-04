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

/* TODO: Port to libcmpiutil/args_util.c */
/**
 * Get a reference property of an instance
 *
 * @param inst The instance
 * @param prop The property name
 * @param reference A pointer to a CMPIObjectPath* that will be set
 *                  if successful
 * @returns
 *      - CMPI_RC_OK on success
 *      - CMPI_RC_ERR_NO_SUCH_PROPERTY if prop is not present
 *      - CMPI_RC_ERR_TYPE_MISMATCH if prop is not a reference
 *      - CMPI_RC_OK otherwise
 */
static CMPIrc cu_get_ref_prop(const CMPIInstance *instance,
                              const char *prop,
                              CMPIObjectPath **reference)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIData value;

        /* REQUIRE_PROPERY_DEFINED(instance, prop, value, &s); */
        value = CMGetProperty(instance, prop, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullValue(value))
                return CMPI_RC_ERR_NO_SUCH_PROPERTY;

        if ((value.type != CMPI_ref) ||  CMIsNullObject(value.value.ref))
                return CMPI_RC_ERR_TYPE_MISMATCH;

        *reference = value.value.ref;

        return CMPI_RC_OK;
}

/* TODO: Port to libcmpiutil/args_util.c */
/**
 * Get a reference component of an object path
 *
 * @param _reference The reference
 * @param key The key name
 * @param reference A pointer to a CMPIObjectPath* that will be set
 *                  if successful
 * @returns
 *      - CMPI_RC_OK on success
 *      - CMPI_RC_ERR_NO_SUCH_PROPERTY if prop is not present
 *      - CMPI_RC_ERR_TYPE_MISMATCH if prop is not a reference
 *      - CMPI_RC_OK otherwise
 */
static CMPIrc cu_get_ref_path(const CMPIObjectPath *reference,
                              const char *key,
                              CMPIObjectPath **_reference)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIData value;

        /* REQUIRE_PROPERY_DEFINED(instance, prop, value, &s); */
        value = CMGetKey(reference, key, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullValue(value))
                return CMPI_RC_ERR_NO_SUCH_PROPERTY;

        /* how to parse and object path? */

        return CMPI_RC_OK;
}

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

DEFAULT_GI();
DEFAULT_EIN();
DEFAULT_EI();

static CMPIStatus CreateInstance(
        CMPIInstanceMI *self,
        const CMPIContext *context,
        const CMPIResult *results,
        const CMPIObjectPath *reference,
        const CMPIInstance *instance)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIObjectPath *antecedent = NULL;
        const char *parent_name = NULL;
        struct acl_filter *parent_filter = NULL;
        CMPIObjectPath *dependent = NULL;
        const char *child_name = NULL;
        struct acl_filter *child_filter = NULL;
        virConnectPtr conn = NULL;

        CU_DEBUG("Reference = %s", REF2STR(reference));

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        if (cu_get_ref_prop(instance, "Antecedent",
                &antecedent) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Antecedent property");
                goto out;
        }

        CU_DEBUG("Antecedent = %s", REF2STR(antecedent));

        if (cu_get_str_path(antecedent, "Name", &parent_name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Antecedent.Name property");
                goto out;
        }

        CU_DEBUG("Antecedent.Name = %s", parent_name);

        get_filter_by_name(conn, parent_name, &parent_filter);
        if (parent_filter == NULL) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Antecedent.Name object does not exist");
                goto out;
        }

        if (cu_get_ref_prop(instance, "Dependent",
                &dependent) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Dependent property");
                goto out;
        }

        CU_DEBUG("Dependent = %s", REF2STR(dependent));

        if (cu_get_str_path(dependent, "Name", &child_name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Dependent.Name property");
                goto out;
        }

        CU_DEBUG("Dependent.Name = %s", child_name);

        get_filter_by_name(conn, child_name, &child_filter);
        if (child_filter == NULL) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Dependent.Name object does not exist");
                goto out;
        }

        if (append_filter_ref(parent_filter, strdup(child_name)) == 0) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Failed to append filter reference");
                goto out;
        }

        CU_DEBUG("filter appended, parent_filter->name = %s",
                parent_filter->name);

        if (update_filter(conn, parent_filter) == 0) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Failed to update filter");
                goto out;
        }

        CMReturnObjectPath(results, reference);
        CU_DEBUG("CreateInstance completed");

 out:
        cleanup_filter(parent_filter);
        cleanup_filter(child_filter);
        virConnectClose(conn);

        return s;
}

static CMPIStatus DeleteInstance(
        CMPIInstanceMI *self,
        const CMPIContext *context,
        const CMPIResult *results,
        const CMPIObjectPath *reference)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIObjectPath *antecedent = NULL;
        const char *parent_name = NULL;
        struct acl_filter *parent_filter = NULL;
        CMPIObjectPath *dependent = NULL;
        const char *child_name = NULL;
        struct acl_filter *child_filter = NULL;
        virConnectPtr conn = NULL;

        CU_DEBUG("Reference = %s", REF2STR(reference));

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        if (cu_get_ref_path(reference, "Antecedent",
                &antecedent) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Antecedent property");
                goto out;
        }

        if (cu_get_str_path(reference, "Name", &parent_name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Antecedent.Name property");
                goto out;
        }

        get_filter_by_name(conn, parent_name, &parent_filter);
        if (parent_filter == NULL) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Antecedent.Name object does not exist");
                goto out;
        }

        if (cu_get_ref_path(reference, "Dependent",
                &dependent) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Dependent property");
                goto out;
        }

        if (cu_get_str_path(reference, "Name", &child_name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Dependent.Name property");
                goto out;
        }

        get_filter_by_name(conn, child_name, &child_filter);
        if (child_filter == NULL) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Dependent.Name object does not exist");
                goto out;
        }

        if (remove_filter_ref(parent_filter, child_name) == 0) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Failed to remove filter reference");
                goto out;
        }

        if (update_filter(conn, parent_filter) == 0) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Failed to update filter");
                goto out;
        }

        CU_DEBUG("CreateInstance completed");

 out:
        cleanup_filter(parent_filter);
        cleanup_filter(child_filter);
        virConnectClose(conn);

        return s;
}

DEFAULT_MI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(,
        Virt_NestedFilterList,
        _BROKER,
        libvirt_cim_init());

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
