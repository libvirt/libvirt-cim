/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_indication.h>

#include <misc_util.h>
#include <cs_util.h>
#include <list_util.h>

#include "Virt_ComputerSystem.h"
#include "Virt_ComputerSystemIndication.h"
#include "Virt_HostSystem.h"

#define CSI_NUM_PLATFORMS 3
enum CSI_PLATFORMS {
        CSI_XEN,
        CSI_KVM,
        CSI_LXC,
};

#define CS_NUM_EVENTS 3
enum CS_EVENTS {
        CS_CREATED,
        CS_DELETED,
        CS_MODIFIED,
};

typedef struct _csi_dom_xml_t csi_dom_xml_t;
struct _csi_dom_xml_t {
        char uuid[VIR_UUID_STRING_BUFLEN];
        char *name;
        char *xml;
};

typedef struct _csi_thread_data_t csi_thread_data_t;
struct _csi_thread_data_t {
        CMPI_THREAD_TYPE id;
        int active_filters;
        int dom_count;
        list_t *dom_list;
        struct ind_args *args;
};

static const CMPIBroker *_BROKER;
static pthread_mutex_t lifecycle_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool lifecycle_enabled = false;
static csi_thread_data_t csi_thread_data[CSI_NUM_PLATFORMS] = {{0}, {0}, {0}};

void set_source_inst_props(const CMPIBroker *broker,
                           const CMPIContext *context,
                           const CMPIObjectPath *ref,
                           CMPIInstance *ind)
{
        const char *host;
        const char *hostccn;
        CMPIStatus s;
        CMPIString *str;

        str = CMObjectPathToString(ref, &s);
        if ((str == NULL) || (s.rc != CMPI_RC_OK)) {
                CU_DEBUG("Unable to get path string");
        } else {
                CMSetProperty(ind, "SourceInstanceModelPath",
                              (CMPIValue *)&str, CMPI_string);
        }

        s = get_host_system_properties(&host, &hostccn, ref, broker, context);
        if (s.rc != CMPI_RC_OK) {
                CU_DEBUG("Unable to get host properties (%s): %s",
                         CLASSNAME(ref), CMGetCharPtr(s.msg));
        } else {
                CMSetProperty(ind, "SourceInstanceHost",
                              (CMPIValue *)host, CMPI_chars);
        }
}

static bool _do_indication(const CMPIBroker *broker,
                           const CMPIContext *ctx,
                           CMPIInstance *prev_inst,
                           CMPIInstance *affected_inst,
                           int ind_type,
                           const char *prefix,
                           struct ind_args *args)
{
        const char *ind_type_name = NULL;
        CMPIObjectPath *affected_op;
        CMPIObjectPath *ind_op;
        CMPIInstance *ind;
        CMPIData uuid;
        CMPIDateTime *timestamp;
        CMPIStatus s;
        bool ret = true;

        switch (ind_type) {
        case CS_CREATED:
                ind_type_name = "ComputerSystemCreatedIndication";
                break;
        case CS_DELETED:
                ind_type_name = "ComputerSystemDeletedIndication";
                break;
        case CS_MODIFIED:
                ind_type_name = "ComputerSystemModifiedIndication";
                break;
        }

        ind = get_typed_instance(broker,
                                 prefix,
                                 ind_type_name,
                                 args->ns,
                                 false);

        /* Generally report errors and hope to continue, since we have no one
           to actually return status to. */
        if (ind == NULL) {
                CU_DEBUG("Failed to create ind, type '%s:%s_%s'",
                         args->ns,
                         prefix,
                         ind_type_name);
                ret = false;
                goto out;
        }

        ind_op = CMGetObjectPath(ind, &s);
        if (s.rc != CMPI_RC_OK) {
                CU_DEBUG("Failed to get ind_op.  Error: '%s'", CMGetCharPtr(s.msg));
                ret = false;
                goto out;
        }
        CMSetNameSpace(ind_op, args->ns);

        affected_op = CMGetObjectPath(affected_inst, &s);
        if (s.rc != CMPI_RC_OK) {
                ret = false;
                CU_DEBUG("problem getting affected_op: '%s'", CMGetCharPtr(s.msg));
                goto out;
        }

        CMSetNameSpace(affected_op, args->ns);

        uuid = CMGetProperty(affected_inst, "UUID", &s);
        CMSetProperty(ind, "IndicationIdentifier",
                (CMPIValue *)&(uuid.value), CMPI_string);

        timestamp =  CMNewDateTime(broker, &s);
        CMSetProperty(ind, "IndicationTime",
                (CMPIValue *)&timestamp, CMPI_dateTime);

        if (ind_type == CS_MODIFIED) {
                CMSetProperty(ind, "PreviousInstance",
                              (CMPIValue *)&prev_inst, CMPI_instance);
        }

        CMSetProperty(ind, "SourceInstance",
                      (CMPIValue *)&affected_inst, CMPI_instance);

        set_source_inst_props(broker, ctx, affected_op, ind);

        CU_DEBUG("Delivering Indication: %s",
                 CMGetCharPtr(CMObjectPathToString(ind_op, NULL)));

        s = stdi_deliver(broker, ctx, args, ind);
        if (s.rc == CMPI_RC_OK) {
                CU_DEBUG("Indication delivered");
        } else {
                CU_DEBUG("Not delivered: %s", CMGetCharPtr(s.msg));
        }

 out:
        return ret;
}

