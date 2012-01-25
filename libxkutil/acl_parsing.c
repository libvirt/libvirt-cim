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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <sys/stat.h>

#include <libcmpiutil/libcmpiutil.h>

#include "acl_parsing.h"
#include "device_parsing.h"
#include "xmlgen.h"
#include "../src/svpc_types.h"

void cleanup_rule(struct acl_rule *rule)
{
        if(rule == NULL)
                return;

        free(rule->name);
        free(rule->protocol_id);
        free(rule->action);
        free(rule->direction);
        free(rule->priority);

        switch(rule->type) {
        case MAC_RULE:
                free(rule->var.mac.srcmacaddr);
                free(rule->var.mac.srcmacmask);
                free(rule->var.mac.dstmacaddr);
                free(rule->var.mac.dstmacmask);
                free(rule->var.mac.protocol_id);
                free(rule->var.mac.comment);
                break;
        case ARP_RULE:
                free(rule->var.arp.srcmacaddr);
                free(rule->var.arp.srcmacmask);
                free(rule->var.arp.dstmacaddr);
                free(rule->var.arp.dstmacmask);
                free(rule->var.arp.hw_type);
                free(rule->var.arp.protocol_type);
                free(rule->var.arp.opcode);
                free(rule->var.arp.arpsrcmacaddr);
                free(rule->var.arp.arpdstmacaddr);
                free(rule->var.arp.arpsrcipaddr);
                free(rule->var.arp.arpdstipaddr);
                free(rule->var.arp.comment);
                break;
        case IP_RULE:
                free(rule->var.ip.srcmacaddr);
                free(rule->var.ip.srcmacmask);
                free(rule->var.ip.dstmacaddr);
                free(rule->var.ip.dstmacmask);
                free(rule->var.ip.srcipaddr);
                free(rule->var.ip.srcipmask);
                free(rule->var.ip.dstipaddr);
                free(rule->var.ip.dstipmask);
                free(rule->var.ip.srcportstart);
                free(rule->var.ip.srcportend);
                free(rule->var.ip.dstportstart);
                free(rule->var.ip.dstportend);
                free(rule->var.ip.comment);
                break;
        case TCP_RULE:
                free(rule->var.tcp.srcmacaddr);
                free(rule->var.tcp.srcipaddr);
                free(rule->var.tcp.srcipmask);
                free(rule->var.tcp.dstipaddr);
                free(rule->var.tcp.srcipfrom);
                free(rule->var.tcp.srcipto);
                free(rule->var.tcp.dstipfrom);
                free(rule->var.tcp.dstipto);
                free(rule->var.tcp.srcportstart);
                free(rule->var.tcp.srcportend);
                free(rule->var.tcp.dstportstart);
                free(rule->var.tcp.dstportend);
                free(rule->var.tcp.comment);
                free(rule->var.tcp.state);
                break;
        case ICMP_IGMP_RULE:
                free(rule->var.icmp_igmp.srcmacaddr);
                free(rule->var.icmp_igmp.srcmacmask);
                free(rule->var.icmp_igmp.dstmacaddr);
                free(rule->var.icmp_igmp.dstmacmask);
                free(rule->var.icmp_igmp.srcipaddr);
                free(rule->var.icmp_igmp.srcipmask);
                free(rule->var.icmp_igmp.dstipaddr);
                free(rule->var.icmp_igmp.dstipmask);
                free(rule->var.icmp_igmp.srcipfrom);
                free(rule->var.icmp_igmp.srcipto);
                free(rule->var.icmp_igmp.dstipfrom);
                free(rule->var.icmp_igmp.dstipto);
                free(rule->var.icmp_igmp.type);
                free(rule->var.icmp_igmp.code);
                free(rule->var.icmp_igmp.comment);
                free(rule->var.icmp_igmp.state);
                break;
        case UNKNOWN_RULE:
        default:
                break;
        };

        rule->type = UNKNOWN_RULE;
        free(rule);
}

void cleanup_filter(struct acl_filter *filter)
{
        int i;

        if(filter == NULL)
                return;

        free(filter->uuid);
        free(filter->name);
        free(filter->chain);

        for (i = 0; i < filter->rule_ct; i++)
                cleanup_rule(filter->rules[i]);

        free(filter->rules);
        filter->rule_ct = 0;

        for (i = 0; i < filter->ref_ct; i++)
                free(filter->refs[i]);

        free(filter->refs);
        filter->ref_ct = 0;
}

