/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *  Heidi Eckhart <heidieck@linux.vnet.ibm.com>
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_instance.h>

#include "misc_util.h"

#include "Virt_HostSystem.h"

const static CMPIBroker *_BROKER;

static int resolve_host(char *host, char *buf, int size)
{
        struct hostent *he;
        int i;

        he = gethostbyname(host);
        if (he == NULL) {
                CU_DEBUG("gethostbyname(%s): %m", host);
                return -1;
        }

        for (i = 0; he->h_aliases[i] != NULL; i++) {
               if ((strchr(he->h_aliases[i], '.') != NULL) &&
                   (strstr(he->h_aliases[i], "localhost") == NULL)) {
                           strncpy(buf, he->h_aliases[i], size);
                           return 0;
                   }
        }

        CU_DEBUG("Unable to find FQDN, using hostname.");
 
        /* FIXME: An ugly hack to ensure we return something for the hostname,
                  but also be sure the value isn't empty and that it doesn't
                  contain "localhost" */
        if ((he->h_name != NULL) && (!STREQC(he->h_name, "")) && 
            (strstr(he->h_name, "localhost") == NULL))
                strncpy(buf, he->h_name, size);
        else if ((host != NULL) && (!STREQC(host, "")) && 
                 (strstr(host, "localhost") == NULL))
                strncpy(buf, host, size);
        else {
                CU_DEBUG("Unable to find valid hostname value.");
                return -1;
        }

        return 0;
}

static int get_fqdn(char *buf, int size)
{
        char host[256];
        int ret = 0;

        if (gethostname(host, sizeof(host)) != 0) {
                CU_DEBUG("gethostname(): %m");
                return -1;
        }

        if (strchr(host, '.') != NULL)
                strncpy(buf, host, size);
        else
                ret = resolve_host(host, buf, size);

        return ret;
}

static int set_host_system_properties(CMPIInstance *instance)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIObjectPath *op;
        char hostname[256] = {0};

        op = CMGetObjectPath(instance, &s);
        if ((s.rc == CMPI_RC_OK) || !CMIsNullObject(op)) {
                CMSetProperty(instance, "CreationClassName",
                              (CMPIValue *)CLASSNAME(op), CMPI_chars);
        }

        if (get_fqdn(hostname, sizeof(hostname)) != 0)
                strcpy(hostname, "unknown");

        CMSetProperty(instance, "Name",
                      (CMPIValue *)hostname, CMPI_chars);
        
        return 1;
}

static CMPIStatus fake_host(const CMPIBroker *broker,
                            const CMPIObjectPath *reference,
                            CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        virConnectPtr conn = NULL;

        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance");
                goto out;
        }

        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "HostSystem",
                                  NAMESPACE(reference));

        if (inst == NULL) {
                cu_statusf(broker, &s, 
                           CMPI_RC_ERR_FAILED,
                           "Can't create HostSystem instance");
                goto out;
        }

        set_host_system_properties(inst);
        *_inst = inst;
 out:
        virConnectClose(conn);

        return s;
}

CMPIStatus get_host(const CMPIBroker *broker,
                    const CMPIContext *context,
                    const CMPIObjectPath *reference,
                    CMPIInstance **_inst,
                    bool is_get_inst)
{
        CMPIStatus s;

        if (!is_get_inst && (s.rc == CMPI_RC_ERR_NOT_FOUND)) {
                /* This is not an error */
                return (CMPIStatus){CMPI_RC_OK, NULL};
        }

        if ((s.rc == CMPI_RC_OK) && is_get_inst)
                s = cu_validate_ref(broker, reference, *_inst);

        return s;
}

static CMPIStatus return_host(const CMPIContext *context,
                              const CMPIObjectPath *reference,
                              const CMPIResult *results,
                              bool name_only,
                              bool is_get_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        s = get_host(_BROKER, context, reference, &inst, is_get_inst);
        if (s.rc != CMPI_RC_OK || inst == NULL)
                goto out;

        if (name_only)
                cu_return_instance_name(results, inst);
        else
                CMReturnInstance(results, inst);

 out:
        return s;
}

CMPIStatus get_host_system_properties(const char **name,
                                      const char **ccname,
                                      const CMPIObjectPath *ref,
                                      const CMPIBroker *broker,
                                      const CMPIContext *context)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *host = NULL;

        s = get_host(broker, context, ref, &host, false);
        if (s.rc != CMPI_RC_OK || host == NULL)
                goto out;

        if (cu_get_str_prop(host, "Name", name) != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get name of HostSystem");
                goto out;
        }

        if (cu_get_str_prop(host, "CreationClassName", ccname) != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get creation class of HostSystem");
                goto out;
        }

 out:
        return s;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_host(context, reference, results, true, false);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{

        return return_host(context, reference, results, false, false);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        return return_host(context, reference, results, false, true);
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(, 
                   Virt_HostSystem,
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