static bool set_instance_state(CMPIInstance *instance)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIUint16 cim_state;
        CMPIString *cim_state_other = NULL;
        CMPIUint16 health_state;
        CMPIUint16 req_state;
        CMPIUint16 oping_status;
        CMPIUint16 op_status;
        CMPIArray *array;

        cim_state = CIM_STATE_OTHER;
        cim_state_other = CMNewString(_BROKER, "Guest destroyed", &s);
        CMSetProperty(instance, "EnabledState",
                      (CMPIValue *)&cim_state, CMPI_uint16);
        CMSetProperty(instance, "OtherEnabledState",
                      (CMPIValue *)&cim_state_other, CMPI_string);

        health_state = CIM_HEALTH_UNKNOWN;
        CMSetProperty(instance, "HealthState",
                      (CMPIValue *)&health_state, CMPI_uint16);

        array = CMNewArray(_BROKER, 2, CMPI_uint16, &s);
        if ((s.rc != CMPI_RC_OK) || (CMIsNullObject(array)))
                return false;

        op_status = CIM_OP_STATUS_COMPLETED;
        CMSetArrayElementAt(array, 0, &op_status, CMPI_uint16);
        op_status = CIM_OP_STATUS_OK;
        CMSetArrayElementAt(array, 1, &op_status, CMPI_uint16);

        CMSetProperty(instance, "OperationalStatus",
                      (CMPIValue *)&array, CMPI_uint16A);

        oping_status = CIM_OPING_STATUS_COMPLETED;
        CMSetProperty(instance, "OperatingStatus",
                      (CMPIValue *)&oping_status, CMPI_uint16);

        req_state = CIM_STATE_UNKNOWN;
        CMSetProperty(instance, "RequestedState",
                      (CMPIValue *)&req_state, CMPI_uint16);

        return true;
}

static bool create_deleted_guest_inst(const char *xml,
                                      const char *namespace,
                                      const char *prefix,
                                      CMPIInstance **inst)
{
        bool rc = false;
        struct domain *dominfo = NULL;
        int res;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        res = get_dominfo_from_xml(xml, &dominfo);
        if (res == 0) {
                CU_DEBUG("failed to extract domain info from xml");
                goto out;
        }

        s = instance_from_dominfo(_BROKER,
                                  namespace,
                                  prefix,
                                  dominfo,
                                  inst);
        if (s.rc != CMPI_RC_OK) {
                CU_DEBUG("instance from domain info error: %s",
                         CMGetCharPtr(s.msg));
                goto out;
        }

        rc = set_instance_state(*inst);
        if (!rc)
                CU_DEBUG("Error setting instance state");

 out:
        cleanup_dominfo(&dominfo);

        return rc;
}

static int platform_from_class(const char *cn)
{
        if (STARTS_WITH(cn, "Xen")) {
                return CSI_XEN;
        } else if (STARTS_WITH(cn, "KVM")) {
                return CSI_KVM;
        } else if (STARTS_WITH(cn, "LXC")) {
                return CSI_LXC;
        } else {
                return -1;
        }
}

static _EI_RTYPE EnableIndications(CMPIIndicationMI *mi,
                                   const CMPIContext *ctx)
{
        CU_DEBUG("EnableIndications");
        pthread_mutex_lock(&lifecycle_mutex);
        lifecycle_enabled = true;
        pthread_mutex_unlock(&lifecycle_mutex);

        _EI_RET();
}

static _EI_RTYPE DisableIndications(CMPIIndicationMI *mi,
                                    const CMPIContext *ctx)
{
        CU_DEBUG("DisableIndications");
        pthread_mutex_lock(&lifecycle_mutex);
        lifecycle_enabled = false;
        pthread_mutex_unlock(&lifecycle_mutex);

        _EI_RET();
}

DECLARE_FILTER(xen_created, "Xen_ComputerSystemCreatedIndication");
DECLARE_FILTER(xen_deleted, "Xen_ComputerSystemDeletedIndication");
DECLARE_FILTER(xen_modified, "Xen_ComputerSystemModifiedIndication");
DECLARE_FILTER(kvm_created, "KVM_ComputerSystemCreatedIndication");
DECLARE_FILTER(kvm_deleted, "KVM_ComputerSystemDeletedIndication");
DECLARE_FILTER(kvm_modified, "KVM_ComputerSystemModifiedIndication");
DECLARE_FILTER(lxc_created, "LXC_ComputerSystemCreatedIndication");
DECLARE_FILTER(lxc_deleted, "LXC_ComputerSystemDeletedIndication");
DECLARE_FILTER(lxc_modified, "LXC_ComputerSystemModifiedIndication");

static struct std_ind_filter *filters[] = {
        &xen_created,
        &xen_deleted,
        &xen_modified,
        &kvm_created,
        &kvm_deleted,
        &kvm_modified,
        &lxc_created,
        &lxc_deleted,
        &lxc_modified,
        NULL,
};

#ifndef USE_LIBVIRT_EVENT
/* libvirt-cim's private CSI implement */

#define WAIT_TIME 60
#define FAIL_WAIT_TIME 2

static pthread_cond_t lifecycle_cond = PTHREAD_COND_INITIALIZER;

struct dom_xml {
        char uuid[VIR_UUID_STRING_BUFLEN];
        char *xml;
        enum {DOM_OFFLINE,
              DOM_ONLINE,
              DOM_PAUSED,
              DOM_CRASHED,
              DOM_GONE,
        } state;
};

static void free_dom_xml(struct dom_xml dom)
{
        free(dom.xml);
        dom.xml = NULL;
}

