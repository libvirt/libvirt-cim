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

#include "libcmpiutil.h"
#include "misc_util.h"

#include "Virt_HostSystem.h"

const static CMPIBroker *_BROKER;

static int set_host_system_properties(CMPIInstance *instance,
                                      const char *classname)
{
        char hostname[256] = {0};

        CMSetProperty(instance, "CreationClassName",
                      (CMPIValue *)classname, CMPI_chars);

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
        CMPIInstance *inst = NULL;
        CMPIObjectPath *op;
        CMPIStatus s;
        char *ns;
        char *classname;

        ns = NAMESPACE(reference);

        classname = get_typed_class("HostSystem");
        if (classname == NULL) {
                CMSetStatusWithChars(broker, &s,
                                     CMPI_RC_ERR_FAILED,
                                     "Invalid class");
                goto out;
        }

        op = CMNewObjectPath(broker, ns, classname, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullObject(op)) {
                CMSetStatusWithChars(broker, &s,
                                     CMPI_RC_ERR_FAILED,
                                     "Cannot get object path for HostSystem");
                goto out;
        }

        inst = CMNewInstance(broker, op, &s);
        if ((s.rc != CMPI_RC_OK) || (CMIsNullObject(op))) {
                CMSetStatusWithChars(broker, &s,
                                     CMPI_RC_ERR_FAILED,
                                     "Failed to instantiate HostSystem");
                goto out;
        }

        set_host_system_properties(inst, classname);

 out:
        *instance = inst;

        free(classname);

        return s;
}

static CMPIStatus return_host_cs(const CMPIObjectPath *reference,
                                 const CMPIResult *results,
                                 int name_only)
{
        CMPIStatus s;
        CMPIInstance *instance;
        char *ns;

        if (!provider_is_responsible(_BROKER, reference, &s))
                return s;

        ns = NAMESPACE(reference);

        s = get_host_cs(_BROKER, reference, &instance);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (name_only)
                cu_return_instance_name(results, instance);
        else
                CMReturnInstance(results, instance);
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

/* Avoid a warning in the stub macro below */
CMPIInstanceMI *
Virt_HostSystemProvider_Create_InstanceMI(const CMPIBroker *,
                                          const CMPIContext *,
                                          CMPIStatus *rc);

CMInstanceMIStub(, Virt_HostSystemProvider, _BROKER, CMNoHook);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */