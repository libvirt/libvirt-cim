/*
 * Copyright IBM Corp. 2008
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include <uuid/uuid.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_invokemethod.h>
#include <libcmpiutil/std_instance.h>

#include "misc_util.h"

#include "Virt_VirtualSystemSnapshotService.h"
#include "Virt_HostSystem.h"

#define CIM_JOBSTATE_STARTING 3
#define CIM_JOBSTATE_RUNNING 4
#define CIM_JOBSTATE_COMPLETE 7

#define CIM_RETURN_COMPLETED 0
#define CIM_RETURN_FAILED 2

static const CMPIBroker *_BROKER;

struct snap_context {
        CMPIContext *context;
        char *domain;
        char uuid[33];
        char *save_path;
        char *ref_ns;
        char *ref_cn;

        bool save;
        bool restore;
};

static void snap_job_free(struct snap_context *ctx)
{
        free(ctx->domain);
        free(ctx->save_path);
        free(ctx->ref_ns);
        free(ctx->ref_cn);
        free(ctx);
}

static void _snap_job_set_status(struct snap_context *ctx,
                                 uint16_t state,
                                 const char *status,
                                 uint16_t errcode,
                                 const char *errdesc)
{
        CMPIInstance *inst;
        CMPIStatus s;
        CMPIObjectPath *op;
        char *desc = NULL;
        int ret;

        op = CMNewObjectPath(_BROKER,
                             ctx->ref_ns,
                             "CIM_ConcreteJob",
                             &s);
        if (s.rc != CMPI_RC_OK) {
                CU_DEBUG("Failed to create job path for update");
                return;
        }

        CMAddKey(op, "InstanceID", (CMPIValue *)ctx->uuid, CMPI_chars);

        inst = CBGetInstance(_BROKER, ctx->context, op, NULL, &s);
        if ((inst == NULL) || (s.rc != CMPI_RC_OK)) {
                CU_DEBUG("Failed to get job instance for update of %s",
                         ctx->uuid);
                return;
        }

        CMSetProperty(inst, "JobState",
                      (CMPIValue *)&state, CMPI_uint16);
        CMSetProperty(inst, "Status",
                      (CMPIValue *)status, CMPI_chars);

        ret = asprintf(&desc,
                       "%s of %s (%s)",
                       ctx->save ? "Snapshot" : "Restore",
                       ctx->domain,
                       ctx->save_path);
        if (ret != -1) {
                CMSetProperty(inst, "Description",
                              (CMPIValue *)desc, CMPI_chars);
                free(desc);
        }

        if ((errcode != 0) && (errdesc != NULL)) {
                CMSetProperty(inst, "ErrorCode",
                              (CMPIValue *)&errcode, CMPI_uint16);
                CMSetProperty(inst, "ErrorDescription",
                              (CMPIValue *)errdesc, CMPI_chars);

                CU_DEBUG("Set error properties to %i:%s",
                         errcode,
                         errdesc);
        }

        s = CBModifyInstance(_BROKER, ctx->context, op, inst, NULL);
        if (s.rc != CMPI_RC_OK) {
                CU_DEBUG("Failed to update job instance %s: %s",
                         ctx->uuid,
                         CMGetCharPtr(s.msg));
                return;
        }

        CU_DEBUG("Set %s status to %i:%s", ctx->uuid, state, status);
}

static void snap_job_set_status(struct snap_context *ctx,
                                uint16_t state,
                                const char *status)
{
        _snap_job_set_status(ctx, state, status, 0, NULL);
}

static void snap_job_set_failed(struct snap_context *ctx,
                                uint16_t errcode,
                                const char *errdesc)
{
        _snap_job_set_status(ctx,
                             CIM_JOBSTATE_COMPLETE,
                             "Failed",
                             errcode,
                             errdesc);
}

static void do_snapshot(struct snap_context *ctx,
                        virConnectPtr conn,
                        virDomainPtr dom)
{
        int ret;

        if (ctx->save) {
                CU_DEBUG("Starting save to %s", ctx->save_path);

                ret = virDomainSave(dom, ctx->save_path);
                if (ret == -1) {
                        CU_DEBUG("Save failed");
                        snap_job_set_failed(ctx,
                                            VIR_VSSS_ERR_SAVE_FAILED,
                                            "Save failed");
                        return;
                }

                CU_DEBUG("Save completed");
                snap_job_set_status(ctx,
                                    CIM_JOBSTATE_RUNNING,
                                    "Save finished");
        }

        if (ctx->restore) {
                CU_DEBUG("Starting restore from %s", ctx->save_path);

                ret = virDomainRestore(conn, ctx->save_path);
                if (ret == -1) {
                        CU_DEBUG("Restore failed");
                        snap_job_set_failed(ctx,
                                            VIR_VSSS_ERR_REST_FAILED,
                                            "Restore failed");
                        return;
                }

                CU_DEBUG("Restore completed");
                snap_job_set_status(ctx,
                                    CIM_JOBSTATE_RUNNING,
                                    "Restore finished");
        }

        CU_DEBUG("Snapshot (%s/%s) completed",
                 ctx->save ? "Save" : "None",
                 ctx->restore ? "Restore" : "None");

        snap_job_set_status(ctx,
                            CIM_JOBSTATE_COMPLETE,
                            "Snapshot complete");

        return;
}

static CMPI_THREAD_RETURN snapshot_thread(struct snap_context *ctx)
{
        CMPIStatus s;
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;

        CU_DEBUG("Snapshot thread alive");

        CBAttachThread(_BROKER, ctx->context);

        snap_job_set_status(ctx, CIM_JOBSTATE_RUNNING, "Running");

        conn = connect_by_classname(_BROKER, ctx->ref_cn, &s);
        if (conn == NULL) {
                CU_DEBUG("Failed to connect with classname `%s'", ctx->ref_cn);
                snap_job_set_failed(ctx,
                                    VIR_VSSS_ERR_CONN_FAILED,
                                    "Unable to connect to hypervisor");
                goto out;
        }

        dom = virDomainLookupByName(conn, ctx->domain);
        if (dom == NULL) {
                CU_DEBUG("No such domain `%s'", ctx->domain);
                snap_job_set_failed(ctx,
                                    VIR_VSSS_ERR_NO_SUCH_DOMAIN,
                                    "No such domain");
                goto out;
        }

        do_snapshot(ctx, conn, dom);

 out:
        virDomainFree(dom);
        virConnectClose(conn);

        snap_job_free(ctx);

        return NULL;
}

static CMPIStatus create_job(const CMPIContext *context,
                             const CMPIObjectPath *ref,
                             struct snap_context *ctx,
                             CMPIObjectPath **job)
{
        CMPIObjectPath *op;
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        op = CMNewObjectPath(_BROKER,
                             NAMESPACE(ref),
                             "CIM_ConcreteJob", /*FIXME*/
                             &s);
        if ((s.rc != CMPI_RC_OK) || (op == NULL)) {
                CU_DEBUG("Failed to create job path");
                goto out;
        }

        inst = CMNewInstance(_BROKER, op, &s);
        if ((s.rc != CMPI_RC_OK) || (inst == NULL)) {
                CU_DEBUG("Failed to create job instance");
                goto out;
        }

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)ctx->uuid, CMPI_chars);

        CMSetProperty(inst, "Name",
                      (CMPIValue *)"Snapshot", CMPI_chars);

        CMSetProperty(inst, "Status",
                      (CMPIValue *)"Queued", CMPI_chars);

        op = CMGetObjectPath(inst, &s);
        if ((op == NULL) || (s.rc != CMPI_RC_OK)) {
                CU_DEBUG("Failed to get path of job instance");
                goto out;
        }

        CMSetNameSpace(op, NAMESPACE(ref));

        CU_DEBUG("ref was %s", CMGetCharPtr(CMObjectPathToString(op, NULL)));

        *job = CBCreateInstance(_BROKER, context, op, inst, &s);
        if ((*job == NULL) || (s.rc != CMPI_RC_OK)) {
                CU_DEBUG("Failed to create job");
                goto out;
        }

        ctx->ref_ns = strdup(NAMESPACE(ref));
        ctx->ref_cn = strdup(CLASSNAME(ref));

        ctx->context = CBPrepareAttachThread(_BROKER, context);

        _BROKER->xft->newThread((void *)snapshot_thread, ctx, 0);
 out:
        return s;
}

