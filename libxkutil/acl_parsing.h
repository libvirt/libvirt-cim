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
#ifndef __ACL_PARSING_H
#define __ACL_PARSING_H

#include <libvirt/libvirt.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#include "list_util.h"

struct acl_mac_rule {
        char *srcmacaddr;
        char *srcmacmask;
        char *dstmacaddr;
        char *dstmacmask;

        char *protocol_id;
        char *comment;
};

struct acl_arp_rule {
        char *srcmacaddr;
        char *srcmacmask;
        char *dstmacaddr;
        char *dstmacmask;

        char *hw_type;
        char *protocol_type;
        char *opcode;
        char *arpsrcmacaddr;
        char *arpdstmacaddr;
        char *arpsrcipaddr;
        char *arpdstipaddr;
        char *comment;
};

struct acl_ip_rule {
        char *srcmacaddr;
        char *srcmacmask;
        char *dstmacaddr;
        char *dstmacmask;

        char *srcipaddr;
        char *srcipmask;
        char *dstipaddr;
        char *dstipmask;

        char *protocol;

        char *srcportstart;
        char *srcportend;
        char *dstportstart;
        char *dstportend;

        char *comment;
};

struct acl_tcp_rule {
        char *srcmacaddr;

        char *srcipaddr;
        char *srcipmask;
        char *dstipaddr;
        char *dstipmask;

        char *srcipfrom;
        char *srcipto;
        char *dstipfrom;
        char *dstipto;

        char *srcportstart;
        char *srcportend;
        char *dstportstart;
        char *dstportend;

        char *comment;
        char *state;
};

struct acl_icmp_igmp_rule {
        char *srcmacaddr;
        char *srcmacmask;
        char *dstmacaddr;
        char *dstmacmask;

        char *srcipaddr;
        char *srcipmask;
        char *dstipaddr;
        char *dstipmask;

        char *srcipfrom;
        char *srcipto;
        char *dstipfrom;
        char *dstipto;

        char *type;
        char *code;
        char *comment;
        char *state;
};


struct acl_rule {
        char *name;
        char *protocol_id;
        char *action;
        char *direction;
        char *priority;
        char *statematch;

        enum {
                UNKNOWN_RULE,
                MAC_RULE,
                ARP_RULE,
                IP_RULE,
                TCP_RULE,
                ICMP_IGMP_RULE,
        } type;

        union {
                struct acl_mac_rule mac;
                struct acl_arp_rule arp;
                struct acl_ip_rule ip;
                struct acl_tcp_rule tcp;
                struct acl_icmp_igmp_rule icmp_igmp;
        } var;
};

struct acl_filter {
        char *uuid;
        char *name;
        char *chain;
        char *priority;

        struct acl_rule **rules;
        int rule_ct;

        list_t *refs;
};

void cleanup_rule(struct acl_rule *rule);
void cleanup_filter(struct acl_filter *filter);
void cleanup_filters(struct acl_filter **filters, int count);

int get_filters(virConnectPtr conn, struct acl_filter **list);

int get_filter_from_xml(const char *xml, struct acl_filter **filter);
int get_filter_by_uuid(virConnectPtr conn, const char *uuid, 
        struct acl_filter **filter);
int get_filter_by_name(virConnectPtr conn, const char *name,
        struct acl_filter **filter);

char *make_rule_id(const char *filter, int index);
int parse_rule_id(const char *rule_id, char **filter, int *index);

int create_filter(virConnectPtr conn, struct acl_filter *filter);
int update_filter(virConnectPtr conn, struct acl_filter *filter);
int delete_filter(virConnectPtr conn, struct acl_filter *filter);

/** NOTE: Both append functions take parameters allocated by caller and
 *  freed by cleanup_filter(s)
 */
int append_filter_rule(struct acl_filter *filter, struct acl_rule *rule);
int append_filter_ref(struct acl_filter *filter, char *name);
int remove_filter_ref(struct acl_filter *filter, const char *name);

#endif

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
