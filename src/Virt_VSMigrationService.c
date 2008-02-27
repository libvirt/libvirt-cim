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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uuid/uuid.h>

#include <libvirt/libvirt.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include "misc_util.h"
#include <libcmpiutil/std_instance.h>
#include <libcmpiutil/std_invokemethod.h>
#include <libcmpiutil/std_indication.h>

#include "Virt_VSMigrationService.h"
#include "Virt_HostSystem.h"
#include "Virt_VSMigrationSettingData.h"

#define CIM_JOBSTATE_STARTING 3
#define CIM_JOBSTATE_RUNNING 4
#define CIM_JOBSTATE_COMPLETE 7

#define METHOD_RETURN(r, v) do {                                        \
                uint32_t rc = v;                                        \
                CMReturnData(r, (CMPIValue *)&rc, CMPI_uint32);         \
        } while (0);

const static CMPIBroker *_BROKER;

struct migration_job {
        CMPIContext *context;
        char *domain;
        char *host;
        char *ref_cn;
        char *ref_ns;
        uint16_t type;
        char uuid[33];
};

static const char *transport_from_class(const char *cn)
{
        if (STARTS_WITH(cn, "Xen"))
                return "xen+ssh";
        else if (STARTS_WITH(cn, "KVM"))
                return "qemu+ssh";
        else
                return NULL;
}

static char *dest_uri(const char *cn,
                      const char *dest)
{
        char *uri;
        const char *tport = NULL;

        tport = transport_from_class(cn);
        if (tport == NULL) {
                CU_DEBUG("Failed to get transport for %s", cn);
                return NULL;
        }

        if (asprintf(&uri, "%s://%s/system", tport, dest) == -1)
                uri = NULL;

        return uri;
}

static CMPIStatus check_caps(virConnectPtr conn, virConnectPtr dconn)
{
        return (CMPIStatus){CMPI_RC_OK, NULL};
}

static CMPIStatus check_hver(virConnectPtr conn, virConnectPtr dconn)
{
        CMPIStatus s;
        unsigned long local;
        unsigned long remote;

        if (virConnectGetVersion(conn, &local)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get local Hypervisor version");
                goto out;
        }

        if (virConnectGetVersion(dconn, &remote)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get remote hypervisor version");
                goto out;
        }

        if (remote >= local) {
                CU_DEBUG("Version check OK (%lu >= %lu)", remote, local);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_OK,
                           "");
        } else {
                CU_DEBUG("Version check FAILED (%lu < %lu)", remote, local);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Remote hypervisor is older than local (%lu < %lu)",
                           remote, local);
        }

        cu_statusf(_BROKER, &s,
                   CMPI_RC_OK,
                   "");
 out:
        return s;
}

