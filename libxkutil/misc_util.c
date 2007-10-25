/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *  Heidi Eckhart <heidieck@linux.vnet.ibm.com>
 *  Jay Gagnon <grendel@linux.vnet.ibm.com>
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
#include <unistd.h>
#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include "libcmpiutil.h"

#include "misc_util.h"
#include "cs_util.h"

#define URI_ENV "HYPURI"

static const char *cn_to_uri(const char *classname)
{
        if (STARTS_WITH(classname, "Xen"))
                return "xen";
        else if (STARTS_WITH(classname, "KVM"))
                return "qemu:///system";
        else
                return NULL;
}

virConnectPtr connect_by_classname(const CMPIBroker *broker,
                                   const char *classname,
                                   CMPIStatus *s)
{
        const char *uri;
        virConnectPtr conn;

        uri = cn_to_uri(classname);
        if (!uri) {
                CMSetStatusWithChars(broker, s, 
                                     CMPI_RC_ERR_FAILED,
                                     "Unable to generate URI from classname");
                return NULL;
        }

        conn = virConnectOpen(uri);
        if (!conn) {
                CMSetStatusWithChars(broker, s,
                                     CMPI_RC_ERR_FAILED,
                                     "Unable to connect to hypervisor");
                return NULL;
        }

        return conn;
}

void free_domain_list(virDomainPtr *list, int count)
{
        int i;

        for (i = 0; i < count; i++)
                virDomainFree(list[i]);
}

char *get_key_from_ref_arg(const CMPIArgs *args, char *arg, char *key)
{
        CMPIObjectPath *ref = NULL;

        ref = cu_get_ref_arg(args, arg);
        if (ref == NULL)
                return NULL;

        return cu_get_str_path(ref, key);
}

bool domain_exists(virConnectPtr conn, const char *name)
{
        virDomainPtr dom = virDomainLookupByName(conn, name);
        if (dom == NULL) {
                virConnResetLastError(conn);
                return false;
        }

        virDomainFree(dom);
        return true;
}

char *class_prefix_name(const char *classname)
{
        char *tmp;
        char *delim;

        tmp = strdup(classname);

        delim = strchr(tmp, '_');
        if (delim) {
                *delim = '\0';
                return tmp;
        } else {
                free(tmp);
                return NULL;
        }
}

char *association_prefix(const char *provider_name)
{
        const char *name;

        if (!STARTS_WITH(provider_name, "association"))
                return NULL;

        name = provider_name + strlen("association");

        return class_prefix_name(name);
}

bool match_pn_to_cn(const char *pn, const char *cn)
{
        char *pn_pfx = NULL;
        char *cn_pfx = NULL;
        bool result = false;

        pn_pfx = association_prefix(pn);
        cn_pfx = class_prefix_name(cn);

        if (pn_pfx && cn_pfx)
                result = STREQC(pn_pfx, cn_pfx);

        free(pn_pfx);
        free(cn_pfx);

        return result;
}

int parse_instance_id(char *_iid, char **orgid, char **locid)
{
        char *iid = NULL;
        char *delim = NULL;
        int ret = 0;

        iid = strdup(_iid);

        delim = strchr(iid, ':');
        if (!delim) {
                free(iid);
                goto out;
        }

        *delim = '\0';
        *orgid = iid;
        *locid = strdup(delim+1);

        ret = 1;
 out:
        return ret;
}

static const char *prefix_from_uri(const char *uri)
{
        if (strstr(uri, "xen"))
                return "Xen";
        else if (strstr(uri, "qemu"))
                return "KVM";
        else
                return NULL;
}

static bool is_xen(void)
{
        return access("/proc/xen/privcmd", R_OK) == 0;
}

static bool is_kvm(void)
{
        return access("/sys/module/kvm", R_OK) == 0;
}

static bool is_user(void)
{
        return getenv(URI_ENV) != NULL;
}

static const char *default_prefix(void)
{
        if (is_user())
                return prefix_from_uri(getenv(URI_ENV));
        else if (is_xen())
                return "Xen";
        else if (is_kvm())
                return "KVM";
        else
                return NULL;
}

uint64_t allocated_memory(virConnectPtr conn)
{
        virDomainPtr *list;
        int count;
        int i;
        uint64_t memory = 0;

        count = get_domain_list(conn, &list);
        if (count <= 0)
                return 0;

        for (i = 0; i < count; i ++) {
                virDomainPtr dom = list[i];
                virDomainInfo info;

                if (virDomainGetInfo(dom, &info) == 0)
                        memory += info.memory;

                virDomainFree(dom);
        }

        free(list);

        return memory;
}

CMPIInstance *get_typed_instance(const CMPIBroker *broker,
                                 const char *base,
                                 const char *namespace)
{
        const char *prefix;
        char *new_cn;
        CMPIObjectPath *op;
        CMPIInstance *inst = NULL;
        CMPIStatus s;

        prefix = default_prefix();
        if (prefix == NULL)
                goto out;

        if (asprintf(&new_cn, "%s_%s", prefix, base) == -1) {
                new_cn = NULL;
                goto out;
        }

        op = CMNewObjectPath(broker, namespace, new_cn, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op))
                goto out;

        inst = CMNewInstance(broker, op, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(inst))
                goto out;

        CMSetProperty(inst, "CreationClassName",
                      (CMPIValue *)new_cn, CMPI_chars);

 out:
        free(new_cn);

        return inst;
}

char *get_typed_class(const char *new_base)
{
        const char *pfx;
        char *class = NULL;

        pfx = default_prefix();
        if (pfx == NULL)
                return NULL;

        if (asprintf(&class, "%s_%s", pfx, new_base) == -1)
                class = NULL;

        return class;
}

char *class_base_name(const char *classname)
{
        char *tmp;

        tmp = strchr(classname, '_');
        if (tmp)
                return strdup(tmp + 1);
        else
                return NULL;
}

virConnectPtr lv_connect(const CMPIBroker *broker, CMPIStatus *s)
{
        const char *uri = NULL;
        virConnectPtr conn;

        if (is_user())
                uri = getenv(URI_ENV);
        else if (is_xen())
                uri = "xen";
        else if (is_kvm())
                uri = "qemu:///system";
        else {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to determine hypervisor type");
                return NULL;
        }

        conn = virConnectOpen(uri);
        if (conn == NULL)
                cu_statusf(broker, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to connect to %s", uri);
        else
                CMSetStatus(s, CMPI_RC_OK);

        return conn;
}

bool provider_is_responsible(const CMPIBroker *broker,
                             const CMPIObjectPath *reference,
                             CMPIStatus *status)
{
        const char *dft_pfx;
        char *pfx;
        bool rc = false;

        CMSetStatus(status, CMPI_RC_OK);

        pfx = class_prefix_name(CLASSNAME(reference));

        if (STREQC(pfx, "CIM"))
                cu_statusf(broker, status,
                           CMPI_RC_ERR_FAILED,
                           "Please exactly specify the class (check CIMOM behavior!): %s", 
                           CLASSNAME(reference));

        dft_pfx = default_prefix();
        if (dft_pfx == NULL)
                goto out;
        
        if (STREQC(pfx, dft_pfx)) 
                rc = true;

 out:
        free(pfx);
        return rc;
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
