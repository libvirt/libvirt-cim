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

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_association.h>

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
                cu_statusf(broker, s, 
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

const char *get_key_from_ref_arg(const CMPIArgs *args, char *arg, char *key)
{
        CMPIObjectPath *ref = NULL;
        const char *val = NULL;

        if (cu_get_ref_arg(args, arg, &ref) != CMPI_RC_OK)
                return NULL;

        if (cu_get_str_path(ref, key, &val) != CMPI_RC_OK)
                return NULL;

        return val;
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
        CMPIData data;

        new_cn = get_typed_class(refcn, base);
        if (new_cn == NULL)
                goto out;

        op = CMNewObjectPath(broker, namespace, new_cn, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op))
                goto out;

        inst = CMNewInstance(broker, op, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(inst))
                goto out;
        
        data = CMGetProperty(inst, "CreationClassName", &s);
        if (s.rc == CMPI_RC_OK)
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

CMPIInstance *make_reference(const CMPIBroker *broker,
                             const CMPIObjectPath *source_ref,
                             const CMPIInstance *target_inst,
                             struct std_assoc_info *info,
                             struct std_assoc *assoc)
{
        CMPIInstance *ref_inst = NULL;
        char* assoc_classname;

        assoc_classname = class_base_name(assoc->assoc_class[0]);

        ref_inst = get_typed_instance(broker,
                                      CLASSNAME(source_ref),
                                      assoc_classname,
                                      NAMESPACE(source_ref));
        
        if (ref_inst != NULL) {
                CMPIObjectPath *target_ref;
                
                target_ref = CMGetObjectPath(target_inst, NULL);

                set_reference(assoc, ref_inst, 
                              source_ref, target_ref);
        }

        free(assoc_classname);

        return ref_inst;
}

bool domain_online(virDomainPtr dom)
{
        virDomainInfo info;
        virDomainPtr _dom;
        bool rc;

        _dom = virDomainLookupByName(virDomainGetConnect(dom),
                                     virDomainGetName(dom));
        if (_dom == NULL) {
                CU_DEBUG("Unable to re-lookup domain");
                return false;
        }

        if (virDomainGetInfo(_dom, &info) != 0)
                rc = false;
        else
                rc = (info.state == VIR_DOMAIN_BLOCKED) ||
                        (info.state == VIR_DOMAIN_RUNNING) ||
                        (info.state == VIR_DOMAIN_NOSTATE);
        virDomainFree(_dom);

        return rc;
}

int parse_id(const char *id,
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
        const char *id = NULL;

        if (cu_get_str_path(ref, "InstanceID", &id) != CMPI_RC_OK)
                 return false;

        ret = parse_id(id, pfx, name);

        if (!ret)
                 return false;

        return true;
}

bool libvirt_cim_init(void)
{
        return virInitialize == 0;
}

bool check_refs_pfx_match(const CMPIObjectPath *refa,
                          const CMPIObjectPath *refb)
{
        bool result = false;
        const char *refa_cn;
        const char *refb_cn;
        const char *ccn;
        char *refa_pfx = NULL;
        char *refb_pfx = NULL;

        refa_cn = CLASSNAME(refa);
        refb_cn = CLASSNAME(refb);

        if ((refa_cn == NULL) || (refb_cn == NULL)) {
                CU_DEBUG("Error getting ref classes %s:%s",
                         refa_cn, refb_cn);
                goto out;
        }

        refa_pfx = class_prefix_name(refa_cn);
        refb_pfx = class_prefix_name(refb_cn);

        if ((refa_pfx == NULL) || (refb_pfx == NULL)) {
                CU_DEBUG("Error getting ref prefixes %s:%s %s:%s",
                         refa_pfx, refb_pfx,
                         refa_cn, refb_cn);
                goto out;
        }

        if (!STREQC(refa_pfx, refb_pfx)) {
                CU_DEBUG("Ref mismatch: %s != %s",
                         refa_pfx,
                         refb_pfx);
                goto out;
        }

        if (cu_get_str_path(refb, "CreationClassName", &ccn) == CMPI_RC_OK) {
                if (!STREQC(ccn, refb_cn)) {
                        CU_DEBUG("ClassName(%s) != CreationClassName(%s)",
                                 refb_cn,
                                 ccn);
                        goto out;
                }
        }

        result = true;

 out:
        free(refa_pfx);
        free(refb_pfx);

        return result;
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
