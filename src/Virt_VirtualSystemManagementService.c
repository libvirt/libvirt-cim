/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <libvirt/libvirt.h>

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include "cs_util.h"
#include "misc_util.h"
#include "device_parsing.h"
#include "xmlgen.h"

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_invokemethod.h>
#include <libcmpiutil/std_indication.h>
#include <libcmpiutil/std_instance.h>

#include "misc_util.h"

#include "Virt_VirtualSystemManagementService.h"
#include "Virt_ComputerSystem.h"
#include "Virt_ComputerSystemIndication.h"
#include "Virt_RASD.h"
#include "Virt_HostSystem.h"
#include "svpc_types.h"

const static CMPIBroker *_BROKER;

enum ResourceAction {
        RESOURCE_ADD,
        RESOURCE_DEL,
        RESOURCE_MOD,
};

static CMPIStatus define_system_parse_args(const CMPIArgs *argsin,
                                           CMPIInstance **sys,
                                           const char *ns,
                                           CMPIArray **res)
{
        CMPIStatus s = {CMPI_RC_ERR_FAILED, NULL};

        if (cu_get_inst_arg(argsin, "SystemSettings", sys) != CMPI_RC_OK) {
                CU_DEBUG("No SystemSettings string argument");
                goto out;
        }

        if (cu_get_array_arg(argsin, "ResourceSettings", res) !=
            CMPI_RC_OK) {
                CU_DEBUG("Failed to get array arg");
                goto out;
        }

        cu_statusf(_BROKER, &s,
                   CMPI_RC_OK,
                   "");
 out:
        return s;
}

static int xenpv_vssd_to_domain(CMPIInstance *inst,
                                struct domain *domain)
{
        int ret;
        const char *val;

        domain->type = DOMAIN_XENPV;

        ret = cu_get_str_prop(inst, "Bootloader", &val);
        if (ret != CMPI_RC_OK)
                val = "";

        free(domain->bootloader);
        domain->bootloader = strdup(val);

        ret = cu_get_str_prop(inst, "BootloaderArgs", &val);
        if (ret != CMPI_RC_OK)
                val = "";

        free(domain->bootloader_args);
        domain->bootloader_args = strdup(val);

        return 1;
}

static int fv_vssd_to_domain(CMPIInstance *inst,
                             struct domain *domain,
                             const char *pfx)
{
        int ret;
        const char *val;

        if (STREQC(pfx, "KVM"))
                domain->type = DOMAIN_KVM;
        else if (STREQC(pfx, "Xen"))
                domain->type = DOMAIN_XENFV;
        else {
                CU_DEBUG("Unknown fullvirt domain type: %s", pfx);
                return 0;
        }

        ret = cu_get_str_prop(inst, "BootDevice", &val);
        if (ret != CMPI_RC_OK)
                val = "hd";

        free(domain->os_info.fv.boot);
        domain->os_info.fv.boot = strdup(val);

        return 1;
}

static int vssd_to_domain(CMPIInstance *inst,
                          struct domain *domain)
{
        uint16_t tmp;
        int ret = 0;
        const char *val;
        const char *cn;
        char *pfx = NULL;
        bool fullvirt;

        cn = CLASSNAME(CMGetObjectPath(inst, NULL));
        pfx = class_prefix_name(cn);
        if (pfx == NULL) {
                CU_DEBUG("Unknown prefix for class: %s", cn);
                return 0;
        }

        ret = cu_get_str_prop(inst, "VirtualSystemIdentifier", &val);
        if (ret != CMPI_RC_OK)
                goto out;

        free(domain->name);
        domain->name = strdup(val);

        ret = cu_get_u16_prop(inst, "AutomaticShutdownAction", &tmp);
        if (ret != CMPI_RC_OK)
                tmp = 0;

        domain->on_poweroff = (int)tmp;

        ret = cu_get_u16_prop(inst, "AutomaticRecoveryAction", &tmp);
        if (ret != CMPI_RC_OK)
                tmp = CIM_VSSD_RECOVERY_NONE;

        domain->on_crash = (int)tmp;