static char *sys_name_from_xml(char *xml)
{
        char *tmp = NULL;
        char *name = NULL;
        int rc;

        tmp = strstr(xml, "<name>");
        if (tmp == NULL) {
                goto out;
        }

        rc = sscanf(tmp, "<name>%a[^<]s</name>", &name);
        if (rc != 1) {
                name = NULL;
        }

 out:
        return name;
}

static int dom_state(virDomainPtr dom)
{
        virDomainInfo info;
        int ret;

        ret = virDomainGetInfo(dom, &info);
        if (ret != 0) {
                return DOM_GONE;
        }

        switch (info.state) {
        case VIR_DOMAIN_NOSTATE:
        case VIR_DOMAIN_RUNNING:
        case VIR_DOMAIN_BLOCKED:
                return DOM_ONLINE;

        case VIR_DOMAIN_PAUSED:
                return DOM_PAUSED;

        case VIR_DOMAIN_SHUTOFF:
                return DOM_OFFLINE;

        case VIR_DOMAIN_CRASHED:
                return DOM_CRASHED;

        default:
                return DOM_GONE;
        };
}

static CMPIStatus doms_to_xml(struct dom_xml **dom_xml_list,
                              virDomainPtr *dom_ptr_list,
                              int dom_ptr_count)
{
        int i;
        int rc;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if (dom_ptr_count <= 0) {
                *dom_xml_list = NULL;
                return s;
        }
        *dom_xml_list = calloc(dom_ptr_count, sizeof(struct dom_xml));
        if (!*dom_xml_list) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed calloc %d dom_xml.", dom_ptr_count);
                return s;
        }
        for (i = 0; i < dom_ptr_count; i++) {
                rc = virDomainGetUUIDString(dom_ptr_list[i],
                                            (*dom_xml_list)[i].uuid);
                if (rc == -1) {
                        cu_statusf(_BROKER, &s,
                                   CMPI_RC_ERR_FAILED,
                                   "Failed to get UUID");
                        /* If any domain fails, we fail. */
                        break;
                }

                (*dom_xml_list)[i].xml = virDomainGetXMLDesc(dom_ptr_list[i],
                              VIR_DOMAIN_XML_INACTIVE | VIR_DOMAIN_XML_SECURE);
                if ((*dom_xml_list)[i].xml == NULL) {
                        cu_statusf(_BROKER, &s,
                                   CMPI_RC_ERR_FAILED,
                                   "Failed to get xml desc");
                        break;
                }

                (*dom_xml_list)[i].state = dom_state(dom_ptr_list[i]);
        }

        return s;
}

static bool dom_changed(struct dom_xml prev_dom,
                        struct dom_xml *cur_xml,
                        int cur_count)
{
        int i;
        bool ret = false;

        for (i = 0; i < cur_count; i++) {
                if (strcmp(cur_xml[i].uuid, prev_dom.uuid) != 0) {
                        continue;
                }

                if (strcmp(cur_xml[i].xml, prev_dom.xml) != 0) {
                        CU_DEBUG("Domain config changed");
                        ret = true;
                }

                if (prev_dom.state != cur_xml[i].state) {
                        CU_DEBUG("Domain state changed");
                        ret = true;
                }

                break;
        }

        return ret;
}

static bool wait_for_event(int wait_time)
{
        struct timespec timeout;
        int ret;


        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += wait_time;

        ret = pthread_cond_timedwait(&lifecycle_cond,
                                     &lifecycle_mutex,
                                     &timeout);
        return !ret;
}

static bool dom_in_list(char *uuid, int count, struct dom_xml *list)
{
        int i;

        for (i = 0; i < count; i++) {
                if (STREQ(uuid, list[i].uuid)) {
                        return true;
                }
        }

        return false;
}

static bool async_ind_native(CMPIContext *context,
                      int ind_type,
                      struct dom_xml prev_dom,
                      char *prefix,
                      struct ind_args *args)
{
        bool rc = false;
        char *name = NULL;
        char *cn = NULL;
        CMPIObjectPath *op;
        CMPIInstance *prev_inst;
        CMPIInstance *affected_inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        CU_DEBUG("Entering native indication dilivery with type %d.", ind_type)
        if (!lifecycle_enabled) {
                CU_DEBUG("CSI not enabled, skipping indication delivery");
                return false;
        }

        name = sys_name_from_xml(prev_dom.xml);
        CU_DEBUG("Name for system: '%s'", name);
        if (name == NULL) {
                rc = false;
                goto out;
        }

        cn = get_typed_class(prefix, "ComputerSystem");

        op = CMNewObjectPath(_BROKER, args->ns, cn, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op)) {
                CU_DEBUG("op error");
                goto out;
        }

        if (ind_type == CS_CREATED || ind_type == CS_MODIFIED) {
                s = get_domain_by_name(_BROKER, op, name, &affected_inst);
                if (s.rc != CMPI_RC_OK) {
                        CU_DEBUG("domain by name error");
                        goto out;
                }
        } else if (ind_type == CS_DELETED) {
                rc = create_deleted_guest_inst(prev_dom.xml,
                                               args->ns,
                                               prefix,
                                               &affected_inst);
                if (!rc) {
                        CU_DEBUG("Could not recreate guest instance");
                        goto out;
                }
        } else {
                CU_DEBUG("Unrecognized indication type %d", ind_type);
                goto out;
        }

        /* FIXME: We are unable to get the previous CS instance after it has
                  been modified. Consider keeping track of the previous
                  state in the place we keep track of the requested state */
        prev_inst = affected_inst;

        CMSetProperty(affected_inst, "Name",
                      (CMPIValue *)name, CMPI_chars);
        CMSetProperty(affected_inst, "UUID",
                      (CMPIValue *)prev_dom.uuid, CMPI_chars);

        rc = _do_indication(_BROKER, context, prev_inst, affected_inst,
                            ind_type, prefix, args);

 out:
        free(cn);
        free(name);
        return rc;
}

