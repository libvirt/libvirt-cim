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

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_instance.h>

#include "cs_util.h"
#include "misc_util.h"
#include "device_parsing.h"

#include "Virt_VSSD.h"

const static CMPIBroker *_BROKER;

static void _set_fv_prop(struct domain *dominfo,
                         CMPIInstance *inst)
{
        bool fv = true;

        if (dominfo->type == DOMAIN_XENFV)
                CMSetProperty(inst, "IsFullVirt",
                              (CMPIValue *)&fv, CMPI_boolean);

        if (dominfo->os_info.fv.boot != NULL)
                CMSetProperty(inst,
                              "BootDevice",
                              (CMPIValue *)dominfo->os_info.fv.boot,
                              CMPI_chars);
}

static void _set_pv_prop(struct domain *dominfo,
                         CMPIInstance *inst)
{
        bool fv = false;

        CMSetProperty(inst, "IsFullVirt",
                      (CMPIValue *)&fv, CMPI_boolean);

        if (dominfo->bootloader != NULL)
                CMSetProperty(inst, "Bootloader",
                              (CMPIValue *)dominfo->bootloader,
                              CMPI_chars);

        if (dominfo->bootloader_args != NULL)
                CMSetProperty(inst, "BootloaderArgs",
                              (CMPIValue *)dominfo->bootloader_args,
                              CMPI_chars);

        if (dominfo->os_info.pv.kernel != NULL)
                CMSetProperty(inst, "Kernel",
                              (CMPIValue *)dominfo->os_info.pv.kernel,
                              CMPI_chars);

        if (dominfo->os_info.pv.initrd != NULL)
                CMSetProperty(inst, "Ramdisk",
                              (CMPIValue *)dominfo->os_info.pv.initrd,
                              CMPI_chars);

        if (dominfo->os_info.pv.cmdline != NULL)
                CMSetProperty(inst, "CommandLine",
                              (CMPIValue *)dominfo->os_info.pv.cmdline,
                              CMPI_chars);
}

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

        if (dominfo->clock != NULL) {
                uint16_t clock = VSSD_CLOCK_UTC;

                if (STREQC(dominfo->clock, "localtime"))
                        clock = VSSD_CLOCK_LOC;

                CMSetProperty(inst, "ClockOffset",
                              (CMPIValue *)&clock, CMPI_uint16);
        }

        if ((dominfo->type == DOMAIN_XENFV) ||
            (dominfo->type == DOMAIN_KVM))
                _set_fv_prop(dominfo, inst);
        else if (dominfo->type == DOMAIN_XENPV)
                _set_pv_prop(dominfo, inst);
        else
                CU_DEBUG("Unknown domain type %i for creating VSSD",
                         dominfo->type);

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

static CMPIInstance *_get_vssd(const CMPIBroker *broker,
                               const CMPIObjectPath *reference,
                               virConnectPtr conn,
                               virDomainPtr dom,
                               CMPIStatus *s)
{
        CMPIInstance *inst = NULL;
        
        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "VirtualSystemSettingData",
                                  NAMESPACE(reference));

        if (inst == NULL) {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to init VirtualSystemSettingData instance");
                goto out;
        }
        
        if (instance_from_dom(dom, inst) != 1) {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get VSSD instance from Domain");
        }
        
 out:
        return inst;
}

static CMPIStatus return_enum_vssd(const CMPIObjectPath *reference,
                                   const CMPIResult *results,
                                   bool names_only)
{
        virConnectPtr conn;
        virDomainPtr *list;
        int count;
        int i;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        
        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                return s;

        count = get_domain_list(conn, &list);
        if (count < 0) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to enumerate domains");
                goto out;
        } else if (count == 0)
                goto out;

        for (i = 0; i < count; i++) {
                CMPIInstance *inst = NULL;
                
                inst = _get_vssd(_BROKER, reference, conn, list[i], &s);
                
                virDomainFree(list[i]);
                if (inst == NULL)
                        continue;

                if (names_only)
                        cu_return_instance_name(results, inst);
                else
                        CMReturnInstance(results, inst);
        }

 out:
        free(list);

        return s;
}

CMPIStatus get_vssd_by_name(const CMPIBroker *broker,
                            const CMPIObjectPath *reference,
                            const char *name,
                            CMPIInstance **_inst)
{
        virConnectPtr conn;
        virDomainPtr dom;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        
        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance");
                goto out;
        }
        
        dom = virDomainLookupByName(conn, name);
        if (dom == NULL) {
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_NOT_FOUND,
                                conn,
                                "No such instance (%s)",
                                name);
                goto out;
        }
        
        inst = _get_vssd(broker, reference, conn, dom, &s);
        
        virDomainFree(dom);

        if (s.rc != CMPI_RC_OK)
                goto out;

        *_inst = inst;
        
 out:
        virConnectClose(conn);

        return s;
}

CMPIStatus get_vssd_by_ref(const CMPIBroker *broker,
                           const CMPIObjectPath *reference,
                           CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        char *name = NULL;
        
        if (!parse_instanceid(reference, NULL, &name)) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (InstanceID)");
                goto out;
        }
        
        s = get_vssd_by_name(broker, reference, name, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;
        
        s = cu_validate_ref(broker, reference, inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        *_inst = inst;
        
 out:
        free(name);
        
        return s;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_enum_vssd(reference, results, true);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{
        return return_enum_vssd(reference, results, false);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        CMPIStatus s;
        CMPIInstance *inst = NULL;

        s = get_vssd_by_ref(_BROKER, reference, &inst);
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
                   Virt_VSSD,
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
