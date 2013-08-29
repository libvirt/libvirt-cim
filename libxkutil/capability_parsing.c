/*
 * Copyright IBM Corp. 2013
 *
 * Authors:
 *  Boris Fiuczynski <fiuczy@linux.vnet.ibm.com>
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
 * License along with this library. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <stdint.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libvirt/libvirt.h>
#include <libxml/xpath.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "misc_util.h"
#include "capability_parsing.h"
#include "xmlgen.h"
#include "../src/svpc_types.h"

static void cleanup_cap_machine(struct cap_machine *machine)
{
        if (machine == NULL)
                return;
        free(machine->name);
        free(machine->canonical_name);
}

static void cleanup_cap_domain_info(struct cap_domain_info *cgdi)
{
        int i;
        if (cgdi == NULL)
                return;
        free(cgdi->emulator);
        free(cgdi->loader);
        for (i = 0; i < cgdi->num_machines; i++)
                cleanup_cap_machine(&cgdi->machines[i]);
        free(cgdi->machines);
}

static void cleanup_cap_domain(struct cap_domain *cgd)
{
        if (cgd == NULL)
                return;
        free(cgd->typestr);
        cleanup_cap_domain_info(&cgd->guest_domain_info);
}

static void cleanup_cap_arch(struct cap_arch *cga)
{
        int i;
        if (cga == NULL)
                return;
        free(cga->name);
        cleanup_cap_domain_info(&cga->default_domain_info);
        for (i = 0; i < cga->num_domains; i++)
                cleanup_cap_domain(&cga->domains[i]);
        free(cga->domains);
}

static void cleanup_cap_guest(struct cap_guest *cg)
{
        if (cg == NULL)
                return;
        free(cg->ostype);
        cleanup_cap_arch(&cg->arch);
}

static void cleanup_cap_host(struct cap_host *ch)
{
        if (ch == NULL)
                return;
        free(ch->cpu_arch);
}

void cleanup_capabilities(struct capabilities **caps)
{
        int i;
        struct capabilities *cap;

        if ((caps == NULL) || (*caps == NULL))
                return;

        cap = *caps;
        cleanup_cap_host(&cap->host);
        for (i = 0; i < cap->num_guests; i++)
                cleanup_cap_guest(&cap->guests[i]);

        free(cap->guests);
        free(cap);
        *caps = NULL;
}

static void extend_cap_machines(struct cap_domain_info *cg_domaininfo,
                                char *name, char *canonical_name)
{
        struct cap_machine *tmp_list = NULL;
        tmp_list = realloc(cg_domaininfo->machines,
                           (cg_domaininfo->num_machines + 1) *
                           sizeof(struct cap_machine));

        if (tmp_list == NULL) {
                /* Nothing you can do. Just go on. */
                CU_DEBUG("Could not alloc space for "
                         "guest domain info list");
                return;
        }
        cg_domaininfo->machines = tmp_list;

        struct cap_machine *cap_gm =
                &cg_domaininfo->machines[cg_domaininfo->num_machines];
        cap_gm->name = name;
        cap_gm->canonical_name = canonical_name;
        cg_domaininfo->num_machines++;
}

static void parse_cap_domain_info(struct cap_domain_info *cg_domaininfo,
                                  xmlNode *domain_child_node)
{
        CU_DEBUG("Capabilities guest domain info element node: %s",
                 domain_child_node->name);

        if (XSTREQ(domain_child_node->name, "emulator")) {
                cg_domaininfo->emulator =
                        get_node_content(domain_child_node);
        } else if (XSTREQ(domain_child_node->name, "loader")) {
                cg_domaininfo->loader =
                        get_node_content(domain_child_node);
        } else if (XSTREQ(domain_child_node->name, "machine")) {
                extend_cap_machines(cg_domaininfo,
                                    get_node_content(domain_child_node),
                                    get_attr_value(domain_child_node,
                                                   "canonical"));
        }
}