static CMPIStatus vs_migratable(const CMPIObjectPath *ref,
                                const char *domain,
                                const char *destination,
                                const CMPIResult *results,
                                CMPIArgs *argsout)
{
        CMPIStatus s;
        char *uri = NULL;
        virConnectPtr conn = NULL;
        virConnectPtr dconn = NULL;
        uint32_t retcode = 1;
        CMPIBoolean isMigratable = 0;

        uri = dest_uri(CLASSNAME(ref), destination);
        if (uri == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to construct a valid libvirt URI");
                goto out;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        dconn = virConnectOpen(uri);
        if (dconn == NULL) {
                CU_DEBUG("Failed to connect to remote host (%s)", uri);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to connect to remote host (%s)", uri);
                goto out;
        }

        s = check_hver(conn, dconn);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = check_caps(conn, dconn);
        if (s.rc != CMPI_RC_OK)
                goto out;

        retcode = 0;
        cu_statusf(_BROKER, &s,
                   CMPI_RC_OK,
                   "");
 out:
        CMReturnData(results, (CMPIValue *)&retcode, CMPI_uint32);

        isMigratable = (retcode == 0);
        CMAddArg(argsout, "IsMigratable",
                 (CMPIValue *)&isMigratable, CMPI_boolean);

        free(uri);
        virConnectClose(conn);
        virConnectClose(dconn);

        return s;
}

static CMPIStatus vs_migratable_host(CMPIMethodMI *self,
                                     const CMPIContext *ctx,
                                     const CMPIResult *results,
                                     const CMPIObjectPath *ref,
                                     const CMPIArgs *argsin,
                                     CMPIArgs *argsout)
{
        CMPIStatus s;
        const char *dhost = NULL;
        CMPIObjectPath *system;
        const char *name = NULL;

        cu_get_str_arg(argsin, "DestinationHost", &dhost);
        cu_get_ref_arg(argsin, "ComputerSystem", &system);

        if (cu_get_str_path(system, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing key (Name) in ComputerSystem");
                METHOD_RETURN(results, 1);
                return s;
        }

        if (!check_refs_pfx_match(ref, system)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid REF in ComputerSystem");
                METHOD_RETURN(results, 1);
                return s;
        }

        return vs_migratable(ref, name, dhost, results, argsout);
}

static CMPIStatus vs_migratable_system(CMPIMethodMI *self,
                                       const CMPIContext *ctx,
                                       const CMPIResult *results,
                                       const CMPIObjectPath *ref,
                                       const CMPIArgs *argsin,
                                       CMPIArgs *argsout)
{
        CMPIStatus s;
        CMPIObjectPath *dsys;
        CMPIObjectPath *sys;
        const char *dname;
        const char *name;

        cu_get_ref_arg(argsin, "DestinationSystem", &dsys);
        cu_get_ref_arg(argsin, "ComputerSystem", &sys);

        if (cu_get_str_path(dsys, "Name", &dname) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing key (Name) in DestinationSystem");
                METHOD_RETURN(results, 1);
                return s;
        }

        if (cu_get_str_path(sys, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing key (Name) in ComputerSystem");
                METHOD_RETURN(results, 1);
                return s;
        }

        if (!check_refs_pfx_match(ref, sys)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid REF in ComputerSystem");
                METHOD_RETURN(results, 1);
                return s;
        }

        return vs_migratable(ref, name, dname, results, argsout);
}

static bool raise_indication(const CMPIContext *context,
                             const char *base_type,
                             const char *ns,
                             CMPIInstance *inst,
                             CMPIInstance *ind)
{
        char *type;
        CMPIStatus s;

        CU_DEBUG("Setting SourceInstance");
        CMSetProperty(ind, "SourceInstance",
                      (CMPIValue *)&inst, CMPI_instance);

        /* Seems like this shouldn't be hardcoded. */
        type = get_typed_class("Xen", base_type);

        s = stdi_raise_indication(_BROKER, context, type, ns, ind);

        free(type);

        return s.rc == CMPI_RC_OK;
}

static CMPIInstance *prepare_indication(const CMPIBroker *broker,
                                        CMPIInstance *inst,
                                        char *ns,
                                        CMPIStatus *s)
{
        CMPIInstance *ind = NULL;
        CMPIInstance *prev_inst = NULL;

        CU_DEBUG("Creating indication.");
        /* Prefix needs to be dynamic */
        ind = get_typed_instance(broker,
                                 "Xen",
                                 "ComputerSystemMigrationIndication",
                                 ns);
        /* Prefix needs to be dynamic */
        if (ind == NULL) {
                CU_DEBUG("Failed to create ind, type '%s:%s_%s'", 
                         ns,
                         "Xen",
                         "ComputerSystemMigrationIndication");
        }

        /* Need to copy job inst before attaching as PreviousInstance because 
           otherwise the changes we are about to make to job inst are made 
           to PreviousInstance as well. */
        prev_inst = cu_dup_instance(_BROKER, inst, s);
        if (s->rc != CMPI_RC_OK || prev_inst == NULL) {
                CU_DEBUG("dup_instance failed (%i:%s)", s->rc, s->msg);
                ind = NULL;
                goto out;
        }

        CU_DEBUG("Setting PreviousInstance");
        CMSetProperty(ind, "PreviousInstance", 
                      (CMPIValue *)&prev_inst, CMPI_instance);

 out:
        return ind;
}

static void migrate_job_set_state(struct migration_job *job,
                                  uint16_t state,
                                  const char *status)
{
        CMPIInstance *inst;
        CMPIInstance *ind;
        bool rc;
        CMPIStatus s;
        CMPIObjectPath *op;

        op = CMNewObjectPath(_BROKER,
                             job->ref_ns,
                             "Virt_MigrationJob",
                             &s);
        if (s.rc != CMPI_RC_OK) {
                CU_DEBUG("Failed to create job path for update");
                return;
        }

        CMSetNameSpace(op, job->ref_ns);
        CMAddKey(op, "InstanceID", (CMPIValue *)job->uuid, CMPI_chars);

        CU_DEBUG("Getting job instance %s", job->uuid);
        CU_DEBUG("  OP: %s", CMGetCharPtr(CMObjectPathToString(op, NULL)));
        inst = CBGetInstance(_BROKER, job->context, op, NULL, &s);
        if ((inst == NULL) || (s.rc != CMPI_RC_OK)) {
                CU_DEBUG("Failed to get job instance for update (%i)", s.rc);
                return;
        }

        ind = prepare_indication(_BROKER, inst, job->ref_ns, &s);

        CMSetProperty(inst, "JobState",
                      (CMPIValue *)&state, CMPI_uint16);
        CMSetProperty(inst, "Status",
                      (CMPIValue *)status, CMPI_chars);

        CU_DEBUG("Modifying job %s (%i:%s)", job->uuid, state, status);

        s = CBModifyInstance(_BROKER, job->context, op, inst, NULL);
        if (s.rc != CMPI_RC_OK)
                CU_DEBUG("Failed to update job instance: %s",
                         CMGetCharPtr(s.msg));

        rc = raise_indication(job->context,
                              "ComputerSystemMigrationIndication",
                              job->ref_ns,
                              inst,
                              ind);
        if (!rc)
                CU_DEBUG("Failed to raise indication");
}

static CMPIStatus handle_migrate(virConnectPtr dconn,
                                 virDomainPtr dom,
                                 char *uri,
                                 int type,
                                 struct migration_job *job)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virDomainPtr ddom = NULL;

        CU_DEBUG("Migrating %s -> %s", job->domain, uri);
        ddom = virDomainMigrate(dom, dconn, type, NULL, NULL, 0);
        if (ddom == NULL) {
                CU_DEBUG("Migration failed");
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Migration Failed");
        }

        virDomainFree(ddom);

        return s;
}

static CMPIStatus prepare_offline_migrate(virDomainPtr dom,
                                          char **xml)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virDomainInfo info;
        int ret;

        ret = virDomainGetInfo(dom, &info);
        if (ret != 0) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unabled to get info for domain.");
                goto out;
        }

        if ((const int)info.state != VIR_DOMAIN_SHUTOFF) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Domain must be shutoff for offline migration.");
                goto out;
        }

        *xml = virDomainGetXMLDesc(dom, 0);
        if (*xml == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to retrieve domain XML.");
                goto out;
        }

 out:
        return s;
}

