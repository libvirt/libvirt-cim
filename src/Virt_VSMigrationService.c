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
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
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

#include <uuid.h>

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
#include "infostore.h"

#include "config.h"

#define CIM_JOBSTATE_STARTING 3
#define CIM_JOBSTATE_RUNNING 4
#define CIM_JOBSTATE_COMPLETE 7

#define MIGRATE_SHUTDOWN_TIMEOUT 120

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
        char *host;
        uint16_t type;
        char uuid[VIR_UUID_STRING_BUFLEN];
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
        uint16_t _type;

        if (cu_get_u16_prop(msd, "MigrationType", &_type) != CMPI_RC_OK) {
                CU_DEBUG("Using default MigrationType: %d", CIM_MIGRATE_LIVE);
                _type = CIM_MIGRATE_LIVE;
        }

        if ((_type < CIM_MIGRATE_OTHER) || (_type > CIM_MIGRATE_RESTART)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unsupported migration type %d", _type);
                goto out;
        }

        if (type != NULL)
                *type = _type;

 out:
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
                      const char *dest_params,
                      uint16_t transport)
{
        const char *prefix;
        const char *tport = NULL;
        const char *param = "";
        char *uri = NULL;
        int rc;
        int param_labeled = 0;

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

        if (STREQC(prefix, "xen"))
                rc = asprintf(&uri, "%s+%s://%s", prefix, tport, dest);
        else {
                rc = asprintf(&uri, "%s+%s://%s/system", prefix, tport, dest);
        }

        if (rc == -1) {
                uri = NULL;
                goto out;
        }

        if (!STREQC(param, "")) {
                rc = asprintf(&uri, "%s/%s", uri, param);
                param_labeled = 1;
        }

        if (rc == -1) {
                uri = NULL;
                goto out;
        }

        if (dest_params) {
                if (param_labeled == 0) {
                    rc = asprintf(&uri, "%s?%s", uri, dest_params);
                } else {
                    /* ? is already added */
                    rc = asprintf(&uri, "%s%s", uri, dest_params);
                }
                if (rc == -1) {
                        uri = NULL;
                        goto out;
                }
        }
 out:
        return uri;
}

/* Todo: move it to libcmpiutil */
static CMPIrc cu_get_bool_arg_my(const CMPIArgs *args,
                                 const char *name,
                                 bool *target)
{
        CMPIData argdata;
        CMPIStatus s;

        argdata = CMGetArg(args, name, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullValue(argdata)) {
                return CMPI_RC_ERR_INVALID_PARAMETER;
        }

        if (argdata.type != CMPI_boolean) {
                return CMPI_RC_ERR_TYPE_MISMATCH;
        }

        *target = (bool)argdata.value.boolean;

        return CMPI_RC_OK;
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
        bool use_non_root_ssh_key = false;
        char *dest_params = NULL;
        int ret;

        cu_get_bool_arg_my(argsin,
                           "MigrationWithoutRootKey",
                           &use_non_root_ssh_key);

        s = get_msd(ref, argsin, &msd);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = get_migration_type(msd, type);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = get_migration_uri(msd, &uri_type);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (use_non_root_ssh_key) {
                const char *tmp_keyfile = get_mig_ssh_tmp_key();
                if (!tmp_keyfile) {
                        cu_statusf(_BROKER, &s,
                                   CMPI_RC_ERR_FAILED,
                                   "Migration with special ssh key "
                                   "is not enabled in config file.");
                        CU_DEBUG("Migration with special ssh key "
                                 "is not enabled in config file.");
                        goto out;
                }
                CU_DEBUG("Trying migrate with specified ssh key file [%s].",
                         tmp_keyfile);
                ret = asprintf(&dest_params, "keyfile=%s", tmp_keyfile);
                if (ret < 0) {
                        CU_DEBUG("Failed in generating param string.");
                        goto out;
                }
        }

        uri = dest_uri(CLASSNAME(ref), destination, dest_params, uri_type);
        if (uri == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to construct a valid libvirt URI");
                goto out;
        }

        CU_DEBUG("Migrate tring to connect remote host with uri %s.", uri);
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
        free(dest_params);
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
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "Unable to get local Hypervisor version");
                goto out;
        }

        if (virConnectGetVersion(dconn, &remote)) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                dconn,
                                "Unable to get remote Hypervisor version");
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

 out:
        return s;
}