char *vsss_get_save_path(const char *domname)
{
        int ret;
        char *path = NULL;

        ret = asprintf(&path,
                       "/var/lib/libvirt/%s.save", domname);
        if (ret == -1)
                return NULL;

        return path;
}

static struct snap_context *new_context(const char *name,
                                        CMPIStatus *s)
{
        struct snap_context *ctx;
        uuid_t uuid;

        ctx = calloc(1, sizeof(*ctx));
        if (ctx == NULL) {
                CU_DEBUG("Failed to alloc snapshot context");
                goto out;
        }

        ctx->domain = strdup(name);

        uuid_generate(uuid);
        uuid_unparse(uuid, ctx->uuid);

        ctx->save_path = vsss_get_save_path(ctx->domain);
        if (ctx->save_path == NULL) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get save_path");
                goto out;
        }

        cu_statusf(_BROKER, s,
                   CMPI_RC_OK,
                   "");
 out:
        if (s->rc != CMPI_RC_OK) {
                snap_job_free(ctx);
                ctx = NULL;
        }

        return ctx;
}

static CMPIStatus start_snapshot_job(const CMPIObjectPath *ref,
                                     const CMPIContext *context,
                                     const char *name,
                                     uint16_t type)
{
        struct snap_context *ctx;
        CMPIStatus s;
        CMPIObjectPath *job;

        ctx = new_context(name, &s);
        if (ctx == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to create snapshot context");
                goto out;
        }

        ctx->save = (type != 0);
        ctx->restore = (type != VIR_VSSS_SNAPSHOT_MEM);

        s = create_job(context, ref, ctx, &job);

 out:
        return s;
}

