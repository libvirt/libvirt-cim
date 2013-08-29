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
#ifndef __CAPABILITY_PARSING_H
#define __CAPABILITY_PARSING_H

#include <stdint.h>
#include <stdbool.h>

struct cap_host {
        char *cpu_arch;
};

struct cap_machine {
        char *name;
        char *canonical_name;
};

struct cap_domain_info {
        char *emulator;
        char *loader;
        int num_machines;
        struct cap_machine *machines;
};

struct cap_domain {
        char *typestr;
        struct cap_domain_info guest_domain_info;
};

struct cap_arch {
        char *name;
        unsigned int wordsize;
        struct cap_domain_info default_domain_info;
        int num_domains;
        struct cap_domain *domains;
};

struct cap_guest {
        char *ostype;
        struct cap_arch arch;
};

struct capabilities {
        struct cap_host host;
        int num_guests;
        struct cap_guest *guests;
};

int get_caps_from_xml(const char *xml, struct capabilities **caps);
int get_capabilities(virConnectPtr conn, struct capabilities **caps);
char *get_default_arch(struct capabilities *caps,
                       const char *os_type);
char *get_default_machine(struct capabilities *caps,
                          const char *os_type,
                          const char *arch,
                          const char *domain_type);
char *get_default_emulator(struct capabilities *caps,
                           const char *os_type,
                           const char *arch,
                           const char *domain_type);
struct cap_domain_info *findDomainInfo(struct capabilities *caps,
                                       const char *os_type,
                                       const char *arch,
                                       const char *domain_type);
bool use_kvm(struct capabilities *caps);
bool host_supports_kvm(struct capabilities *caps);
void cleanup_capabilities(struct capabilities **caps);

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