static bool is_valid_check(const char *path)
{
        struct stat64 s;

        if (stat64(path, &s) != 0)
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
        virConnectPtr conn = virDomainGetConnect(dom);
        const char *name = virDomainGetName(dom);
        const char *uri = virConnectGetURI(conn);

        if (name == NULL) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "Failed to get domain name");
                goto out;        
        }

        if (uri == NULL) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "Failed to get URI of connection");
                goto out; 
        }

        pid = fork();
        if (pid == 0) {

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

 out:
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

        if (fd >= 0)
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

static void log_status(CMPIStatus *s, const char *prefix)
{
        CU_DEBUG("%s: %s", prefix, CMGetCharPtr(s->msg));

        s->rc = CMPI_RC_OK;
        s->msg = NULL;
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
        uint32_t retcode = CIM_SVPC_RETURN_COMPLETED;
        CMPIBoolean isMigratable = 0;
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

        s = get_msd_values(ref, destination, argsin, NULL, &dconn);
        if (s.rc != CMPI_RC_OK)
                goto out;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        s = check_hver(conn, dconn);
        if (s.rc != CMPI_RC_OK) {
                log_status(&s, "Hypervisor version check failed");
                goto out;
        }

        dom = virDomainLookupByName(conn, domain);
        if (dom == NULL) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_NOT_FOUND,
                                conn,
                                "No such domain");
                goto out;
        }

        CMSetNameSpace(system, NAMESPACE(ref));
        s = get_domain_by_ref(_BROKER, system, &dominst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = check_caps(conn, dconn);
        if (s.rc != CMPI_RC_OK) {
                log_status(&s, "Hypervisor capabilities check failed");
                goto out;
        }

        path = get_parms_file(ref, argsin);
        s = call_external_checks(dom, path);
        if (s.rc != CMPI_RC_OK) {
                log_status(&s, "An external check failed");
                goto out;
        }

        isMigratable = 1;
        cu_statusf(_BROKER, &s,
                   CMPI_RC_OK,
                   "");
 out:
        CMReturnData(results, (CMPIValue *)&retcode, CMPI_uint32);

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
        CMPIObjectPath *ref;
        const char *ind_name = NULL;
        const char *host = NULL;
        const char *ccname = NULL;

        if (ind == NULL)
                return false;

        ind_name = ind_type_to_name(ind_type);
        CU_DEBUG("Raising %s indication", ind_name);

        ref = CMGetObjectPath(inst, &s);

        /* This is a workaround for Pegasus, it loses its objectpath by
           CMGetObjectPath. So set it back. */
        if (ref != NULL)
                inst->ft->setObjectPath((CMPIInstance *)inst, ref);

        if ((ref == NULL) || (s.rc != CMPI_RC_OK)) {
                CU_DEBUG("Failed to get job reference");
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get job reference");
                goto out;
        } else {
                s = get_host_system_properties(&host,
                                               &ccname,
                                               ref,
                                               _BROKER,
                                               context);
                if (s.rc == CMPI_RC_OK) {
                        CMSetProperty(ind, "SourceInstanceHost",
                                      (CMPIValue *)host, CMPI_chars);
                } else {
                        CU_DEBUG("Unable to get HostSystem properties");
                }

                CMPIString *str;

                str = CMObjectPathToString(ref, &s);
                if ((str == NULL) || (s.rc != CMPI_RC_OK)) {
                        CU_DEBUG("Failed to get path string");
                } else {
                        CMSetProperty(ind, "SourceInstanceModelPath",
                                      (CMPIValue *)&str, CMPI_string);
                }
        }

        CU_DEBUG("Setting SourceInstance");
        CMSetProperty(ind, "SourceInstance",
                      (CMPIValue *)&inst, CMPI_instance);

        type = get_typed_class(CLASSNAME(ref), ind_name);

        s = stdi_raise_indication(_BROKER, context, type, ns, ind);

        free(type);
 out:
        return s.rc == CMPI_RC_OK;
}

