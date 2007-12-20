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

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_instance.h>

#include "misc_util.h"

#include "Virt_HostSystem.h"

const static CMPIBroker *_BROKER;

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

        if (gethostname(hostname, sizeof(hostname) - 1) != 0)
                strcpy(hostname, "unknown");

        CMSetProperty(instance, "Name",
                      (CMPIValue *)hostname, CMPI_chars);
        
        return 1;
}

CMPIStatus get_host_cs(const CMPIBroker *broker,
                       const CMPIObjectPath *reference,
                       CMPIInstance **instance)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        virConnectPtr conn = NULL;

        *instance = NULL;
        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL)
                return s;

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

 out:
        virConnectClose(conn);
        *instance = inst;

        return s;
}

static CMPIStatus return_host_cs(const CMPIObjectPath *reference,
                                 const CMPIResult *results,
                                 int name_only)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance;

        s = get_host_cs(_BROKER, reference, &instance);
        if (s.rc != CMPI_RC_OK || instance == NULL)
                goto out;

        if (name_only)
                cu_return_instance_name(results, instance);
        else
                CMReturnInstance(results, instance);
 out:
        return s;
}

CMPIStatus get_host_system_properties(const char **name,
                                      const char **ccname,
                                      const CMPIObjectPath *ref,
                                      const CMPIBroker *broker)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *host = NULL;

        s = get_host_cs(broker, ref, &host);
        if (s.rc != CMPI_RC_OK)
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
        return return_host_cs(reference, results, 1);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{

        return return_host_cs(reference, results, 0);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        return return_host_cs(reference, results, 0);
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
