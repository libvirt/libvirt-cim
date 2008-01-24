/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
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
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libvirt/libvirt.h>

#include <libcmpiutil/libcmpiutil.h>
#include <misc_util.h>
#include <libcmpiutil/std_indication.h>
#include <cs_util.h>

#include "config.h"

#include "Virt_ComputerSystem.h"
#include "Virt_ComputerSystemIndication.h"

static const CMPIBroker *_BROKER;

static CMPI_THREAD_TYPE lifecycle_thread_id = 0;

enum CS_EVENTS {CS_CREATED,
                CS_DELETED,
                CS_MODIFIED,
};

static pthread_cond_t lifecycle_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t lifecycle_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool lifecycle_enabled = 0;

#define WAIT_TIME 3
#define FAIL_WAIT_TIME 2

#ifdef CMPI_EI_VOID
# define _EI_RTYPE void
# define _EI_RET() return
#else
# define _EI_RTYPE CMPIStatus
# define _EI_RET() return (CMPIStatus){CMPI_RC_OK, NULL}
#endif

struct dom_xml {
        char uuid[VIR_UUID_STRING_BUFLEN];
        char *xml;
};

struct ind_args {
        CMPIContext *context;
        char *ns;
        char *classname;
};

static void free_dom_xml (struct dom_xml dom)
{
        free(dom.xml);
}

static void free_ind_args (struct ind_args **args)
{
        free((*args)->ns);
        free((*args)->classname);
        free(*args);
        *args = NULL;
}

static char *sys_name_from_xml(char *xml)
{
        char *tmp = NULL;
        char *name = NULL;
        int rc;

        tmp = strstr(xml, "<name>");
        if (tmp == NULL)
                goto out;

        rc = sscanf(tmp, "<name>%a[^<]s</name>", &name);
        if (rc != 1)
                name = NULL;

 out:        
        return name;
}

static CMPIStatus doms_to_xml(struct dom_xml **dom_xml_list, 
                              virDomainPtr *dom_ptr_list,
                              int dom_ptr_count)
{
        int i;
        int rc;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        *dom_xml_list = calloc(dom_ptr_count, sizeof(struct dom_xml));
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
                                                             0);
                if ((*dom_xml_list)[i].xml == NULL) {
                        cu_statusf(_BROKER, &s, 
                                   CMPI_RC_ERR_FAILED,
                                   "Failed to get xml desc");
                        break;
                }
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
                if (strcmp(cur_xml[i].uuid, prev_dom.uuid) != 0)
                        continue;
                
                if (strcmp(cur_xml[i].xml, prev_dom.xml) != 0)
                        ret = true;

                break;
        }
        
        return ret;
}

static bool _do_indication(const CMPIBroker *broker,
                           const CMPIContext *ctx,
                           CMPIInstance *affected_inst,
                           int ind_type,
                           const char *ind_type_name,
                           char *prefix,
                           char *ns)
{
        CMPIObjectPath *affected_op;
        CMPIObjectPath *ind_op;
        CMPIInstance *ind;
        CMPIStatus s;
        bool ret = true;

        ind = get_typed_instance(broker,
                                 prefix,
                                 ind_type_name,
                                 ns);

        /* Generally report errors and hope to continue, since we have no one 
           to actually return status to. */
        if (ind == NULL) {
                CU_DEBUG("Failed to create ind, type '%s:%s_%s'", 
                         ns,
                         prefix,
                         ind_type_name);
                ret = false;
        }

        ind_op = CMGetObjectPath(ind, &s);
        if (s.rc != CMPI_RC_OK) {
                CU_DEBUG("Failed to get ind_op.  Error: '%s'", s.msg);
                ret = false;
        }

        switch (ind_type) {
        case CS_CREATED:
        case CS_DELETED:
                affected_op = CMGetObjectPath(affected_inst, &s);
                if (s.rc != CMPI_RC_OK) {
                        ret = false;
                        CU_DEBUG("problem getting affected_op: '%s'", s.msg);
                        goto out;
                }
                CMSetProperty(ind, "AffectedSystem",
                              (CMPIValue *)&affected_op, CMPI_ref);
                break;
        case CS_MODIFIED:
                CMSetProperty(ind, "PreviousInstance",
                              (CMPIValue *)&affected_inst, CMPI_instance);
                break;
        }

        CU_DEBUG("Delivering Indication: %s",
                 CMGetCharPtr(CMObjectPathToString(ind_op, NULL)));

        CBDeliverIndication(broker,
                            ctx,
                            ns,
                            ind);
        CU_DEBUG("Delivered");

 out:
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

        return true;
}

