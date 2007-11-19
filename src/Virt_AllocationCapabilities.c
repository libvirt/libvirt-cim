/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Jay Gagnon <grendel@linux.vnet.ibm.com>
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
#include <unistd.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include "libcmpiutil.h"
#include "std_instance.h"

#include "misc_util.h"

#include "Virt_AllocationCapabilities.h"
#include "Virt_RASD.h"

const static CMPIBroker *_BROKER;

CMPIStatus get_alloc_cap(const CMPIBroker *broker,
                         const CMPIObjectPath *ref,
                         CMPIInstance **inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        char *inst_id;
        uint16_t type;
        int ret;

        *inst = get_typed_instance(broker, "AllocationCapabilities", 
                                   NAMESPACE(ref));

        if (rasd_type_from_classname(CLASSNAME(ref), &type) != CMPI_RC_OK) {
                CMSetStatusWithChars(broker, &s, CMPI_RC_ERR_FAILED,
                                     "Could not get ResourceType.");
                goto out;
        }

        ret = asprintf(&inst_id, "%hi/%s", type, "0");
        if (ret == -1) {
                CMSetStatusWithChars(broker, &s, CMPI_RC_ERR_FAILED,
                                     "Could not get InstanceID.");
                goto out;
        }

        CMSetProperty(*inst, "InstanceID", inst_id, CMPI_chars);
        CMSetProperty(*inst, "ResourceType", &type, CMPI_uint16);

 out:
        return s;
}

static CMPIStatus return_alloc_cap(const CMPIObjectPath *ref, 
                                   const CMPIResult *results, 
                                   int names_only)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        s = get_alloc_cap(_BROKER, ref, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (names_only)
                cu_return_instance_name(results, inst);
        else
                CMReturnInstance(results, inst);
 out:
        return s;
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        return return_alloc_cap(reference, results, 0);
}

DEFAULT_EI();
DEFAULT_EIN();
DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(, Virt_AllocationCapabilitiesProvider, _BROKER,
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