static void parse_cap_domain(struct cap_domain *cg_domain,
                             xmlNode *guest_dom)
{
        CU_DEBUG("Capabilities guest domain node: %s", guest_dom->name);

        xmlNode *child;

        cg_domain->typestr = get_attr_value(guest_dom, "type");

        for (child = guest_dom->children; child != NULL; child = child->next)
                parse_cap_domain_info(&cg_domain->guest_domain_info, child);
}

static void parse_cap_arch(struct cap_arch *cg_archinfo,
                           xmlNode *arch)
{
        CU_DEBUG("Capabilities arch node: %s", arch->name);

        xmlNode *child;

        cg_archinfo->name = get_attr_value(arch, "name");

        for (child = arch->children; child != NULL; child = child->next) {
                if (XSTREQ(child->name, "wordsize")) {
                        char *wordsize_str;
                        unsigned int wordsize;
                        wordsize_str = get_node_content(child);
                        /* Default to 0 wordsize if garbage */
                        if (wordsize_str == NULL ||
                            sscanf(wordsize_str, "%i", &wordsize) != 1)
                                wordsize = 0;
                        free(wordsize_str);
                        cg_archinfo->wordsize = wordsize;
                } else if (XSTREQ(child->name, "domain")) {
                        struct cap_domain *tmp_list = NULL;
                        tmp_list = realloc(cg_archinfo->domains,
                                           (cg_archinfo->num_domains + 1) *
                                           sizeof(struct cap_domain));
                        if (tmp_list == NULL) {
                                /* Nothing you can do. Just go on. */
                                CU_DEBUG("Could not alloc space for "
                                         "guest domain");
                                continue;
                        }
                        memset(&tmp_list[cg_archinfo->num_domains],
                               0, sizeof(struct cap_domain));
                        cg_archinfo->domains = tmp_list;
                        parse_cap_domain(&cg_archinfo->
                                         domains[cg_archinfo->num_domains],
                                         child);
                        cg_archinfo->num_domains++;
                } else {
                        /* Check for the default domain child nodes */
                        parse_cap_domain_info(&cg_archinfo->default_domain_info,
                                              child);
                }
        }
}

static void parse_cap_guests(xmlNodeSet *nsv, struct cap_guest *cap_guests)
{
        xmlNode **nodes = nsv->nodeTab;
        xmlNode *child;
        int numGuestNodes = nsv->nodeNr;
        int i;

        for (i = 0; i < numGuestNodes; i++) {
                for (child = nodes[i]->children; child != NULL;
                     child = child->next) {
                        if (XSTREQ(child->name, "os_type")) {
                                STRPROP((&cap_guests[i]), ostype, child);
                        } else if (XSTREQ(child->name, "arch")) {
                                parse_cap_arch(&cap_guests[i].arch, child);
                        }
                }
        }
}

static int parse_cap_host_cpu(struct cap_host *cap_host, xmlNode *cpu)
{
        xmlNode *child;

        for (child = cpu->children; child != NULL; child = child->next) {
                if (XSTREQ(child->name, "arch")) {
                        cap_host->cpu_arch = get_node_content(child);
                        if (cap_host->cpu_arch != NULL)
                                return 1; /* success - host arch node found */
                        else {
                                CU_DEBUG("Host architecture is not defined");
                                break;
                        }
                }
        }
        return 0; /* error - no arch node or empty arch node */
}

static int parse_cap_host(xmlNodeSet *nsv, struct cap_host *cap_host)
{
        xmlNode **nodes = nsv->nodeTab;
        xmlNode *child;
        if (nsv->nodeNr < 1)
                return 0; /* error no node below host */

        for (child = nodes[0]->children; child != NULL; child = child->next) {
                if (XSTREQ(child->name, "cpu"))
                        return parse_cap_host_cpu(cap_host, child);
        }
        return 0;  /* error - no cpu node */
}

static void compare_copy_domain_info_machines(
        struct cap_domain_info *def_gdomi,
        struct cap_domain_info *cap_gadomi)
{
        int i,j;
        int org_l = cap_gadomi->num_machines;
        char *cp_name = NULL;
        char *cp_canonical_name = NULL;
        bool found;