static CMPI_THREAD_RETURN lifecycle_thread_native(void *params)
{
        CU_DEBUG("Entering libvirtc-cim native CSI thread.");
        csi_thread_data_t *thread = (csi_thread_data_t *) params;
        struct ind_args *args = NULL;
        CMPIContext *context = NULL;
        char *prefix = NULL;
        virConnectPtr conn;
        CMPIStatus s;
        int retry_time = FAIL_WAIT_TIME;

        struct dom_xml *cur_xml = NULL;
        struct dom_xml *prev_xml = NULL;
        int prev_count = 0;
        int cur_count = 0;
        virDomainPtr *tmp_list = NULL;
        int CBAttached = 0;

        if (thread->args != NULL) {
                args = thread->args;
                context = args->context;
                prefix = class_prefix_name(args->classname);
        }
        if (prefix == NULL) {
                goto init_out;
        }

        pthread_mutex_lock(&lifecycle_mutex);
        conn = connect_by_classname(_BROKER, args->classname, &s);
        if (conn == NULL) {
                CU_DEBUG("Unable to start lifecycle thread: "
                         "Failed to connect (cn: %s)", args->classname);
                pthread_mutex_unlock(&lifecycle_mutex);
                goto conn_out;
        }

        CBAttachThread(_BROKER, args->context);
        CBAttached = 1;
        prev_count = get_domain_list(conn, &tmp_list);
        s = doms_to_xml(&prev_xml, tmp_list, prev_count);
        free_domain_list(tmp_list, prev_count);
        free(tmp_list);
        tmp_list = NULL;
        if (s.rc != CMPI_RC_OK) {
                CU_DEBUG("doms_to_xml failed.  Attempting to continue.");
        }

        CU_DEBUG("Entering libvirt-cim native CSI event loop (%s)", prefix);

        int i;
        while (1) {
                if (thread->active_filters <= 0) {
                        break;
                }

                bool res;
                bool failure = false;

                cur_count = get_domain_list(conn, &tmp_list);
                s = doms_to_xml(&cur_xml, tmp_list, cur_count);
                free_domain_list(tmp_list, cur_count);
                free(tmp_list);
                tmp_list = NULL;
                if (s.rc != CMPI_RC_OK) {
                        CU_DEBUG("doms_to_xml failed. retry in %d seconds",
                                 retry_time);
                        failure = true;
                        goto fail;
                }

                /* CU_DEBUG("cur_count %d, prev_count %d.",
                             cur_count, prev_count); */
                for (i = 0; i < cur_count; i++) {
                        res = dom_in_list(cur_xml[i].uuid,
                                          prev_count, prev_xml);
                        if (!res) {
                                async_ind_native(context, CS_CREATED,
                                                 cur_xml[i], prefix, args);
                        }

                }

                for (i = 0; i < prev_count; i++) {
                        res = dom_in_list(prev_xml[i].uuid,
                                          cur_count, cur_xml);
                        if (!res) {
                                async_ind_native(context, CS_DELETED,
                                                 prev_xml[i], prefix, args);
                        } else if (dom_changed(prev_xml[i],
                                               cur_xml, cur_count)) {
                                async_ind_native(context, CS_MODIFIED,
                                                 prev_xml[i], prefix, args);
                        }
                        free_dom_xml(prev_xml[i]);
                }

        fail:
                if (failure) {
                        wait_for_event(FAIL_WAIT_TIME);
                } else {
                        free(prev_xml);
                        prev_xml = cur_xml;
                        cur_xml = NULL;
                        prev_count = cur_count;
                        cur_count = 0;
                        wait_for_event(WAIT_TIME);
                }
        }

        CU_DEBUG("Exiting libvirt-cim native CSI event loop (%s)", prefix);

        if (prev_xml != NULL) {
                for (i = 0; i < prev_count; i++) {
                        free_dom_xml(prev_xml[i]);
                }
                free(prev_xml);
                prev_xml = NULL;
        }

        pthread_mutex_unlock(&lifecycle_mutex);

        virConnectClose(conn);

 conn_out:
        free(prefix);

 init_out:
        pthread_mutex_lock(&lifecycle_mutex);
        thread->id = 0;
        thread->active_filters = 0;

        /* it seems tog-pegasus try kill this thread after detached, use this
        flag to delay detach as much as possible. */
        if (CBAttached > 0) {
                CBDetachThread(_BROKER, args->context);
        }
        if (thread->args != NULL) {
                stdi_free_ind_args(&thread->args);
        }

        pthread_mutex_unlock(&lifecycle_mutex);

        return (CMPI_THREAD_RETURN) 0;
}