void cleanup_filters(struct acl_filter **filters, int count)
{
        int i;
        struct acl_filter *_filters;

        if((filters == NULL) || (*filters == NULL) || (count == 0))
                return;

        _filters = *filters;

        for (i = 0; i < count; i++)
                cleanup_filter(&_filters[i]);

        free(_filters);
        *filters = NULL;
}


static int parse_acl_mac_rule(xmlNode *rnode, struct acl_rule *rule)
{
        CU_DEBUG("ACL mac rule %s", rnode->name);

        rule->type = MAC_RULE;
        rule->var.mac.protocol_id = get_attr_value(rnode, "protocolid");
        rule->var.mac.srcmacaddr = get_attr_value(rnode, "srcmacaddr");
        rule->var.mac.srcmacmask = get_attr_value(rnode, "srcmacmask");
        rule->var.mac.dstmacaddr = get_attr_value(rnode, "dstmacaddr");
        rule->var.mac.dstmacmask = get_attr_value(rnode, "dstmacmask");
        rule->var.mac.comment = get_attr_value(rnode, "comment");

        return 1;
}

static int parse_acl_arp_rule(xmlNode *rnode, struct acl_rule *rule)
{
        CU_DEBUG("ACL arp rule %s", rnode->name);

        rule->type = ARP_RULE;
        rule->var.arp.srcmacaddr = get_attr_value(rnode, "srcmacaddr");
        rule->var.arp.srcmacmask = get_attr_value(rnode, "srcmacmask");
        rule->var.arp.dstmacaddr = get_attr_value(rnode, "dstmacaddr");
        rule->var.arp.dstmacmask = get_attr_value(rnode, "dstmacmask");
        rule->var.arp.hw_type = get_attr_value(rnode, "hwtype");
        rule->var.arp.protocol_type = get_attr_value(rnode, "protocoltype");
        rule->var.arp.opcode = get_attr_value(rnode, "opcode");
        rule->var.arp.arpsrcmacaddr = get_attr_value(rnode, "arpsrcmacaddr");
        rule->var.arp.arpdstmacaddr = get_attr_value(rnode, "arpdstmacaddr");
        rule->var.arp.arpsrcipaddr = get_attr_value(rnode, "arpsrcipaddr");
        rule->var.arp.arpdstipaddr = get_attr_value(rnode, "arpdstipaddr");
        rule->var.arp.comment = get_attr_value(rnode, "comment");

        return 1;
}

static int parse_acl_ip_rule(xmlNode *rnode, struct acl_rule *rule)
{
        CU_DEBUG("ACP ip rule %s", rnode->name);

        rule->type = IP_RULE;
        rule->var.ip.srcmacaddr = get_attr_value(rnode, "srcmacaddr");
        rule->var.ip.srcmacmask = get_attr_value(rnode, "srcmacmask");
        rule->var.ip.dstmacaddr = get_attr_value(rnode, "dstmacaddr");
        rule->var.ip.dstmacmask = get_attr_value(rnode, "dstmacmaks");
        rule->var.ip.srcipaddr = get_attr_value(rnode, "srcipaddr");
        rule->var.ip.srcipmask = get_attr_value(rnode, "srcipmask");
        rule->var.ip.dstipaddr = get_attr_value(rnode, "dstipaddr");
        rule->var.ip.dstipmask = get_attr_value(rnode, "dstipmask");
        rule->var.ip.protocol = get_attr_value(rnode, "protocol");
        rule->var.ip.srcportstart = get_attr_value(rnode, "srcportstart");
        rule->var.ip.srcportend = get_attr_value(rnode, "srcportend");
        rule->var.ip.dstportstart = get_attr_value(rnode, "dstportstart");
        rule->var.ip.dstportend = get_attr_value(rnode, "dstportend");
        rule->var.ip.comment = get_attr_value(rnode, "comment");

        return 1;
}

