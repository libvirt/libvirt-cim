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
#include <stdlib.h>
#include <string.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libvirt/libvirt.h>

#include "cs_util.h"
#include <libcmpiutil/libcmpiutil.h>
#include "misc_util.h"
#include <libcmpiutil/std_invokemethod.h>
#include <libcmpiutil/std_instance.h>

#include "Virt_ComputerSystem.h"

const static CMPIBroker *_BROKER;

enum CIM_state {
        CIM_STATE_UNKNOWN      = 0,
        CIM_STATE_ENABLED      = 2,
        CIM_STATE_DISABLED     = 3,
        CIM_STATE_SHUTDOWN     = 4,
        CIM_STATE_NOCHANGE     = 5,
        CIM_STATE_SUSPENDED    = 6,
        CIM_STATE_PAUSED       = 9,
        CIM_STATE_REBOOT       = 10,
        CIM_STATE_RESET        = 11,
};

/* Set the "Name" property of an instance from a domain */
static int set_name_from_dom(virDomainPtr dom, CMPIInstance *instance)
{
        const char *name;

        name = virDomainGetName(dom);
        if (name == NULL)
                return 0;

        CMSetProperty(instance, "Name",
                      (CMPIValue *)name, CMPI_chars);

        CMSetProperty(instance, "ElementName",
                      (CMPIValue *)name, CMPI_chars);

        return 1;
}

/* Set the "UUID" property of an instance from a domain */
static int set_uuid_from_dom(virDomainPtr dom, 
                             CMPIInstance *instance, 
                             char **out_uuid)
{
        char uuid[VIR_UUID_STRING_BUFLEN];
        int ret;

        ret = virDomainGetUUIDString(dom, uuid);
        if (ret != 0)
                return 0;

        CMSetProperty(instance, "UUID",
                      (CMPIValue *)uuid, CMPI_chars);

        *out_uuid = strdup(uuid);

        return 1;
}

static int set_capdesc_from_dom(virDomainPtr dom, CMPIInstance *instance)
{
        CMSetProperty(instance, "Caption",
                      (CMPIValue *)"Virtual System", CMPI_chars);

        CMSetProperty(instance, "Description",
                      (CMPIValue *)"Virtual System", CMPI_chars);

        return 1;
}

static uint16_t state_lv_to_cim(const char lv_state)
{
        if (lv_state == VIR_DOMAIN_NOSTATE)
                return CIM_STATE_UNKNOWN;
        else if (lv_state == VIR_DOMAIN_RUNNING)
                return CIM_STATE_ENABLED;
        else if (lv_state == VIR_DOMAIN_BLOCKED)
                return CIM_STATE_ENABLED;
        else if (lv_state == VIR_DOMAIN_PAUSED)
                return CIM_STATE_PAUSED;
        else if (lv_state == VIR_DOMAIN_SHUTDOWN)
                return CIM_STATE_SHUTDOWN;
        else if (lv_state == VIR_DOMAIN_SHUTOFF)
                return CIM_STATE_DISABLED;
        else if (lv_state == VIR_DOMAIN_CRASHED)
                return CIM_STATE_DISABLED;
        else
                return CIM_STATE_UNKNOWN;
}

static uint16_t state_lv_to_cim_health(const char lv_state)
{
        enum CIM_health_state {
                CIM_HEALTH_UNKNOWN = 0,
                CIM_HEALTH_OK = 5,
                CIM_HEALTH_MINOR_FAILURE = 15,
                CIM_HEALTH_MAJOR_FAILURE = 20,
                CIM_HEALTH_CRITICAL_FAILURE = 25,
                CIM_HEALTH_NON_RECOVERABLE = 30,
        };

        switch (lv_state) {
        case VIR_DOMAIN_NOSTATE:
        case VIR_DOMAIN_SHUTDOWN:
        case VIR_DOMAIN_SHUTOFF:
                return CIM_HEALTH_UNKNOWN;

        case VIR_DOMAIN_CRASHED:
                return CIM_HEALTH_MAJOR_FAILURE;

        case VIR_DOMAIN_RUNNING:
        case VIR_DOMAIN_BLOCKED:
        case VIR_DOMAIN_PAUSED:
                return CIM_HEALTH_OK;

        default:
                return CIM_HEALTH_UNKNOWN;
        }
}