static CMPIStatus ActivateFilter(CMPIIndicationMI *mi,
                                 const CMPIContext *ctx,
                                 const CMPISelectExp *se,
                                 const char *ns,
                                 const CMPIObjectPath *op,
                                 CMPIBoolean first)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct std_indication_ctx *_ctx;
        struct ind_args *args = NULL;
        int platform;
        bool error = false;
        csi_thread_data_t *thread = NULL;

        CU_DEBUG("ActivateFilter for %s", CLASSNAME(op));

        pthread_mutex_lock(&lifecycle_mutex);

        CU_DEBUG("Using libvirt-cim's event implemention.");

        _ctx = (struct std_indication_ctx *)mi->hdl;

        if (CMIsNullObject(op)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "No ObjectPath given");
                goto out;
        }

        /* FIXME: op is stale the second time around, for some reason */
        platform = platform_from_class(CLASSNAME(op));
        if (platform < 0) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unknown platform");
                goto out;
        }

        thread = &csi_thread_data[platform];
        thread->active_filters += 1;

        /* Check if thread is already running */
        if (thread->id > 0) {
                goto out;
        }

        args = malloc(sizeof(*args));
        if (args == NULL) {
                CU_DEBUG("Failed to allocate ind_args");
                cu_statusf(_BROKER, &s, CMPI_RC_ERR_FAILED,
                           "Unable to allocate ind_args");
                error = true;
                goto out;
        }

        args->context = CBPrepareAttachThread(_BROKER, ctx);
        if (args->context == NULL) {
                CU_DEBUG("Failed to create thread context");
                cu_statusf(_BROKER, &s, CMPI_RC_ERR_FAILED,
                           "Unable to create thread context");
                error = true;
                goto out;
        }

        args->ns = strdup(NAMESPACE(op));
        args->classname = strdup(CLASSNAME(op));
        args->_ctx = _ctx;

        thread->args = args;

        thread->id = _BROKER->xft->newThread(lifecycle_thread_native,
                                             thread, 0);

        if (thread->id == NULL) {
            CU_DEBUG("Error, failed to create new thread.");
            error = true;
        }

 out:
        if (error == true) {
                thread->active_filters -= 1;
                free(args);
        }

        pthread_mutex_unlock(&lifecycle_mutex);

        return s;
}

static CMPIStatus DeActivateFilter(CMPIIndicationMI *mi,
                                   const CMPIContext *ctx,
                                   const CMPISelectExp *se,
                                   const  char *ns,
                                   const CMPIObjectPath *op,
                                   CMPIBoolean last)
{
        int platform;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        CU_DEBUG("DeActivateFilter for %s", CLASSNAME(op));

        platform = platform_from_class(CLASSNAME(op));
        if (platform < 0) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unknown platform");
                goto out;
        }


        pthread_mutex_lock(&lifecycle_mutex);
        csi_thread_data[platform].active_filters -= 1;
        pthread_mutex_unlock(&lifecycle_mutex);

        pthread_cond_signal(&lifecycle_cond);

 out:
        return s;
}

static CMPIStatus trigger_indication(const CMPIContext *context)
{
        CU_DEBUG("triggered");
        pthread_cond_signal(&lifecycle_cond);
        return (CMPIStatus){CMPI_RC_OK, NULL};
}

static CMPIInstance *get_prev_inst(const CMPIBroker *broker,
                                   const CMPIInstance *ind,
                                   CMPIStatus *s)
{
        CMPIData data;
        CMPIInstance *prev_inst = NULL;

        data = CMGetProperty(ind, "PreviousInstance", s);
        if (s->rc != CMPI_RC_OK || CMIsNullValue(data)) {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_NO_SUCH_PROPERTY,
                           "Unable to get PreviousInstance of the indication");
                goto out;
        }

        if (data.type != CMPI_instance) {
                cu_statusf(broker, s,
                           CMPI_RC_ERR_TYPE_MISMATCH,
                           "Indication SourceInstance is of unexpected type");
                goto out;
        }

        prev_inst = data.value.inst;

 out:
        return prev_inst;
}

static CMPIStatus raise_indication(const CMPIBroker *broker,
                                   const CMPIContext *ctx,
                                   const CMPIObjectPath *ref,
                                   const CMPIInstance *ind)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *prev_inst;
        CMPIInstance *src_inst;
        CMPIObjectPath *_ref = NULL;
        struct std_indication_ctx *_ctx = NULL;
        struct ind_args *args = NULL;
        char *prefix = NULL;
        bool rc;

        if (!lifecycle_enabled) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "CSI not enabled, skipping indication delivery");
                goto out;
        }

        prev_inst = get_prev_inst(broker, ind, &s);
        if (s.rc != CMPI_RC_OK || CMIsNullObject(prev_inst)) {
                goto out;
        }

        _ref = CMGetObjectPath(prev_inst, &s);
        if (s.rc != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get a reference to the guest");
                goto out;
        }

        /* FIXME:  This is a Pegasus work around. Pegsus loses the namespace
                   when an ObjectPath is pulled from an instance */
        if (STREQ(NAMESPACE(_ref), "")) {
                CMSetNameSpace(_ref, "root/virt");
        }

        s = get_domain_by_ref(broker, _ref, &src_inst);
        if (s.rc != CMPI_RC_OK || CMIsNullObject(src_inst)) {
                goto out;
        }

        _ctx = malloc(sizeof(struct std_indication_ctx));
        if (_ctx == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to allocate indication context");
                goto out;
        }

        _ctx->brkr = broker;
        _ctx->handler = NULL;
        _ctx->filters = filters;
        _ctx->enabled = lifecycle_enabled;

        args = malloc(sizeof(struct ind_args));
        if (args == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to allocate ind_args");
                goto out;
       }

        args->ns = strdup(NAMESPACE(_ref));
        args->classname = strdup(CLASSNAME(_ref));
        if (!args->classname || !args->ns) {
                CU_DEBUG("Failed in strdup");
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed in strdup in indication raising");
                goto out;
        }
        args->_ctx = _ctx;

        prefix = class_prefix_name(args->classname);

        rc = _do_indication(broker, ctx, prev_inst, src_inst,
                            CS_MODIFIED, prefix, args);

        if (!rc) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to generate indication");
        }

 out:
        if (args != NULL) {
                stdi_free_ind_args(&args);
        }

        if (_ctx != NULL) {
                free(_ctx);
        }

        free(prefix);
        return s;
}