static CMPIInstance *prepare_indication(const CMPIBroker *broker,
                                        CMPIInstance *inst,
                                        struct migration_job *job,
                                        int ind_type,
                                        CMPIStatus *s)
{
        const char *ind_name = NULL;
        CMPIInstance *ind = NULL;
        CMPIInstance *prev_inst = NULL;
        const char *pfx = NULL;
        virDomainPtr dom = NULL;
        char uuid[VIR_UUID_STRING_BUFLEN];
        CMPIDateTime *timestamp = NULL;
        
        ind_name = ind_type_to_name(ind_type);

        CU_DEBUG("Creating indication.");

        pfx = pfx_from_conn(job->conn);

        ind = get_typed_instance(broker, 
                                 pfx, 
                                 ind_name, 
                                 job->ref_ns,
                                 false);
        if (ind == NULL) {
                CU_DEBUG("Failed to create ind, type '%s:%s_%s'", 
                         job->ref_ns, pfx, ind_name);
                goto out;
        }
        
        dom = virDomainLookupByName(job->conn, job->domain);
        if(dom == NULL) {
                CU_DEBUG("Failed to connect to domain %s", job->domain);
                goto out;
        }

        if(virDomainGetUUIDString(dom, uuid) != 0) {
                CU_DEBUG("Failed to get UUID from domain name");
                goto out;
        }

        CMSetProperty(ind, "IndicationIdentifier",
                (CMPIValue *)uuid, CMPI_chars);

        timestamp = CMNewDateTime(broker, s);
        CMSetProperty(ind, "IndicationTime",
                (CMPIValue *)&timestamp, CMPI_dateTime);

        if (ind_type == MIG_MODIFIED) {
                /* Need to copy job inst before attaching as PreviousInstance 
                   because otherwise the changes we are about to make to job
                   inst are made to PreviousInstance as well. */
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
        virDomainFree(dom);
        return ind;
}

static CMPIObjectPath *ref_from_job(struct migration_job *job,
                                    CMPIStatus *s)
{
        CMPIObjectPath *ref = NULL;
        char *type;
        
        type = get_typed_class(job->ref_cn, "MigrationJob");
        
        ref = CMNewObjectPath(_BROKER,
                              job->ref_ns,
                              type,
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
        free(type);

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

        ind = prepare_indication(_BROKER, inst, job, MIG_DELETED, &s);
        
        rc = raise_indication(job->context, MIG_DELETED, job->ref_ns,
                              inst, ind);
        if (!rc)
                CU_DEBUG("Failed to raise indication");

        return;
}

static void migrate_job_set_state(struct migration_job *job,
                                  uint16_t state,
                                  int error_code,
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

        ind = prepare_indication(_BROKER, inst, job, MIG_MODIFIED, &s);

        CMSetProperty(inst, "JobState",
                      (CMPIValue *)&state, CMPI_uint16);
        CMSetProperty(inst, "ErrorCode",
                      (CMPIValue *)&error_code, CMPI_uint16);
        CMSetProperty(inst, "Status",
                      (CMPIValue *)status, CMPI_chars);

        CU_DEBUG("Modifying job %s (%i:%s) Error Code is  %i", 
                  job->uuid, state, status, error_code);

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
                                 int type,
                                 struct migration_job *job)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virDomainPtr ddom = NULL;
        virDomainInfo info;
        int ret;

        ret = virDomainGetInfo(dom, &info);
        if (ret == -1) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                virDomainGetConnect(dom),
                                "Error getting domain info");
                goto out;
        }

        if ((const int)info.state == VIR_DOMAIN_SHUTOFF) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_INVALID_PARAMETER,
                           "Domain must be running for live " 
                           "or resume migration");
                goto out;
        }

        CU_DEBUG("Migrating %s", job->domain);
        ddom = virDomainMigrate(dom, dconn, type, NULL, NULL, 0);
        if (ddom == NULL) {
                CU_DEBUG("Migration failed");
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                dconn,
                                "Migration Failed");
        }
 out:
        virDomainFree(ddom);

        return s;
}

