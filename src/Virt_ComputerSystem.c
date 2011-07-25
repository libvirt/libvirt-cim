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
#include <unistd.h>
#include <limits.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libvirt/libvirt.h>

#include "cs_util.h"
#include <libcmpiutil/libcmpiutil.h>
#include "misc_util.h"
#include "infostore.h"
#include "device_parsing.h"
#include <libcmpiutil/std_invokemethod.h>
#include <libcmpiutil/std_instance.h>
#include <libcmpiutil/std_indication.h>

#include "Virt_ComputerSystem.h"
#include "Virt_HostSystem.h"
#include "Virt_VirtualSystemSnapshotService.h"

const static CMPIBroker *_BROKER;

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

static int set_capdesc_from_dominfo(const CMPIBroker *broker,
                                    struct domain *domain,
                                    const CMPIObjectPath *ref,
                                    CMPIInstance *instance)
{
        char *cap = NULL;
        int ret;
        char host[HOST_NAME_MAX];

        if (gethostname(host, sizeof(host)) != 0) {
                CU_DEBUG("Unable to get hostname: %m");
                strcpy(host, "localhost");
        }

        if (domain->dev_graphics_ct > 0)
                ret = asprintf(&cap,
                               "Virtual System (Console on %s://%s:%s)",
                               domain->dev_graphics[0].dev.graphics.type,
                               host,
                               domain->dev_graphics[0].dev.graphics.dev.vnc.port);
        else
                ret = asprintf(&cap,
                               "Virtual System (No console)");

        if (ret == -1) {
                CU_DEBUG("Failed to create caption string");
                goto out;
        }

        CMSetProperty(instance, "Caption",
                      (CMPIValue *)cap, CMPI_chars);

        CMSetProperty(instance, "Description",
                      (CMPIValue *)"Virtual System", CMPI_chars);
 out:
        free(cap);

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

static uint16_t state_lv_to_cim_oings(const char lv_state, const bool migrating)
{
        if (migrating)
                return CIM_OPING_STATUS_MIGRATING;

        switch (lv_state) {
        case VIR_DOMAIN_NOSTATE:
        case VIR_DOMAIN_SHUTDOWN:
        case VIR_DOMAIN_SHUTOFF:
                return CIM_OPING_STATUS_STOPPED;

        case VIR_DOMAIN_CRASHED:
                return CIM_OPING_STATUS_ABORTED;

        case VIR_DOMAIN_RUNNING:
                return CIM_OPING_STATUS_STARTED;

        case VIR_DOMAIN_BLOCKED:
        case VIR_DOMAIN_PAUSED:
                return CIM_OPING_STATUS_DORMANT;

        default:
                return CIM_OPING_STATUS_UNKNOWN;
        }
}

static uint16_t state_lv_to_cim_os(const char lv_state)
{
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

static unsigned char adjust_state_xen(virDomainPtr dom,
                                      unsigned char state)
{
        virConnectPtr conn;

        if (state != VIR_DOMAIN_NOSTATE)
                return state;

        conn = virDomainGetConnect(dom);

        if (STREQC(virConnectGetType(conn), "Xen"))
           return VIR_DOMAIN_RUNNING;

        return state;
}

static uint16_t adjust_state_if_saved(const char *name,
                                      uint16_t state)
{
        if (state != CIM_STATE_DISABLED)
                return state;

        if (vsss_has_save_image(name))
                return CIM_STATE_SUSPENDED;

        return state;
}

static int set_state_from_dom(const CMPIBroker *broker,
                              virDomainPtr dom,
                              CMPIInstance *instance)
{
        virDomainInfo info;
        int ret;
        uint16_t cim_state;
        uint16_t health_state;
        uint16_t req_state;
        uint16_t op_status;
        uint16_t oping_status;
        CMPIArray *array;
        CMPIStatus s;
        struct infostore_ctx *infostore = NULL;
        bool migrating = false;

        ret = virDomainGetInfo(dom, &info);
        if (ret != 0) 
                return 0;

        info.state = adjust_state_xen(dom, info.state);

        cim_state = state_lv_to_cim((const int)info.state);
        cim_state = adjust_state_if_saved(virDomainGetName(dom), cim_state);
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

        infostore = infostore_open(dom);

        if (infostore != NULL) 
                migrating = infostore_get_bool(infostore, "migrating");

        oping_status = state_lv_to_cim_oings((const int)info.state, migrating);
        CMSetProperty(instance, "OperatingStatus",
                      (CMPIValue *)&oping_status, CMPI_uint16);

        if (infostore != NULL)
                req_state = (uint16_t)infostore_get_u64(infostore, "reqstate");
        else
                req_state = CIM_STATE_UNKNOWN;

        CMSetProperty(instance, "RequestedState",
                      (CMPIValue *)&req_state, CMPI_uint16);

        infostore_close(infostore);

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

static CMPIStatus set_properties_from_dominfo(const CMPIBroker *broker,
                                              const char *prefix,
                                              struct domain *dominfo,
                                              CMPIInstance *instance)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIObjectPath *ref = NULL;

        ref = CMGetObjectPath(instance, &s);
        if ((ref == NULL) || (s.rc != CMPI_RC_OK))
                return s;

        CMSetProperty(instance, "Name",
                      (CMPIValue *)dominfo->name, CMPI_chars);

        CMSetProperty(instance, "ElementName",
                      (CMPIValue *)dominfo->name, CMPI_chars);

        CMSetProperty(instance, "UUID",
                      (CMPIValue *)dominfo->uuid, CMPI_chars);

        if (!set_capdesc_from_dominfo(broker, dominfo, ref, instance)) {
                CU_DEBUG("Problem in set_capdesc_from_dominfo function");
                cu_statusf(broker, &s, 
                           CMPI_RC_ERR_FAILED, 
                           "Could not set caption and description properties");
                goto out;
        }

        /* We don't set state, because struct domain doesn't have that
         * information */

        if (!set_creation_class(instance)) {
                CU_DEBUG("Problem in set_creation_class function");
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Could not set creation class");
                goto out;
        }

        if (!set_other_id_info(broker, dominfo->uuid, prefix, instance)) {
                CU_DEBUG("Problem in set_other_id_info function");
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Could not set other OtherIdentifyingInfo and "
                           "IdentifyingDescription");
                goto out;
        }

 out:
        return s;
}

CMPIStatus instance_from_dominfo(const CMPIBroker *broker,
                                 const char *namespace,
                                 const char *prefix,
                                 struct domain *dominfo,
                                 CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        inst = get_typed_instance(broker,
                                  prefix,
                                  "ComputerSystem",
                                  namespace);

        if (inst == NULL) {
                CU_DEBUG("Could not init CS instance. "
                         "typestr: %s, namespace: %s", prefix, namespace);
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to init ComputerSystem instance");
                goto out;
        }
       
        s = set_properties_from_dominfo(broker,
                                        prefix,
                                        dominfo,
                                        inst);
        if (s.rc != CMPI_RC_OK) {
                CU_DEBUG("Could not set instance properties");
                goto out;
        }

        *_inst = inst;

 out:
        return s;

}

/* Populate an instance with information from a domain */
static CMPIStatus set_properties(const CMPIBroker *broker,
                                 virDomainPtr dom,
                                 const char *prefix,
                                 CMPIInstance *instance)
{
        CMPIStatus s = {CMPI_RC_ERR_FAILED, NULL};
        char *uuid = NULL;
        struct domain *domain = NULL;
        CMPIObjectPath *ref = NULL;

        ref = CMGetObjectPath(instance, &s);
        if ((ref == NULL) || (s.rc != CMPI_RC_OK))
                return s;

        if (get_dominfo(dom, &domain) == 0) {
                CU_DEBUG("Unable to get domain information");
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_FAILED,
                                virDomainGetConnect(dom),
                                "Unable to get domain information");
                goto out;
        }

        if (!set_name_from_dom(dom, instance)) {
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_FAILED,
                                virDomainGetConnect(dom),
                                "Unable to get domain name");
                goto out;
        }

        if (!set_uuid_from_dom(dom, instance, &uuid)) {
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_FAILED,
                                virDomainGetConnect(dom),
                                "Unable to get domain UUID");

                goto out;
        }

        if (!set_capdesc_from_dominfo(broker, domain, ref, instance)) {
                /* Print trace error */
                goto out;
        }

        if (!set_state_from_dom(broker, dom, instance)) {
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_FAILED,
                                virDomainGetConnect(dom),
                                "Unable to get domain info");

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
        cleanup_dominfo(&domain);

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
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_NOT_FOUND,
                                conn,
                                "Referenced domain `%s' does not exist", 
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