static struct std_indication_handler csi = {
        .raise_fn = raise_indication,
        .trigger_fn = trigger_indication,
        .activate_fn = ActivateFilter,
        .deactivate_fn = DeActivateFilter,
        .enable_fn = EnableIndications,
        .disable_fn = DisableIndications,
};
#else
/* Using libvirt's event to implement CSI */

/*
 * Domain manipulation
 */
static void csi_dom_xml_free(void *data)
{
        csi_dom_xml_t *dom = (csi_dom_xml_t *) data;
        free(dom->xml);
        free(dom->name);
        free(dom);
}

static int csi_dom_xml_cmp(void *data, void *cmp_cb_data)
{
        csi_dom_xml_t *dom = (csi_dom_xml_t *) data;
        const char *uuid = (const char *) cmp_cb_data;

        return strcmp(dom->uuid, uuid);
}

static int csi_dom_xml_set(csi_dom_xml_t *dom,
                           virDomainPtr dom_ptr,
                           CMPIStatus *s)
{
        const char *name;

        name = virDomainGetName(dom_ptr);
        if (name == NULL) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get domain name");
                return -1;
        }

        dom->name = strdup(name);

        /* xml */
        dom->xml = virDomainGetXMLDesc(dom_ptr,
                VIR_DOMAIN_XML_INACTIVE | VIR_DOMAIN_XML_SECURE);
        if (dom->xml == NULL) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get xml desc");
                return -1;
        }

        return 0;
}

static csi_dom_xml_t *csi_dom_xml_new(virDomainPtr dom_ptr, CMPIStatus *s)
{
        int rc;
        csi_dom_xml_t *dom;

        dom = calloc(1, sizeof(*dom));
        if (dom == NULL) {
                return NULL;
        }

        /* uuid */
        rc = virDomainGetUUIDString(dom_ptr, dom->uuid);
        if (rc == -1) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get domain UUID");
                goto error;
        }

        if (csi_dom_xml_set(dom, dom_ptr, s) == -1) {
                goto error;
        }

        return dom;

 error:
        csi_dom_xml_free(dom);
        return NULL;
}

static void csi_thread_dom_list_append(csi_thread_data_t *thread,
                                       csi_dom_xml_t *dom)
{
        if (thread->dom_list == NULL) {
                thread->dom_list = list_new(csi_dom_xml_free, csi_dom_xml_cmp);
        }

        list_append(thread->dom_list, dom);
}

static void csi_free_thread_data(void *data)
{
        csi_thread_data_t *thread = (csi_thread_data_t *) data;

        if (data == NULL) {
                return;
        }

        pthread_mutex_lock(&lifecycle_mutex);
        list_free(thread->dom_list);
        thread->dom_list = NULL;
        stdi_free_ind_args(&thread->args);
        pthread_mutex_unlock(&lifecycle_mutex);
}

static bool async_ind(struct ind_args *args,
                      int ind_type,
                      csi_dom_xml_t *dom,
                      const char *prefix)
{
        bool rc = false;
        char *cn = NULL;
        CMPIObjectPath *op;
        CMPIInstance *prev_inst;
        CMPIInstance *affected_inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if (!lifecycle_enabled) {
                CU_DEBUG("CSI not enabled, skipping indication delivery");
                return false;
        }

        cn = get_typed_class(prefix, "ComputerSystem");

        op = CMNewObjectPath(_BROKER, args->ns, cn, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op)) {
                CU_DEBUG("op error");
                goto out;
        }

        if (ind_type == CS_CREATED || ind_type == CS_MODIFIED) {
                s = get_domain_by_name(_BROKER, op, dom->name, &affected_inst);

                /* If domain is not found, we create the instance from the data
                   previously stored */
                if (s.rc == CMPI_RC_ERR_NOT_FOUND) {
                        rc = create_deleted_guest_inst(dom->xml,
                                                       args->ns,
                                                       prefix,
                                                       &affected_inst);
                         if (!rc) {
                                CU_DEBUG("Could not recreate guest instance");
                                goto out;
                         }

                         s.rc = CMPI_RC_OK;
                }

                if (s.rc != CMPI_RC_OK) {
                        CU_DEBUG("domain by name error");
                        goto out;
                }

        } else if (ind_type == CS_DELETED) {
                rc = create_deleted_guest_inst(dom->xml,
                                               args->ns,
                                               prefix,
                                               &affected_inst);
                if (!rc) {
                        CU_DEBUG("Could not recreate guest instance");
                        goto out;
                }
        } else {
                CU_DEBUG("Unrecognized indication type");
                goto out;
        }

        /* FIXME: We are unable to get the previous CS instance after it has
                  been modified. Consider keeping track of the previous
                  state in the place we keep track of the requested state */
        prev_inst = affected_inst;

        CMSetProperty(affected_inst, "Name",
                      (CMPIValue *) dom->name, CMPI_chars);
        CMSetProperty(affected_inst, "UUID",
                      (CMPIValue *) dom->uuid, CMPI_chars);

        rc = _do_indication(_BROKER, args->context, prev_inst, affected_inst,
                            ind_type, prefix, args);

 out:
        free(cn);
        return rc;
}