        for (i = 0; i < def_gdomi->num_machines; i++) {
                found = false;
                for (j = 0; j < org_l; j++) {
                        if (STREQC(def_gdomi->machines[i].name,
                                   cap_gadomi->machines[j].name)) {
                                found = true;
                                continue;
                                /* found match => check next default */
                        }
                }
                if (!found) { /* no match => insert default */
                        cp_name = NULL;
                        cp_canonical_name = NULL;
                        if (def_gdomi->machines[i].name != NULL)
                                cp_name = strdup(def_gdomi->machines[i].name);
                        if (def_gdomi->machines[i].canonical_name != NULL)
                                cp_canonical_name =
                                        strdup(def_gdomi->
                                               machines[i].canonical_name);

                        extend_cap_machines(cap_gadomi,
                                            cp_name,
                                            cp_canonical_name);
                }
        }
}

static void extend_defaults_cap_guests(struct capabilities *caps)
{
        struct cap_arch *cap_garch;
        struct cap_domain_info *cap_gadomi;
        struct cap_domain_info *def_gdomi;
        int i,j;

        if (caps == NULL)
                return;

        for (i = 0; i < caps->num_guests; i++) {
                cap_garch = &caps->guests[i].arch;
                def_gdomi = &cap_garch->default_domain_info;

                for (j = 0; j < cap_garch->num_domains; j++) {
                        /* compare guest_domain_info */
                        cap_gadomi = &cap_garch->domains[j].guest_domain_info;
                        if (cap_gadomi->emulator == NULL &&
                            def_gdomi->emulator != NULL)
                                cap_gadomi->emulator =
                                        strdup(def_gdomi->emulator);
                        if (cap_gadomi->loader == NULL &&
                            def_gdomi->loader != NULL)
                                cap_gadomi->loader = strdup(def_gdomi->loader);

                        compare_copy_domain_info_machines(def_gdomi,
                                                          cap_gadomi);
                }
        }
}

static int _get_capabilities(const char *xml, struct capabilities *caps)
{
        int len;
        int ret = 0;

        xmlDoc *xmldoc = NULL;
        xmlXPathContext *xpathctx = NULL;
        xmlXPathObject *xpathobj = NULL;
        const xmlChar *xpathhoststr = (xmlChar *)"//capabilities//host";
        const xmlChar *xpathgueststr = (xmlChar *)"//capabilities//guest";
        xmlNodeSet *nsv;

        len = strlen(xml) + 1;

        if ((xmldoc = xmlParseMemory(xml, len)) == NULL)
                goto err;

        if ((xpathctx = xmlXPathNewContext(xmldoc)) == NULL)
                goto err;

        /* host node */
        if ((xpathobj = xmlXPathEvalExpression(xpathhoststr, xpathctx)) == NULL)
                goto err;
        if (xmlXPathNodeSetIsEmpty(xpathobj->nodesetval)) {
                CU_DEBUG("No capabilities host node found!");
                goto err;
        }

        nsv = xpathobj->nodesetval;
        if (!parse_cap_host(nsv, &caps->host))
                goto err;
        xmlXPathFreeObject(xpathobj);

        /* all guest nodes */
        if ((xpathobj = xmlXPathEvalExpression(xpathgueststr, xpathctx)) == NULL)
                goto err;
        if (xmlXPathNodeSetIsEmpty(xpathobj->nodesetval)) {
                CU_DEBUG("No capabilities guest nodes found!");
                goto err;
        }

        nsv = xpathobj->nodesetval;
        caps->guests = calloc(nsv->nodeNr, sizeof(struct cap_guest));
        if (caps->guests == NULL)
                goto err;
        caps->num_guests = nsv->nodeNr;

        parse_cap_guests(nsv, caps->guests);
        extend_defaults_cap_guests(caps);
        ret = 1;

 err:
        xmlXPathFreeObject(xpathobj);
        xmlXPathFreeContext(xpathctx);
        xmlFreeDoc(xmldoc);
        return ret;
}

