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
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_instance.h>

#include "acl_parsing.h"
#include "misc_util.h"
#include "xmlgen.h"

#include "Virt_FilterList.h"
#include "Virt_HostSystem.h"
#include "Virt_FilterEntry.h"

const static CMPIBroker *_BROKER;

static CMPIInstance *convert_filter_to_instance(
        struct acl_filter *filter,
        const CMPIBroker *broker,
        const CMPIContext *context,
        const CMPIObjectPath *reference,
        CMPIStatus *s)
{
        CMPIInstance *inst = NULL;
        const char *sys_name = NULL;
        const char *sys_ccname = NULL;
        int direction = 0, priority;

        inst = get_typed_instance(broker,
                                  CLASSNAME(reference),
                                  "FilterList",
                                  NAMESPACE(reference),
                                  true);
        if (inst == NULL) {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get filter list instance");
                goto out;
        }

        *s = get_host_system_properties(&sys_name,
                                       &sys_ccname,
                                       reference,
                                       broker,
                                       context);

        if (s->rc != CMPI_RC_OK) {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get host attributes");
                goto out;
        }

        CMSetProperty(inst, "SystemName", sys_name, CMPI_chars);
        CMSetProperty(inst, "SystemCreationClassName", sys_ccname, CMPI_chars);
        CMSetProperty(inst, "Name", (CMPIValue *)filter->name, CMPI_chars);
        CMSetProperty(inst, "InstanceID", (CMPIValue *)filter->uuid,
                        CMPI_chars);
        CMSetProperty(inst, "Direction", (CMPIValue *)&direction, CMPI_uint16);

        priority = convert_priority(filter->priority);
        CMSetProperty(inst, "Priority", (CMPIValue *)&priority, CMPI_sint16);
 out:
        return inst;
}

static struct acl_filter *convert_instance_to_filter(
        const CMPIInstance *instance,
        const CMPIContext *context,
        CMPIStatus *s)
{
        struct acl_filter *filter = NULL;
        const char *name = NULL;

        if (cu_get_str_prop(instance, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get Name property");
                goto out;
        }

        filter = malloc(sizeof(*filter));
        if (filter == NULL)
                goto out;

        memset(filter, 0, sizeof(*filter));
        filter->name = strdup(name);

 out:
        return filter;
}

CMPIStatus enum_filter_lists(const CMPIBroker *broker,
                        const CMPIContext *context,
                        const CMPIObjectPath *reference,
                        struct inst_list *list)
{
        virConnectPtr conn = NULL;
        struct acl_filter *filters = NULL;
        int i, count = 0;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;

        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        count = get_filters(conn, &filters);

        CU_DEBUG("found %d filters", count);

        for (i = 0; i < count; i++) {
                instance = convert_filter_to_instance(&filters[i],
                                                broker,
                                                context,
                                                reference,
                                                &s);

                if (instance != NULL)
                        inst_list_add(list, instance);
        }

 out:
        cleanup_filters(&filters, count);
        virConnectClose(conn);

        return s;
}

CMPIStatus get_filter_by_ref(const CMPIBroker *broker,
                        const CMPIContext *context,
                        const CMPIObjectPath *reference,
                        CMPIInstance **instance)
{
        virConnectPtr conn = NULL;
        struct acl_filter *filter = NULL;

        CMPIStatus s = {CMPI_RC_OK, NULL};
        const char *name = NULL;

        if (cu_get_str_path(reference, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_NOT_FOUND,
                        "Unable to get Name from reference");
                goto out;
        }

        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        get_filter_by_name(conn, name, &filter);
        if (filter == NULL) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_NOT_FOUND,
                        "No such instance (Name)");
                goto out;
        }

        s = instance_from_filter(broker, context, reference, filter, instance);

 out:
        cleanup_filters(&filter, 1);
        virConnectClose(conn);

        return s;
}

CMPIStatus instance_from_filter(const CMPIBroker *broker,
                        const CMPIContext *context,
                        const CMPIObjectPath *reference,
                        struct acl_filter *filter,
                        CMPIInstance **instance)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};

        *instance = convert_filter_to_instance(filter, broker, context,
                                                reference, &s);

        return s;
}

static CMPIStatus GetInstance(
        CMPIInstanceMI *self,
        const CMPIContext *context,
        const CMPIResult *results,
        const CMPIObjectPath *reference,
        const char **properties)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;

        s = get_filter_by_ref(_BROKER, context, reference, &instance);

        if (instance != NULL)
                CMReturnInstance(results, instance);

        return s;
}

static CMPIStatus EnumInstanceNames(
        CMPIInstanceMI *self,
        const CMPIContext *context,
        const CMPIResult *results,
        const CMPIObjectPath *reference)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct inst_list list;

        inst_list_init(&list);

        s = enum_filter_lists(_BROKER, context, reference, &list);

        cu_return_instance_names(results, &list);

        inst_list_free(&list);

        return s;
}

static CMPIStatus EnumInstances(
        CMPIInstanceMI *self,
        const CMPIContext *context,
        const CMPIResult *results,
        const CMPIObjectPath *reference,
        const char **properties)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct inst_list list;

        inst_list_init(&list);

        s = enum_filter_lists(_BROKER, context, reference, &list);

        cu_return_instances(results, &list);

        inst_list_free(&list);

        return s;
}

static CMPIStatus CreateInstance(
        CMPIInstanceMI *self,
        const CMPIContext *context,
        const CMPIResult *results,
        const CMPIObjectPath *reference,
        const CMPIInstance *instance)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        const char *name = NULL;
        struct acl_filter *filter = NULL;
        CMPIInstance *_instance = NULL;
        virConnectPtr conn = NULL;

        CU_DEBUG("Reference = %s", REF2STR(reference));

        /**Get Name from instance rather than reference since keys
         * are set by this provider, not the client.
         */
        if (cu_get_str_prop(instance, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get Name property");
                goto out;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        get_filter_by_name(conn, name, &filter);
        if (filter != NULL) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_ALREADY_EXISTS,
                        "Instance already exists");
                goto out;
        }

        filter = convert_instance_to_filter(instance, context, &s);
        if (filter == NULL) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Failed to convert instance to filter");
                goto out;
        }

        if (create_filter(conn, filter) == 0) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Failed to create filter");
                goto out;
        }

        _instance = convert_filter_to_instance(filter,
                                        _BROKER,
                                        context,
                                        reference,
                                        &s);

        if(_instance != NULL)
                cu_return_instance_name(results, _instance);

        CU_DEBUG("CreateInstance complete");

 out:
        cleanup_filters(&filter, 1);
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
        const char *name = NULL;
        struct acl_filter *filter = NULL;
        virConnectPtr conn = NULL;

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
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_NOT_FOUND,
                        "Instance does not exist");
                goto out;
        }

        if (delete_filter(conn, filter) != 0) {
                CU_DEBUG("Failed to delete filter %s", filter->name);
                goto out;
        }

 out:
        cleanup_filters(&filter, 1);
        virConnectClose(conn);

        return s;
}

DEFAULT_MI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(,
        Virt_FilterList,
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