static CMPIStatus handle_offline_migrate(virConnectPtr dconn,
                                         virDomainPtr dom,
                                         char *uri,
                                         char *xml,
                                         struct migration_job *job)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virDomainPtr new_dom;

        if (domain_exists(dconn, job->domain)) {
                CU_DEBUG("This domain already exists on the target system.");
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "This domain already exists on the target system");
                goto out;
        }

        new_dom = virDomainDefineXML(dconn, xml);
        if (new_dom == NULL) {
                CU_DEBUG("Failed to define domain from XML");
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to create domain");
        }

 out:
        return s;
}

static CMPIStatus migrate_vs(struct migration_job *job)
{
        CMPIStatus s;
        virConnectPtr conn = NULL;
        virConnectPtr dconn = NULL;
        virDomainPtr dom = NULL;
        char *uri = NULL;
        char *xml = NULL;

        uri = dest_uri(job->ref_cn, job->host);
        if (uri == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to construct a valid libvirt URI");
                goto out;
        }

        conn = connect_by_classname(_BROKER, job->ref_cn, &s);
        if (conn == NULL)
                goto out;

        dom = virDomainLookupByName(conn, job->domain);
        if (dom == NULL) {
                CU_DEBUG("Failed to lookup `%s'", job->domain);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to lookup domain `%s'", job->domain);
                goto out;
        }

        if (job->type == CIM_MIGRATE_OTHER) {
                s = prepare_offline_migrate(dom, &xml);
                if (s.rc != CMPI_RC_OK)
                        goto out;
        }

        dconn = virConnectOpen(uri);
        if (dconn == NULL) {
                CU_DEBUG("Failed to connect to remote host (%s)", uri);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to connect to remote host (%s)", uri);
                goto out;
        }

        switch(job->type) {
        case CIM_MIGRATE_OTHER:
                CU_DEBUG("Preparing for offline migration");
                s = handle_offline_migrate(dconn, dom, uri, xml, job);
                break;
        case CIM_MIGRATE_LIVE:
                CU_DEBUG("Preparing for live migration");
                s = handle_migrate(dconn, dom, uri, VIR_MIGRATE_LIVE, job);
                break;
        case CIM_MIGRATE_RESUME:
        case CIM_MIGRATE_RESTART:
                CU_DEBUG("Preparing for static migration");
                s = handle_migrate(dconn, dom, uri, 0, job);
                break;
        default: 
                CU_DEBUG("Unsupported migration type (%d)", job->type);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unsupported migration type (%d)", job->type);
                goto out;
        }

        if (s.rc != CMPI_RC_OK)
                goto out;

        CU_DEBUG("Migration succeeded");
        cu_statusf(_BROKER, &s,
                   CMPI_RC_OK,
                   "");

 out:
        free(uri);
        free(xml);
        virDomainFree(dom);
        virConnectClose(conn);
        virConnectClose(dconn);

        return s;
}

static CMPI_THREAD_RETURN migration_thread(struct migration_job *job)
{
        CMPIStatus s;

        CBAttachThread(_BROKER, job->context);

        CU_DEBUG("Migration Job %s started", job->uuid);
        migrate_job_set_state(job, CIM_JOBSTATE_RUNNING, "Running");

        s = migrate_vs(job);

        CU_DEBUG("Migration Job %s finished: %i", job->uuid, s.rc);
        if (s.rc != CMPI_RC_OK)
                migrate_job_set_state(job,
                                      CIM_JOBSTATE_COMPLETE,
                                      CMGetCharPtr(s.msg));
        else
                migrate_job_set_state(job,
                                      CIM_JOBSTATE_COMPLETE,
                                      "Completed");

        free(job->domain);
        free(job->host);
        free(job->ref_cn);
        free(job->ref_ns);
        free(job);

        return NULL;
}

static CMPIInstance *_migrate_job_new_instance(const char *cn,
                                               const char *ns)
{
        CMPIObjectPath *op;
        CMPIInstance *inst;
        CMPIStatus s;

        op = CMNewObjectPath(_BROKER, ns, cn, &s);
        if ((s.rc != CMPI_RC_OK) || (CMIsNullObject(op))) {
                CU_DEBUG("Failed to create ref: %s:%s", ns, cn);
                return NULL;
        }

        inst = CMNewInstance(_BROKER, op, &s);
        if ((s.rc != CMPI_RC_OK) || (CMIsNullObject(op))) {
                CU_DEBUG("Failed to create instance from ref: %s",
                         CMGetCharPtr(CMObjectPathToString(op, NULL)));
                return NULL;
        }

        return inst;
}

static CMPIStatus migrate_create_job_instance(const CMPIContext *context,
                                              struct migration_job *job,
                                              CMPIObjectPath **job_op)
{
        CMPIStatus s;
        CMPIInstance *jobinst;
        CMPIDateTime *start;
        CMPIBoolean autodelete = true;
        uint16_t state = CIM_JOBSTATE_STARTING;

        start = CMNewDateTime(_BROKER, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(start)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get job start time");
                goto out;
        }

        jobinst = _migrate_job_new_instance("Virt_MigrationJob",
                                            job->ref_ns);
        if (jobinst == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get instance of MigrationJob");
                goto out;
        }

        CMSetProperty(jobinst, "InstanceID",
                      (CMPIValue *)job->uuid, CMPI_chars);
        CMSetProperty(jobinst, "Name",
                      (CMPIValue *)"Migration", CMPI_chars);
        CMSetProperty(jobinst, "StartTime",
                      (CMPIValue *)&start, CMPI_dateTime);
        CMSetProperty(jobinst, "JobState",
                      (CMPIValue *)&state, CMPI_uint16);
        CMSetProperty(jobinst, "Status",
                      (CMPIValue *)"Queued", CMPI_chars);
        CMSetProperty(jobinst, "DeleteOnCompletion",
                      (CMPIValue *)&autodelete, CMPI_boolean);

        *job_op = CMGetObjectPath(jobinst, &s);
        if ((*job_op == NULL) || (s.rc != CMPI_RC_OK)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get path for job instance");
                goto out;
        }

        CMSetNameSpace(*job_op, job->ref_ns);

        CU_DEBUG("Creating instance: %s",
                 CMGetCharPtr(CMObjectPathToString(*job_op, NULL)));

        *job_op = CBCreateInstance(_BROKER, context, *job_op, jobinst, &s);
        if ((s.rc != CMPI_RC_OK) || (CMIsNullObject(*job_op))) {
                CU_DEBUG("Failed to create job instance: %i", s.rc);
                goto out;
        }

 out:
        return s;
}

static struct migration_job *migrate_job_prepare(const CMPIContext *context,
                                                 const CMPIObjectPath *ref,
                                                 const char *domain,
                                                 const char *host,
                                                 uint16_t type)
{
        struct migration_job *job;
        uuid_t uuid;

        job = malloc(sizeof(*job));
        if (job == NULL)
                return NULL;

        job->domain = strdup(domain);
        job->host = strdup(host);
        job->ref_cn = strdup(CLASSNAME(ref));
        job->ref_ns = strdup(NAMESPACE(ref));
        job->type = type;

        uuid_generate(uuid);
        uuid_unparse(uuid, job->uuid);

        job->context = CBPrepareAttachThread(_BROKER, context);

        return job;
}

static CMPIStatus migrate_do(const CMPIObjectPath *ref,
                             const CMPIContext *context,
                             const char *domain,
                             const char *host,
                             uint16_t type,
                             const CMPIResult *results,
                             CMPIArgs *argsout)
{
        CMPIStatus s;
        CMPIObjectPath *job_op;
        struct migration_job *job;
        CMPI_THREAD_TYPE thread;
        uint32_t retcode = 1;

        job = migrate_job_prepare(context, ref, domain, host, type);
        if (job == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to prepare migration job");
                goto out;
        }

        CU_DEBUG("Prepared migration job %s", job->uuid);

        s = migrate_create_job_instance(context, job, &job_op);
        if (s.rc != CMPI_RC_OK)
                goto out;

        CMAddArg(argsout, "Job", (CMPIValue *)&job_op, CMPI_ref);

        thread = _BROKER->xft->newThread((void*)migration_thread, job, 0);

        retcode = 0;

 out:
        CMReturnData(results, (CMPIValue *)&retcode, CMPI_uint32);

        return s;
}

static CMPIStatus get_migration_type(const CMPIObjectPath *ref,
                                     const CMPIArgs *argsin,
                                     uint16_t *type)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *msd;
        int ret;
        
        ret = cu_get_inst_arg(argsin, "MigrationSettingData", &msd);
        if ((ret != CMPI_RC_OK) || (msd == NULL)) {
                CU_DEBUG("Using default values for MigrationSettingData param");
                s = get_migration_sd(ref, &msd, _BROKER, false);
                if ((s.rc != CMPI_RC_OK) || (msd == NULL)) {
                        cu_statusf(_BROKER, &s,
                                   s.rc,
                                   "Unable to get default setting data values");
                        return s;
                }
        }

        ret = cu_get_u16_prop(msd, "MigrationType", type);
        if (ret != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           ret,
                           "Invalid MigrationType value");
        }

        return s;
}

static CMPIStatus migrate_vs_host(CMPIMethodMI *self,
                                  const CMPIContext *ctx,
                                  const CMPIResult *results,
                                  const CMPIObjectPath *ref,
                                  const CMPIArgs *argsin,
                                  CMPIArgs *argsout)
{
        CMPIStatus s;
        const char *dhost = NULL;
        CMPIObjectPath *system;
        const char *name = NULL;
        uint16_t type;
          
        cu_get_str_arg(argsin, "DestinationHost", &dhost);
        cu_get_ref_arg(argsin, "ComputerSystem", &system);

        if (cu_get_str_path(system, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing key (Name) in ComputerSystem");
                METHOD_RETURN(results, 1);
                return s;
        }

        if (!check_refs_pfx_match(ref, system)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid REF in ComputerSystem");
                METHOD_RETURN(results, 1);
                return s;
        }

        s = get_migration_type(ref, argsin, &type);
        if (s.rc != CMPI_RC_OK) {
                METHOD_RETURN(results, 1);
                return s;
        }

        return migrate_do(ref, ctx, name, dhost, type, results, argsout);
}

static CMPIStatus migrate_vs_system(CMPIMethodMI *self,
                                    const CMPIContext *ctx,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *ref,
                                    const CMPIArgs *argsin,
                                    CMPIArgs *argsout)
{
        CMPIStatus s;
        CMPIObjectPath *dsys;
        CMPIObjectPath *sys;
        const char *dname;
        const char *name;
        uint16_t type;

        cu_get_ref_arg(argsin, "DestinationSystem", &dsys);
        cu_get_ref_arg(argsin, "ComputerSystem", &sys);

        if (cu_get_str_path(dsys, "Name", &dname) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing key (Name) in DestinationSystem");
                METHOD_RETURN(results, 1);
                return s;
        }

        if (cu_get_str_path(sys, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing key (Name) in ComputerSystem");
                METHOD_RETURN(results, 1);
                return s;
        }

        if (!check_refs_pfx_match(ref, sys)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid REF in ComputerSystem");
                METHOD_RETURN(results, 1);
                return s;
        }

        s = get_migration_type(ref, argsin, &type);
        if (s.rc != CMPI_RC_OK) {
                METHOD_RETURN(results, 1);
                return s;
        }

        return migrate_do(ref, ctx, name, dname, type, results, argsout);
}

static struct method_handler vsimth = {
        .name = "CheckVirtualSystemIsMigratableToHost",
        .handler = vs_migratable_host,
        .args = {{"ComputerSystem", CMPI_ref, false},
                 {"DestinationHost", CMPI_string, false},
                 {"MigrationSettingData", CMPI_instance, true},
                 ARG_END
        }
};

static struct method_handler vsimts = {
        .name = "CheckVirtualSystemIsMigratableToSystem",
        .handler = vs_migratable_system,
        .args = {{"ComputerSystem", CMPI_ref, false},
                 {"DestinationSystem", CMPI_ref, false},
                 {"MigrationSettingData", CMPI_instance, true},
                 ARG_END
        }
};

static struct method_handler mvsth = {
        .name = "MigrateVirtualSystemToHost",
        .handler = migrate_vs_host,
        .args = {{"ComputerSystem", CMPI_ref, false},
                 {"DestinationHost", CMPI_string, false},
                 {"MigrationSettingData", CMPI_instance, true},
                 ARG_END
        }
};

static struct method_handler mvsts = {
        .name = "MigrateVirtualSystemToSystem",
        .handler = migrate_vs_system,
        .args = {{"ComputerSystem", CMPI_ref, false},
                 {"DestinationSystem", CMPI_ref, false},
                 {"MigrationSettingData", CMPI_instance, true},
                 ARG_END
        }
};

static struct method_handler *my_handlers[] = {
        &vsimth,
        &vsimts,
        &mvsth,
        &mvsts,
        NULL
};

STDIM_MethodMIStub(, Virt_VSMigrationService, _BROKER,
                   libvirt_cim_init(), my_handlers);

CMPIStatus get_migration_service(const CMPIObjectPath *ref,
                                 CMPIInstance **_inst,
                                 const CMPIBroker *broker,
                                 bool is_get_inst)
{
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;
        const char *name = NULL;
        const char *ccname = NULL;

        conn = connect_by_classname(broker, CLASSNAME(ref), &s);
        if (conn == NULL) {
                if (is_get_inst)
                        cu_statusf(broker, &s,
                                   CMPI_RC_ERR_NOT_FOUND,
                                   "No such instance");
                goto out;
        }

        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "VirtualSystemMigrationService",
                                  NAMESPACE(ref));
        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get instance for %s", CLASSNAME(ref));
                goto out;
        }

        s = get_host_system_properties(&name, 
                                       &ccname, 
                                       ref, 
                                       broker);
        if (s.rc != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get host attributes");
                goto out;
        }

        CMSetProperty(inst, "Name",
                      (CMPIValue *)"MigrationService", CMPI_chars);

        CMSetProperty(inst, "SystemName",
                      (CMPIValue *)name, CMPI_chars);

        CMSetProperty(inst, "SystemCreationClassName",
                      (CMPIValue *)ccname, CMPI_chars);

        if (is_get_inst) {
                s = cu_validate_ref(broker, ref, inst);
                if (s.rc != CMPI_RC_OK)
                        goto out;
        }
 
        cu_statusf(broker, &s,
                   CMPI_RC_OK,
                   "");

        *_inst = inst;

 out:
        virConnectClose(conn);

        return s;
}

static CMPIStatus return_vsms(const CMPIObjectPath *ref,
                              const CMPIResult *results,
                              bool name_only,
                              bool is_get_inst)
{
        CMPIInstance *inst = NULL;
        CMPIStatus s;

        s = get_migration_service(ref, &inst, _BROKER, is_get_inst);
        if ((s.rc != CMPI_RC_OK) || (inst == NULL))
                goto out;

        if (name_only)
                cu_return_instance_name(results, inst);
        else
                CMReturnInstance(results, inst);
 out:
        return s;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *ref)
{
        return return_vsms(ref, results, true, false);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *ref,
                                const char **properties)
{

        return return_vsms(ref, results, false, false);
}


static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *ref,
                              const char **properties)
{
        return return_vsms(ref, results, false, true);
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(, 
                   Virt_VSMigrationService,
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