static CMPIStatus handle_restart_migrate(virConnectPtr dconn,
                                         virDomainPtr dom,
                                         struct migration_job *job)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int ret;
        int i;

        CU_DEBUG("Shutting down domain for migration");
        ret = virDomainShutdown(dom);
        if (ret != 0) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                virDomainGetConnect(dom),
                                "Unable to shutdown guest");
                goto out;
        }

        for (i = 0; i < MIGRATE_SHUTDOWN_TIMEOUT; i++) {
                if ((i % 30) == 0) {
                        CU_DEBUG("Polling for shutdown completion...");
                }

                if (!domain_online(dom))
                        goto out;

                sleep(1);
        }

        cu_statusf(_BROKER, &s,
                   CMPI_RC_ERR_FAILED,
                   "Domain failed to shutdown in %i seconds",
                   MIGRATE_SHUTDOWN_TIMEOUT);
 out:
        CU_DEBUG("Domain %s shutdown",
                 s.rc == CMPI_RC_OK ? "did" : "did NOT");

        return s;
}


static CMPIStatus prepare_migrate(virDomainPtr dom,
                                  char **xml)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};

        *xml = virDomainGetXMLDesc(dom,
                VIR_DOMAIN_XML_INACTIVE | VIR_DOMAIN_XML_SECURE);
        if (*xml == NULL) {

                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                virDomainGetConnect(dom),
                                "Unable to retrieve domain XML");

                goto out;
        }

 out:
        return s;
}

static CMPIStatus complete_migrate(virDomainPtr ldom,
                                   virConnectPtr rconn,
                                   const char *xml,
                                   bool restart)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virDomainPtr newdom = NULL;
        virDomainPtr dom;
        int i;
        int ret;

        for (i = 0; i < MIGRATE_SHUTDOWN_TIMEOUT; i++) {
                if ((i % 30) == 0) {
                        CU_DEBUG("Polling to undefine guest %s...", 
                                 virDomainGetName(ldom));
                }

                dom = virDomainLookupByName(virDomainGetConnect(ldom),
                                            virDomainGetName(ldom));
                if (dom == NULL) {
                        CU_DEBUG("Unable to re-lookup domain");

                        ret = -1;
                        break;
                }

                ret = virDomainUndefine(dom);
                virDomainFree(dom);

                if (ret == 0)
                        break;

                sleep(1);
        }

        if (ret != 0) {
                CU_DEBUG("Undefine of local domain failed");
        }

        newdom = virDomainDefineXML(rconn, xml);
        if (newdom == NULL) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                rconn,
                                "Failed to define domain");
                goto out;
        }

        CU_DEBUG("Defined domain on destination host");

        if (restart) {
                CU_DEBUG("Restarting domain on remote host");
                if (virDomainCreate(newdom) != 0) {
                        CU_DEBUG("Failed to start domain on remote host");
                        virt_set_status(_BROKER, &s,
                                        CMPI_RC_ERR_FAILED,
                                        rconn,
                                        "Failed to start domain on remote \
                                        host");
                }
        }
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
                
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                virDomainGetConnect(dom),
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

