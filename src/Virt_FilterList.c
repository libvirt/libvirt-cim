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
#include <libcmpiutil/std_instance.h>

#include "acl_parsing.h"
#include "misc_util.h"
#include "xmlgen.h"

#include "Virt_FilterList.h"
#include "Virt_HostSystem.h"

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
        int direction = 0;

        inst = get_typed_instance(broker,
                                  CLASSNAME(reference),
                                  "FilterList",
                                  NAMESPACE(reference));
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

 out:
        return inst;
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
        cleanup_filter(filter);
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

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
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
