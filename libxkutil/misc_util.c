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
#include "std_association.h"

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

        CMSetStatus(s, CMPI_RC_OK);

        uri = cn_to_uri(classname);
        if (!uri) {
                CMSetStatusWithChars(broker, s, 
                                     CMPI_RC_ERR_FAILED,
                                     "Unable to generate URI from classname");
                return NULL;
        }

        CU_DEBUG("Connecting to libvirt with uri `%s'", uri);

        conn = virConnectOpen(uri);
        if (!conn) {
                CU_DEBUG("Unable to connect to `%s'", uri);
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

const char *pfx_from_conn(virConnectPtr conn)
{
        char *uri;
        const char *pfx = "Xen";

        uri = virConnectGetURI(conn);
        if (uri == NULL)
                return pfx; /* Default/Error case */

        CU_DEBUG("URI of connection is: %s", uri);

        if (STARTS_WITH(uri, "xen"))
                pfx = "Xen";
        else if (STARTS_WITH(uri, "qemu"))
                pfx = "KVM";

        free(uri);

        return pfx;
}

char *get_typed_class(const char *refcn, const char *new_base)
{
        char *class = NULL;
        char *pfx;

        if (strchr(refcn, '_'))
                pfx = class_prefix_name(refcn);
        else
                pfx = strdup(refcn);

        if (pfx == NULL)
                return NULL;

        if (asprintf(&class, "%s_%s", pfx, new_base) == -1)
                class = NULL;

        free(pfx);

        return class;
}

CMPIInstance *get_typed_instance(const CMPIBroker *broker,
                                 const char *refcn,
                                 const char *base,
                                 const char *namespace)
{
        char *new_cn;
        CMPIObjectPath *op;
        CMPIInstance *inst = NULL;
        CMPIStatus s;

        new_cn = get_typed_class(refcn, base);
        if (new_cn == NULL)
                goto out;

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
        virConnectPtr conn;

        /* This is going away, so just assume Xen for right now */

        conn = virConnectOpen("xen");
        if (conn == NULL)
                cu_statusf(broker, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to connect to xen");
        else
                CMSetStatus(s, CMPI_RC_OK);

        return conn;
}

bool provider_is_responsible(const CMPIBroker *broker,
                             const CMPIObjectPath *reference,
                             CMPIStatus *status)
{
        char *pfx;
        bool rc = true;

        CMSetStatus(status, CMPI_RC_OK);

        pfx = class_prefix_name(CLASSNAME(reference));

        if (STREQC(pfx, "CIM")) {
                cu_statusf(broker, status,
                           CMPI_RC_ERR_FAILED,
                           "Please exactly specify the class (check CIMOM behavior!): %s", 
                           CLASSNAME(reference));
                rc = false;
        }

        free(pfx);
        return rc;
}

bool match_hypervisor_prefix(const CMPIObjectPath *reference,
                             struct std_assoc_info *info)
{
        char *ref_pfx = NULL;
        char *pfx = NULL;
        bool rc = true;

        ref_pfx = class_prefix_name(CLASSNAME(reference));

        if (info->assoc_class) {
                pfx = class_prefix_name(info->assoc_class);

                if (!STREQC(ref_pfx, pfx) &&
                    !STREQC("CIM", pfx))
                        rc = false;

                free(pfx);
        }

        if (info->result_class) {
                pfx = class_prefix_name(info->result_class);

                if (!STREQC(ref_pfx, pfx) &&
                    !STREQC("CIM", pfx))
                        rc = false;

                free(pfx);
        }
        
        free(ref_pfx);
        return rc;
}



bool domain_online(virDomainPtr dom)
{
        virDomainInfo info;

        if (virDomainGetInfo(dom, &info) != 0)
                return false;

        return (info.state == VIR_DOMAIN_BLOCKED) ||
                (info.state == VIR_DOMAIN_RUNNING);
}

int parse_id(char *id, 
             char **pfx,
             char **name)
{
        int ret;
        char *tmp_pfx;
        char *tmp_name;

        ret = sscanf(id, "%a[^:]:%as", &tmp_pfx, &tmp_name);
        if (ret != 2) {
                ret = 0;
                goto out;
        }

        if (pfx)
                *pfx = strdup(tmp_pfx);

        if (name)
                *name = strdup(tmp_name);

        ret = 1;

 out:
        free(tmp_pfx);
        free(tmp_name);

        return ret;
}

bool parse_instanceid(const CMPIObjectPath *ref,
                      char **pfx,
                      char **name)
{
        int ret;
        char *id = NULL;

        id = cu_get_str_path(ref, "InstanceID");
        if (id == NULL)
                 return false;

        ret = parse_id(id, pfx, name);

        free(id);

        if (!ret)
                 return false;

        return true;
}

bool libvirt_cim_init(void)
{
        return virInitialize == 0;
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