static bool trigger_mod_indication(const CMPIContext *context,
                                   CMPIInstance *prev_inst,
                                   const CMPIObjectPath *ref)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        const char *ind_name = "ComputerSystemModifiedIndication";
        CMPIInstance *ind = NULL;
        char *type = NULL;

        CU_DEBUG("Preparing ComputerSystem indication");

        ind = get_typed_instance(_BROKER,
                                 CLASSNAME(ref),
                                 ind_name,
                                 NAMESPACE(ref));
        if (ind == NULL) {
                CU_DEBUG("Failed to create ind '%s'", ind_name);
                goto out;
        }

        CU_DEBUG("Setting PreviousInstance");
        CMSetProperty(ind, "PreviousInstance",
                      (CMPIValue *)&prev_inst, CMPI_instance);

        type = get_typed_class(CLASSNAME(ref), ind_name);

        s = stdi_raise_indication(_BROKER, context, type, NAMESPACE(ref), ind);

 out:
        free(type);
        return s.rc == CMPI_RC_OK;
}

static int xen_scheduler_params(struct infostore_ctx *ctx,
                                virSchedParameter **params)
{
        unsigned long long weight;
        unsigned long long cap;
        int nparams = 0;

        *params = calloc(2, sizeof(virSchedParameter));
        if (*params == NULL)
                return -1;

        weight = infostore_get_u64(ctx, "weight");
        cap = infostore_get_u64(ctx, "limit");

        if (weight != 0) {
                strncpy((*params)[0].field,
                        "weight",
                        sizeof((*params)[0].field));
                (*params)[0].type = VIR_DOMAIN_SCHED_FIELD_UINT;
                (*params)[0].value.ui = weight;
                nparams++;
        }

        if (cap != 0) {
                strncpy((*params)[0].field,
                        "cap",
                        sizeof((*params)[0].field));
                (*params)[0].type = VIR_DOMAIN_SCHED_FIELD_UINT;
                (*params)[0].value.ui = cap;
                nparams++;
        }

        return nparams;
}

