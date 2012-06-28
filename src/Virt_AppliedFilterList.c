/*
 * Copyright IBM Corp. 2011
 *
 * Authors:
 *  Chip Vincent <cvincent@us.ibm.com>
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
#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_association.h>
#include <libcmpiutil/std_instance.h>

#include "device_parsing.h"
#include "acl_parsing.h"
#include "misc_util.h"
#include "cs_util.h"
#include "xmlgen.h"

#include "Virt_Device.h"
#include "Virt_FilterList.h"

static const CMPIBroker *_BROKER;

/* TODO: Port to libcmpiutil/args_util.c */
/**
 * Get a reference property of an instance
 *
 * @param inst The instance
 * @param prop The property name
 * @param reference A pointer to a CMPIObjectPath* that will be set
 *                  if successful
 * @returns
 *      - CMPI_RC_OK on success
 *      - CMPI_RC_ERR_NO_SUCH_PROPERTY if prop is not present
 *      - CMPI_RC_ERR_TYPE_MISMATCH if prop is not a reference
 *      - CMPI_RC_OK otherwise
 */
static CMPIrc cu_get_ref_prop(const CMPIInstance *instance,
                              const char *prop,
                              CMPIObjectPath **reference)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIData value;

        /* REQUIRE_PROPERY_DEFINED(instance, prop, value, &s); */
        value = CMGetProperty(instance, prop, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullValue(value))
                return CMPI_RC_ERR_NO_SUCH_PROPERTY;

        if ((value.type != CMPI_ref) ||  CMIsNullObject(value.value.ref))
                return CMPI_RC_ERR_TYPE_MISMATCH;

        *reference = value.value.ref;

        return CMPI_RC_OK;
}

/* TODO: Port to libcmpiutil/args_util.c */
/**
 * Get a reference component of an object path
 *
 * @param _reference The reference
 * @param key The key name
 * @param reference A pointer to a CMPIObjectPath* that will be set
 *                  if successful
 * @returns
 *      - CMPI_RC_OK on success
 *      - CMPI_RC_ERR_NO_SUCH_PROPERTY if prop is not present
 *      - CMPI_RC_ERR_TYPE_MISMATCH if prop is not a reference
 *      - CMPI_RC_OK otherwise
 */
static CMPIrc cu_get_ref_path(const CMPIObjectPath *reference,
                              const char *key,
                              CMPIObjectPath **_reference)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIData value;

        /* REQUIRE_PROPERY_DEFINED(instance, prop, value, &s); */
        value = CMGetKey(reference, key, &s);
        if ((s.rc != CMPI_RC_OK) || CMIsNullValue(value))
                return CMPI_RC_ERR_NO_SUCH_PROPERTY;

        if ((value.type != CMPI_ref) ||  CMIsNullObject(value.value.ref))
                return CMPI_RC_ERR_TYPE_MISMATCH;

        *_reference = value.value.ref;

        return CMPI_RC_OK;
}

static int update_domain(virConnectPtr conn,
                         struct domain *dominfo)
{
        char *xml = NULL;
        virDomainPtr dom = NULL;

        xml = system_to_xml(dominfo);
        if (xml == NULL) {
                CU_DEBUG("Failed to get XML from domain %s", dominfo->name);
                goto out;
        }

        dom = virDomainDefineXML(conn, xml);
        if (dom == NULL) {
                CU_DEBUG("Failed to update domain %s", dominfo->name);
                goto out;
        }

 out:
        free(xml);
        virDomainFree(dom);

        return 0;
}

static int get_device_by_devid(struct domain *dominfo,
                               const char *devid,
                               struct virt_device **dev)
{
        int i;
        struct virt_device *devices = dominfo->dev_net;
        int count = dominfo->dev_net_ct;

        if (dev == NULL)
                return 0;

        for (i = 0; i < count; i++) {
                if (STREQC(devid, devices[i].id)) {
                        CU_DEBUG("Found '%s'", devices[i].id);

                        *dev = &devices[i];
                        return 0;
                }
        }

