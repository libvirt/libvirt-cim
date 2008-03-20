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
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

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
#include "Virt_ComputerSystem.h"
#include "Virt_VSMigrationSettingData.h"
#include "svpc_types.h"

#include "config.h"

#define CIM_JOBSTATE_STARTING 3
#define CIM_JOBSTATE_RUNNING 4
#define CIM_JOBSTATE_COMPLETE 7

#define METHOD_RETURN(r, v) do {                                        \
                uint32_t rc = v;                                        \
                CMReturnData(r, (CMPIValue *)&rc, CMPI_uint32);         \
        } while (0);

const static CMPIBroker *_BROKER;

enum {
        MIG_CREATED,
        MIG_MODIFIED,
        MIG_DELETED,
};

struct migration_job {
        CMPIContext *context;
        char *domain;
        virConnectPtr conn;
        char *ref_cn;
        char *ref_ns;
        uint16_t type;
        char uuid[33];
};

static CMPIStatus get_msd(const CMPIObjectPath *ref,
                          const CMPIArgs *argsin,
                          CMPIInstance **msd)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int ret;

        ret = cu_get_inst_arg(argsin, "MigrationSettingData", msd);
        if ((ret == CMPI_RC_OK) && (*msd != NULL))
                goto out;

        s = get_migration_sd(ref, msd, _BROKER, false);
        if ((s.rc != CMPI_RC_OK) || (*msd == NULL)) {
                cu_statusf(_BROKER, &s,
                           s.rc,
                           "Unable to get default setting data values");
                goto out;
        }
        CU_DEBUG("Using default values for MigrationSettingData param");

 out:
        return s;
}

static CMPIStatus get_migration_type(CMPIInstance *msd,
                                     uint16_t *type)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int ret;

        ret = cu_get_u16_prop(msd, "MigrationType", type);
        if (ret != CMPI_RC_OK) {
                CU_DEBUG("Using default MigrationType: %d", CIM_MIGRATE_LIVE);
                *type = CIM_MIGRATE_LIVE;
        }

        return s;
}

static CMPIStatus get_migration_uri(CMPIInstance *msd,
                                    uint16_t *uri)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int ret;

        ret = cu_get_u16_prop(msd, "TransportType", uri);
        if (ret == CMPI_RC_OK)
                goto out;

        CU_DEBUG("Using default TransportType: %d", CIM_MIGRATE_URI_SSH);
        *uri = CIM_MIGRATE_URI_SSH;

 out:
        return s;
}

static char *dest_uri(const char *cn,
                      const char *dest,
                      uint16_t transport)
{
        const char *prefix;
        const char *tport = NULL;
        const char *param = "";
        char *uri = NULL;
        int rc;

        if (STARTS_WITH(cn, "Xen"))
                prefix = "xen";
        else if (STARTS_WITH(cn, "KVM"))
                prefix = "qemu";
        else
                return NULL;

        switch (transport) {
        case CIM_MIGRATE_URI_SSH: 
                tport = "ssh";
                break; 
        case CIM_MIGRATE_URI_TLS:
                tport = "tls";
                param = "?no_verify=1";
                break; 
        case CIM_MIGRATE_URI_TLS_STRICT:
                tport = "tls";
                break; 
        case CIM_MIGRATE_URI_UNIX:
                tport = "unix";
                break; 
        case CIM_MIGRATE_URI_TCP:
                tport = "tcp";
                break; 
        default:
                goto out;
        }

        rc = asprintf(&uri, "%s+%s://%s/system/%s", prefix, tport, dest, param);
        if (rc == -1)
                uri = NULL;

 out:
        return uri;
}

