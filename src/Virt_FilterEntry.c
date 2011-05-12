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

#include <arpa/inet.h>

#include "acl_parsing.h"
#include "misc_util.h"
#include "xmlgen.h"

#include "Virt_FilterEntry.h"
#include "Virt_HostSystem.h"

const static CMPIBroker *_BROKER;

static bool is_mac_rule(int type)
{
        if (type == MAC_RULE || type == ARP_RULE)
                return 1;

        return 0;
}

static bool is_ip_rule(int type)
{
        if (type == IP_RULE || type == TCP_RULE || type == ICMP_RULE ||
                type == IGMP_RULE)
                return 1;

        return 0;
}

static int octets_from_mac(const char * s, unsigned int *buffer,
                                unsigned int size)
{
        unsigned int _buffer[6];
        unsigned int i, n = 0;

        if ((s == 0) || (s[0] == '\0') || (buffer == NULL) || (size < 6))
                return 0;

        if (s[0] == '$') {
                for (i = 0; (s[i] != '\0') && (i < size); i++)
                        buffer[i] = s[i];

                n = i;
        }
        else {
                n = sscanf(s, "%x:%x:%x:%x:%x:%x",
                        &_buffer[0], &_buffer[1], &_buffer[2],
                        &_buffer[3], &_buffer[4], &_buffer[5]);

                for (i = 0; (i < n) && (i < size); i++)
                        buffer[i] = _buffer[i];
        }

        return n;
}

static int octets_from_ip(const char * s, unsigned int *buffer,
                                unsigned int size)
{
        struct in6_addr addr;
        unsigned int family = 0;
        unsigned int i, n = 0;

        if ((s == 0) || (s[0] == '\0') || (buffer == NULL) || (size < 4))
                return 0;

        if (s[0] == '$') {
                for (i = 0; (s[i] != '\0') && (i < size); i++)
                        buffer[i] = s[i];

                n = i;
        }
        else {
                family = strstr(s, ":") ? AF_INET6 : AF_INET;
                n  = family == AF_INET6 ? 16 : 4;

                if (size < n)
                        return 0;

                if (inet_pton(family, s, &addr)) {
                        n = n <= size ? n : size;
                        for (i = 0; i < n; i++)
                                buffer[i] = addr.s6_addr[i];
                }
        }

        return n;
}

static CMPIArray *octets_to_cmpi(const CMPIBroker *broker, unsigned int *bytes, int size)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIArray *array = NULL;
        int i;

        if (bytes == 0 || size == 0)
                return array;

        array = CMNewArray(broker, size, CMPI_uint8, &s);

        for (i = 0; i < size; i++) {
                s = CMSetArrayElementAt(array, i,
                        (CMPIValue*)&bytes[i], CMPI_uint8);
        }

        return array;
}

static int convert_direction(const char *s)
{
        enum {NOT_APPLICABLE, INPUT, OUTPUT, BOTH} direction = NOT_APPLICABLE;

        if (s != NULL) {
                if (STREQC(s, "in"))
                        direction = INPUT;
                else if (STREQC(s, "out"))
                        direction = OUTPUT;
                else if (STREQC(s, "inout"))
                        direction = BOTH;
        }

        return direction;
}

static int convert_priority(const char *s)
{
        int priority = 0;

        if (s != NULL) {
                priority = atoi(s);
        }

        return priority;
}