        if (STREQC(pfx, "KVM"))
                fullvirt = true;
        else if (cu_get_bool_prop(inst, "IsFullVirt", &fullvirt) != CMPI_RC_OK)
                fullvirt = false;

        if (fullvirt)
                ret = fv_vssd_to_domain(inst, domain, pfx);
        else
                ret = xenpv_vssd_to_domain(inst, domain);

 out:
        free(pfx);

        return ret;
}

static int xen_net_rasd_to_vdev(CMPIInstance *inst,
                                struct virt_device *dev)
{
        free(dev->dev.net.type);
        dev->dev.net.type = strdup("bridge");

        return 1;
}

static int kvm_net_rasd_to_vdev(CMPIInstance *inst,
                                struct virt_device *dev)
{
        free(dev->dev.net.type);
        dev->dev.net.type = strdup("network");

        free(dev->dev.net.source);
        dev->dev.net.source = strdup("default");

        return 1;
}

static int rasd_to_vdev(CMPIInstance *inst,
                        struct virt_device *dev)
{
        uint16_t type;
        const char *id = NULL;
        const char *val = NULL;
        char *name = NULL;
        char *devid = NULL;
        CMPIObjectPath *op;

        op = CMGetObjectPath(inst, NULL);
        if (op == NULL)
                goto err;

        if (rasd_type_from_classname(CLASSNAME(op), &type) != CMPI_RC_OK)
                goto err;

        dev->type = (int)type;

        if (cu_get_str_prop(inst, "InstanceID", &id) != CMPI_RC_OK)
                goto err;

        if (!parse_fq_devid(id, &name, &devid))
                goto err;

        if (type == VIRT_DEV_DISK) {
                free(dev->dev.disk.virtual_dev);
                dev->dev.disk.virtual_dev = devid;

                if (cu_get_str_prop(inst, "Address", &val) != CMPI_RC_OK)
                        val = "/dev/null";

                free(dev->dev.disk.source);
                dev->dev.disk.source = strdup(val);
                dev->dev.disk.disk_type = disk_type_from_file(val);
        } else if (type == VIRT_DEV_NET) {
                free(dev->dev.net.mac);
                dev->dev.net.mac = devid;

                if (STARTS_WITH(CLASSNAME(op), "Xen"))
                        xen_net_rasd_to_vdev(inst, dev);
                else if (STARTS_WITH(CLASSNAME(op), "KVM"))
                        kvm_net_rasd_to_vdev(inst, dev);
                else
                        CU_DEBUG("Unknown class type for net device: %s",
                                 CLASSNAME(op));

        } else if (type == VIRT_DEV_MEM) {
                cu_get_u64_prop(inst, "VirtualQuantity", &dev->dev.mem.size);
                cu_get_u64_prop(inst, "Reservation", &dev->dev.mem.size);
                dev->dev.mem.maxsize = dev->dev.mem.size;
                cu_get_u64_prop(inst, "Limit", &dev->dev.mem.maxsize);
                dev->dev.mem.size <<= 10;
                dev->dev.mem.maxsize <<= 10;
        }

        free(name);

        return 1;
 err:
        free(name);
        free(devid);

        return 0;
}

static int classify_resources(CMPIArray *resources,
                              struct domain *domain)
{
        int i;
        uint16_t type;
        int count;

        domain->dev_disk_ct = domain->dev_net_ct = 0;
        domain->dev_vcpu_ct = domain->dev_mem_ct = 0;
  
        count = CMGetArrayCount(resources, NULL);
        if (count < 1)
                return 0;

        domain->dev_disk = calloc(count, sizeof(struct virt_device));
        domain->dev_vcpu = calloc(count, sizeof(struct virt_device));
        domain->dev_mem = calloc(count, sizeof(struct virt_device));
        domain->dev_net = calloc(count, sizeof(struct virt_device));