static uint16_t state_lv_to_cim_os(const char lv_state)
{
        enum CIM_op_status {
                CIM_OP_STATUS_UNKNOWN = 0,
                CIM_OP_STATUS_OTHER = 1,
                CIM_OP_STATUS_OK = 2,
                CIM_OP_STATUS_DEGRADED = 3,
                CIM_OP_STATUS_STRESSED = 4,
                CIM_OP_STATUS_PREDICTIVE_FAILURE = 5,
                CIM_OP_STATUS_ERROR = 6,
                CIM_OP_STATUS_NON_RECOVERABLE = 7,
                CIM_OP_STATUS_STARTING = 8,
                CIM_OP_STATUS_STOPPING = 9,
                CIM_OP_STATUS_STOPPED = 10,
                CIM_OP_STATUS_IN_SERVICE = 11,
                CIM_OP_STATUS_NO_CONTACT = 12,
                CIM_OP_STATUS_LOST_COMMS = 13,
                CIM_OP_STATUS_ABORTED = 14,
                CIM_OP_STATUS_DORMANT = 15,
                CIM_OP_STATUS_COMPLETED = 17,
                CIM_OP_STATUS_POWER_MODE = 18,
        };

        switch (lv_state) {
        case VIR_DOMAIN_NOSTATE:
        case VIR_DOMAIN_SHUTDOWN:
        case VIR_DOMAIN_SHUTOFF:
                return CIM_OP_STATUS_DORMANT;

        case VIR_DOMAIN_CRASHED:
                return CIM_OP_STATUS_ERROR;

        case VIR_DOMAIN_RUNNING:
        case VIR_DOMAIN_BLOCKED:
        case VIR_DOMAIN_PAUSED:
                return CIM_OP_STATUS_OK;

        default:
                return CIM_OP_STATUS_UNKNOWN;

        }
}

static int set_state_from_dom(const CMPIBroker *broker,
                              virDomainPtr dom,
                              CMPIInstance *instance)
{
        virDomainInfo info;
        int ret;
        uint16_t cim_state;
        uint16_t health_state;
        uint16_t op_status;
        CMPIArray *array;
        CMPIStatus s;

        ret = virDomainGetInfo(dom, &info);
        if (ret != 0)
                return 0;

        cim_state = state_lv_to_cim((const int)info.state);
        CMSetProperty(instance, "EnabledState",
                      (CMPIValue *)&cim_state, CMPI_uint16);

        health_state = state_lv_to_cim_health((const int)info.state);
        CMSetProperty(instance, "HealthState",
                      (CMPIValue *)&health_state, CMPI_uint16);

        array = CMNewArray(broker, 1, CMPI_uint16, &s);
        if ((s.rc != CMPI_RC_OK) || (CMIsNullObject(array)))
                return 0;

        op_status = state_lv_to_cim_os((const int)info.state);
        CMSetArrayElementAt(array, 0, &op_status, CMPI_uint16);

        CMSetProperty(instance, "OperationalStatus",
                      (CMPIValue *)&array, CMPI_uint16A);

        return 1;
}

static int set_creation_class(CMPIInstance *instance)
{
        CMPIObjectPath *op;

        op = CMGetObjectPath(instance, NULL);

        CMSetProperty(instance, "CreationClassName",
                      (CMPIValue *)CLASSNAME(op), CMPI_chars);

        return 1;
}

static int set_other_id_info(const CMPIBroker *broker,
                             char *uuid,
                             const char *prefix,
                             CMPIInstance *instance)
{
        CMPIStatus s;
        CMPIArray *id_info;
        CMPIArray *id_desc;
        char *desc[3] = {"Type", "Model", "UUID"};
        char *info[3];
        int count = 3; 
        char *type = "Virtual System";
        char *model;
        int i;

        id_info = CMNewArray(broker,
                             count,
                             CMPI_string,
                             &s);

        if (s.rc != CMPI_RC_OK)
                return 0;

        id_desc = CMNewArray(broker,
                             count,
                             CMPI_string,
                             &s);

        if (s.rc != CMPI_RC_OK)
                return 0;

        if (asprintf(&model, "%s %s", prefix, type) == -1)
                return 0;

        info[0] = type;
        info[1] = model;
        info[2] = uuid;

        for (i = 0; i < count; i++) {
                CMPIString *tmp = CMNewString(broker, info[i], NULL);
                CMSetArrayElementAt(id_info, i,
                                    &tmp,
                                    CMPI_string);

                tmp = CMNewString(broker, desc[i], NULL);
                CMSetArrayElementAt(id_desc, i,
                                    &tmp,
                                    CMPI_string);
        }

        CMSetProperty(instance, "OtherIdentifyingInfo",
                      &id_info, CMPI_stringA);

        CMSetProperty(instance, "IdentifyingDescriptions",
                      (CMPIValue *)&id_desc, CMPI_stringA);
        return 1;
}