static CMPIStatus create_snapshot(CMPIMethodMI *self,
                                  const CMPIContext *context,
                                  const CMPIResult *results,
                                  const CMPIObjectPath *reference,
                                  const CMPIArgs *argsin,
                                  CMPIArgs *argsout)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIObjectPath *system;
        CMPIInstance *sd;
        uint16_t type;
        uint32_t retcode = CIM_RETURN_FAILED;
        const char *name;

        if (cu_get_u16_arg(argsin, "SnapshotType", &type) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Missing SnapshotType");
                goto out;
        }

        if ((type != VIR_VSSS_SNAPSHOT_MEM) &&
            (type != VIR_VSSS_SNAPSHOT_MEMT)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_SUPPORTED,
                           "Only memory(%i,%i) snapshots are supported",
                           VIR_VSSS_SNAPSHOT_MEM,
                           VIR_VSSS_SNAPSHOT_MEMT);
                goto out;
        }

        if (cu_get_ref_arg(argsin, "AffectedSystem", &system) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Missing AffectedSystem");
                goto out;
        }

        if (cu_get_inst_arg(argsin, "SnapshotSettings", &sd) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Missing SnapshotSettings");
                goto out;
        }

        if (cu_get_str_path(system, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Missing Name property of AffectedSystem");
                goto out;
        }

        s = start_snapshot_job(reference, context, name, type);

        retcode = CIM_RETURN_COMPLETED;
 out:
        CMReturnData(results, (CMPIValue *)&retcode, CMPI_uint32);

        return s;
}

static CMPIStatus destroy_snapshot(CMPIMethodMI *self,
                                   const CMPIContext *context,
                                   const CMPIResult *results,
                                   const CMPIObjectPath *reference,
                                   const CMPIArgs *argsin,
                                   CMPIArgs *argsout)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIObjectPath *snap;
        char *name = NULL;
        char *path = NULL;
        uint32_t retcode = CIM_RETURN_FAILED;

        if (cu_get_ref_arg(argsin, "AffectedSnapshot", &snap) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Missing Snapshot");
                goto out;
        }

        if (!parse_instanceid(snap, NULL, &name)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid InstanceID in Snapshot");
                goto out;
        }

        path = vsss_get_save_path(name);
        if (path == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get save_path");
                goto out;
        }

        if (unlink(path) == -1) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to remove snapshot: %s", path);
        }

        retcode = CIM_RETURN_COMPLETED;
 out:
        CMReturnData(results, (CMPIValue *)&retcode, CMPI_uint32);

        free(path);
        free(name);

        return s;
}