static int parse_acl_tcp_rule(xmlNode *rnode, struct acl_rule *rule)
{
        CU_DEBUG("ACL tcp rule %s", rnode->name);

        rule->type = TCP_RULE;
        rule->var.tcp.srcmacaddr = get_attr_value(rnode, "srcmacaddr");
        rule->var.tcp.srcipaddr = get_attr_value(rnode, "srcipaddr");
        rule->var.tcp.srcipmask = get_attr_value(rnode, "srcipmask");
        rule->var.tcp.dstipaddr = get_attr_value(rnode, "dstipaddr");
        rule->var.tcp.dstipmask = get_attr_value(rnode, "dstipmask");
        rule->var.tcp.srcipfrom = get_attr_value(rnode, "srcipfrom");
        rule->var.tcp.srcipto = get_attr_value(rnode, "srcipto");
        rule->var.tcp.dstipfrom = get_attr_value(rnode, "dstipfrom");
        rule->var.tcp.dstipto = get_attr_value(rnode, "dstipto");
        rule->var.tcp.srcportstart = get_attr_value(rnode, "srcportstart");
        rule->var.tcp.srcportend = get_attr_value(rnode, "srcportend");
        rule->var.tcp.dstportstart = get_attr_value(rnode, "dstportstart");
        rule->var.tcp.dstportend = get_attr_value(rnode, "dstportend");
        rule->var.tcp.comment = get_attr_value(rnode, "comment");
        rule->var.tcp.state = get_attr_value(rnode, "state");

        return 1;
}

static int parse_acl_icmp_igmp_rule(xmlNode *rnode, struct acl_rule *rule)
{
        CU_DEBUG("ACL %s rule %s", rule->protocol_id, rnode->name);

        rule->type = ICMP_IGMP_RULE;
        rule->var.icmp_igmp.srcmacaddr = get_attr_value(rnode, "srcmacaddr");
        rule->var.icmp_igmp.srcmacmask = get_attr_value(rnode, "srcmacmask");
        rule->var.icmp_igmp.dstmacaddr = get_attr_value(rnode, "dstmacaddr");
        rule->var.icmp_igmp.dstmacmask = get_attr_value(rnode, "dstmacmask");
        rule->var.icmp_igmp.srcipaddr = get_attr_value(rnode, "srcipaddr");
        rule->var.icmp_igmp.srcipmask = get_attr_value(rnode, "srcipmask");
        rule->var.icmp_igmp.dstipaddr = get_attr_value(rnode, "dstipaddr");
        rule->var.icmp_igmp.dstipmask = get_attr_value(rnode, "dstipmask");
        rule->var.icmp_igmp.srcipfrom = get_attr_value(rnode, "srcipfrom");
        rule->var.icmp_igmp.srcipto = get_attr_value(rnode, "srcipto");
        rule->var.icmp_igmp.dstipfrom = get_attr_value(rnode, "dstipfrom");
        rule->var.icmp_igmp.dstipto = get_attr_value(rnode, "dstipto");
        rule->var.icmp_igmp.comment = get_attr_value(rnode, "comment");
        rule->var.icmp_igmp.state = get_attr_value(rnode, "state");

        return 1;
}

static int parse_acl_rule(xmlNode *rnode, struct acl_rule *rule)
{
        xmlNode *child = NULL;

        memset(rule, 0, sizeof(*rule));

        rule->action = get_attr_value(rnode, "action");
        if (rule->action == NULL)
                goto err;

        rule->direction = get_attr_value(rnode, "direction");
        if (rule->direction == NULL)
                goto err;

        rule->priority = get_attr_value(rnode, "priority");
        rule->statematch = get_attr_value(rnode, "statematch");

        for (child = rnode->children; child != NULL; child = child->next) {
                if (XSTREQ(child->name, "mac")) {
                        rule->protocol_id = strdup((char *)child->name);
                        parse_acl_mac_rule(child, rule);
                } else if (XSTREQ(child->name, "arp") ||
                        XSTREQ(child->name, "rarp")) {
                        rule->protocol_id = strdup((char *)child->name);
                        parse_acl_arp_rule(child, rule);
                } else if (XSTREQ(child->name, "ip") ||
                        XSTREQ(child->name, "ipv6")) {
                        rule->protocol_id = strdup((char *)child->name);
                        parse_acl_ip_rule(child, rule);
                } else if (XSTREQ(child->name, "tcp") ||
                        XSTREQ(child->name, "tcp-ipv6") ||
                        XSTREQ(child->name, "udp") ||
                        XSTREQ(child->name, "udp-ipv6") ||
                        XSTREQ(child->name, "sctp") ||
                        XSTREQ(child->name, "sctp-ipv6")) {
                        rule->protocol_id = strdup((char *)child->name);
                        parse_acl_tcp_rule(child, rule);
                } else if (XSTREQ(child->name, "icmp") ||
                        XSTREQ(child->name, "icmpv6") ||
                        XSTREQ(child->name, "igmp") ||
                        XSTREQ(child->name, "igmp-ipv6") ||
                        XSTREQ(child->name, "esp") ||
                        XSTREQ(child->name, "esp-ipv6") ||
                        XSTREQ(child->name, "ah") ||
                        XSTREQ(child->name, "ah-ipv6") ||
                        XSTREQ(child->name, "udplite") ||
                        XSTREQ(child->name, "udplite-ipv6") ||
                        XSTREQ(child->name, "all") ||
                        XSTREQ(child->name, "all-ipv6")) {
                        rule->protocol_id = strdup((char *)child->name);
                        parse_acl_icmp_igmp_rule(child, rule);
                }
        }

        return 1;

 err:
        cleanup_rule(rule);

        return 0;
}