static bool dom_in_list(char *uuid, int count, struct dom_xml *list)
{
        int i;

        for (i = 0; i < count; i++) {
                if (STREQ(uuid, list[i].uuid))
                        return true;
        }

        return false;
}

static bool async_ind(CMPIContext *context,
                      virConnectPtr conn,
                      int ind_type,
                      struct dom_xml prev_dom,
                      char *prefix,
                      char *ns)
{
        bool rc;
        char *name = NULL;
        char *type_name = NULL;
        CMPIInstance *affected_inst;

        affected_inst = get_typed_instance(_BROKER,
                                           prefix,
                                           "ComputerSystem",
                                           ns);

        name = sys_name_from_xml(prev_dom.xml);
        CU_DEBUG("Name for system: '%s'", name);
        if (name == NULL) {
                rc = false;
                goto out;
        }

        switch (ind_type) {
        case CS_CREATED:
                type_name = "ComputerSystemCreatedIndication";
                break;
        case CS_DELETED:
                type_name = "ComputerSystemDeletedIndication";
                break;
        case CS_MODIFIED:
                type_name = "ComputerSystemModifiedIndication";
                break;
        }

        CMSetProperty(affected_inst, "Name", 
                      (CMPIValue *)name, CMPI_chars);
        CMSetProperty(affected_inst, "UUID",
                      (CMPIValue *)prev_dom.uuid, CMPI_chars);

        rc = _do_indication(_BROKER, context, affected_inst, 
                            ind_type, type_name, prefix, ns);

 out:
        free(name);
        return rc;
}

static CMPI_THREAD_RETURN lifecycle_thread(void *params)
{
        struct ind_args *args = (struct ind_args *)params;
        CMPIContext *context = args->context;
        CMPIStatus s;
        int prev_count;
        int cur_count;
        virDomainPtr *tmp_list;
        struct dom_xml *cur_xml = NULL;
        struct dom_xml *prev_xml = NULL;
        virConnectPtr conn;
        char *prefix = class_prefix_name(args->classname);
        char *ns = args->ns;

        conn = connect_by_classname(_BROKER, args->classname, &s);
        if (conn == NULL) {
                CU_DEBUG("Failed to connect: %s", CMGetCharPtr(s.msg));
                goto out;
        }

        pthread_mutex_lock(&lifecycle_mutex);

        CBAttachThread(_BROKER, context);

        prev_count = get_domain_list(conn, &tmp_list);
        s = doms_to_xml(&prev_xml, tmp_list, prev_count);
        if (s.rc != CMPI_RC_OK)
                CU_DEBUG("doms_to_xml failed.  Attempting to continue.");
        free_domain_list(tmp_list, prev_count);
        free(tmp_list);

        CU_DEBUG("entering event loop");
        while (lifecycle_enabled) {
                int i;
                bool res;
                bool failure = false;

                cur_count = get_domain_list(conn, &tmp_list);
                s = doms_to_xml(&cur_xml, tmp_list, cur_count);
                if (s.rc != CMPI_RC_OK) {
                        CU_DEBUG("doms_to_xml failed. retry in %d seconds", 
                                 FAIL_WAIT_TIME);
                        failure = true;
                        goto fail;
                }

                free_domain_list(tmp_list, cur_count);
                free(tmp_list);

                for (i = 0; i < cur_count; i++) {
                        res = dom_in_list(cur_xml[i].uuid, prev_count, prev_xml);
                        if (!res)
                                async_ind(context, conn, CS_CREATED,
                                          cur_xml[i], prefix, ns);

                }

                for (i = 0; i < prev_count; i++) {
                        res = dom_in_list(prev_xml[i].uuid, cur_count, cur_xml);
                        if (!res)
                                async_ind(context, conn, CS_DELETED, 
                                          prev_xml[i], prefix, ns);
                }

                for (i = 0; i < prev_count; i++) {
                        res = dom_changed(prev_xml[i], cur_xml, cur_count);
                        if (res) {
                                async_ind(context, conn, CS_MODIFIED, 
                                          prev_xml[i], prefix, ns);

                        }
                        free_dom_xml(prev_xml[i]);
                }

        fail:
                if (failure) {
                        wait_for_event(FAIL_WAIT_TIME);
                } else {
                        free(prev_xml);
                        prev_xml = cur_xml;
                        prev_count = cur_count;

                        wait_for_event(WAIT_TIME);
                }
        }

 out:
        pthread_mutex_unlock(&lifecycle_mutex);
        free_ind_args(&args);
        free(prefix);
        virConnectClose(conn);

        lifecycle_thread_id = 0;

        return NULL;
}

