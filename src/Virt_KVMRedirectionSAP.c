/*
 * Copyright IBM Corp. 2008
 *
 * Authors:
 *  Kaitlin Rupert <karupert@us.ibm.com>
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

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include "misc_util.h"
#include "svpc_types.h"
#include "device_parsing.h"
#include "cs_util.h"

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_invokemethod.h>
#include <libcmpiutil/std_instance.h>

#include "Virt_KVMRedirectionSAP.h"

const static CMPIBroker *_BROKER;

static int inst_from_dom(const CMPIBroker *broker,
                         const CMPIObjectPath *ref,
                         struct domain *dominfo,
                         CMPIInstance *inst)
{
        char *sccn = NULL;
        char *id = NULL;
        char *pfx = NULL;
        uint16_t prop_val;
        int ret = 1;

        if (asprintf(&id, "%s:%s", dominfo->name,
                     dominfo->dev_graphics->dev.graphics.type) == -1) { 
                CU_DEBUG("Unable to format name");
                ret = 0;
                goto out;
        }

        pfx = class_prefix_name(CLASSNAME(ref));
        sccn = get_typed_class(pfx, "ComputerSystem");

        CMSetProperty(inst, "Name",
                      (CMPIValue *)id, CMPI_chars);

        CMSetProperty(inst, "SystemName",
                      (CMPIValue *)dominfo->name, CMPI_chars);

        CMSetProperty(inst, "SystemCreationClassName",
                      (CMPIValue *)sccn, CMPI_chars);

        CMSetProperty(inst, "ElementName",
                      (CMPIValue *)id, CMPI_chars);

        if (STREQ(dominfo->dev_graphics->dev.graphics.type, "vnc"))
                prop_val = (uint16_t)CIM_CRS_VNC;
        else
                prop_val = (uint16_t)CIM_CRS_OTHER;

        CMSetProperty(inst, "KVMProtocol",
                      (CMPIValue *)&prop_val, CMPI_uint16);

        /* Need to replace this with a check that determines whether
           the console session is enabled (in use) or available (not actively
           in use).
         */
        prop_val = (uint16_t)CIM_CRS_ENABLED_STATE;
        CMSetProperty(inst, "EnabledState",
                      (CMPIValue *)&prop_val, CMPI_uint16);

 out:
        free(pfx);
        free(id);
        free(sccn);

        return ret;
}

static CMPIInstance *get_console_sap(const CMPIBroker *broker,
                                     const CMPIObjectPath *reference,
                                     virConnectPtr conn,
                                     struct domain *dominfo,
                                     CMPIStatus *s)

{ 
        CMPIInstance *inst = NULL;

        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "KVMRedirectionSAP",
                                  NAMESPACE(reference));

        if (inst == NULL) {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to create instance");
                goto out;
        }

        if (inst_from_dom(broker, reference, dominfo, inst) != 1) {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get instance from domain");
        }

 out:
        return inst;
}

static bool check_graphics(virDomainPtr dom,
                           struct domain **dominfo)
{
        int ret = 0;

        ret = get_dominfo(dom, dominfo);
        if (!ret) {
                CU_DEBUG("Unable to get domain info");
                return false;
        }

        if ((*dominfo)->dev_graphics == NULL) {
                CU_DEBUG("No graphics device associated with guest");
                return false;
        }

        return true;
}

static CMPIStatus return_console_sap(const CMPIObjectPath *ref,
                                     const CMPIResult *results,
                                     bool names_only)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn;
        virDomainPtr *domain_list;
        struct domain *dominfo = NULL;
        struct inst_list list;
        int count;
        int i;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                return s;

        inst_list_init(&list);

        count = get_domain_list(conn, &domain_list);
        if (count < 0) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to enumerate domains");
                goto out;
        } else if (count == 0)
                goto out;

        for (i = 0; i < count; i++) {
                CMPIInstance *inst = NULL;

                if (!check_graphics(domain_list[i], &dominfo)) {
                        virDomainFree(domain_list[i]);
                        cleanup_dominfo(&dominfo);
                        continue;
                }

                inst = get_console_sap(_BROKER, ref, conn, dominfo, &s);

                virDomainFree(domain_list[i]);
                cleanup_dominfo(&dominfo);

                if (inst != NULL)
                        inst_list_add(&list, inst);
        }

        if (names_only)
                cu_return_instance_names(results, &list);
        else
                cu_return_instances(results, &list);

 out:
        free(domain_list);
        inst_list_free(&list);

        return s;
}

CMPIStatus get_console_sap_by_name(const CMPIBroker *broker,
                                   const CMPIObjectPath *ref,
                                   const char *name,
                                   CMPIInstance **_inst)
{
        virConnectPtr conn;
        virDomainPtr dom;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        struct domain *dominfo = NULL;

        conn = connect_by_classname(broker, CLASSNAME(ref), &s);
        if (conn == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance");
                goto out;
        }

        dom = virDomainLookupByName(conn, name);
        if (dom == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s)",
                           name);
                goto out;
        }

        if (!check_graphics(dom, &dominfo)) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "No console device for this guest");
        }

        inst = get_console_sap(_BROKER, ref, conn, dominfo, &s);

        virDomainFree(dom);

        if (s.rc != CMPI_RC_OK)
                goto out;

        *_inst = inst;

 out:
        virConnectClose(conn);

        return s;
}

CMPIStatus get_console_sap_by_ref(const CMPIBroker *broker,
                                  const CMPIObjectPath *reference,
                                  CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        const char *sys = NULL;

        if (cu_get_str_path(reference, "System", &sys) != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (System)");
                goto out;
        }

        s = get_console_sap_by_name(broker, reference, sys, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = cu_validate_ref(broker, reference, inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        *_inst = inst;

 out:
        return s;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_console_sap(reference, results, true);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{

        return return_console_sap(reference, results, false);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *ref,
                              const char **properties)
{
        CMPIStatus s;
        CMPIInstance *inst = NULL;

        s = get_console_sap_by_ref(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        CMReturnInstance(results, inst);

 out:
        return s;
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(, 
                   Virt_KVMRedirectionSAP, 
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