/* Populate an instance with information from a domain */
static CMPIStatus set_properties(const CMPIBroker *broker,
                                 virDomainPtr dom,
                                 const char *prefix,
                                 CMPIInstance *instance)
{
        CMPIStatus s = {CMPI_RC_ERR_FAILED, NULL};
        char *uuid = NULL;

        if (!set_name_from_dom(dom, instance)) {
                /* Print trace error */
                goto out;
        }

        if (!set_uuid_from_dom(dom, instance, &uuid)) {
                /* Print trace error */
                goto out;
        }

        if (!set_capdesc_from_dom(dom, instance)) {
                /* Print trace error */
                goto out;
        }

        if (!set_state_from_dom(broker, dom, instance)) {
                /* Print trace error */
                goto out;
        }

        if (!set_creation_class(instance)) {
                /* Print trace error */
                goto out;
        }

        if (!set_other_id_info(broker, uuid, prefix, instance)) {
                /* Print trace error */
                goto out;
        }

        /* More attributes here, of course */

        cu_statusf(broker, &s,
                   CMPI_RC_OK,
                   "");

 out:
        free(uuid);

        return s;
}

static CMPIStatus instance_from_dom(const CMPIBroker *broker,
                                     const CMPIObjectPath *reference,
                                     virConnectPtr conn,
                                     virDomainPtr domain,
                                     CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "ComputerSystem",
                                  NAMESPACE(reference));
        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to init ComputerSystem instance");
                goto out;
        }

        s = set_properties(broker,
                           domain, 
                           pfx_from_conn(conn), 
                           inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        *_inst = inst;

 out:
        return s;
}

CMPIStatus enum_domains(const CMPIBroker *broker,
                        const CMPIObjectPath *reference,
                        struct inst_list *instlist)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virDomainPtr *list = NULL;
        virConnectPtr conn = NULL;
        int count;
        int i;

        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        count = get_domain_list(conn, &list);
        if (count < 0) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get domain list");
                goto out;
        }

        for (i = 0; i < count; i++) {
                CMPIInstance *inst = NULL;
                
                s = instance_from_dom(broker,
                                      reference,
                                      conn,
                                      list[i],  
                                      &inst);
                if (s.rc != CMPI_RC_OK)
                        goto end;

                inst_list_add(instlist, inst);

          end:
                virDomainFree(list[i]);
        }

 out:
        virConnectClose(conn);
        free(list);

        return s;
}

static CMPIStatus return_enum_domains(const CMPIObjectPath *reference,
                                      const CMPIResult *results,
                                      bool names_only)
{
        struct inst_list list;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        inst_list_init(&list);

        s = enum_domains(_BROKER, reference, &list);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (names_only)
                cu_return_instance_names(results, &list);
        else
                cu_return_instances(results, &list);

 out:
        inst_list_free(&list);

        return s;
}

CMPIStatus get_domain_by_name(const CMPIBroker *broker,
                              const CMPIObjectPath *reference,
                              const char *name,
                              CMPIInstance **_inst)
{
        CMPIInstance *inst = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;
        virDomainPtr dom;

        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance.");
                return s;
        }

        dom = virDomainLookupByName(conn, name);
        if (dom == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s).",
                           name);
                goto out;
        }

        s = instance_from_dom(broker,
                              reference,
                              conn,
                              dom,  
                              &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        *_inst = inst;

 out:
        virDomainFree(dom);
        virConnectClose(conn);

        return s;
}

CMPIStatus get_domain_by_ref(const CMPIBroker *broker,
                             const CMPIObjectPath *reference,
                             CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        const char *name = NULL;

        if (cu_get_str_path(reference, "Name", &name) != CMPI_RC_OK) {                
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "No domain name specified");
                goto out;
        }

        s = get_domain_by_name(broker, reference, name, &inst);
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
        return return_enum_domains(reference, results, true);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{

        return return_enum_domains(reference, results, false);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst;
        
        s = get_domain_by_ref(_BROKER, reference, &inst);
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

/* This composite operation may be supported as a flag to reboot */
static int domain_reset(virDomainPtr dom)
{
        int ret;

        ret = virDomainDestroy(dom);
        if (ret)
                return ret;

        ret = virDomainCreate(dom);

        return ret;
}

static CMPIStatus state_change_enable(virDomainPtr dom, virDomainInfoPtr info)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int ret = 0;

        switch (info->state) {
        case VIR_DOMAIN_SHUTOFF:
                CU_DEBUG("Start domain");
                ret = virDomainCreate(dom);
                break;
        case VIR_DOMAIN_PAUSED:
                CU_DEBUG("Unpause domain");
                ret = virDomainResume(dom);
                break;
        default:
                CU_DEBUG("Cannot go to enabled state from %i", info->state);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid state transition");
        };

        if (ret != 0)
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Domain Operation Failed");

        return s;
}