        return 1;
}

/**
 *  given a filter, get the network interface
 */
static CMPIStatus list_to_net(
        const CMPIObjectPath *reference,
        struct std_assoc_info *info,
        struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        const char *name = NULL;
        struct acl_filter *filter = NULL;
        virDomainPtr *doms = NULL;
        virConnectPtr conn = NULL;
        int i, j, dcount, ncount;

        CU_DEBUG("Reference = %s", REF2STR(reference));

        if (cu_get_str_path(reference, "Name", &name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_NOT_FOUND,
                        "Unable to get Name from reference");
                goto out;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        /* validate filter */
        get_filter_by_name(conn, name, &filter);
        if (filter == NULL)
                goto out;

        cleanup_filters(&filter, 1);

        /* get domains */
        dcount = get_domain_list(conn, &doms);
        if (dcount <= 0) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Failed to get domain list");
                goto out;
        }

        for (i = 0; i < dcount; i++) {
                /* get domain's network devices */
                struct virt_device *devices = NULL;
                ncount = get_devices(doms[i], &devices, CIM_RES_TYPE_NET,
                                                    VIR_DOMAIN_XML_INACTIVE);

                CU_DEBUG("Found %u network devices", ncount);

                for (j = 0; j < ncount; j++) {
                        struct net_device *ndev = &(devices[j].dev.net);

                        CU_DEBUG("filterref = %s", ndev->filter_ref);

                        if ((ndev->filter_ref != NULL) &&
                        STREQC(name, ndev->filter_ref)) {
                                CU_DEBUG("Getting network device instance");

                                CMPIInstance *instance = NULL;
                                char *device_id =
                                        get_fq_devid(
                                        (char *)virDomainGetName(doms[i]),
                                        devices[j].id);

                                CU_DEBUG("Processing %s", device_id);

                                s = get_device_by_name(_BROKER,
                                                        reference,
                                                        device_id,
                                                        CIM_RES_TYPE_NET,
                                                        &instance);

                                free(device_id);

                                if (instance != NULL)
                                        inst_list_add(list, instance);
                        }
                }

                cleanup_virt_devices(&devices, ncount);
                virDomainFree(doms[i]);
        }

 out:
        free(doms);
        virConnectClose(conn);

        return s;
}

/**
 * given a network interface, find the filter lists
 */