        for (i = 0; i < count; i++) {
                CMPIObjectPath *op;
                CMPIData item;

                item = CMGetArrayElementAt(resources, i, NULL);
                if (CMIsNullObject(item.value.inst))
                        return 0;

                op = CMGetObjectPath(item.value.inst, NULL);
                if (op == NULL)
                        return 0;

                if (rasd_type_from_classname(CLASSNAME(op), &type) != 
                    CMPI_RC_OK)
                        return 0;

                if (type == CIM_RASD_TYPE_PROC)
                        rasd_to_vdev(item.value.inst,
                                     &domain->dev_vcpu[domain->dev_vcpu_ct++]);
                else if (type == CIM_RASD_TYPE_MEM)
                        rasd_to_vdev(item.value.inst,
                                     &domain->dev_mem[domain->dev_mem_ct++]);
                else if (type == CIM_RASD_TYPE_DISK)
                        rasd_to_vdev(item.value.inst,
                                     &domain->dev_disk[domain->dev_disk_ct++]);
                else if (type == CIM_RASD_TYPE_NET)
                        rasd_to_vdev(item.value.inst,
                                     &domain->dev_net[domain->dev_net_ct++]);
        }

       return 1;
}

static CMPIInstance *connect_and_create(char *xml,
                                        const CMPIObjectPath *ref,
                                        CMPIStatus *s)
{
        virConnectPtr conn;
        virDomainPtr dom;
        const char *name;
        CMPIInstance *inst = NULL;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), s);
        if (conn == NULL) {
                CU_DEBUG("libvirt connection failed");
                return NULL;
        }

        dom = virDomainDefineXML(conn, xml);
        if (dom == NULL) {
                CU_DEBUG("Failed to define domain from XML");
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to create domain");
                return NULL;
        }

        name = virDomainGetName(dom);

        *s = get_domain_by_name(_BROKER, ref, name, &inst);
        if (s->rc != CMPI_RC_OK) {
                CU_DEBUG("Failed to get new instance");
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to lookup resulting system");
        }

        virDomainFree(dom);
        virConnectClose(conn);

        return inst;
}

static CMPIInstance *create_system(CMPIInstance *vssd,
                                   CMPIArray *resources,
                                   const CMPIObjectPath *ref,
                                   CMPIStatus *s)
{
        CMPIInstance *inst = NULL;
        char *xml = NULL;

        struct domain *domain;

        domain = calloc(1, sizeof(*domain));
        if (domain == NULL) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to allocate memory");
                goto out;
        }

        if (!classify_resources(resources, domain)) {
                CU_DEBUG("Failed to classify resources");
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "ResourceSettings Error");
                goto out;
        }

        if (!vssd_to_domain(vssd, domain)) {
                CU_DEBUG("Failed to create domain from VSSD");
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "SystemSettings Error");
                goto out;
        }

        xml = system_to_xml(domain);
        CU_DEBUG("System XML:\n%s", xml);

        inst = connect_and_create(xml, ref, s);

 out:
        cleanup_dominfo(&domain);
        free(xml);

        return inst;
}

static bool trigger_indication(const CMPIContext *context,
                               const char *base_type,
                               const char *ns)
{
        char *type;
        CMPIStatus s;

        type = get_typed_class("Xen", base_type);

        s = stdi_trigger_indication(_BROKER, context, type, ns);

        free(type);

        return s.rc == CMPI_RC_OK;
}

static CMPIStatus define_system(CMPIMethodMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const CMPIArgs *argsin,
                                CMPIArgs *argsout)
{
        CMPIInstance *vssd;
        CMPIInstance *sys;
        CMPIArray *res;
        CMPIStatus s;

        CU_DEBUG("DefineSystem");

        s = define_system_parse_args(argsin, &vssd, NAMESPACE(reference), &res);
        if (s.rc != CMPI_RC_OK)
                goto out;

        sys = create_system(vssd, res, reference, &s);
        if (sys == NULL)
                goto out;

        CMAddArg(argsout, "ResultingSystem", &sys, CMPI_instance);

        trigger_indication(context,
                           "ComputerSystemCreatedIndication",
                           NAMESPACE(reference));
 out:
        return s;
}