static int update_domain_list(virConnectPtr conn, csi_thread_data_t *thread)
{
        virDomainPtr *dom_ptr_list;
        csi_dom_xml_t *dom;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int i, count;

        list_free(thread->dom_list);

        count = get_domain_list(conn, &dom_ptr_list);

        for (i = 0; i < count; i++) {
                dom = csi_dom_xml_new(dom_ptr_list[i], &s);
                if (dom == NULL) {
                        CU_DEBUG("Failed to get domain info %s", CMGetCharPtr(s.msg));
                        break;
                }

                csi_thread_dom_list_append(thread, dom);
        }

        free_domain_list(dom_ptr_list, count);
        free(dom_ptr_list);

        return s.rc;
}

/* following function will protect global data with lifecycle_mutex.
   TODO: improve it with seperate lock later. */
static void csi_domain_event_cb(virConnectPtr conn,
                                virDomainPtr dom,
                                int event,
                                int detail,
                                void *data)
{
        int cs_event = CS_MODIFIED;
        csi_thread_data_t *thread = (csi_thread_data_t *) data;
        csi_dom_xml_t *dom_xml = NULL;
        char *prefix = class_prefix_name(thread->args->classname);
        CMPIStatus s = {CMPI_RC_OK, NULL};

        pthread_mutex_lock(&lifecycle_mutex);
        if (lifecycle_enabled == false || thread->active_filters <= 0) {
                CU_DEBUG("%s indications deactivated, return", prefix);
                pthread_mutex_unlock(&lifecycle_mutex);
                return;
        }
        pthread_mutex_unlock(&lifecycle_mutex);

        CU_DEBUG("Event: Domain %s(%d) event: %d detail: %d\n",
                 virDomainGetName(dom), virDomainGetID(dom), event, detail);

        switch (event) {
        case VIR_DOMAIN_EVENT_DEFINED:
                if (detail == VIR_DOMAIN_EVENT_DEFINED_ADDED) {
                        CU_DEBUG("Domain defined");
                        cs_event = CS_CREATED;
                        dom_xml = csi_dom_xml_new(dom, &s);
                } else if (detail == VIR_DOMAIN_EVENT_DEFINED_UPDATED) {
                        CU_DEBUG("Domain modified");
                        cs_event = CS_MODIFIED;
                }

                break;

        case VIR_DOMAIN_EVENT_UNDEFINED:
                CU_DEBUG("Domain undefined");
                cs_event = CS_DELETED;
                break;

        default: /* STARTED, SUSPENDED, RESUMED, STOPPED, SHUTDOWN */
                CU_DEBUG("Domain modified");
                cs_event = CS_MODIFIED;
                break;
        }

        if (cs_event != CS_CREATED) {
                char uuid[VIR_UUID_STRING_BUFLEN] = {0};
                virDomainGetUUIDString(dom, &uuid[0]);
                pthread_mutex_lock(&lifecycle_mutex);
                dom_xml = list_find(thread->dom_list, uuid);
                pthread_mutex_unlock(&lifecycle_mutex);
        }

        if (dom_xml == NULL) {
                CU_DEBUG("Domain not found in current list");
                goto end;
        }

        pthread_mutex_lock(&lifecycle_mutex);
        async_ind(thread->args, cs_event, dom_xml, prefix);
        pthread_mutex_unlock(&lifecycle_mutex);

        /* Update the domain list accordingly */
        if (event == VIR_DOMAIN_EVENT_DEFINED) {
                if (detail == VIR_DOMAIN_EVENT_DEFINED_ADDED) {
                        pthread_mutex_lock(&lifecycle_mutex);
                        csi_thread_dom_list_append(thread, dom_xml);
                        pthread_mutex_unlock(&lifecycle_mutex);
                } else if (detail == VIR_DOMAIN_EVENT_DEFINED_UPDATED) {
                        free(dom_xml->name);
                        free(dom_xml->xml);
                        csi_dom_xml_set(dom_xml, dom, &s);
                }
        } else if (event == VIR_DOMAIN_EVENT_DEFINED &&
                   detail == VIR_DOMAIN_EVENT_UNDEFINED_REMOVED) {
                pthread_mutex_lock(&lifecycle_mutex);
                list_remove(thread->dom_list, dom_xml);
                pthread_mutex_unlock(&lifecycle_mutex);
        }

 end:
        free(prefix);
}