static CMPIInstance *convert_mac_rule_to_instance(
        struct acl_rule *rule,
        const CMPIBroker *broker,
        const CMPIContext *context,
        const CMPIObjectPath *reference,
        CMPIStatus *s)
{
        CMPIInstance *inst = NULL;
        const char *sys_name = NULL;
        const char *sys_ccname = NULL;
        int direction, priority = 0;
        unsigned int bytes[48];
        unsigned int size = 0;
        CMPIArray *array = NULL;

        inst = get_typed_instance(broker,
                                  CLASSNAME(reference),
                                  "Hdr8021Filter",
                                  NAMESPACE(reference));
        if (inst == NULL) {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get 8021 filter instance");

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
        CMSetProperty(inst, "Name", (CMPIValue *)rule->name, CMPI_chars);

        direction = convert_direction(rule->direction);
        CMSetProperty(inst, "Direction", (CMPIValue *)&direction, CMPI_uint16);

        priority = convert_priority(rule->priority);
        CMSetProperty(inst, "Priority", (CMPIValue *)&priority, CMPI_uint16);

        memset(bytes, 0, sizeof(bytes));
        size = octets_from_mac(rule->var.mac.srcmacaddr,
                bytes, sizeof(bytes));

        array = octets_to_cmpi(broker, bytes, size);
        if (array != NULL)
                CMSetProperty(inst, "HdrSrcMACAddr8021",
                        (CMPIValue *)&array, CMPI_uint8A);

        memset(bytes, 0, sizeof(bytes));
        size = octets_from_mac(rule->var.mac.srcmacmask,
                bytes, sizeof(bytes));

        array = octets_to_cmpi(broker, bytes, size);
        if (array != NULL)
                CMSetProperty(inst, "HdrSrcMACMask8021",
                        (CMPIValue *)&array, CMPI_uint8A);

        memset(bytes, 0, sizeof(bytes));
        size = octets_from_mac(rule->var.mac.dstmacaddr,
                bytes, sizeof(bytes));

        array = octets_to_cmpi(broker, bytes, size);
        if (array != NULL)
                CMSetProperty(inst, "HdrDestMACAddr8021", (CMPIValue *)
                        (CMPIValue *)&array, CMPI_uint8A);

        memset(bytes, 0, sizeof(bytes));
        size = octets_from_mac(rule->var.mac.dstmacmask,
                bytes, sizeof(bytes));

        array = octets_to_cmpi(broker, bytes, size);
        if (array != NULL)
                CMSetProperty(inst, "HdrDestMACMask8021", (CMPIValue *)
                        (CMPIValue *)&array, CMPI_uint8A);

 out:
        return inst;
}

static CMPIInstance *convert_ip_rule_to_instance(
        struct acl_rule *rule,
        const CMPIBroker *broker,
        const CMPIContext *context,
        const CMPIObjectPath *reference,
        CMPIStatus *s)
{
        CMPIInstance *inst = NULL;
        const char *sys_name = NULL;
        const char *sys_ccname = NULL;
        int direction, priority = 0;
        unsigned int bytes[48];
        unsigned int size = 0;
        unsigned int n = 0;
        CMPIArray *array = NULL;

        inst = get_typed_instance(broker,
                                  CLASSNAME(reference),
                                  "IPHeadersFilter",
                                  NAMESPACE(reference));
        if (inst == NULL) {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get ip headers filter instance");
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
        CMSetProperty(inst, "Name", (CMPIValue *)rule->name, CMPI_chars);

        direction = convert_direction(rule->direction);
        CMSetProperty(inst, "Direction", (CMPIValue *)&direction, CMPI_uint16);

        priority = convert_priority(rule->priority);
        CMSetProperty(inst, "Priority", (CMPIValue *)&priority, CMPI_uint16);

        if (strstr(rule->protocol_id, "v6"))
                n = 6;
        else
                n = 4;

        CMSetProperty(inst, "HdrIPVersion",(CMPIValue *)&n, CMPI_uint8);

        if (rule->var.tcp.srcipfrom && rule->var.tcp.srcipto) {
                memset(bytes, 0, sizeof(bytes));
                size = octets_from_ip(rule->var.tcp.srcipfrom,
                        bytes, sizeof(bytes));

                array = octets_to_cmpi(broker, bytes, size);
                if (array != NULL)
                        CMSetProperty(inst, "HdrSrcAddress",
                                (CMPIValue *)&array, CMPI_uint8A);

                memset(bytes, 0, sizeof(bytes));
                size = octets_from_ip(rule->var.tcp.srcipto,
                        bytes, sizeof(bytes));

                array = octets_to_cmpi(broker, bytes, size);
                if (array != NULL)
                        CMSetProperty(inst, "HdrSrcAddressEndOfRange",
                                (CMPIValue *)&array, CMPI_uint8A);
        } else {
                memset(bytes, 0, sizeof(bytes));
                size = octets_from_ip(rule->var.tcp.srcmacaddr,
                        bytes, sizeof(bytes));

                array = octets_to_cmpi(broker, bytes, size);
                if (array != NULL)
                        CMSetProperty(inst, "HdrSrcAddress",
                                (CMPIValue *)&array, CMPI_uint8A);

                memset(bytes, 0, sizeof(bytes));
                size = octets_from_ip(rule->var.tcp.srcipmask,
                        bytes, sizeof(bytes));

                array = octets_to_cmpi(broker, bytes, size);
                if (array != NULL)
                        CMSetProperty(inst, "HdrSrcMask",
                                (CMPIValue *)&array, CMPI_uint8A);
        }

        if (rule->var.tcp.dstipfrom && rule->var.tcp.dstipto) {
                memset(bytes, 0, sizeof(bytes));
                size = octets_from_ip(rule->var.tcp.dstipfrom,
                        bytes, sizeof(bytes));

                array = octets_to_cmpi(broker, bytes, size);
                if (array != NULL)
                        CMSetProperty(inst, "HdrDestAddress",
                                (CMPIValue *)&array, CMPI_uint8A);

                memset(bytes, 0, sizeof(bytes));
                size = octets_from_ip(rule->var.tcp.dstipto,
                        bytes, sizeof(bytes));

                array = octets_to_cmpi(broker, bytes, size);
                if (array != NULL)
                        CMSetProperty(inst, "HdrDestAddressEndOfRange",
                                (CMPIValue *)&array, CMPI_uint8A);
        } else {
                memset(bytes, 0, sizeof(bytes));
                size = octets_from_ip(rule->var.tcp.dstipaddr,
                        bytes, sizeof(bytes));

                array = octets_to_cmpi(broker, bytes, size);
                if (array != NULL)
                        CMSetProperty(inst, "HdrDestAddress",
                                (CMPIValue *)&array, CMPI_uint8A);

                memset(bytes, 0, sizeof(bytes));
                size = octets_from_ip(rule->var.tcp.dstipmask,
                        bytes, sizeof(bytes));

                array = octets_to_cmpi(broker, bytes, size);
                if (array != NULL)
                        CMSetProperty(inst, "HdrDestMask",
                                (CMPIValue *)&array, CMPI_uint8A);
        }

        if ((rule->type == IP_RULE) || (rule->type == TCP_RULE)) {
                if (rule->var.tcp.srcportstart) {
                        n = atoi(rule->var.tcp.srcportstart);
                        CMSetProperty(inst, "HdrSrcPortStart",
                                (CMPIValue *)&n, CMPI_uint16);
                }

                if (rule->var.tcp.srcportend) {
                        n = atoi(rule->var.tcp.srcportend);
                        CMSetProperty(inst, "HdrSrcPortEnd",
                                (CMPIValue *)&n, CMPI_uint16);
                }

                if (rule->var.tcp.dstportstart) {
                        n = atoi(rule->var.tcp.dstportstart);
                        CMSetProperty(inst, "HdrDestPortStart",
                                (CMPIValue *)&n, CMPI_uint16);
                }

                if (rule->var.tcp.dstportend) {
                        n = atoi(rule->var.tcp.dstportend);
                        CMSetProperty(inst, "HdrDestPortEnd",
                                (CMPIValue *)&n, CMPI_uint16);
                }
        }

 out:
        return inst;
}

static CMPIInstance *convert_rule_to_instance(
        struct acl_rule *rule,
        const CMPIBroker *broker,
        const CMPIContext *context,
        const CMPIObjectPath *reference,
        CMPIStatus *s)
{
        CMPIInstance *instance = NULL;

        if (rule == NULL)
                return NULL;

        if(is_mac_rule(rule->type)) {
                instance = convert_mac_rule_to_instance(rule,
                                                broker,
                                                context,
                                                reference,
                                                s);
        }
        else if(is_ip_rule(rule->type)) {
                instance = convert_ip_rule_to_instance(rule,
                                                broker,
                                                context,
                                                reference,
                                                s);
        }

        return instance;
}

CMPIStatus enum_filter_rules(
        const CMPIBroker *broker,
        const CMPIContext *context,
        const CMPIObjectPath *reference,
        struct inst_list *list)
{
        virConnectPtr conn = NULL;
        struct acl_filter *filters = NULL;
        int i, j, count = 0;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        enum {NONE, MAC, IP} class_type = NONE;

        CU_DEBUG("Reference = %s", REF2STR(reference));

        if (STREQC(CLASSNAME(reference), "KVM_Hdr8021Filter"))
                class_type = MAC;
        else if (STREQC(CLASSNAME(reference), "KVM_IPHeadersFilter"))
                class_type = IP;

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        count = get_filters(conn, &filters);

        for (i = 0; i < count; i++) {
                for (j = 0; j < filters[i].rule_ct; j++) {
                        CMPIInstance *instance = NULL;

                        if (((class_type == NONE) ||
                                (class_type == MAC)) &&
                                is_mac_rule(filters[i].rules[j]->type)) {
                                instance = convert_mac_rule_to_instance(
                                                filters[i].rules[j],
                                                broker,
                                                context,
                                                reference,
                                                &s);
                        }
                        else if (((class_type == NONE) ||
                                (class_type == IP)) &&
                                is_ip_rule(filters[i].rules[j]->type)) {
                                instance = convert_ip_rule_to_instance(
                                                filters[i].rules[j],
                                                broker,
                                                context,
                                                reference,
                                                &s);
                        }
                        else
                                CU_DEBUG("Unrecognized rule type %u",
                                        filters[i].rules[j]->type);

                        if (instance != NULL)
                                inst_list_add(list, instance);
                }

        }

 out:
        cleanup_filters(&filters, count);
        virConnectClose(conn);

        return s;
}

CMPIStatus get_rule_by_ref(
        const CMPIBroker *broker,
        const CMPIContext *context,
        const CMPIObjectPath *reference,
        CMPIInstance **instance)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct acl_filter *filter = NULL;
        struct acl_rule *rule = NULL;
        const char *name = NULL;
        char *filter_name = NULL;
        int rule_index;
        virConnectPtr conn = NULL;
        int i;

        CU_DEBUG("Reference = %s", REF2STR(reference));

        if (cu_get_str_path(reference, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_NOT_FOUND,
                        "Unable to get Name from reference");
                goto out;
        }

        if (parse_rule_id(name,  &filter_name, &rule_index) == 0) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_NOT_FOUND,
                        "Could not parse filter name");
                goto out;
        }

        CU_DEBUG("Filter name = %s, rule index = %u", filter_name, rule_index);

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        get_filter_by_name(conn, filter_name, &filter);
        if (filter == NULL) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_NOT_FOUND,
                        "Could not retrieve filter");
                goto out;
        }

        for (i = 0; i < filter->rule_ct; i++) {
                if (rule_index == i) {
                        rule = filter->rules[i];
                        break;
                }
        }

        if (rule == NULL) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_NOT_FOUND,
                        "Could not retrieve rule");
                goto out;
        }

        *instance = convert_rule_to_instance(rule,
                                        broker,
                                        context,
                                        reference,
                                        &s);
 out:
        free(filter_name);
        cleanup_filter(filter);
        virConnectClose(conn);

        return s;
}

CMPIStatus instance_from_rule(
        const CMPIBroker *broker,
        const CMPIContext *context,
        const CMPIObjectPath *reference,
        struct acl_rule *rule,
        CMPIInstance **instance)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};

        *instance = convert_rule_to_instance(rule,
                                        broker,
                                        context,
                                        reference,
                                        &s);

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

        s = get_rule_by_ref(_BROKER, context, reference, &instance);

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

        s = enum_filter_rules(_BROKER, context, reference, &list);

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

        s = enum_filter_rules(_BROKER, context, reference, &list);

        cu_return_instances(results, &list);

        return s;
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(,
        Virt_FilterEntry,
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