static int lxc_scheduler_params(struct infostore_ctx *ctx,
                                virSchedParameter **params)
{
        unsigned long long value;

        *params = calloc(1, sizeof(virSchedParameter));
        if (*params == NULL)
                return -1;

        value = infostore_get_u64(ctx, "weight");

        if (value != 0) {
                strncpy((*params)[0].field,
                        "cpu_shares",
                        sizeof((*params)[0].field));
                (*params)[0].type = VIR_DOMAIN_SCHED_FIELD_UINT;
                (*params)[0].value.ui = value;

                return 1;
        }

        return 0;
}

static void set_scheduler_params(virDomainPtr dom)
{
        struct infostore_ctx *ctx;
        virConnectPtr conn = NULL;
        virSchedParameter *params = NULL;
        int count;

        conn = virDomainGetConnect(dom);
        if (conn == NULL) {
                CU_DEBUG("Unable to get connection from domain");
                return;
        }

        ctx = infostore_open(dom);
        if (ctx == NULL) {
                CU_DEBUG("Unable to open infostore for domain");
                return;
        }

        if (STREQC(virConnectGetType(conn), "xen"))
                count = xen_scheduler_params(ctx, &params);
        else if (STREQC(virConnectGetType(conn), "lxc"))
                count = lxc_scheduler_params(ctx, &params);
        else {
                CU_DEBUG("Not setting sched params for type %s",
                         virConnectGetType(conn));
                goto out;
        }

        if (count < 0) {
                CU_DEBUG("Unable to set scheduler parameters");
                goto out;
        }

        if (count > 0)
                virDomainSetSchedulerParameters(dom, params, count);
        else
                CU_DEBUG("No sched parameters to set");
 out:
        infostore_close(ctx);
        free(params);
}