static CMPI_THREAD_RETURN lifecycle_thread(void *params)
{
        csi_thread_data_t *thread = (csi_thread_data_t *) params;
        struct ind_args *args = thread->args;
        char *prefix = class_prefix_name(args->classname);

        virConnectPtr conn;

        CMPIStatus s;
        int cb_id;

        if (prefix == NULL)
                goto init_out;

        pthread_mutex_lock(&lifecycle_mutex);
        conn = connect_by_classname(_BROKER, args->classname, &s);
        if (conn == NULL) {
                CU_DEBUG("Unable to start lifecycle thread: "
                         "Failed to connect (cn: %s)", args->classname);
                pthread_mutex_unlock(&lifecycle_mutex);
                goto conn_out;
        }

        /* register callback */
        cb_id = virConnectDomainEventRegisterAny(conn, NULL,
                                VIR_DOMAIN_EVENT_ID_LIFECYCLE,
                                VIR_DOMAIN_EVENT_CALLBACK(csi_domain_event_cb),
                                params, csi_free_thread_data);

        if (cb_id == -1) {
                CU_DEBUG("Failed to register domain event watch for '%s'",
                         args->classname);
                pthread_mutex_unlock(&lifecycle_mutex);
                goto cb_out;
        }

        CBAttachThread(_BROKER, args->context);

        /* Get currently defined domains */
        if (update_domain_list(conn, thread) != CMPI_RC_OK) {
                pthread_mutex_unlock(&lifecycle_mutex);
                goto end;
        }

        pthread_mutex_unlock(&lifecycle_mutex);

        CU_DEBUG("Entering CSI event loop (%s)", prefix);
        while (1) {
                pthread_mutex_lock(&lifecycle_mutex);
                if (thread->active_filters <= 0) {
                        pthread_mutex_unlock(&lifecycle_mutex);
                        break;
                }
                pthread_mutex_unlock(&lifecycle_mutex);
                if (virEventRunDefaultImpl() < 0) {
                        virErrorPtr err = virGetLastError();
                        CU_DEBUG("Failed to run event loop: %s\n",
                        err && err->message ? err->message : "Unknown error");
                }
                usleep(1);
        }

        CU_DEBUG("Exiting CSI event loop (%s)", prefix);

        pthread_mutex_lock(&lifecycle_mutex);
        CBDetachThread(_BROKER, args->context);
        pthread_mutex_unlock(&lifecycle_mutex);
 end:
        virConnectDomainEventDeregisterAny(conn, cb_id);

 cb_out:
        pthread_mutex_lock(&lifecycle_mutex);

        thread->id = 0;
        thread->active_filters = 0;

        if (thread->args != NULL)
                stdi_free_ind_args(&thread->args);

        pthread_mutex_unlock(&lifecycle_mutex);

 conn_out:
        virConnectClose(conn);

 init_out:
        free(prefix);
        return (CMPI_THREAD_RETURN) 0;
}

static CMPIStatus ActivateFilter(CMPIIndicationMI* mi,
                                 const CMPIContext* ctx,
                                 const CMPISelectExp* se,
                                 const char *ns,
                                 const CMPIObjectPath* op,
                                 CMPIBoolean first)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct std_indication_ctx *_ctx;
        struct ind_args *args = NULL;
        int platform;
        bool error = false;
        csi_thread_data_t *thread = NULL;
        static int events_registered = 0;

        CU_DEBUG("ActivateFilter for %s", CLASSNAME(op));

        pthread_mutex_lock(&lifecycle_mutex);

        if (events_registered == 0) {
                events_registered = 1;
                CU_DEBUG("Registering libvirt event.");
                virEventRegisterDefaultImpl();
        }

        _ctx = (struct std_indication_ctx *)mi->hdl;

        if (CMIsNullObject(op)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "No ObjectPath given");
                goto out;
        }

        /* FIXME: op is stale the second time around, for some reason */
        platform = platform_from_class(CLASSNAME(op));
        if (platform < 0) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unknown platform");
                goto out;
        }

        thread = &csi_thread_data[platform];
        thread->active_filters += 1;

        /* Check if thread is already running */
        if (thread->id > 0)
                goto out;

        args = malloc(sizeof(*args));
        if (args == NULL) {
                CU_DEBUG("Failed to allocate ind_args");
                cu_statusf(_BROKER, &s, CMPI_RC_ERR_FAILED,
                           "Unable to allocate ind_args");
                error = true;
                goto out;
        }

        args->context = CBPrepareAttachThread(_BROKER, ctx);
        if (args->context == NULL) {
                CU_DEBUG("Failed to create thread context");
                cu_statusf(_BROKER, &s, CMPI_RC_ERR_FAILED,
                           "Unable to create thread context");
                error = true;
                goto out;
        }

        args->ns = strdup(NAMESPACE(op));
        args->classname = strdup(CLASSNAME(op));
        args->_ctx = _ctx;

        thread->args = args;
        thread->id = _BROKER->xft->newThread(lifecycle_thread, thread, 0);

        if (thread->id == NULL) {
            CU_DEBUG("Error, failed to create new thread.");
            error = true;
        }

 out:
        if (error == true) {
                thread->active_filters -= 1;
                free(args);
        }

        pthread_mutex_unlock(&lifecycle_mutex);

        return s;
}

static CMPIStatus DeActivateFilter(CMPIIndicationMI* mi,
                                   const CMPIContext* ctx,
                                   const CMPISelectExp* se,
                                   const  char *ns,
                                   const CMPIObjectPath* op,
                                   CMPIBoolean last)
{
        int platform;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        CU_DEBUG("DeActivateFilter for %s", CLASSNAME(op));

        platform = platform_from_class(CLASSNAME(op));
        if (platform < 0) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unknown platform");
                goto out;
        }


        pthread_mutex_lock(&lifecycle_mutex);
        csi_thread_data[platform].active_filters -= 1;
        pthread_mutex_unlock(&lifecycle_mutex);

 out:
        return s;
}

static struct std_indication_handler csi = {
        .activate_fn = ActivateFilter,
        .deactivate_fn = DeActivateFilter,
        .enable_fn = EnableIndications,
        .disable_fn = DisableIndications,
};
#endif

DEFAULT_IND_CLEANUP();
DEFAULT_AF();
DEFAULT_MP();

STDI_IndicationMIStub(,
                      Virt_ComputerSystemIndicationProvider,
                      _BROKER,
                      libvirt_cim_init(),
                      &csi,
                      filters);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