static CMPIStatus apply_snapshot(CMPIMethodMI *self,
                                 const CMPIContext *context,
                                 const CMPIResult *results,
                                 const CMPIObjectPath *reference,
                                 const CMPIArgs *argsin,
                                 CMPIArgs *argsout)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIObjectPath *snap;
        char *name = NULL;
        uint32_t retcode = CIM_RETURN_FAILED;

        if (cu_get_ref_arg(argsin, "AffectedSnapshot", &snap) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Missing Snapshot");
                goto out;
        }

        if (!parse_instanceid(snap, NULL, &name)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid InstanceID in Snapshot");
                goto out;
        }

        s = start_snapshot_job(reference, context, name, 0);

        retcode = CIM_RETURN_COMPLETED;

 out:
        CMReturnData(results, (CMPIValue *)&retcode, CMPI_uint32);

        free(name);

        return s;
}

static struct method_handler CreateSnapshot = {
        .name = "CreateSnapshot",
        .handler = create_snapshot,
        .args = {{"AffectedSystem", CMPI_ref, false},
                 {"SnapshotSettings", CMPI_instance, false},
                 {"SnapshotType", CMPI_uint16, false},
                 ARG_END}
};

static struct method_handler DestroySnapshot = {
        .name = "DestroySnapshot",
        .handler = destroy_snapshot,
        .args = {{"AffectedSnapshot", CMPI_ref, false},
                 ARG_END}
};

static struct method_handler ApplySnapshot = {
        .name = "ApplySnapshot",
        .handler = apply_snapshot,
        .args = {{"AffectedSnapshot", CMPI_ref, false},
                 ARG_END}
};

static struct method_handler *handlers[] = {
        &CreateSnapshot,
        &DestroySnapshot,
        &ApplySnapshot,
        NULL
};

STDIM_MethodMIStub(, Virt_VirtualSystemSnapshotService,
                   _BROKER, libvirt_cim_init(), handlers);

static CMPIStatus set_inst_properties(const CMPIBroker *broker,
                                      const CMPIObjectPath *reference,
                                      CMPIInstance *inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        const char *name;
        const char *ccname;

        s = get_host_system_properties(&name,
                                       &ccname,
                                       reference,
                                       broker);
        if (s.rc != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get host attributes");
                goto out;
        }

        CMSetProperty(inst, "Name",
                      (CMPIValue *)"SnapshotService", CMPI_chars);

        CMSetProperty(inst, "SystemName",
                      (CMPIValue *)name, CMPI_chars);

        CMSetProperty(inst, "SystemCreationClassName",
                      (CMPIValue *)ccname, CMPI_chars);

 out:
        return s;
}

CMPIStatus get_vsss(const CMPIBroker *broker,
                    const CMPIObjectPath *ref,
                    CMPIInstance **_inst,
                    bool is_get_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        virConnectPtr conn = NULL;

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
                                  "VirtualSystemSnapshotService",
                                  NAMESPACE(ref));
        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Can't create instance for %s", CLASSNAME(ref));
                goto out;
        }

        s = set_inst_properties(broker, ref, inst);

        if (is_get_inst) {
                s = cu_validate_ref(broker, ref, inst);
                if (s.rc != CMPI_RC_OK)
                        goto out;
        }

        *_inst = inst;
 out:
        virConnectClose(conn);

        return s;
}

static CMPIStatus return_vsss(const CMPIObjectPath *ref,
                              const CMPIResult *results,
                              bool names_only,
                              bool is_get_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        s = get_vsss(_BROKER, ref, &inst, is_get_inst);
        if ((s.rc != CMPI_RC_OK) || (inst == NULL))
                goto out;

        if (names_only)
                cu_return_instance_name(results, inst);
        else
                CMReturnInstance(results, inst);
 out:
        return s;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_vsss(reference, results, true, false);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{

        return return_vsss(reference, results, false, false);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        return return_vsss(reference, results, false, true);
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(,
                   Virt_VirtualSystemSnapshotService,
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