/* This composite operation may be supported as a flag to reboot */
static CMPIStatus domain_reset(virDomainPtr dom)
{
        int ret;
        virConnectPtr conn = NULL;
        virDomainPtr _dom = NULL;
        char *xml = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        conn = virDomainGetConnect(dom);
        if (conn == NULL) {
                CU_DEBUG("Unable to get connection from domain");
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get domain connection");
                return s;
        }

        xml = virDomainGetXMLDesc(dom, VIR_DOMAIN_XML_SECURE);
        if (xml == NULL) {
                CU_DEBUG("Unable to retrieve domain XML");
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "Unable to get domain definition");
                return s;
        }

        ret = virDomainDestroy(dom);
        if (ret != 0) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "Unable to destroy domain");
                goto out;
        }

        _dom = virDomainLookupByName(conn,
                                    virDomainGetName(dom));

        if (_dom == NULL) {
            _dom = virDomainDefineXML(conn, xml);
            if (_dom == NULL) {
                    CU_DEBUG("Failed to define domain from XML");
                    virt_set_status(_BROKER, &s,
                                    CMPI_RC_ERR_FAILED,
                                    conn,
                                    "Unable to define domain");
                goto out;
            }
        }

        if (!domain_online(_dom))
            CU_DEBUG("Guest is now offline");

        ret = virDomainCreate(_dom);
        if (ret != 0)
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "Failed to start domain");

 out:
        free(xml);
        virDomainFree(_dom);

        return s;
}

static CMPIStatus start_domain(virDomainPtr dom)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if (vsss_has_save_image(virDomainGetName(dom))) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_SUPPORTED,
                           "Domain has a snapshot");
                return s;
        }

        if (virDomainCreate(dom) != 0) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                virDomainGetConnect(dom),
                                "Unable to start domain");
                return s;
        }

        set_scheduler_params(dom);

        return s;
}

static CMPIStatus state_change_enable(virDomainPtr dom, virDomainInfoPtr info)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};

        switch (info->state) {
        case VIR_DOMAIN_SHUTOFF:
                CU_DEBUG("Start domain");
                s = start_domain(dom);
                break;
        case VIR_DOMAIN_PAUSED:
                CU_DEBUG("Unpause domain");
                if (virDomainResume(dom) != 0)
                        virt_set_status(_BROKER, &s,
                                        CMPI_RC_ERR_FAILED,
                                        virDomainGetConnect(dom),
                                        "Unable to unpause domain");
                break;
        default:
                CU_DEBUG("Cannot go to enabled state from %i", info->state);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid state transition");
        };

        return s;
}

static CMPIStatus state_change_disable(virDomainPtr dom, virDomainInfoPtr info)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};

        info->state = adjust_state_xen(dom, info->state);

        switch (info->state) {
        case VIR_DOMAIN_RUNNING:
        case VIR_DOMAIN_BLOCKED:
                CU_DEBUG("Disable domain");
                if (virDomainDestroy(dom) != 0)
                        virt_set_status(_BROKER, &s,
                                        CMPI_RC_ERR_FAILED,
                                        virDomainGetConnect(dom),
                                        "Unable to disable domain");
                break;
        default:
                CU_DEBUG("Cannot go to disabled state from %i", info->state);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid state transition");
        };

        return s;
}