static int parse_acl_filter(xmlNode *fnode, struct acl_filter *filter)
{
        struct acl_rule *rule = NULL;
        char *filter_ref = NULL;
        xmlNode *child = NULL;

        filter->name = get_attr_value(fnode, "name");
        if (filter->name == NULL)
                goto err;

        filter->chain = get_attr_value(fnode, "chain");

        for (child = fnode->children; child != NULL; child = child->next) {
                if (XSTREQ(child->name, "uuid")) {
                        STRPROP(filter, uuid, child);
                } else if (XSTREQ(child->name, "rule")) {
                        rule = malloc(sizeof(*rule));
                        if (rule == NULL)
                                goto err;

                        if (parse_acl_rule(child, rule) == 0)
                                goto err;

                        if (append_filter_rule(filter, rule) == 0) {
                                cleanup_rule(rule);
                                goto err;
                        }
                }
                else if (XSTREQ(child->name, "filterref")) {
                        filter_ref = get_attr_value(child, "filter");
                        if (filter_ref == NULL)
                                goto err;

                        if (append_filter_ref(filter, filter_ref) == 0)
                                goto err;
                }
        }

        return 1;

 err:
        cleanup_filter(filter);

        return 0;
}

/* Dummy function to suppress error message from libxml2 */
static void swallow_err_msg(void *ctx, const char *msg, ...)
{
        /* do nothing, just swallow the message. */
}

int get_filter_from_xml(const char *xml, struct acl_filter **filter)
{
        xmlDoc *xmldoc = NULL;
        int ret = 0;

        if (xml == NULL || filter == NULL)
                return 0;

        xmlSetGenericErrorFunc(NULL, swallow_err_msg);

        xmldoc = xmlParseMemory(xml, strlen(xml) + 1);
        if (xmldoc == NULL)
                goto err;

        *filter = calloc(1, sizeof(**filter));
        if (*filter == NULL)
                goto err;

        ret = parse_acl_filter(xmldoc->children, *filter);
        if (ret == 0) {
                free(*filter);
                *filter = NULL;
        }

 err:
        xmlSetGenericErrorFunc(NULL, NULL);
        xmlFreeDoc(xmldoc);

        return ret;
}

int get_filter_by_name(
        virConnectPtr conn,
        const char *name,
        struct acl_filter **filter)
{
#if LIBVIR_VERSION_NUMBER > 8000
        virNWFilterPtr vfilter = NULL;
        char *xml = NULL;

        if (name == NULL || filter == NULL)
                return 0;

        vfilter = virNWFilterLookupByName(conn, name);
        if (vfilter == NULL)
                return 0;

        xml = virNWFilterGetXMLDesc(vfilter, 0);

        virNWFilterFree(vfilter);

        if (xml == NULL)
                return 0;

        return get_filter_from_xml(xml, filter);
#else
        return 0;
#endif
}

int get_filter_by_uuid(
        virConnectPtr conn,
        const char *uuid,
        struct acl_filter **filter)
{
#if LIBVIR_VERSION_NUMBER > 8000
        virNWFilterPtr vfilter = NULL;
        char *xml = NULL;

        if (uuid == NULL || filter == NULL)
                return 0;

        vfilter = virNWFilterLookupByUUIDString(conn, uuid);
        if (vfilter == NULL)
                return 0;

        xml = virNWFilterGetXMLDesc(vfilter, 0);

        virNWFilterFree(vfilter);

        if (xml == NULL)
                return 0;

        return get_filter_from_xml(xml, filter);
#else
        return 0;
#endif
}

int get_filters(
        virConnectPtr conn,
        struct acl_filter **list)
{
#if LIBVIR_VERSION_NUMBER > 8000
        int count = 0;
        char **names = NULL;
        struct acl_filter *filters = NULL;
        int i = 0;

        count = virConnectNumOfNWFilters(conn);

        names = calloc(count, sizeof(char *));
        if (names == NULL)
                goto err;

        virConnectListNWFilters(conn, names, count);

        filters = calloc(count, sizeof(*filters));