static CMPIStatus net_to_list(
        const CMPIObjectPath *reference,
        struct std_assoc_info *info,
        struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *instance = NULL;
        const char *device_name = NULL;
        char *domain_name = NULL;
        char *net_name = NULL;
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        int i;
        struct acl_filter *filter = NULL;

        CU_DEBUG("Reference %s", REF2STR(reference));

        /* validate device
         * TODO: This may be redundant since it's necessary to get
         * the device structure in order to determine the filter_ref
         */
        if (!STREQC(CLASSNAME(reference), "KVM_NetworkPort"))
                goto out;

        s = get_device_by_ref(_BROKER, reference, &instance);
        if ((s.rc != CMPI_RC_OK) || (instance == NULL))
                goto out;

        if (cu_get_str_path(reference, "DeviceID",
                &device_name) != CMPI_RC_OK) {
                CU_DEBUG("Failed to get DeviceID");
                goto out;
        }

        if (parse_fq_devid(device_name, &domain_name, &net_name) == 0) {
                CU_DEBUG("Failed to parse devid");
                goto out;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        /* connect to domain */
        dom = virDomainLookupByName(conn, domain_name);
        if (dom == NULL) {
                CU_DEBUG("Failed to connect to Domain '%s'", domain_name);
                goto out;
        }

        /* get domain's network devices */
        struct virt_device *devices = NULL;
        int count = get_devices(dom, &devices, CIM_RES_TYPE_NET,
                                                   VIR_DOMAIN_XML_INACTIVE);

        CU_DEBUG("Found %u net devices on dom '%s'", count, domain_name);

        for (i = 0; i < count; i++) {
                struct net_device *ndev = &(devices[i].dev.net);

                CU_DEBUG("Checking net device '%s' for filterref",
                        devices[i].id);

                if (STREQC(net_name, devices[i].id)) {
                        CMPIInstance *instance = NULL;

                        CU_DEBUG("Processing %s", ndev->filter_ref);

                        get_filter_by_name(conn, ndev->filter_ref, &filter);
                        if (filter == NULL)
                                continue;

                        s = instance_from_filter(_BROKER,
                                                info->context,
                                                reference,
                                                filter,
                                                &instance);

                        cleanup_filters(&filter, 1);

                        if (instance != NULL)
                                inst_list_add(list, instance);
                }

        }

        cleanup_virt_devices(&devices, count);

 out:
        free(domain_name);
        free(net_name);

        virDomainFree(dom);
        virConnectClose(conn);

        return s;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char *antecedent[] = {
        "KVM_FilterList",
        NULL
};

static char *dependent[] = {
        "KVM_NetworkPort",
        NULL
};

static char *assoc_class_name[] = {
        "KVM_AppliedFilterList",
        NULL
};

static struct std_assoc _list_to_net = {
        .source_class = (char **)&antecedent,
        .source_prop = "Antecedent",

        .target_class = (char **)&dependent,
        .target_prop = "Dependent",

        .assoc_class = (char **)&assoc_class_name,

        .handler = list_to_net,
        .make_ref = make_ref
};

static struct std_assoc _net_to_list = {
        .source_class = (char **)&dependent,
        .source_prop = "Dependent",

        .target_class = (char **)&antecedent,
        .target_prop = "Antecedent",

        .assoc_class = (char **)&assoc_class_name,

        .handler = net_to_list,
        .make_ref = make_ref
};

static struct std_assoc *handlers[] = {
        &_list_to_net,
        &_net_to_list,
        NULL
};

STDA_AssocMIStub(,
        Virt_AppliedFilterList,
        _BROKER,
        libvirt_cim_init(),
        handlers);

static CMPIStatus CreateInstance(
        CMPIInstanceMI *self,
        const CMPIContext *context,
        const CMPIResult *results,
        const CMPIObjectPath *reference,
        const CMPIInstance *instance)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIObjectPath *antecedent = NULL;
        const char *filter_name = NULL;
        struct acl_filter *filter = NULL;
        CMPIObjectPath *dependent = NULL;
        char *domain_name = NULL;
        const char *device_name = NULL;
        char *net_name = NULL;
        struct virt_device *device = NULL;
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        struct domain *dominfo = NULL;
        CMPIObjectPath *_reference = NULL;

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        CU_DEBUG("Reference = %s", REF2STR(reference));

        if (cu_get_ref_prop(instance, "Antecedent",
                &antecedent) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Antecedent property");
                goto out;
        }

        CU_DEBUG("Antecedent = %s", REF2STR(antecedent));

        if (cu_get_str_path(antecedent, "DeviceID",
                &device_name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Antecedent.DeviceID property");
                goto out;
        }

        if (cu_get_ref_prop(instance, "Dependent",
                &dependent) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Dependent property");
                goto out;
        }

        CU_DEBUG("Dependent = %s", REF2STR(dependent));

        if (cu_get_str_path(dependent, "Name",
                &filter_name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Dependent.Name property");
                goto out;
        }

        get_filter_by_name(conn, filter_name, &filter);
        if (filter == NULL) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Antecedent.Name object does not exist");
                goto out;
        }

        if (parse_fq_devid(device_name, &domain_name, &net_name) == 0) {
                CU_DEBUG("Failed to parse devid");
                goto out;
        }

        dom = virDomainLookupByName(conn, domain_name);
        if (dom == NULL) {
                CU_DEBUG("Failed to connect to Domain '%s'", domain_name);
                goto out;
        }

        if (get_dominfo(dom, &dominfo) == 0) {
                CU_DEBUG("Failed to get dominfo");
                goto out;
        }

        if (get_device_by_devid(dominfo, net_name, &device) != 0) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Dependent.Name object does not exist");
                goto out;
        }

        if (device->dev.net.filter_ref != NULL) {
                free(device->dev.net.filter_ref);
                device->dev.net.filter_ref = NULL;
        }

        device->dev.net.filter_ref = strdup(filter_name);

        if (update_domain(conn, dominfo) != 0) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Failed to update domain");
                goto out;
        }

        /* create new object path */
        _reference = CMClone(reference, NULL);
        CMAddKey(_reference, "Antecedent", (CMPIValue *)&antecedent, CMPI_ref);
        CMAddKey(_reference, "Dependent", (CMPIValue *)&dependent, CMPI_ref);

        CMReturnObjectPath(results, _reference);
        CU_DEBUG("CreateInstance complete");

 out:
        free(domain_name);
        free(net_name);

        cleanup_filters(&filter, 1);
        cleanup_dominfo(&dominfo);

        virDomainFree(dom);
        virConnectClose(conn);

        return s;
}