static void clear_infstore_migration_flag(virDomainPtr dom)
{
        struct infostore_ctx *infp;

        infp = infostore_open(dom);
        if (infp == NULL) {
                CU_DEBUG("Unable to open domain information store."
                          "Migration flag won't be cleared");
                return;
        }

        infostore_set_bool(infp, "migrating", false);
        CU_DEBUG("Clearing infostore migrating flag");

        infostore_close(infp);
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
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "Failed to lookup domain `%s'", job->domain);

                goto out;
        }

        if ((!STREQ(job->host, "localhost"))  &&
           (domain_exists(job->conn, job->domain))) {
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
                s = handle_migrate(job->conn, dom, VIR_MIGRATE_LIVE, job);
                break;
        case CIM_MIGRATE_RESUME:
                CU_DEBUG("Static migration");
                s = handle_migrate(job->conn, dom, 0, job);
                break;
        case CIM_MIGRATE_RESTART:
                CU_DEBUG("Restart migration");
                s = handle_restart_migrate(job->conn, dom, job);
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

        s = complete_migrate(dom,
                             job->conn,
                             xml,
                             job->type == CIM_MIGRATE_RESTART);
        if (s.rc == CMPI_RC_OK) {
                CU_DEBUG("Migration succeeded");
        } else {
                CU_DEBUG("Migration failed: %s",
                         CMGetCharPtr(s.msg));
        }
 out:
        clear_infstore_migration_flag(dom);
        
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
        migrate_job_set_state(job, CIM_JOBSTATE_RUNNING, 0, "Running");

        s = migrate_vs(job);

        CU_DEBUG("Migration Job %s finished: %i", job->uuid, s.rc);
        if (s.rc != CMPI_RC_OK)
                migrate_job_set_state(job,
                                      CIM_JOBSTATE_COMPLETE,
                                      s.rc,
                                      CMGetCharPtr(s.msg));
        else
                migrate_job_set_state(job,
                                      CIM_JOBSTATE_COMPLETE,
                                      0,
                                      "Completed");

        raise_deleted_ind(job);
        virConnectClose(job->conn);
        free(job->domain);
        free(job->ref_cn);
        free(job->ref_ns);
        free(job->host);
        free(job);

        return NULL;
}

