/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *  Heidi Eckhart <heidieck@linux.vnet.ibm.com>
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
#include <stdlib.h>
#include <string.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include "libcmpiutil.h"
#include "std_instance.h"

#include "cs_util.h"
#include "misc_util.h"
#include "device_parsing.h"

#include "Virt_VSSD.h"

const static CMPIBroker *_BROKER;

static int instance_from_dom(virDomainPtr dom,
                             CMPIInstance *inst)
{
        char *pfx = NULL;
        char *vsid = NULL;
        int ret = 1;
        CMPIObjectPath *op;
        struct domain *dominfo = NULL;

        ret = get_dominfo(dom, &dominfo);
        if (!ret)
                goto out;

        op = CMGetObjectPath(inst, NULL);
        pfx = class_prefix_name(CLASSNAME(op));

        CMSetProperty(inst, "VirtualSystemIdentifier",
                      (CMPIValue *)dominfo->name, CMPI_chars);

        CMSetProperty(inst, "ElementName",
                      (CMPIValue *)dominfo->name, CMPI_chars);

        CMSetProperty(inst, "VirtualSystemType",
                      (CMPIValue *)pfx, CMPI_chars);

        CMSetProperty(inst, "Caption",
                      (CMPIValue *)"Virtual System", CMPI_chars);

        CMSetProperty(inst, "Description",
                      (CMPIValue *)"Virtual System", CMPI_chars);

        CMSetProperty(inst, "AutomaticShutdownAction",
                      (CMPIValue *)&dominfo->on_poweroff, CMPI_uint16);

        CMSetProperty(inst, "AutomaticRecoveryAction",
                      (CMPIValue *)&dominfo->on_crash, CMPI_uint16);

        if (dominfo->bootloader)
                CMSetProperty(inst, "Bootloader",
                              (CMPIValue *)dominfo->bootloader,
                              CMPI_chars);

        if (dominfo->bootloader_args)
                CMSetProperty(inst, "BootloaderArgs",
                              (CMPIValue *)dominfo->bootloader_args,
                              CMPI_chars);

        if (asprintf(&vsid, "%s:%s", pfx, dominfo->name) == -1) {
                ret = 0;
                goto out;
        }

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)vsid, CMPI_chars);

 out:
        cleanup_dominfo(&dominfo);
        free(pfx);
        free(vsid);

        return ret;
}

CMPIInstance *get_vssd_instance(virDomainPtr dom,
                                const CMPIBroker *broker,
                                const CMPIObjectPath *ref)
{
        CMPIInstance *inst;

        inst = get_typed_instance(broker,
                                  "VirtualSystemSettingData",
                                  NAMESPACE(ref));

        if (inst == NULL)
                return NULL;

        if (instance_from_dom(dom, inst))
                return inst;
        else
                return NULL;
}

static CMPIStatus enum_vssd(const CMPIObjectPath *reference,
                            const CMPIResult *results,
                            int names_only)
{
        virConnectPtr conn;
        virDomainPtr *list;
        int count;
        int i;
        CMPIStatus s;
        const char *ns;

        if (!provider_is_responsible(_BROKER, reference, &s))
                return s;

        ns = NAMESPACE(reference);

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                return s;

        count = get_domain_list(conn, &list);
        if (count < 0) {
                CMSetStatusWithChars(_BROKER, &s,
                                     CMPI_RC_ERR_FAILED,
                                     "Failed to enumerate domains");
                goto out;
        } else if (count == 0) {
                CMSetStatus(&s, CMPI_RC_OK);
                goto out;
        }

        for (i = 0; i < count; i++) {
                CMPIInstance *inst;

                inst = get_vssd_instance(list[i], _BROKER, reference);
                virDomainFree(list[i]);
                if (inst == NULL)
                        continue;

                if (names_only)
                        cu_return_instance_name(results, inst);
                else
                        CMReturnInstance(results, inst);
        }

        CMSetStatus(&s, CMPI_RC_OK);
 out:
        free(list);

        return s;

}

static CMPIInstance *get_vssd_for_name(const CMPIObjectPath *reference,
                                       char *name)
{
        virConnectPtr conn;
        virDomainPtr dom;
        CMPIStatus s;
        CMPIInstance *inst = NULL;

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                return NULL;

        dom = virDomainLookupByName(conn, name);
        if (dom == NULL)
                goto out;

        inst = get_vssd_instance(dom, _BROKER, reference);

 out:
        virConnectClose(conn);
        virDomainFree(dom);

        return inst;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return enum_vssd(reference, results, 1);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{
        return enum_vssd(reference, results, 0);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        CMPIStatus s;
        CMPIInstance *inst;
        char *iid;
        char *orgid;
        char *locid;

        iid = cu_get_str_path(reference, "InstanceID");
        if (iid == NULL) {
                CMSetStatusWithChars(_BROKER, &s,
                            CMPI_RC_ERR_FAILED,
                            "InstanceID not specified");
                return s;
        }

        if (!parse_instance_id(iid, &orgid, &locid)) {
                CMSetStatusWithChars(_BROKER, &s,
                            CMPI_RC_ERR_FAILED,
                            "Invalid InstanceID specified");
                free(iid);
                return s;
        }

        inst = get_vssd_for_name(reference, locid);
        if (inst)
                CMReturnInstance(results, inst);

        CMSetStatus(&s, CMPI_RC_OK);

        free(iid);
        free(orgid);
        free(locid);

        return s;
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(, Virt_VSSDProvider, _BROKER, libvirt_cim_init());

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