static CMPIStatus destroy_system(CMPIMethodMI *self,
                                 const CMPIContext *context,
                                 const CMPIResult *results,
                                 const CMPIObjectPath *reference,
                                 const CMPIArgs *argsin,
                                 CMPIArgs *argsout)
{
        const char *dom_name = NULL;
        CMPIStatus status = {CMPI_RC_OK, NULL};
        CMPIValue rc;
        CMPIObjectPath *sys;

        virConnectPtr conn;

        conn = connect_by_classname(_BROKER,
                                    CLASSNAME(reference),
                                    &status);
        if (conn == NULL) {
                rc.uint32 = IM_RC_FAILED;
                goto error1;
        }

        if (cu_get_ref_arg(argsin, "AffectedSystem", &sys) != CMPI_RC_OK) {
                rc.uint32 = IM_RC_FAILED;
                goto error2;
        }

        dom_name = get_key_from_ref_arg(argsin, "AffectedSystem", "Name");
        if (dom_name == NULL) {
                rc.uint32 = IM_RC_FAILED;
                goto error2;
        }

        // Make sure system exists and destroy it.
        if (domain_exists(conn, dom_name)) {
                virDomainPtr dom = virDomainLookupByName(conn, dom_name);
                if (!virDomainDestroy(dom)) {
                        rc.uint32 = IM_RC_OK;
                } else {
                        rc.uint32 = IM_RC_FAILED;
                        cu_statusf(_BROKER, &status,
                                   CMPI_RC_ERR_FAILED,
                                   "Domain already exists");
                }
                virDomainFree(dom);
                trigger_indication(context,
                                   "ComputerSystemDeletedIndication",
                                   NAMESPACE(reference));
        } else {
                rc.uint32 = IM_RC_SYS_NOT_FOUND;
        }

 error2:
        virConnectClose(conn);
 error1:
        CMReturnData(results, &rc, CMPI_uint32);
        return status;
}

static CMPIStatus update_system_settings(const CMPIContext *context,
                                         const CMPIObjectPath *ref,
                                         CMPIInstance *vssd)
{
        CMPIStatus s;
        const char *name = NULL;
        CMPIrc ret;
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        struct domain *dominfo = NULL;
        char *xml = NULL;

        ret = cu_get_str_prop(vssd, "VirtualSystemIdentifier", &name);
        if (ret != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           ret,
                           "Missing VirtualSystemIdentifier");
                goto out;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        dom = virDomainLookupByName(conn, name);
        if (dom == NULL)
                goto out;

        if (!get_dominfo(dom, &dominfo)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to find existing domain `%s' to modify",
                           name);
                goto out;
        }

        if (!vssd_to_domain(vssd, dominfo)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid SystemSettings");
                goto out;
        }

        xml = system_to_xml(dominfo);
        if (xml != NULL) {
                CU_DEBUG("New XML is:\n%s", xml);
                connect_and_create(xml, ref, &s);
        }

        if (s.rc == CMPI_RC_OK) {
                trigger_indication(context,
                                   "ComputerSystemModifiedIndication",
                                   NAMESPACE(ref));
        }

 out:
        free(xml);
        virDomainFree(dom);
        virConnectClose(conn);
        cleanup_dominfo(&dominfo);

        return s;
}


static CMPIStatus mod_system_settings(CMPIMethodMI *self,
                                      const CMPIContext *context,
                                      const CMPIResult *results,
                                      const CMPIObjectPath *reference,
                                      const CMPIArgs *argsin,
                                      CMPIArgs *argsout)
{
        CMPIInstance *inst;

        if (cu_get_inst_arg(argsin, "SystemSettings", &inst) != CMPI_RC_OK) {
                CMPIStatus s;

                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing SystemSettings");
                return s;
        }

        return update_system_settings(context, reference, inst);
}

typedef CMPIStatus (*resmod_fn)(struct domain *,
                                CMPIInstance *,
                                uint16_t,
                                const char *);

static struct virt_device **find_list(struct domain *dominfo,
                                      uint16_t type,
                                      int **count)
{
        struct virt_device **list = NULL;

        if (type == VIRT_DEV_NET) {
                list = &dominfo->dev_net;
                *count = &dominfo->dev_net_ct;
        } else if (type == VIRT_DEV_DISK) {
                list = &dominfo->dev_disk;
                *count = &dominfo->dev_disk_ct;
        } else if (type == VIRT_DEV_VCPU) {
                list = &dominfo->dev_vcpu;
                *count = &dominfo->dev_vcpu_ct;
        } else if (type == VIRT_DEV_MEM) {
                list = &dominfo->dev_mem;
                *count = &dominfo->dev_mem_ct;
        }

        return list;
}

static CMPIStatus _resource_dynamic(struct domain *dominfo,
                                    struct virt_device *dev,
                                    enum ResourceAction action,
                                    const char *refcn)
{
        CMPIStatus s;
        virConnectPtr conn;
        virDomainPtr dom;
        int (*func)(virDomainPtr, struct virt_device *);

        if (action == RESOURCE_ADD)
                func = attach_device;
        else if (action == RESOURCE_DEL)
                func = detach_device;
        else if (action == RESOURCE_MOD)
                func = change_device;
        else {
                CU_DEBUG("Unknown dynamic resource action: %i", action);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Internal error (undefined resource action)");
                return s;
        }

        conn = connect_by_classname(_BROKER, refcn, &s);
        if (conn == NULL) {
                CU_DEBUG("Failed to connect");
                return s;
        }

        dom = virDomainLookupByName(conn, dominfo->name);
        if (dom == NULL) {
                CU_DEBUG("Failed to lookup VS `%s'", dominfo->name);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "Virtual System `%s' not found", dominfo->name);
                goto out;
        }

        if (!domain_online(dom)) {
                CU_DEBUG("VS `%s' not online; skipping dynamic update",
                         dominfo->name);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_OK,
                           "");
                goto out;
        }

        CU_DEBUG("Doing dynamic device update for `%s'", dominfo->name);

        if (func(dom, dev) == 0) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to change (%i) device", action);
        } else {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_OK,
                           "");
        }
 out:
        virDomainFree(dom);
        virConnectClose(conn);

        return s;
}