static CMPIStatus DeleteInstance(
        CMPIInstanceMI *self,
        const CMPIContext *context,
        const CMPIResult *results,
        const CMPIObjectPath *reference)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIObjectPath *antecedent = NULL;
        const char *filter_name = NULL;
        struct acl_filter *filter = NULL;
        CMPIObjectPath *dependent = NULL;
        char *domain_name = NULL;
        const char *device_name = NULL;
        char *net_name = NULL;
        struct virt_device *device = NULL;
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        struct domain *dominfo = NULL;

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        CU_DEBUG("Reference = %s", REF2STR(reference));

        if (cu_get_ref_path(reference, "Antecedent",
                &antecedent) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Antecedent property");
                goto out;
        }

        if (cu_get_str_path(antecedent, "DeviceID",
                &device_name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Antecedent.DeviceID property");
                goto out;
        }

        if (cu_get_ref_path(reference, "Dependent",
                &dependent) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Dependent property");
                goto out;
        }

        if (cu_get_str_path(dependent, "Name",
                &filter_name) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Unable to get Dependent.Name property");
                goto out;
        }

        get_filter_by_name(conn, filter_name, &filter);
        if (filter == NULL) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Dependent.Name object does not exist");
                goto out;
        }

        if (parse_fq_devid(device_name, &domain_name, &net_name) == 0) {
                CU_DEBUG("Failed to parse devid");
                goto out;
        }

        dom = virDomainLookupByName(conn, domain_name);
        if (dom == NULL) {
                CU_DEBUG("Failed to connect to Domain '%s'", domain_name);
                goto out;
        }

        if (get_dominfo(dom, &dominfo) == 0) {
                CU_DEBUG("Failed to get dominfo");
                goto out;
        }

        if (get_device_by_devid(dominfo, net_name, &device) != 0) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Antecedent.Name object does not exist");
                goto out;
        }

        if (device->dev.net.filter_ref != NULL) {
                free(device->dev.net.filter_ref);
                device->dev.net.filter_ref = NULL;
        }

        if (update_domain(conn, dominfo) != 0) {
                cu_statusf(_BROKER, &s,
                        CMPI_RC_ERR_FAILED,
                        "Failed to update domain");
                goto out;
        }

        CU_DEBUG("DeleteInstance complete");

 out:
        free(domain_name);
        free(net_name);

        cleanup_filters(&filter, 1);
        cleanup_dominfo(&dominfo);

        virDomainFree(dom);
        virConnectClose(conn);

        return s;
}

DEFAULT_GI();
DEFAULT_EIN();
DEFAULT_EI();
DEFAULT_MI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(,
        Virt_AppliedFilterList,
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