static CMPIStatus state_change_disable(virDomainPtr dom, virDomainInfoPtr info)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int ret = 0;

        switch (info->state) {
        case VIR_DOMAIN_RUNNING:
        case VIR_DOMAIN_BLOCKED:
                CU_DEBUG("Stop domain");
                ret = virDomainShutdown(dom);
                break;
        default:
                CU_DEBUG("Cannot go to disabled state from %i", info->state);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid state transition");
        };

        if (ret != 0)
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Domain Operation Failed");

        return s;
}

static CMPIStatus state_change_pause(virDomainPtr dom, virDomainInfoPtr info)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int ret = 0;

        switch (info->state) {
        case VIR_DOMAIN_RUNNING:
        case VIR_DOMAIN_BLOCKED:
                CU_DEBUG("Pause domain");
                ret = virDomainSuspend(dom);
                break;
        default:
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Domain not running");
        };

        if (ret != 0)
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Domain Operation Failed");

        return s;
}

static CMPIStatus state_change_reboot(virDomainPtr dom, virDomainInfoPtr info)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int ret = 0;

        switch (info->state) {
        case VIR_DOMAIN_RUNNING:
        case VIR_DOMAIN_BLOCKED:
                CU_DEBUG("Reboot domain");
                ret = virDomainReboot(dom, 0);
                break;
        default:
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Domain not running");
        };

        if (ret != 0)
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Domain Operation Failed");

        return s;
}

static CMPIStatus state_change_reset(virDomainPtr dom, virDomainInfoPtr info)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int ret = 0;

        switch (info->state) {
        case VIR_DOMAIN_RUNNING:
        case VIR_DOMAIN_BLOCKED:
                CU_DEBUG("Reset domain");
                ret = domain_reset(dom);
                break;
        default:
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Domain not running");
        };

        if (ret != 0)
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Domain Operation Failed");

        return s;
}

static CMPIStatus __state_change(const char *name,
                                 uint16_t state,
                                 const CMPIObjectPath *ref)
{
        CMPIStatus s;
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        virDomainInfo info;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        dom = virDomainLookupByName(conn, name);
        if (dom == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Domain not found");
                goto out;
        }

        if (virDomainGetInfo(dom, &info) != 0) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get current state");
                goto out;
        }

        if (state == CIM_STATE_ENABLED)
                s = state_change_enable(dom, &info);
        else if (state == CIM_STATE_DISABLED)
                s = state_change_disable(dom, &info);
        else if (state == CIM_STATE_PAUSED)
                s = state_change_pause(dom, &info);
        else if (state == CIM_STATE_REBOOT)
                s = state_change_reboot(dom, &info);
        else if (state == CIM_STATE_RESET)
                s = state_change_reset(dom, &info);
        else
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_SUPPORTED,
                           "State not supported");

 out:
        virDomainFree(dom);
        virConnectClose(conn);

        return s;
}

static CMPIStatus state_change(CMPIMethodMI *self,
                               const CMPIContext *context,
                               const CMPIResult *results,
                               const CMPIObjectPath *reference,
                               const CMPIArgs *argsin,
                               CMPIArgs *argsout)
{
        CMPIStatus s;
        uint16_t state;
        int ret;
        const char *name = NULL;

        ret = cu_get_u16_arg(argsin, "RequestedState", &state);
        if (ret != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Invalid RequestedState");
                goto out;
        }

        if (cu_get_str_path(reference, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Name key not specified");
                goto out;
        }

        s = __state_change(name, state, reference);

 out:
        return s;
}

STD_InstanceMIStub(, 
                   Virt_ComputerSystem, 
                   _BROKER, 
                   libvirt_cim_init());

static struct method_handler RequestStateChange = {
        .name = "RequestStateChange",
        .handler = state_change,
        .args = {{"RequestedState", CMPI_uint16, false},
                 {"TimeoutPeriod", CMPI_dateTime, false},
                 ARG_END
        }
};

static struct method_handler *my_handlers[] = {
        &RequestStateChange,
        NULL
};

STDIM_MethodMIStub(,
                   Virt_ComputerSystem,
                   _BROKER,
                   libvirt_cim_init(),
                   my_handlers);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