static CMPIStatus resource_del(struct domain *dominfo,
                               CMPIInstance *rasd,
                               uint16_t type,
                               const char *devid)
{
        CMPIStatus s;
        CMPIObjectPath *op;
        struct virt_device **_list;
        struct virt_device *list;
        int *count;
        int i;

        op = CMGetObjectPath(rasd, &s);
        if ((op == NULL) || (s.rc != CMPI_RC_OK))
                goto out;

        _list = find_list(dominfo, type, &count);
        if ((type == CIM_RASD_TYPE_MEM) || (_list != NULL))
                list = *_list;
        else {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Cannot delete resources of type %" PRIu16, type);
                goto out;
        }

        cu_statusf(_BROKER, &s,
                   CMPI_RC_ERR_FAILED,
                   "Device `%s' not found", devid);

        for (i = 0; i < *count; i++) {
                struct virt_device *dev = &list[i];

                if (STREQ(dev->id, devid)) {
                        s = _resource_dynamic(dominfo,
                                              dev,
                                              RESOURCE_DEL,
                                              CLASSNAME(op));
                        dev->type = VIRT_DEV_UNKNOWN;
                        break;
                }
        }

 out:
        return s;
}

static CMPIStatus resource_add(struct domain *dominfo,
                               CMPIInstance *rasd,
                               uint16_t type,
                               const char *devid)
{
        CMPIStatus s;
        CMPIObjectPath *op;
        struct virt_device **_list;
        struct virt_device *list;
        struct virt_device *dev;
        int *count;

        op = CMGetObjectPath(rasd, &s);
        if ((op == NULL) || (s.rc != CMPI_RC_OK))
                goto out;

        _list = find_list(dominfo, type, &count);
        if ((type == CIM_RASD_TYPE_MEM) || (_list == NULL)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Cannot add resources of type %" PRIu16, type);
                goto out;
        }

        if (*count < 0) {
                /* If count is negative, there was an error
                 * building the list for this device class
                 */
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "[TEMP] Cannot add resources of type %" PRIu16, type);
                goto out;
        }

        list = realloc(*_list, ((*count)+1)*sizeof(struct virt_device));
        if (list == NULL) {
                /* No memory */
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to allocate memory");
                goto out;
        }

        *_list = list;
        memset(&list[*count], 0, sizeof(list[*count]));

        dev = &list[*count];

        dev->type = type;
        dev->id = strdup(devid);
        rasd_to_vdev(rasd, dev);

        s = _resource_dynamic(dominfo, dev, RESOURCE_ADD, CLASSNAME(op));
        if (s.rc != CMPI_RC_OK)
                goto out;

        cu_statusf(_BROKER, &s,
                   CMPI_RC_OK,
                   "");
        (*count)++;

 out:
        return s;
}