static bool set_infstore_migration_flag(const virConnectPtr conn,
                                        const char *domain)
{
        struct infostore_ctx *infp;
        bool ret = false;
        virDomainPtr dom = NULL;
        
        dom = virDomainLookupByName(conn, domain);
        if (dom == NULL) {
                CU_DEBUG("No such domain");
                goto out;
        }

        infp = infostore_open(dom);
        if (infp == NULL) {
                CU_DEBUG("Unable to open domain information store."
                         "Migration flag won't be placed");
                goto out;
        }

        ret = infostore_set_bool(infp, "migrating", true);
        CU_DEBUG("Migration flag set");

        infostore_close(infp);

 out:
        virDomainFree(dom);

        return ret;
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
        char *type = NULL;

        start = CMNewDateTime(_BROKER, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(start)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get job start time");
                goto out;
        }
        
        type = get_typed_class(job->ref_cn, "MigrationJob");

        jobinst = _migrate_job_new_instance(type, job->ref_ns);
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

        CMSetNameSpace(*job_op, job->ref_ns);

 out:
        free(type);

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
        job->host = strdup(host);

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

        rc = set_infstore_migration_flag(job->conn, domain);
        if (!rc)
                CU_DEBUG("Failed to set migration flag in infostore");

        ind = prepare_indication(_BROKER, inst, job, MIG_CREATED, &s);
        rc = raise_indication(job->context, MIG_CREATED, job->ref_ns, 
                              inst, ind);
        if (!rc)
                CU_DEBUG("Failed to raise indication");

        _BROKER->xft->newThread((void*)migration_thread, job, 0);

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

/* return 0 on success */
static int pipe_exec(const char *cmd)
{
        FILE *stream = NULL;
        int ret = 0;
        char buf[256];

        CU_DEBUG("executing system cmd [%s].", cmd);
        /* Todo: We need a better popen, currently stdout have been closed
        and SIGCHILD is handled by tog-pegasus, so fgets always got NULL
        making error detection not possible. */
        stream = popen(cmd, "r");
        if (stream == NULL) {
                CU_DEBUG("Failed to open pipe to run the command.");
                ret = -1;
                goto out;
        }
        usleep(10000);

        buf[255] = 0;
        while (fgets(buf, sizeof(buf), stream) != NULL) {
                CU_DEBUG("Exception got: [%s].", buf);
                ret = -2;
                goto out;
        }

 out:
        if (stream != NULL) {
                pclose(stream);
        }
        return ret;
}

/*
 * libvirt require private key specified to be placed in a directory owned by
 * root, because libvirt-cim now runs as root. So here the key would be copied.
 * In this way libvirt-cim could borrow a non-root ssh private key, instead of
 * using root's private key, avoid security risk.
 */
static int ssh_key_copy(const char *src, const char *dest)
{
        char *cmd = NULL;
        int ret = 0;
        struct stat sb;

        /* try delete it */
        unlink(dest);
        ret = stat(dest, &sb);
        if (ret == 0) {
                CU_DEBUG("Can not delete [%s] before copy, "
                         "maybe someone is using it.",
                         dest);
                ret = -1;
                goto out;
        }

        ret = asprintf(&cmd, "cp -f %s %s", src, dest);
        if (ret < 0) {
                CU_DEBUG("Failed in combination for shell command.");
                goto out;
        }

        ret = pipe_exec(cmd);
        if (ret < 0) {
                CU_DEBUG("Error in executing command [%s]");
                goto out;
        }

        ret = stat(dest, &sb);
        if (ret < 0) {
                CU_DEBUG("Can not find file [%s] after copy.", dest);
        }
 out:
        free(cmd);
        return ret;
}

static CMPIStatus migrate_sshkey_copy(CMPIMethodMI *self,
                                    const CMPIContext *ctx,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *ref,
                                    const CMPIArgs *argsin,
                                    CMPIArgs *argsout)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        const char *ssh_key_src = NULL;
        int ret;

        const char *tmp_keyfile = get_mig_ssh_tmp_key();
        if (!tmp_keyfile) {
                cu_statusf(_BROKER, &s, CMPI_RC_ERR_FAILED,
                           "Migration with special ssh key "
                           "is not enabled in config file.");
                CU_DEBUG("Migration with special ssh key "
                         "is not enabled in config file.");
                goto out;
        }

        cu_get_str_arg(argsin, "SSH_Key_Src", &ssh_key_src);
        if (!ssh_key_src) {
                cu_statusf(_BROKER, &s, CMPI_RC_ERR_FAILED,
                           "Failed to get property 'SSH_Key_Src'.");
                CU_DEBUG("Failed to get property 'SSH_Key_Src'.");
                goto out;
        }

        ret = ssh_key_copy(ssh_key_src, tmp_keyfile);
        if (ret < 0) {
                cu_statusf(_BROKER, &s, CMPI_RC_ERR_FAILED,
                           "Got error in copying ssh key from [%s] to [%s].",
                           ssh_key_src, tmp_keyfile);
                CU_DEBUG("Got error in copying ssh key from [%s] to [%s].",
                         ssh_key_src, tmp_keyfile);
        }

 out:
        METHOD_RETURN(results, s.rc);
        return s;
}

static CMPIStatus migrate_sshkey_delete(CMPIMethodMI *self,
                                  const CMPIContext *ctx,
                                  const CMPIResult *results,
                                  const CMPIObjectPath *ref,
                                  const CMPIArgs *argsin,
                                  CMPIArgs *argsout)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int ret;
        struct stat sb;

        const char *tmp_keyfile = get_mig_ssh_tmp_key();
        if (!tmp_keyfile) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Migration with special ssh key "
                           "is not enabled in config file.");
                CU_DEBUG("Migration with special ssh key "
                         "is not enabled in config file.");
                goto out;
        }

        ret = stat(tmp_keyfile, &sb);
        if (ret == 0) {
                /* need delete */
                ret = unlink(tmp_keyfile);
                if (ret < 0) {
                        cu_statusf(_BROKER, &s,
                                   CMPI_RC_ERR_FAILED,
                                   "Failed to delete [%s].",
                                   tmp_keyfile);
                        CU_DEBUG("Failed to delete [%s].", tmp_keyfile);
                }
        } else {
                /* not exist */
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Can not find file [%s] before delete.",
                           tmp_keyfile);
                CU_DEBUG("Can not find file [%s] before delete.", tmp_keyfile);
        }

 out:
        METHOD_RETURN(results, s.rc);
        return s;
};