static CMPIStatus state_change_shutdown(virDomainPtr dom, virDomainInfoPtr info)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};

        info->state = adjust_state_xen(dom, info->state);

        switch (info->state) {
        case VIR_DOMAIN_RUNNING:
        case VIR_DOMAIN_BLOCKED:
                CU_DEBUG("Shudown domain");
                if (virDomainShutdown(dom) != 0)
                        virt_set_status(_BROKER, &s,
                                        CMPI_RC_ERR_FAILED,
                                        virDomainGetConnect(dom),
                                        "Unable to shutdown domain");
                break;
        default:
                CU_DEBUG("Cannot go to shutdown state from %i", info->state);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid state transition");
        };

        return s;
}

static CMPIStatus state_change_pause(virDomainPtr dom, virDomainInfoPtr info)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};

        info->state = adjust_state_xen(dom, info->state);

        switch (info->state) {
        case VIR_DOMAIN_RUNNING:
        case VIR_DOMAIN_BLOCKED:
                CU_DEBUG("Pause domain");
                if (virDomainSuspend(dom) != 0)
                        virt_set_status(_BROKER, &s,
                                        CMPI_RC_ERR_FAILED,
                                        virDomainGetConnect(dom),
                                        "Unable to pause domain");
                break;
        default:
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Domain not running");
        };

        return s;
}

static CMPIStatus state_change_reboot(virDomainPtr dom, virDomainInfoPtr info)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};

        info->state = adjust_state_xen(dom, info->state);

        switch (info->state) {
        case VIR_DOMAIN_RUNNING:
        case VIR_DOMAIN_BLOCKED:
        case VIR_DOMAIN_PAUSED:
                CU_DEBUG("Reboot domain");
                if (virDomainReboot(dom, 0) != 0)
                        virt_set_status(_BROKER, &s,
                                        CMPI_RC_ERR_FAILED,
                                        virDomainGetConnect(dom),
                                        "Unable to reboot domain");
                break;
        default:
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Domain not running");
        };

        return s;
}

static CMPIStatus state_change_reset(virDomainPtr dom, virDomainInfoPtr info)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};

        info->state = adjust_state_xen(dom, info->state);

        switch (info->state) {
        case VIR_DOMAIN_RUNNING:
        case VIR_DOMAIN_BLOCKED:
        case VIR_DOMAIN_PAUSED:
                CU_DEBUG("Reset domain");
                s = domain_reset(dom);
                break;
        default:
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Domain not running");
        };

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
        struct infostore_ctx *infostore = NULL;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        dom = virDomainLookupByName(conn, name);
        if (dom == NULL) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "Domain not found");
                goto out;
        }

        if (virDomainGetInfo(dom, &info) != 0) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "Unable to get current state");
                goto out;
        }

        if (state == CIM_STATE_ENABLED)
                s = state_change_enable(dom, &info);
        else if (state == CIM_STATE_DISABLED)
                s = state_change_disable(dom, &info);
        else if (state == CIM_STATE_SHUTDOWN)
                s = state_change_shutdown(dom, &info);
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

        infostore = infostore_open(dom);
        if (infostore != NULL) {
                infostore_set_u64(infostore, "reqstate", (uint64_t)state);
                infostore_close(infostore);
        }

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
        CMPIInstance *prev_inst = NULL;
        uint16_t state;
        int ret;
        const char *name = NULL;
        uint32_t rc = 1;
        bool ind_rc;

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

        /* Retain original instance of the guest to use for the PreviousInstance           attribute when generating an indication. */
        s = get_domain_by_name(_BROKER, reference, name, &prev_inst);
        if (s.rc != CMPI_RC_OK || prev_inst == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Unable to get instance for guest '%s'",
                           name);
                goto out;
        }

        s = __state_change(name, state, reference);

        if (s.rc == CMPI_RC_OK) {
                ind_rc= trigger_mod_indication(context, prev_inst, reference);
                if (!ind_rc)
                        CU_DEBUG("Unable to trigger indication");
         
                rc = 0;
        }
 out:
        CMReturnData(results, &rc, CMPI_uint32);

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
                 {"TimeoutPeriod", CMPI_dateTime, true},
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