static CMPIStatus resource_mod(struct domain *dominfo,
                               CMPIInstance *rasd,
                               uint16_t type,
                               const char *devid)
{
        CMPIStatus s;
        CMPIObjectPath *op;
        struct virt_device **_list;
        struct virt_device *list;
        int *count;
        int i;

        op = CMGetObjectPath(rasd, &s);
        if ((op == NULL) || (s.rc != CMPI_RC_OK))
                goto out;

        _list = find_list(dominfo, type, &count);
        if (_list != NULL)
                list = *_list;
        else {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Cannot modify resources of type %" PRIu16, type);
                goto out;
        }

        cu_statusf(_BROKER, &s,
                   CMPI_RC_ERR_FAILED,
                   "Device `%s' not found", devid);

        for (i = 0; i < *count; i++) {
                struct virt_device *dev = &list[i];

                if (STREQ(dev->id, devid)) {
                        rasd_to_vdev(rasd, dev);
                        s = _resource_dynamic(dominfo,
                                              dev,
                                              RESOURCE_MOD,
                                              CLASSNAME(op));
                        break;
                }
        }

 out:
        return s;
}

static CMPIStatus _update_resources_for(const CMPIObjectPath *ref,
                                        virDomainPtr dom,
                                        const char *devid,
                                        CMPIInstance *rasd,
                                        resmod_fn func)
{
        CMPIStatus s;
        struct domain *dominfo = NULL;
        uint16_t type;
        char *xml = NULL;
        CMPIObjectPath *op;

        if (!get_dominfo(dom, &dominfo)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Internal error (getting domain info)");
                goto out;
        }

        op = CMGetObjectPath(rasd, NULL);
        if (op == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get RASD path");
                goto out;
        }

        if (rasd_type_from_classname(CLASSNAME(op), &type) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to determine RASD type");
                goto out;
        }

        s = func(dominfo, rasd, type, devid);
        if (s.rc != CMPI_RC_OK) {
                CU_DEBUG("Resource transform function failed");
                goto out;
        }

        xml = system_to_xml(dominfo);
        if (xml != NULL) {
                CU_DEBUG("New XML:\n%s", xml);
                connect_and_create(xml, ref, &s);
        } else {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Internal error (xml generation failed)");
        }

 out:
        cleanup_dominfo(&dominfo);
        free(xml);

        return s;
}

static CMPIStatus _update_resource_settings(const CMPIObjectPath *ref,
                                            CMPIArray *resources,
                                            resmod_fn func)
{
        int i;
        virConnectPtr conn = NULL;
        CMPIStatus s;
        int count;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        count = CMGetArrayCount(resources, NULL);

        for (i = 0; i < count; i++) {
                CMPIData item;
                CMPIInstance *inst;
                const char *id = NULL;
                char *name = NULL;
                char *devid = NULL;
                virDomainPtr dom = NULL;

                item = CMGetArrayElementAt(resources, i, NULL);
                inst = item.value.inst;

                if (cu_get_str_prop(inst, "InstanceID", &id) != CMPI_RC_OK) {
                        cu_statusf(_BROKER, &s,
                                   CMPI_RC_ERR_FAILED,
                                   "Missing InstanceID");
                        goto end;
                }

                if (!parse_fq_devid(id, &name, &devid)) {
                        cu_statusf(_BROKER, &s,
                                   CMPI_RC_ERR_FAILED,
                                   "Bad InstanceID `%s'", id);
                        goto end;
                }

                dom = virDomainLookupByName(conn, name);
                if (dom == NULL) {
                        cu_statusf(_BROKER, &s,
                                   CMPI_RC_ERR_FAILED,
                                   "Unknown system `%s'", name);
                        goto end;
                }

                s = _update_resources_for(ref, dom, devid, inst, func);

        end:
                free(name);
                free(devid);
                virDomainFree(dom);

                if (s.rc != CMPI_RC_OK)
                        break;

        }
 out:
        virConnectClose(conn);

        return s;
}