static struct method_handler vsimth = {
        .name = "CheckVirtualSystemIsMigratableToHost",
        .handler = vs_migratable_host,
        .args = {{"ComputerSystem", CMPI_ref, false},
                 {"DestinationHost", CMPI_string, false},
                 {"MigrationWithoutRootKey", CMPI_boolean, true},
                 {"MigrationSettingData", CMPI_instance, true},
                 {"NewSystemSettingData", CMPI_instance, true},
                 {"NewResourceSettingData", CMPI_instanceA, true},
                 ARG_END
        }
};

static struct method_handler vsimts = {
        .name = "CheckVirtualSystemIsMigratableToSystem",
        .handler = vs_migratable_system,
        .args = {{"ComputerSystem", CMPI_ref, false},
                 {"DestinationSystem", CMPI_ref, false},
                 {"MigrationSettingData", CMPI_instance, true},
                 {"NewSystemSettingData", CMPI_instance, true},
                 {"NewResourceSettingData", CMPI_instanceA, true},
                 ARG_END
        }
};

static struct method_handler mvsth = {
        .name = "MigrateVirtualSystemToHost",
        .handler = migrate_vs_host,
        .args = {{"ComputerSystem", CMPI_ref, false},
                 {"DestinationHost", CMPI_string, false},
                 {"MigrationWithoutRootKey", CMPI_boolean, true},
                 {"MigrationSettingData", CMPI_instance, true},
                 {"NewSystemSettingData", CMPI_instance, true},
                 {"NewResourceSettingData", CMPI_instanceA, true},
                 ARG_END
        }
};

static struct method_handler mvsts = {
        .name = "MigrateVirtualSystemToSystem",
        .handler = migrate_vs_system,
        .args = {{"ComputerSystem", CMPI_ref, false},
                 {"DestinationSystem", CMPI_ref, false},
                 {"MigrationSettingData", CMPI_instance, true},
                 {"NewSystemSettingData", CMPI_instance, true},
                 {"NewResourceSettingData", CMPI_instanceA, true},
                 ARG_END
        }
};

static struct method_handler msshkc = {
        .name = "MigrateSSHKeyCopy",
        .handler = migrate_sshkey_copy,
        .args = {{"SSH_Key_Src", CMPI_string, true},
                 ARG_END
        }
};

static struct method_handler msshkd = {
        .name = "MigrateSSHKeyDelete",
        .handler = migrate_sshkey_delete,
        .args = {ARG_END
        }
};

static struct method_handler *my_handlers[] = {
        &vsimth,
        &vsimts,
        &mvsth,
        &mvsts,
        &msshkc,
        &msshkd,
        NULL
};

STDIM_MethodMIStub(, Virt_VSMigrationService, _BROKER,
                   libvirt_cim_init(), my_handlers);

CMPIStatus get_migration_service(const CMPIObjectPath *ref,
                                 CMPIInstance **_inst,
                                 const CMPIBroker *broker,
                                 const CMPIContext *context,
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
                                  NAMESPACE(ref),
                                  true);
        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get instance for %s", CLASSNAME(ref));
                goto out;
        }

        s = get_host_system_properties(&name, 
                                       &ccname, 
                                       ref, 
                                       broker,
                                       context);
        if (s.rc != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get host attributes");
                goto out;
        }

        CMSetProperty(inst, "Name",
                      (CMPIValue *)"MigrationService", CMPI_chars);

        if (name != NULL)
                CMSetProperty(inst, "SystemName",
                              (CMPIValue *)name, CMPI_chars);

        if (ccname != NULL)
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

static CMPIStatus return_vsms(const CMPIContext *context,
                              const CMPIObjectPath *ref,
                              const CMPIResult *results,
                              bool name_only,
                              bool is_get_inst)
{
        CMPIInstance *inst = NULL;
        CMPIStatus s;

        s = get_migration_service(ref, &inst, _BROKER, context, is_get_inst);
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
        return return_vsms(context, ref, results, true, false);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *ref,
                                const char **properties)
{

        return return_vsms(context, ref, results, false, false);
}


static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *ref,
                              const char **properties)
{
        return return_vsms(context, ref, results, false, true);
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
