/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Sharad Mishra <snmishra@us.ibm.com>
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
#include <string.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libvirt/libvirt.h>
#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_indication.h>
#include <misc_util.h>
#include <cs_util.h>

static const CMPIBroker *_BROKER;

DECLARE_FILTER(xen_created,
               "Xen_ResourceAllocationSettingDataCreatedIndication");
DECLARE_FILTER(xen_deleted,
               "Xen_ResourceAllocationSettingDataDeletedIndication");
DECLARE_FILTER(xen_modified,
               "Xen_ResourceAllocationSettingDataModifiedIndication");
DECLARE_FILTER(kvm_created,
               "KVM_ResourceAllocationSettingDataCreatedIndication");
DECLARE_FILTER(kvm_deleted,
               "KVM_ResourceAllocationSettingDataDeletedIndication");
DECLARE_FILTER(kvm_modified,
               "KVM_ResourceAllocationSettingDataModifiedIndication");
DECLARE_FILTER(lxc_created,
               "LXC_ResourceAllocationSettingDataCreatedIndication");
DECLARE_FILTER(lxc_deleted,
               "LXC_ResourceAllocationSettingDataDeletedIndication");
DECLARE_FILTER(lxc_modified,
               "LXC_ResourceAllocationSettingDataModifiedIndication");

static struct std_ind_filter *filters[] = {
        &xen_created,
        &xen_deleted,
        &xen_modified,
        &kvm_created,
        &kvm_deleted,
        &kvm_modified,
        &lxc_created,
        &lxc_deleted,
        &lxc_modified,
        NULL,
};


static CMPIStatus raise_indication(const CMPIBroker *broker,
                                   const CMPIContext *ctx,
                                   const CMPIObjectPath *ref,
                                   const CMPIInstance *ind)
{
        struct std_indication_ctx *_ctx = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct ind_args *args = NULL;
        CMPIObjectPath *_ref = NULL;

        _ctx = malloc(sizeof(struct std_indication_ctx));
        if (_ctx == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to allocate indication context");
                goto out;
        }

        _ctx->brkr = broker;
        _ctx->handler = NULL;
        _ctx->filters = filters;
        _ctx->enabled = 1;

        args = malloc(sizeof(struct ind_args));
        if (args == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to allocate ind_args");
                goto out;
        }

        _ref = CMGetObjectPath(ind, &s);
        if (_ref == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Got a null object path");
                goto out;
        }

        /* FIXME:  This is a Pegasus work around. Pegsus loses the namespace
                   when an ObjectPath is pulled from an instance */


        CMSetNameSpace(_ref, "root/virt");
        args->ns = strdup(NAMESPACE(_ref));
        args->classname = strdup(CLASSNAME(_ref));
        args->_ctx = _ctx;

        /* This is a workaround for Pegasus, it loses its objectpath by
           CMGetObjectPath. So set it back. */
        ind->ft->setObjectPath((CMPIInstance *)ind, _ref);

        s = stdi_deliver(broker, ctx, args, (CMPIInstance *)ind);
        if (s.rc == CMPI_RC_OK) {
                CU_DEBUG("Indication delivered");
        } else {
                if (s.msg == NULL) {
                        CU_DEBUG("Not delivered: msg is NULL.");
                } else {
                        CU_DEBUG("Not delivered: %s", CMGetCharPtr(s.msg));
                }
        }

 out:
        if (args != NULL)
                stdi_free_ind_args(&args);

        if (_ctx != NULL)
                free(_ctx);

        return s;
}

static struct std_indication_handler rasdi = {
        .raise_fn = raise_indication,
        .trigger_fn = NULL,
        .activate_fn = NULL,
        .deactivate_fn = NULL,
        .enable_fn = NULL,
        .disable_fn = NULL,
};

DEFAULT_IND_CLEANUP();
DEFAULT_AF();
DEFAULT_MP();

STDI_IndicationMIStub(,
                      Virt_ResourceAllocationSettingDataIndicationProvider,
                      _BROKER,
                      libvirt_cim_init(),
                      &rasdi,
                      filters);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