static CMPIStatus update_resource_settings(const CMPIObjectPath *ref,
                                           const CMPIArgs *argsin,
                                           resmod_fn func)
{
        CMPIArray *arr;
        CMPIStatus s;

        if (cu_get_array_arg(argsin, "ResourceSettings", &arr) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing ResourceSettings");
                goto out;
        }

        s = _update_resource_settings(ref, arr, func);

 out:
        return s;
}

static CMPIStatus rasd_refs_to_insts(const CMPIContext *ctx,
                                     const CMPIObjectPath *reference,
                                     CMPIArray *arr,
                                     CMPIArray **ret_arr)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIArray *tmp_arr;
        int i;
        int c;

        c = CMGetArrayCount(arr, &s);
        if (s.rc != CMPI_RC_OK)
                return s;

        tmp_arr = CMNewArray(_BROKER,
                             c,
                             CMPI_instance,
                             &s); 

        for (i = 0; i < c; i++) {
                CMPIData d;
                CMPIObjectPath *ref;
                CMPIInstance *inst = NULL;
                const char *id;
                uint16_t type;

                d = CMGetArrayElementAt(arr, i, &s);
                ref = d.value.ref;
                if (s.rc != CMPI_RC_OK) {
                        CU_DEBUG("Unable to get ResourceSettings[%i]", i);
                        continue;
                }

                if (cu_get_str_path(ref, "InstanceID", &id) != CMPI_RC_OK) {
                        CU_DEBUG("Unable to get InstanceID of `%s'",
                                 REF2STR(ref));
                        continue;
                }

                if (rasd_type_from_classname(CLASSNAME(ref), &type) !=
                    CMPI_RC_OK) {
                        CU_DEBUG("Unable to get type of `%s'",
                                 REF2STR(ref));
                        continue;
                }

                s = get_rasd_by_name(_BROKER, reference, id, type, NULL, &inst);
                if (s.rc != CMPI_RC_OK)
                        continue;

                CMSetArrayElementAt(tmp_arr, i,
                                    &inst,
                                    CMPI_instance);
                
        }

        *ret_arr = tmp_arr;
        
        return s;
}

static CMPIStatus add_resource_settings(CMPIMethodMI *self,
                                        const CMPIContext *context,
                                        const CMPIResult *results,
                                        const CMPIObjectPath *reference,
                                        const CMPIArgs *argsin,
                                        CMPIArgs *argsout)
{
        return update_resource_settings(reference, argsin, resource_add);
}

static CMPIStatus mod_resource_settings(CMPIMethodMI *self,
                                        const CMPIContext *context,
                                        const CMPIResult *results,
                                        const CMPIObjectPath *reference,
                                        const CMPIArgs *argsin,
                                        CMPIArgs *argsout)
{
        return update_resource_settings(reference, argsin, resource_mod);
}

static CMPIStatus rm_resource_settings(CMPIMethodMI *self,
                                       const CMPIContext *context,
                                       const CMPIResult *results,
                                       const CMPIObjectPath *reference,
                                       const CMPIArgs *argsin,
                                       CMPIArgs *argsout)
{
        /* The RemoveResources case is different from either Add or
         * Modify, because it takes references instead of instances
         */

        CMPIArray *arr;
        CMPIArray *resource_arr;
        CMPIStatus s;

        if (cu_get_array_arg(argsin, "ResourceSettings", &arr) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing ResourceSettings");
                goto out;
        }

        s = rasd_refs_to_insts(context, reference, arr, &resource_arr);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = _update_resource_settings(reference, resource_arr, resource_del);
 out:
        return s;
}

