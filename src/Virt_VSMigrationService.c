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

#include <libvirt/libvirt.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include "misc_util.h"
#include <libcmpiutil/std_instance.h>
#include <libcmpiutil/std_invokemethod.h>

#include "Virt_VSMigrationService.h"

#define METHOD_RETURN(r, v) do {                                        \
                uint32_t rc = v;                                        \
                CMReturnData(r, (CMPIValue *)&rc, CMPI_uint32);         \
        } while (0);

const static CMPIBroker *_BROKER;

static const char *transport_from_ref(const CMPIObjectPath *ref)
{
        const char *cn;

        cn = CLASSNAME(ref);

        if (STARTS_WITH(cn, "Xen"))
                return "xen+ssh";
        else if (STARTS_WITH(cn, "KVM"))
                return "qemu+ssh";
        else
                return NULL;
}

static char *dest_uri(const CMPIObjectPath *ref,
                      const char *dest)
{
        char *uri;
        const char *tport = NULL;

        tport = transport_from_ref(ref);
        if (tport == NULL) {
                CU_DEBUG("Failed to get transport for %s", CLASSNAME(ref));
                return NULL;
        }

        if (asprintf(&uri, "%s://%s/", tport, dest) == -1)
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
                CMSetStatus(&s, CMPI_RC_OK);
        } else {
                CU_DEBUG("Version check FAILED (%lu < %lu)", remote, local);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Remote hypervisor is older than local (%lu < %lu)",
                           remote, local);
        }

        CMSetStatus(&s, CMPI_RC_OK);

 out:
        return s;
}

static CMPIStatus vs_migratable(const CMPIObjectPath *ref,
                                const char *domain,
                                const char *destination,
                                const CMPIResult *results)
{
        CMPIStatus s;
        char *uri = NULL;
        virConnectPtr conn = NULL;
        virConnectPtr dconn = NULL;
        uint32_t retcode = 1;

        uri = dest_uri(ref, destination);
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
        CMSetStatus(&s, CMPI_RC_OK);

 out:
        CMReturnData(results, (CMPIValue *)&retcode, CMPI_uint32);

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

        return vs_migratable(ref, name, dhost, results);
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

        return vs_migratable(ref, name, dname, results);
}

static CMPIStatus migrate_vs(const CMPIObjectPath *ref,
                             const char *domain,
                             const char *destination,
                             const CMPIResult *results)
{
        CMPIStatus s;
        virConnectPtr conn = NULL;
        virConnectPtr dconn = NULL;
        virDomainPtr dom = NULL;
        virDomainPtr ddom = NULL;
        char *uri = NULL;
        uint32_t retcode = 1;

        uri = dest_uri(ref, destination);
        if (uri == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to construct a valid libvirt URI");
                goto out;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        dom = virDomainLookupByName(conn, domain);
        if (dom == NULL) {
                CU_DEBUG("Failed to lookup `%s'", domain);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to lookup domain `%s'", domain);
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

        CU_DEBUG("Migrating %s -> %s", domain, uri);

        ddom = virDomainMigrate(dom, dconn, VIR_MIGRATE_LIVE, NULL, NULL, 0);
        if (ddom == NULL) {
                CU_DEBUG("Migration failed");
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Migration Failed");
                goto out;
        }

        CU_DEBUG("Migration succeeded");
        retcode = 0;
        CMSetStatus(&s, CMPI_RC_OK);

 out:
        CMReturnData(results, (CMPIValue *)&retcode, CMPI_uint32);

        free(uri);
        virDomainFree(dom);
        virDomainFree(ddom);
        virConnectClose(conn);
        virConnectClose(dconn);

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

        return migrate_vs(ref, name, dhost, results);
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

        return migrate_vs(ref, name, dname, results);
}

static struct method_handler vsimth = {
        .name = "VirtualSystemIsMigratableToHost",
        .handler = vs_migratable_host,
        .args = {{"ComputerSystem", CMPI_ref},
                 {"DestinationHost", CMPI_string},
                 ARG_END
        }
};

static struct method_handler vsimts = {
        .name = "VirtualSystemIsMigratableToSystem",
        .handler = vs_migratable_system,
        .args = {{"ComputerSystem", CMPI_ref},
                 {"DestinationSystem", CMPI_ref},
                 ARG_END
        }
};

static struct method_handler mvsth = {
        .name = "MigrateVirtualSystemToHost",
        .handler = migrate_vs_host,
        .args = {{"ComputerSystem", CMPI_ref},
                 {"DestinationHost", CMPI_string},
                 ARG_END
        }
};

static struct method_handler mvsts = {
        .name = "MigrateVirtualSystemToSystem",
        .handler = migrate_vs_system,
        .args = {{"ComputerSystem", CMPI_ref},
                 {"DestinationSystem", CMPI_ref},
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
                                 const CMPIBroker *broker)
{
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        inst = get_typed_instance(broker,
                                  CLASSNAME(ref),
                                  "VirtualSystemMigrationService",
                                  NAMESPACE(ref));
        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get instance for %s", CLASSNAME(ref));
                return s;
        }

        CMSetProperty(inst, "Name",
                      (CMPIValue *)"MigrationService", CMPI_chars);

        *_inst = inst;

        return s;
}

static CMPIStatus return_vsms(const CMPIObjectPath *ref,
                              const CMPIResult *results,
                              bool name_only)
{
        CMPIInstance *inst;
        CMPIStatus s;

        s = get_migration_service(ref, &inst, _BROKER);
        if (s.rc == CMPI_RC_OK) {
                if (name_only)
                        cu_return_instance_name(results, inst);
                else
                        CMReturnInstance(results, inst);
        }

        return s;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *ref)
{
        return return_vsms(ref, results, true);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *ref,
                                const char **properties)
{

        return return_vsms(ref, results, false);
}


static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *ref,
                              const char **properties)
{
        CMPIInstance *inst;
        CMPIStatus s;
        const char *prop;

        s = get_migration_service(ref, &inst, _BROKER);
        if (s.rc != CMPI_RC_OK)
                return s;

        prop = cu_compare_ref(ref, inst);
        if (prop != NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s)", prop);
        } else {
                CMReturnInstance(results, inst);
        }

        return s;
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(, Virt_VSMigrationService,
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
