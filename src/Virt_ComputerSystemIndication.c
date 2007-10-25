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

#include <libcmpiutil.h>
#include <misc_util.h>
#include <std_indication.h>
#include <cs_util.h>

#include "config.h"

#include "Virt_ComputerSystem.h"
#include "Virt_ComputerSystemIndication.h"

static const CMPIBroker *_BROKER;

static CMPI_THREAD_TYPE lifecycle_thread_id = 0;

enum CS_EVENTS {CS_CREATED,
                CS_DELETED,
};

static pthread_cond_t lifecycle_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t lifecycle_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool lifecycle_enabled = 0;

#ifdef CMPI_EI_VOID
# define _EI_RTYPE void
# define _EI_RET() return
#else
# define _EI_RTYPE CMPIStatus
# define _EI_RET() return (CMPIStatus){CMPI_RC_OK, NULL}
#endif

static bool _lifecycle_indication(const CMPIBroker *broker,
                                  const CMPIContext *ctx,
                                  const CMPIObjectPath *newsystem,
                                  char *type)
{
        CMPIObjectPath *ind_op;
        CMPIInstance *ind;
        CMPIStatus s;

        ind = get_typed_instance(broker, type, NAMESPACE(newsystem));
        if (ind == NULL) {
                printf("Failed to create ind\n");
                return false;
        }

        ind_op = CMGetObjectPath(ind, &s);
        if (s.rc != CMPI_RC_OK) {
                printf("Failed to get ind_op\n");
                return false;
        }

        CMSetProperty(ind, "AffectedSystem",
                      (CMPIValue *)&newsystem, CMPI_ref);

        printf("Delivering Indication: %s\n",
               CMGetCharPtr(CMObjectPathToString(ind_op, NULL)));

        CBDeliverIndication(_BROKER,
                            ctx,
                            NAMESPACE(newsystem),
                            ind);


        return true;
}

static bool wait_for_event(void)
{
        struct timespec timeout;
        int ret;


        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 3;

        ret = pthread_cond_timedwait(&lifecycle_cond,
                                     &lifecycle_mutex,
                                     &timeout);

        return true;
}

static bool dom_in_list(virDomainPtr dom, int count, virDomainPtr *list)
{
        const char *_name;
        int i;

        _name = virDomainGetName(dom);

        for (i = 0; i < count; i++) {
                const char *name;

                name = virDomainGetName(list[i]);
                if (STREQ(name, _name))
                        return true;
        }

        return false;
}

static bool async_ind(CMPIContext *context,
                      virConnectPtr conn,
                      const char *name,
                      int type)
{
        CMPIInstance *newinst;
        CMPIObjectPath *op;
        CMPIStatus s;
        const char *type_name;
        char *type_cn = NULL;
        const char *ns = "root/ibmsd";

        /* FIXME: Hmm, need to get the namespace a better way */

        if (type == CS_CREATED) {
                type_name = "ComputerSystemCreatedIndication";
                type_cn = get_typed_class(type_name);

                op = CMNewObjectPath(_BROKER, ns, type_cn, &s);
        } else if (type == CS_DELETED) {
                type_name = "ComputerSystemDeletedIndication";
                type_cn = get_typed_class(type_name);

                op = CMNewObjectPath(_BROKER, ns, type_cn, &s);
        } else {
                printf("Unknown event type: %i\n", type);
                return false;
        }

        if (type != CS_DELETED)
                newinst = instance_from_name(_BROKER, conn, (char *)name, op);
        else
                /* A deleted domain will have no instance to lookup */
                newinst = CMNewInstance(_BROKER, op, &s);

        op = CMGetObjectPath(newinst, NULL);

        free(type_cn);

        return _lifecycle_indication(_BROKER, context, op, (char *)type_name);
}

static CMPI_THREAD_RETURN lifecycle_thread(void *params)
{
        CMPIContext *context = (CMPIContext *)params;
        CMPIStatus s;
        int prev_count;
        int cur_count;
        virDomainPtr *prev_list;
        virDomainPtr *cur_list;
        virConnectPtr conn;

        conn = lv_connect(_BROKER, &s);
        if (conn == NULL) {
                printf("Failed to connect: %s\n", CMGetCharPtr(s.msg));
                return NULL;
        }

        pthread_mutex_lock(&lifecycle_mutex);

        CBAttachThread(_BROKER, context);

        prev_count = get_domain_list(conn, &prev_list);

        while (lifecycle_enabled) {
                int i;
                bool res;

                cur_count = get_domain_list(conn, &cur_list);

                for (i = 0; i < cur_count; i++) {
                        res = dom_in_list(cur_list[i], prev_count, prev_list);
                        if (!res)
                                async_ind(context,
                                          conn,
                                          virDomainGetName(cur_list[i]),
                                          CS_CREATED);
                }

                for (i = 0; i < prev_count; i++) {
                        res = dom_in_list(prev_list[i], cur_count, cur_list);
                        if (!res)
                                async_ind(context,
                                          conn,
                                          virDomainGetName(prev_list[i]),
                                          CS_DELETED);
                        virDomainFree(prev_list[i]);
                }

                free(prev_list);
                prev_list = cur_list;
                prev_count = cur_count;

                wait_for_event();
        }

        return NULL;
}

static CMPIStatus ActivateFilter(CMPIIndicationMI* mi,
                                 const CMPIContext* ctx,
                                 const CMPISelectExp* se,
                                 const char *ns,
                                 const CMPIObjectPath* op,
                                 CMPIBoolean first)
{
        if (lifecycle_thread_id == 0) {
                CMPIContext *thread_context;

                thread_context = CBPrepareAttachThread(_BROKER, ctx);

                lifecycle_thread_id = _BROKER->xft->newThread(lifecycle_thread,
                                                              thread_context,
                                                              0);
        }

        return (CMPIStatus){CMPI_RC_OK, NULL};
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

        printf("ComputerSystemIndication enabled\n");

        _EI_RET();
}

static _EI_RTYPE DisableIndications(CMPIIndicationMI* mi,
                                    const CMPIContext *ctx)
{
        pthread_mutex_lock(&lifecycle_mutex);
        lifecycle_enabled = false;
        pthread_mutex_unlock(&lifecycle_mutex);

        printf("ComputerSystemIndication disabled\n");

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

STDI_IndicationMIStub(, Virt_ComputerSystemIndicationProvider,
                      _BROKER, CMNoHook, &csi);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