static struct method_handler DefineSystem = {
        .name = "DefineSystem",
        .handler = define_system,
        .args = {{"SystemSettings", CMPI_instance, false},
                 {"ResourceSettings", CMPI_instanceA, false},
                 {"ReferenceConfiguration", CMPI_string, false},
                 ARG_END
        }
};

static struct method_handler DestroySystem = {
        .name = "DestroySystem",
        .handler = destroy_system,
        .args = {{"AffectedSystem", CMPI_ref, false},
                 ARG_END
        }
};

static struct method_handler AddResourceSettings = {
        .name = "AddResourceSettings",
        .handler = add_resource_settings,
        .args = {{"AffectedConfiguration", CMPI_ref, false},
                 {"ResourceSettings", CMPI_instanceA, false},
                 ARG_END
        }
};

static struct method_handler ModifyResourceSettings = {
        .name = "ModifyResourceSettings",
        .handler = mod_resource_settings,
        .args = {{"ResourceSettings", CMPI_instanceA, false},
                 ARG_END
        }
};

static struct method_handler ModifySystemSettings = {
        .name = "ModifySystemSettings",
        .handler = mod_system_settings,
        .args = {{"SystemSettings", CMPI_instance, false},
                 ARG_END
        }
};

static struct method_handler RemoveResourceSettings = {
        .name = "RemoveResourceSettings",
        .handler = rm_resource_settings,
        .args = {{"ResourceSettings", CMPI_refA, false},
                 ARG_END
        }
};

static struct method_handler *my_handlers[] = {
        &DefineSystem,
        &DestroySystem,
        &AddResourceSettings,
        &ModifyResourceSettings,
        &ModifySystemSettings,
        &RemoveResourceSettings,
        NULL,
};

STDIM_MethodMIStub(, Virt_VirtualSystemManagementService,
                   _BROKER, libvirt_cim_init(), my_handlers);

CMPIStatus get_vsms(const CMPIObjectPath *reference,
                    CMPIInstance **_inst,
                    const CMPIBroker *broker,
                    bool is_get_inst)
{ 
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        const char *name = NULL;
        const char *ccname = NULL;
        virConnectPtr conn = NULL;

        *_inst = NULL;
        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL) {
                if (is_get_inst)
                        cu_statusf(broker, &s,
                                   CMPI_RC_ERR_NOT_FOUND,
                                   "No such instance");

                return s;
        }

        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "VirtualSystemManagementService",
                                  NAMESPACE(reference));

        if (inst == NULL) {
                CU_DEBUG("Failed to get typed instance");
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to create instance");
                goto out;
        }

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
                      (CMPIValue *)"Management Service", CMPI_chars);

        CMSetProperty(inst, "SystemName",
                      (CMPIValue *)name, CMPI_chars);

        CMSetProperty(inst, "SystemCreationClassName",
                      (CMPIValue *)ccname, CMPI_chars);

        if (is_get_inst) {
                s = cu_validate_ref(broker, reference, inst);
                if (s.rc != CMPI_RC_OK)
                        goto out;
        }

        cu_statusf(broker, &s,
                   CMPI_RC_OK,
                   "");
 out:
        virConnectClose(conn);
        *_inst = inst;

        return s;
}

static CMPIStatus return_vsms(const CMPIObjectPath *reference,
                              const CMPIResult *results,
                              bool name_only,
                              bool is_get_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst;

        s = get_vsms(reference, &inst, _BROKER, is_get_inst);
        if (s.rc != CMPI_RC_OK || inst == NULL)
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
                                    const CMPIObjectPath *reference)
{
        return return_vsms(reference, results, true, false);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{

        return return_vsms(reference, results, false, false);
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
                   Virt_VirtualSystemManagementService, 
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