static CMPIStatus ActivateFilter(CMPIIndicationMI* mi,
                                 const CMPIContext* ctx,
                                 const CMPISelectExp* se,
                                 const char *ns,
                                 const CMPIObjectPath* op,
                                 CMPIBoolean first)
{
        CU_DEBUG("ActivateFilter");
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct ind_args *args = malloc(sizeof(struct ind_args));

        if (CMIsNullObject(op)) {
                cu_statusf(_BROKER, &s, 
                           CMPI_RC_ERR_FAILED,
                           "No ObjectPath given");
                goto out;
        }
        args->ns = strdup(NAMESPACE(op));
        args->classname = strdup(CLASSNAME(op));

        if (lifecycle_thread_id == 0) {
                args->context = CBPrepareAttachThread(_BROKER, ctx);

                lifecycle_thread_id = _BROKER->xft->newThread(lifecycle_thread,
                                                              args,
                                                              0);
        }

 out:
        return s;
}

static CMPIStatus DeActivateFilter(CMPIIndicationMI* mi,
                                   const CMPIContext* ctx,
                                   const CMPISelectExp* se,
                                   const  char *ns,
                                   const CMPIObjectPath* op,
                                   CMPIBoolean last)
{
        return (CMPIStatus){CMPI_RC_OK, NULL};
}

static _EI_RTYPE EnableIndications(CMPIIndicationMI* mi,
                                   const CMPIContext *ctx)
{
        pthread_mutex_lock(&lifecycle_mutex);
        lifecycle_enabled = true;
        pthread_mutex_unlock(&lifecycle_mutex);

        CU_DEBUG("ComputerSystemIndication enabled");

        _EI_RET();
}

static _EI_RTYPE DisableIndications(CMPIIndicationMI* mi,
                                    const CMPIContext *ctx)
{
        pthread_mutex_lock(&lifecycle_mutex);
        lifecycle_enabled = false;
        pthread_mutex_unlock(&lifecycle_mutex);

        CU_DEBUG("ComputerSystemIndication disabled");

        _EI_RET();
}

static CMPIStatus trigger_indication(const CMPIContext *context)
{
        pthread_cond_signal(&lifecycle_cond);
        return(CMPIStatus){CMPI_RC_OK, NULL};
}

static struct std_indication_handler csi = {
        .raise_fn = NULL,
        .trigger_fn = trigger_indication,
};

DEFAULT_IND_CLEANUP();
DEFAULT_AF();
DEFAULT_MP();

STDI_IndicationMIStub(, 
                      Virt_ComputerSystemIndication,
                      _BROKER,
                      libvirt_cim_init(), 
                      &csi);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