int get_caps_from_xml(const char *xml, struct capabilities **caps)
{
        CU_DEBUG("In get_caps_from_xml");

        free(*caps);
        *caps = calloc(1, sizeof(struct capabilities));
        if (*caps == NULL)
                goto err;

        if (_get_capabilities(xml, *caps) == 0)
                goto err;

        return 1;

 err:
        free(*caps);
        *caps = NULL;
        return 0;
}

int get_capabilities(virConnectPtr conn, struct capabilities **caps)
{
        char *caps_xml = NULL;
        int ret = 0;

        if (conn == NULL) {
                CU_DEBUG("Unable to connect to libvirt.");
                return 0;
        }

        caps_xml = virConnectGetCapabilities(conn);

        if (caps_xml == NULL) {
                CU_DEBUG("Unable to get capabilities xml.");
                return 0;
        }

        ret = get_caps_from_xml(caps_xml, caps);

        free(caps_xml);

        return ret;
}

struct cap_domain_info *findDomainInfo(struct capabilities *caps,
                                       const char *os_type,
                                       const char *arch,
                                       const char *domain_type)
{
        int i,j;
        struct cap_arch *ar;

        for (i = 0; i < caps->num_guests; i++) {
                if (os_type == NULL ||
                    STREQC(caps->guests[i].ostype, os_type)) {
                        ar = &caps->guests[i].arch;
                        if (arch == NULL || STREQC(ar->name,arch))
                                for (j = 0; j < ar->num_domains; j++)
                                        if (domain_type == NULL ||
                                            STREQC(ar->domains[j].typestr,
                                                   domain_type))
                                                return &ar->domains[j].
                                                        guest_domain_info;
                }
        }
        return NULL;
}

static char *_findDefArch(struct capabilities *caps,
                          const char *os_type,
                          const char *host_arch)
{
        char *ret = NULL;
        int i;

        for (i = 0; i < caps->num_guests; i++) {
                if (STREQC(caps->guests[i].ostype, os_type) &&
                    (host_arch == NULL || (host_arch != NULL &&
                    STREQC(caps->guests[i].arch.name, host_arch)))) {
                        ret = caps->guests[i].arch.name;
                        break;
                }
        }
        return ret;
}

char *get_default_arch(struct capabilities *caps,
                       const char *os_type)
{
        char *ret = NULL;

        if (caps != NULL && os_type != NULL) {
                /* search first guest matching os_type and host arch */
                ret = _findDefArch(caps, os_type, caps->host.cpu_arch);
                if (ret == NULL) /* search first matching guest */
                        ret = _findDefArch(caps, os_type, NULL);
        }
        return ret;
}

char *get_default_machine(
        struct capabilities *caps,
        const char *os_type,
        const char *arch,
        const char *domain_type)
{
        char *ret = NULL;
        struct cap_domain_info *di;

        if (caps != NULL) {
                di = findDomainInfo(caps, os_type, arch, domain_type);
                if (di != NULL && di->num_machines > 0) {
                        ret = di->machines[0].canonical_name ?
                                di->machines[0].canonical_name :
                                di->machines[0].name;
                }
        }
        return ret;
}

char *get_default_emulator(struct capabilities *caps,
                           const char *os_type,
                           const char *arch,
                           const char *domain_type)
{
        char *ret = NULL;
        struct cap_domain_info *di;

        if (caps != NULL) {
                di = findDomainInfo(caps, os_type, arch, domain_type);
                if (di != NULL)
                        ret = di->emulator;
        }
        return ret;
}

bool use_kvm(struct capabilities *caps) {
        if (host_supports_kvm(caps) && !get_disable_kvm())
                return true;
        return false;
}

bool host_supports_kvm(struct capabilities *caps)
{
        bool kvm = false;
        if (caps != NULL) {
                if (findDomainInfo(caps, NULL, NULL, "kvm") != NULL)
                        kvm = true;
        }
        return kvm;
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