static CMPIStatus get_msd_values(const CMPIObjectPath *ref,
                                 const char *destination,
                                 const CMPIArgs *argsin,
                                 uint16_t *type,
                                 virConnectPtr *conn)
{
        CMPIStatus s;
        CMPIInstance *msd;
        uint16_t uri_type;
        char *uri = NULL;

        s = get_msd(ref, argsin, &msd);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = get_migration_type(msd, type);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = get_migration_uri(msd, &uri_type);
        if (s.rc != CMPI_RC_OK)
                goto out;

        uri = dest_uri(CLASSNAME(ref), destination, uri_type);
        if (uri == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to construct a valid libvirt URI");
                goto out;
        }

        *conn = virConnectOpen(uri);
        if (*conn == NULL) {
                CU_DEBUG("Failed to connect to remote host (%s)", uri);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to connect to remote host (%s)", uri);
                goto out;
        }

 out:
        free(uri);

        return s;
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

static bool is_valid_check(const char *path)
{
        struct stat s;

        if (stat(path, &s) != 0)
                return false;

        if (!S_ISREG(s.st_mode))
                return false;

        if ((s.st_mode & S_IXUSR) ||
            (s.st_mode & S_IXGRP) ||
            (s.st_mode & S_IXOTH))
                return true;
        else
                return false;
}

static void free_list(char **list, int count)
{
        int i;

        if (list == NULL)
                return;

        for (i = 0; i < count; i++)
                free(list[i]);

        free(list);
}

static char **list_migration_checks(int *count)
{
        DIR *dir;
        struct dirent *de;
        char **list = NULL;
        int len = 0;

        *count = 0;

        dir = opendir(MIG_CHECKS_DIR);
        if (dir == NULL) {
                CU_DEBUG("Unable to open migration checks dir: %s (%s)",
                         MIG_CHECKS_DIR,
                         strerror(errno));
                *count = 0;
                return NULL;
        }

        while ((de = readdir(dir)) != NULL) {
                int ret;
                char *path = NULL;

                if (de->d_name[0] == '.')
                        continue;

                if (*count == len) {
                        char **tmp;

                        len = (len * 2) + 1;
                        tmp = realloc(list, sizeof(char *) * len);
                        if (tmp == NULL) {
                                CU_DEBUG("Failed to alloc check list");
                                goto error;
                        }

                        list = tmp;
                }

                ret = asprintf(&path,
                               "%s/%s",
                               MIG_CHECKS_DIR,
                               de->d_name);
                if (ret == -1) {
                        CU_DEBUG("Failed to alloc path for check");
                        goto error;
                }

                if (is_valid_check(path)) {
                        list[*count] = path;
                        (*count) += 1;
                } else {
                        CU_DEBUG("Invalid check program: %s", path);
                        free(path);
                }
        }

        closedir(dir);

        return list;
 error:
        closedir(dir);

        free_list(list, *count);
        *count = 0;

        return NULL;
}

static CMPIStatus _call_check(virDomainPtr dom,
                              const char *prog,
                              const char *param_path)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        pid_t pid;
        int i;
        int rc = -1;

        pid = fork();
        if (pid == 0) {
                virConnectPtr conn = virDomainGetConnect(dom);
                const char *name = virDomainGetName(dom);
                const char *uri = virConnectGetURI(conn);

                if (setpgrp() == -1)
                        perror("setpgrp");

                execl(prog, prog, name, uri, param_path, NULL);
                CU_DEBUG("exec(%s) failed: %s", prog, strerror(errno));
                _exit(1);
        }

        for (i = 0; i < (MIG_CHECKS_TIMEOUT * 4); i++) {
                int status;
                if (waitpid(pid, &status, WNOHANG) != pid) {
                        usleep(250000);
                } else {
                        rc = WEXITSTATUS(status);
                        break;
                }
        }

        if (rc == -1) {
                CU_DEBUG("Killing off stale child %i", pid);
                killpg(pid, SIGKILL);
                waitpid(pid, NULL, WNOHANG);
        }

        if (rc != 0) {
                char *name = strdup(prog);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Migration check `%s' failed",
                           basename(name));
                free(name);
        }

        return s;
}

static CMPIStatus call_external_checks(virDomainPtr dom,
                                       const char *param_path)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        char **list = NULL;
        int count = 0;
        int i;

        list = list_migration_checks(&count);
        if (count < 0) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to execute migration checks");
                goto out;
        } else if (list == NULL) {
                goto out;
        }

        for (i = 0; i < count; i++) {
                CU_DEBUG("Calling migration check: %s", list[i]);
                s = _call_check(dom, list[i], param_path);
                if (s.rc != CMPI_RC_OK) {
                        CU_DEBUG("...Failed");
                        break;
                } else
                        CU_DEBUG("...OK");
        }
 out:
        free_list(list, count);

        return s;
}

static char *write_params(CMPIArray *array)
{
        int i;
        int fd;
        char *filename = strdup("/tmp/libvirtcim_mig.XXXXXX");
        FILE *file = NULL;

        if (filename == NULL) {
                CU_DEBUG("Unable to get temporary file");
                return NULL;
        }

        fd = mkstemp(filename);
        if (fd < 0) {
                CU_DEBUG("Unable to get temporary file: %s", strerror(errno));
                free(filename);
                filename = NULL;
                goto out;
        }

        file = fdopen(fd, "w");
        if (file == NULL) {
                CU_DEBUG("Unable to open temporary file: %s", strerror(errno));
                free(filename);
                filename = NULL;
                goto out;
        }

        for (i = 0; i < CMGetArrayCount(array, NULL); i++) {
                CMPIData d;
                CMPIStatus s;

                d = CMGetArrayElementAt(array, i, &s);
                if ((s.rc != CMPI_RC_OK) || CMIsNullValue(d)) {
                        CU_DEBUG("Unable to get array[%i]: %s",
                                 i,
                                 CMGetCharPtr(s.msg));
                        continue;
                }

                fprintf(file, "%s\n", CMGetCharPtr(d.value.string));
        }

 out:
        if (file != NULL)
                fclose(file);

        close(fd);

        return filename;
}

static char *get_parms_file(const CMPIObjectPath *ref,
                            const CMPIArgs *argsin)
{
        CMPIStatus s;
        CMPIArray *array;
        CMPIInstance *msd;

        s = get_msd(ref, argsin, &msd);
        if (s.rc != CMPI_RC_OK)
                return NULL;

        if (cu_get_array_prop(msd, "CheckParameters", &array) == CMPI_RC_OK)
                return write_params(array);
        else
                return NULL;
}

static CMPIStatus vs_migratable(const CMPIObjectPath *ref,
                                CMPIObjectPath *system,
                                const char *destination,
                                const CMPIResult *results,
                                const CMPIArgs *argsin,
                                CMPIArgs *argsout)
{
        CMPIStatus s;
        virConnectPtr conn = NULL;
        virConnectPtr dconn = NULL;
        uint32_t retcode = 1;
        CMPIBoolean isMigratable = 0;
        uint16_t type;
        virDomainPtr dom = NULL;
        CMPIInstance *dominst;
        const char *domain;
        char *path = NULL;

        if (cu_get_str_path(system, "Name", &domain) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing key (Name) in ComputerSystem");
                goto out;
        }

        s = get_msd_values(ref, destination, argsin, &type, &dconn);
        if (s.rc != CMPI_RC_OK)
                goto out;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        s = check_hver(conn, dconn);
        if (s.rc != CMPI_RC_OK)
                goto out;

        dom = virDomainLookupByName(conn, domain);
        if (dom == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such domain");
                goto out;
        }

        CMSetNameSpace(system, NAMESPACE(ref));
        s = get_domain_by_ref(_BROKER, system, &dominst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = check_caps(conn, dconn);
        if (s.rc != CMPI_RC_OK)
                goto out;

        path = get_parms_file(ref, argsin);
        s = call_external_checks(dom, path);
        if (s.rc != CMPI_RC_OK)
                goto out;

        retcode = CIM_SVPC_RETURN_COMPLETED;
        cu_statusf(_BROKER, &s,
                   CMPI_RC_OK,
                   "");
 out:
        CMReturnData(results, (CMPIValue *)&retcode, CMPI_uint32);

        isMigratable = (retcode == 0);
        CMAddArg(argsout, "IsMigratable",
                 (CMPIValue *)&isMigratable, CMPI_boolean);

        virDomainFree(dom);
        virConnectClose(conn);
        virConnectClose(dconn);

        if (path != NULL)
                unlink(path);
        free(path);

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

        cu_get_str_arg(argsin, "DestinationHost", &dhost);
        cu_get_ref_arg(argsin, "ComputerSystem", &system);

        if (!check_refs_pfx_match(ref, system)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid REF in ComputerSystem");
                METHOD_RETURN(results, 1);
                return s;
        }

        return vs_migratable(ref, system, dhost, results, argsin, argsout);
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

        cu_get_ref_arg(argsin, "DestinationSystem", &dsys);
        cu_get_ref_arg(argsin, "ComputerSystem", &sys);

        if (cu_get_str_path(dsys, "Name", &dname) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing key (Name) in DestinationSystem");
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

        return vs_migratable(ref, sys, dname, results, argsin, argsout);
}

static const char *ind_type_to_name(int ind_type)
{
        const char *ind_name = NULL;

        switch (ind_type) {
        case MIG_CREATED:
                ind_name = "ComputerSystemMigrationJobCreatedIndication";
                break;
        case MIG_DELETED:
                ind_name = "ComputerSystemMigrationJobDeletedIndication";
                break;
        case MIG_MODIFIED:
                ind_name = "ComputerSystemMigrationJobModifiedIndication";
                break;
        }
        
        return ind_name;
}

static bool raise_indication(const CMPIContext *context,
                             int ind_type,
                             const char *ns,
                             CMPIInstance *inst,
                             CMPIInstance *ind)
{
        char *type;
        CMPIStatus s;
        const char *ind_name = NULL;

        if (ind == NULL)
                return false;

        ind_name = ind_type_to_name(ind_type);

        CU_DEBUG("Setting SourceInstance");
        CMSetProperty(ind, "SourceInstance",
                      (CMPIValue *)&inst, CMPI_instance);

        /* Seems like this shouldn't be hardcoded. */
        type = get_typed_class("Xen", ind_name);

        s = stdi_raise_indication(_BROKER, context, type, ns, ind);

        free(type);

        return s.rc == CMPI_RC_OK;
}

static CMPIInstance *prepare_indication(const CMPIBroker *broker,
                                        CMPIInstance *inst,
                                        char *ns,
                                        int ind_type,
                                        CMPIStatus *s)
{
        const char *ind_name = NULL;
        CMPIInstance *ind = NULL;
        CMPIInstance *prev_inst = NULL;

        ind_name = ind_type_to_name(ind_type);

        CU_DEBUG("Creating indication.");
        /* Prefix needs to be dynamic */
        ind = get_typed_instance(broker, 
                                 "Xen", 
                                 ind_name, 
                                 ns);
        /* Prefix needs to be dynamic */
        if (ind == NULL) {
                CU_DEBUG("Failed to create ind, type '%s:%s_%s'", 
                         ns, "Xen", ind_name);
                goto out;
        }

        if (ind_type == MIG_MODIFIED) {
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
        }

 out:
        return ind;
}

static CMPIObjectPath *ref_from_job(struct migration_job *job,
                                    CMPIStatus *s)
{
        CMPIObjectPath *ref = NULL;
        
        ref = CMNewObjectPath(_BROKER,
                              job->ref_ns,
                              "Virt_MigrationJob",
                              s);
        if (s->rc != CMPI_RC_OK) {
                CU_DEBUG("Failed to create job ref for update");
                goto out;
        }

        CMSetNameSpace(ref, job->ref_ns);
        CMAddKey(ref, "InstanceID", (CMPIValue *)job->uuid, CMPI_chars);

        CU_DEBUG("  MigrationJob ref: %s", 
                 CMGetCharPtr(CMObjectPathToString(ref, NULL)));
        
 out:
        return ref;
}

static void raise_deleted_ind(struct migration_job *job)
{
        CMPIInstance *ind = NULL;
        CMPIInstance *inst = NULL;
        CMPIObjectPath *ref = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        bool rc;

        ref = ref_from_job(job, &s);
        if ((ref == NULL) || (s.rc != CMPI_RC_OK)) {
                CU_DEBUG("Failed to get job ref for delete");
                return;
        }
        inst = CBGetInstance(_BROKER, job->context, ref, NULL, &s);
        if ((inst == NULL) || (s.rc != CMPI_RC_OK)) {
                CU_DEBUG("Failed to get job instance for delete (%i)", s.rc);
                return;
        }

        ind = prepare_indication(_BROKER, inst, job->ref_ns, MIG_DELETED, &s);
        
        rc = raise_indication(job->context, MIG_MODIFIED, job->ref_ns, 
                              inst, ind);
        if (!rc)
                CU_DEBUG("Failed to raise indication");

        return;
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

        op = ref_from_job(job, &s);
        if ((op == NULL) || (s.rc != CMPI_RC_OK)) {
                CU_DEBUG("Failed to get job ref for update");
                return;
        }
        inst = CBGetInstance(_BROKER, job->context, op, NULL, &s);
        if ((inst == NULL) || (s.rc != CMPI_RC_OK)) {
                CU_DEBUG("Failed to get job instance for update (%i)", s.rc);
                return;
        }

        ind = prepare_indication(_BROKER, inst, job->ref_ns, MIG_MODIFIED, &s);

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
                              MIG_MODIFIED, 
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
        virDomainInfo info;
        int ret;

        ret = virDomainGetInfo(dom, &info);
        if (ret == -1) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Error getting domain info");
                goto out;
        }

        if ((const int)info.state == VIR_DOMAIN_SHUTOFF) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Domain must not be shut off for live migration");
                goto out;
        }

        CU_DEBUG("Migrating %s -> %s", job->domain, uri);
        ddom = virDomainMigrate(dom, dconn, type, NULL, NULL, 0);
        if (ddom == NULL) {
                CU_DEBUG("Migration failed");
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Migration Failed");
        }
 out:
        virDomainFree(ddom);

        return s;
}

static CMPIStatus prepare_migrate(virDomainPtr dom,
                                  char **xml)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};

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

static CMPIStatus complete_migrate(virDomainPtr ldom,
                                   virConnectPtr rconn,
                                   const char *xml)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virDomainPtr newdom = NULL;

        if (virDomainUndefine(ldom) == -1) {
                CU_DEBUG("Undefine of local domain failed");
        }

        newdom = virDomainDefineXML(rconn, xml);
        if (newdom == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to define domain");
                goto out;
        }

        CU_DEBUG("Defined domain on destination host");
 out:
        virDomainFree(newdom);

        return s;
}

static CMPIStatus ensure_dom_offline(virDomainPtr dom)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virDomainInfo info;
        int ret;

        ret = virDomainGetInfo(dom, &info);
        if (ret == -1) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Error getting domain info");
                goto out;
        }

        if ((const int)info.state != VIR_DOMAIN_SHUTOFF) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Domain must be shut off for offline migration");
                goto out;
        }
 out:
        return s;
}

static CMPIStatus migrate_vs(struct migration_job *job)
{
        CMPIStatus s;
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        char *uri = NULL;
        char *xml = NULL;

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

        if (domain_exists(job->conn, job->domain)) {
                CU_DEBUG("Remote domain `%s' exists", job->domain);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Remote already has domain `%s'", job->domain);
                goto out;
        }

        s = prepare_migrate(dom, &xml);
        if (s.rc != CMPI_RC_OK)
                goto out;

        switch(job->type) {
        case CIM_MIGRATE_OTHER:
                CU_DEBUG("Offline migration");
                s = ensure_dom_offline(dom);
                break;
        case CIM_MIGRATE_LIVE:
                CU_DEBUG("Live migration");
                s = handle_migrate(job->conn, dom, uri, VIR_MIGRATE_LIVE, job);
                break;
        case CIM_MIGRATE_RESUME:
        case CIM_MIGRATE_RESTART:
                CU_DEBUG("Static migration");
                s = handle_migrate(job->conn, dom, uri, 0, job);
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

        s = complete_migrate(dom, job->conn, xml);
        if (s.rc == CMPI_RC_OK) {
                CU_DEBUG("Migration succeeded");
        } else {
                CU_DEBUG("Migration failed: %s",
                         CMGetCharPtr(s.msg));
        }
 out:
        raise_deleted_ind(job);
        
        free(uri);
        free(xml);
        virDomainFree(dom);
        virConnectClose(conn);

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

        virConnectClose(job->conn);
        free(job->domain);
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
        CMSetNameSpace(*job_op, job->ref_ns);
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
                                                 const char *host)
{
        struct migration_job *job;
        uuid_t uuid;

        job = malloc(sizeof(*job));
        if (job == NULL)
                return NULL;

        job->domain = strdup(domain);
        job->ref_cn = strdup(CLASSNAME(ref));
        job->ref_ns = strdup(NAMESPACE(ref));

        uuid_generate(uuid);
        uuid_unparse(uuid, job->uuid);

        job->context = CBPrepareAttachThread(_BROKER, context);

        return job;
}

static CMPIStatus migrate_do(const CMPIObjectPath *ref,
                             const CMPIContext *context,
                             const char *domain,
                             const char *host,
                             const CMPIArgs *argsin,
                             const CMPIResult *results,
                             CMPIArgs *argsout)
{
        CMPIStatus s;
        CMPIObjectPath *job_op;
        struct migration_job *job;
        CMPI_THREAD_TYPE thread;
        uint32_t retcode = 1;
        CMPIInstance *ind = NULL;
        CMPIInstance *inst = NULL;
        bool rc;

        job = migrate_job_prepare(context, ref, domain, host);
        if (job == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to prepare migration job");
                goto out;
        }

        s = get_msd_values(ref, host, argsin, &job->type, &job->conn);
        if (s.rc != CMPI_RC_OK)
                goto out;

        CU_DEBUG("Prepared migration job %s", job->uuid);

        s = migrate_create_job_instance(context, job, &job_op);
        if (s.rc != CMPI_RC_OK)
                goto out;
        
        CMAddArg(argsout, "Job", (CMPIValue *)&job_op, CMPI_ref);

        inst = CBGetInstance(_BROKER, job->context, job_op, NULL, &s);
        if ((inst == NULL) || (s.rc != CMPI_RC_OK)) {
                CU_DEBUG("Failed to get job instance for create ind", s.rc);
                goto out;
        }

        ind = prepare_indication(_BROKER, inst, job->ref_ns, MIG_CREATED, &s);
        rc = raise_indication(job->context, MIG_CREATED, job->ref_ns, 
                              inst, ind);
        if (!rc)
                CU_DEBUG("Failed to raise indication");

        thread = _BROKER->xft->newThread((void*)migration_thread, job, 0);

        retcode = CIM_SVPC_RETURN_JOB_STARTED;

 out:
        CMReturnData(results, (CMPIValue *)&retcode, CMPI_uint32);

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

        return migrate_do(ref, ctx, name, dhost, argsin, results, argsout);
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

        return migrate_do(ref, ctx, name, dname, argsin, results, argsout);
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
