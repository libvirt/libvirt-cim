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

#include <stdlib.h>

#include "acl_parsing.h"
#include "misc_util.h"
#include "xmlgen.h"

#include "Virt_FilterEntry.h"
#include "Virt_HostSystem.h"

const static CMPIBroker *_BROKER;
struct rule_data_t {
        const char *srcmacaddr;
        const char *srcmacmask;
        const char *dstmacaddr;
        const char *dstmacmask;

        const char *srcipaddr;
        const char *srcipmask;
        const char *dstipaddr;
        const char *dstipmask;

        const char *srcipfrom;
        const char *srcipto;
        const char *dstipfrom;
        const char *dstipto;

        const char *srcportstart;
        const char *srcportend;
        const char *dstportstart;
        const char *dstportend;
};

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

static char *cidr_to_str(const char *cidr)
{
        char *ret = NULL;
        int val;
        unsigned int o1, o2, o3, o4;

        if (cidr == NULL || strlen(cidr) == 0)
                return NULL;

        CU_DEBUG("Enter %s(%s)", __FUNCTION__, cidr);

        /* String value to integer */
        val = atoi(cidr);
        if (val < 0 || val > 32)
                return NULL;

        if (val == 0)
                return strdup("0.0.0.0");
        else if (val == 32)
                return strdup("255.255.255.255");

        /* CIDR to bits */
        val = (0xffffffff >> (32 - val)) << (32 - val);

        /* bits to octets */
        o1 = (val & 0xff000000) >> 24;
        o2 = (val & 0x00ff0000) >> 16;
        o3 = (val & 0x0000ff00) >> 8;
        o4 = val & 0x000000ff;

        /* octets to address string */
        ret = calloc(1, sizeof(*ret) * 16);
        snprintf(ret, 16, "%u.%u.%u.%u", o1, o2, o3, o4);

        CU_DEBUG("%s: returning '%s'", __FUNCTION__, ret);
        return ret;
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

int convert_priority(const char *s)
{
        if (s == NULL)
                return 0;

        return atoi(s);
}

static int convert_action(const char *s)
{
        enum {NONE=0, ACCEPT, DENY, REJECT, RETURN, CONTINUE} action = NONE;

        if (s != NULL) {
                if (STREQC(s, "accept"))
                        action = ACCEPT;
                else if (STREQC(s, "drop"))
                        action = DENY;
                else if (STREQC(s, "reject"))
                        action = REJECT;
                else if (STREQC(s, "return"))
                        action = RETURN;
                else if (STREQC(s, "continue"))
                        action = CONTINUE;
        }
        return action;
}

static void convert_mac_rule_to_instance(
        struct acl_rule *rule,
        CMPIInstance *inst,
        const CMPIBroker *broker)
{
        unsigned int bytes[48];
        unsigned int size = 0;
        CMPIArray *array = NULL;

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
                CMSetProperty(inst, "HdrDestMACAddr8021",
                        (CMPIValue *)&array, CMPI_uint8A);

        memset(bytes, 0, sizeof(bytes));
        size = octets_from_mac(rule->var.mac.dstmacmask,
                bytes, sizeof(bytes));

        array = octets_to_cmpi(broker, bytes, size);
        if (array != NULL)
                CMSetProperty(inst, "HdrDestMACMask8021",
                        (CMPIValue *)&array, CMPI_uint8A);

        if (rule->var.mac.protocol_id != NULL) {
                unsigned long n = strtoul(rule->var.mac.protocol_id,
                                          NULL, 16);
                CMSetProperty(inst, "HdrProtocolID8021",
                              (CMPIValue *)&n, CMPI_uint16);
        }

}

static void fill_rule_data(struct acl_rule *rule,
                           struct rule_data_t *data)
{
        if (rule == NULL || data == NULL)
                return;

        memset(data, 0, sizeof(*data));

        switch (rule->type) {
        case IP_RULE:
                data->srcmacaddr = rule->var.ip.srcmacaddr;
                data->srcmacmask = rule->var.ip.srcmacmask;
                data->dstmacaddr = rule->var.ip.srcmacaddr;
                data->dstmacmask = rule->var.ip.dstmacmask;

                data->srcipaddr = rule->var.ip.srcipaddr;
                data->srcipmask = rule->var.ip.srcipmask;
                data->dstipaddr = rule->var.ip.dstipaddr;
                data->dstipmask = rule->var.ip.dstipmask;

                data->srcportstart = rule->var.ip.srcportstart;
                data->srcportend   = rule->var.ip.srcportend;
                data->dstportstart = rule->var.ip.dstportstart;
                data->dstportend   = rule->var.ip.dstportend;
                break;

        case TCP_RULE:
                data->srcmacaddr = rule->var.tcp.srcmacaddr;

                data->srcipaddr = rule->var.tcp.srcipaddr;
                data->srcipmask = rule->var.tcp.srcipmask;
                data->dstipaddr = rule->var.tcp.dstipaddr;
                data->dstipmask = rule->var.tcp.dstipmask;

                data->srcipfrom = rule->var.tcp.srcipfrom;
                data->srcipto   = rule->var.tcp.srcipto;
                data->dstipfrom = rule->var.tcp.dstipfrom;
                data->dstipto   = rule->var.tcp.dstipto;

                data->srcportstart = rule->var.tcp.srcportstart;
                data->srcportend   = rule->var.tcp.srcportend;
                data->dstportstart = rule->var.tcp.dstportstart;
                data->dstportend   = rule->var.tcp.dstportend;
                break;

        case ICMP_IGMP_RULE:
                data->srcmacaddr = rule->var.icmp_igmp.srcmacaddr;
                data->srcmacmask = rule->var.icmp_igmp.srcmacmask;
                data->dstmacaddr = rule->var.icmp_igmp.srcmacaddr;
                data->dstmacmask = rule->var.icmp_igmp.dstmacmask;

                data->srcipaddr = rule->var.icmp_igmp.srcipaddr;
                data->srcipmask = rule->var.icmp_igmp.srcipmask;
                data->dstipaddr = rule->var.icmp_igmp.dstipaddr;
                data->dstipmask = rule->var.icmp_igmp.dstipmask;

                data->srcipfrom = rule->var.icmp_igmp.srcipfrom;
                data->srcipto   = rule->var.icmp_igmp.srcipto;
                data->dstipfrom = rule->var.icmp_igmp.dstipfrom;
                data->dstipto   = rule->var.icmp_igmp.dstipto;
                break;

        default:
                CU_DEBUG("%s(): unhandled rule type '%d'",
                         __FUNCTION__, rule->type);
                break;
        }
}

static void convert_ip_rule_to_instance(
        struct acl_rule *rule,
        CMPIInstance *inst,
        const CMPIBroker *broker)
{
        unsigned int bytes[48];
        unsigned int size = 0;
        unsigned int n = 0;
        CMPIArray *array = NULL;
        struct rule_data_t rule_data;

        if (strstr(rule->protocol_id, "v6"))
                n = 6;
        else
                n = 4;

        CMSetProperty(inst, "HdrIPVersion",(CMPIValue *)&n, CMPI_uint8);

        fill_rule_data(rule, &rule_data);

        if (rule_data.srcipfrom && rule_data.srcipto) {
                memset(bytes, 0, sizeof(bytes));
                size = octets_from_ip(rule_data.srcipfrom,
                        bytes, sizeof(bytes));

                array = octets_to_cmpi(broker, bytes, size);
                if (array != NULL)
                        CMSetProperty(inst, "HdrSrcAddress",
                                (CMPIValue *)&array, CMPI_uint8A);

                memset(bytes, 0, sizeof(bytes));
                size = octets_from_ip(rule_data.srcipto,
                        bytes, sizeof(bytes));

                array = octets_to_cmpi(broker, bytes, size);
                if (array != NULL)
                        CMSetProperty(inst, "HdrSrcAddressEndOfRange",
                                (CMPIValue *)&array, CMPI_uint8A);
        } else {
                memset(bytes, 0, sizeof(bytes));
                size = octets_from_ip(rule_data.srcipaddr,
                        bytes, sizeof(bytes));

                array = octets_to_cmpi(broker, bytes, size);
                if (array != NULL)
                        CMSetProperty(inst, "HdrSrcAddress",
                                (CMPIValue *)&array, CMPI_uint8A);

                /* CIDR notation? */
                if (rule_data.srcipmask) {
                        char *netmask = strdup(rule_data.srcipmask);
                        if (strstr(netmask, ".") == NULL) {
                                char *tmp = cidr_to_str(netmask);
                                free(netmask);
                                netmask = tmp;
                        }

                        memset(bytes, 0, sizeof(bytes));
                        size = octets_from_ip(netmask, bytes, sizeof(bytes));

                        array = octets_to_cmpi(broker, bytes, size);
                        if (array != NULL)
                                CMSetProperty(inst, "HdrSrcMask",
                                        (CMPIValue *)&array, CMPI_uint8A);

                        free(netmask);
                }
        }

        if (rule_data.dstipfrom && rule_data.dstipto) {
                memset(bytes, 0, sizeof(bytes));
                size = octets_from_ip(rule_data.dstipfrom,
                        bytes, sizeof(bytes));

                array = octets_to_cmpi(broker, bytes, size);
                if (array != NULL)
                        CMSetProperty(inst, "HdrDestAddress",
                                (CMPIValue *)&array, CMPI_uint8A);

                memset(bytes, 0, sizeof(bytes));
                size = octets_from_ip(rule_data.dstipto,
                        bytes, sizeof(bytes));

                array = octets_to_cmpi(broker, bytes, size);
                if (array != NULL)
                        CMSetProperty(inst, "HdrDestAddressEndOfRange",
                                (CMPIValue *)&array, CMPI_uint8A);
        } else {
                memset(bytes, 0, sizeof(bytes));
                size = octets_from_ip(rule_data.dstipaddr,
                        bytes, sizeof(bytes));

                array = octets_to_cmpi(broker, bytes, size);
                if (array != NULL)
                        CMSetProperty(inst, "HdrDestAddress",
                                (CMPIValue *)&array, CMPI_uint8A);

                /* CIDR notation? */
                if (rule_data.dstipmask) {
                        char *netmask = strdup(rule_data.dstipmask);
                        if (strstr(netmask, ".") == NULL) {
                                char *tmp = cidr_to_str(netmask);
                                free(netmask);
                                netmask = tmp;
                        }

                        memset(bytes, 0, sizeof(bytes));
                        size = octets_from_ip(netmask, bytes, sizeof(bytes));

                        array = octets_to_cmpi(broker, bytes, size);
                        if (array != NULL)
                                CMSetProperty(inst, "HdrDestMask",
                                        (CMPIValue *)&array, CMPI_uint8A);

                        free(netmask);
                }
        }

        if (rule_data.srcportstart) {
                n = atoi(rule_data.srcportstart);
                CMSetProperty(inst, "HdrSrcPortStart",
                        (CMPIValue *)&n, CMPI_uint16);
        }

        if (rule_data.srcportend) {
                n = atoi(rule_data.srcportend);
                CMSetProperty(inst, "HdrSrcPortEnd",
                        (CMPIValue *)&n, CMPI_uint16);
        }

        if (rule_data.dstportstart) {
                n = atoi(rule_data.dstportstart);
                CMSetProperty(inst, "HdrDestPortStart",
                        (CMPIValue *)&n, CMPI_uint16);
        }

        if (rule_data.dstportend) {
                n = atoi(rule_data.dstportend);
                CMSetProperty(inst, "HdrDestPortEnd",
                        (CMPIValue *)&n, CMPI_uint16);
        }
}

static CMPIInstance *convert_rule_to_instance(
        struct acl_rule *rule,
        const CMPIBroker *broker,
        const CMPIContext *context,
        const CMPIObjectPath *reference,
        CMPIStatus *s)
{
        CMPIInstance *inst = NULL;
        const char *sys_name = NULL;
        const char *sys_ccname = NULL;
        const char *basename = NULL;
        int action, direction, priority = 0;

        void (*convert_f)(struct acl_rule*, CMPIInstance*, const CMPIBroker*);

        if (rule == NULL)
                return NULL;

        switch (rule->type) {
        case MAC_RULE:
        case ARP_RULE:
                basename = "Hdr8021Filter";
                convert_f = convert_mac_rule_to_instance;
                break;
        case IP_RULE:
        case TCP_RULE:
        case ICMP_IGMP_RULE:
                basename = "IPHeadersFilter";
                convert_f = convert_ip_rule_to_instance;
                break;
        default:
                basename = "FilterEntry";
                convert_f = NULL;
                break;
        }

        inst = get_typed_instance(broker,
                                  CLASSNAME(reference),
                                  basename,
                                  NAMESPACE(reference));

        if (inst == NULL) {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get filter entry instance");
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

        action = convert_action(rule->action);
        CMSetProperty(inst, "Action", (CMPIValue *)&action, CMPI_uint16);

        direction = convert_direction(rule->direction);
        CMSetProperty(inst, "Direction", (CMPIValue *)&direction, CMPI_uint16);

        priority = convert_priority(rule->priority);
        CMSetProperty(inst, "Priority", (CMPIValue *)&priority, CMPI_sint16);

        if (convert_f)
                convert_f(rule, inst, broker);

 out:
        return inst;
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

        CU_DEBUG("Reference = %s", REF2STR(reference));

        if (!STREQC(CLASSNAME(reference), "KVM_Hdr8021Filter") &&
            !STREQC(CLASSNAME(reference), "KVM_IPHeadersFilter") &&
            !STREQC(CLASSNAME(reference), "KVM_FilterEntry")) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unrecognized class type");
                goto out;
        }


        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        count = get_filters(conn, &filters);

        for (i = 0; i < count; i++) {
                for (j = 0; j < filters[i].rule_ct; j++) {
                        CMPIInstance *instance = NULL;

                        instance = convert_rule_to_instance(
                                        filters[i].rules[j],
                                        broker,
                                        context,
                                        reference,
                                        &s);

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
        cleanup_filters(&filter, 1);
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