        if (filters == NULL)
                goto err;

        for(i = 0; i < count; i++)
        {
                struct acl_filter *filter = NULL;

                if (get_filter_by_name(conn, names[i], &filter) == 0)
                        break;

                memcpy(&filters[i], filter, sizeof(*filter));
                free(filter);
        }

        *list = filters;

 err:
        free(names);

        return i;
#else
        return 0;
#endif
}

int create_filter(virConnectPtr conn, struct acl_filter *filter)
{
#if LIBVIR_VERSION_NUMBER > 8000
        virNWFilterPtr vfilter = NULL;
        char *xml = NULL;

        if (filter == NULL)
                return 0;

        xml = filter_to_xml(filter);
        if (xml == NULL)
                return 0;

        vfilter = virNWFilterDefineXML(conn, xml);

        free(xml);

        if (vfilter == NULL)
                return 0;

        virNWFilterFree(vfilter);

        return 1;
#else
        return 0;
#endif
}

int update_filter(virConnectPtr conn, struct acl_filter *filter)
{
        return create_filter(conn, filter);
}

int delete_filter(virConnectPtr conn, struct acl_filter *filter)
{
#if LIBVIR_VERSION_NUMBER > 8000
        int ret = 0;
        virNWFilterPtr vfilter = NULL;

        if (filter == NULL)
                return 0;

        vfilter = virNWFilterLookupByName(conn, filter->name);
        if (vfilter == NULL)
                return 0;

        ret = virNWFilterUndefine(vfilter);

        virNWFilterFree(vfilter);

        return ret == 0 ? 1 : 0;
#else
        return 0;
#endif
}

int append_filter_rule(struct acl_filter *filter, struct acl_rule *rule)
{
        struct acl_rule **old_rules = NULL;

        if ((filter == NULL) || (rule == NULL))
                return 0;

        rule->name = make_rule_id(filter->name, filter->rule_ct);
        if (rule->name == NULL)
                return 0;

        old_rules = filter->rules;

        filter->rules =
                malloc((filter->rule_ct + 1) * sizeof(struct acl_rule *));

        if (filter->rules == NULL) {
                CU_DEBUG("Failed to allocate memory for new rule");
                filter->rules = old_rules;
                return 0;
        }

        memcpy(filter->rules,
                old_rules,
                filter->rule_ct * sizeof(struct acl_rule *));

        filter->rules[filter->rule_ct] = rule;
        filter->rule_ct++;

        free(old_rules);

        return 1;
}

int append_filter_ref(struct acl_filter *filter, char *name)
{
        int i;
        char **old_refs = NULL;

        if ((filter == NULL) || (name == NULL))
                return 0;

        for (i = 0; i < filter->ref_ct; i++)
                if (STREQC(filter->refs[i], name))
                        return 0; /* already exists */

        old_refs = filter->refs;

        filter->refs = malloc((filter->ref_ct + 1) * sizeof(char *));

        if (filter->refs == NULL) {
                CU_DEBUG("Failed to allocate memory for new ref");
                filter->refs = old_refs;
                return 0;
        }

        memcpy(filter->refs, old_refs, filter->ref_ct * sizeof(char *));

        filter->refs[filter->ref_ct] = name;
        filter->ref_ct++;

        free(old_refs);

        return 1;
}

int remove_filter_ref(struct acl_filter *filter, const char *name)
{
        int i;
        char **old_refs = NULL;

        if ((filter == NULL) || (name == NULL))
                return 0;

        /* TODO: called infrequently, but needs optimization */
        old_refs = filter->refs;
        filter->ref_ct = 0;

        for (i = 0; i < filter->ref_ct; i++) {
                if (STREQC(old_refs[i], name)) {
                        free(old_refs[i]);
                }
                else if(append_filter_ref(filter, old_refs[i]) == 0) {
                        return 0;
                }
        }

        return 1;
}

char *make_rule_id(const char *filter, int index)
{
        int ret;
        char *rule_id = NULL;

        if (filter == NULL)
                return NULL;

        ret = asprintf(&rule_id, "%s:%u", filter, index);
        if (ret == -1) {
                free(rule_id);
                rule_id = NULL;
        }


        return rule_id;
}

int parse_rule_id(const char *rule_id, char **filter, int *index)
{
        int ret;

        if ((filter == NULL) || (index == NULL))
                return 0;
        ret = sscanf(rule_id, "%as[^:]:%u", filter, index);
        if (ret != 2) {
                free(*filter);
                *filter = NULL;

                return 0;
        }

        return 1;
}

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
