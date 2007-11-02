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

#include "libcmpiutil.h"

#include "std_invokemethod.h"
#include "std_indication.h"
#include "misc_util.h"

#include "Virt_ComputerSystem.h"
#include "Virt_ComputerSystemIndication.h"
#include "Virt_RASD.h"
#include "Virt_HostSystem.h"
#include "svpc_types.h"

const static CMPIBroker *_BROKER;

static int parse_str_inst_array(CMPIArray *array,
                                struct inst_list *list)
{
        int count;
        int i;

        count = CMGetArrayCount(array, NULL);

        for (i = 0; i < count; i++) {
                CMPIInstance *inst;
                CMPIData item;
                int ret;

                item = CMGetArrayElementAt(array, i, NULL);
                /* FIXME: Check for string here */

                ret = cu_parse_embedded_instance(CMGetCharPtr(item.value.string),
                                        _BROKER,
                                        &inst);

                if (ret == 0)
                        inst_list_add(list, inst);
        }

        return 1;
}

static CMPIStatus define_system_parse_args(const CMPIArgs *argsin,
                                           CMPIInstance **sys,
                                           struct inst_list *res)
{
        CMPIStatus s = {CMPI_RC_ERR_FAILED, NULL};
        char *sys_str = NULL;
        CMPIArray *res_arr;
        int ret;

        sys_str = cu_get_str_arg(argsin, "SystemSettings");
        if (sys_str == NULL) {
                CU_DEBUG("No SystemSettings string argument");
                goto out;
        }

        ret = cu_parse_embedded_instance(sys_str, _BROKER, sys);
        if (ret) {
                CU_DEBUG("Unable to parse SystemSettings instance");
                CMSetStatusWithChars(_BROKER, &s,
                                     CMPI_RC_ERR_FAILED,
                                     "SystemSettings parse error");
                goto out;
        }

        res_arr = cu_get_array_arg(argsin, "ResourceSettings");
        if (res_arr == NULL) {
                CU_DEBUG("Failed to get array arg");
                goto out;
        }

        ret = parse_str_inst_array(res_arr, res);

        CMSetStatus(&s, CMPI_RC_OK);

 out:
        free(sys_str);

        return s;
}

static int vssd_to_domain(CMPIInstance *inst,
                          struct domain *domain)
{
        uint16_t tmp;
        int ret = 0;

        free(domain->name);
        ret = cu_get_str_prop(inst, "VirtualSystemIdentifier", &domain->name);
        if (ret != CMPI_RC_OK)
                goto out;

        ret = cu_get_u16_prop(inst, "AutomaticShutdownAction", &tmp);
        if (ret != CMPI_RC_OK)
                tmp = 0;

        domain->on_poweroff = (int)tmp;

        ret = cu_get_u16_prop(inst, "AutomaticRecoveryAction", &tmp);
        if (ret != CMPI_RC_OK)
                tmp = CIM_VSSD_RECOVERY_NONE;

        domain->on_crash = (int)tmp;

        free(domain->bootloader);
        ret = cu_get_str_prop(inst, "Bootloader", &domain->bootloader);
        if (ret != CMPI_RC_OK)
                domain->bootloader = strdup("");

        free(domain->bootloader_args);
        ret = cu_get_str_prop(inst, "BootloaderArgs", &domain->bootloader_args);
        if (ret != CMPI_RC_OK)
                domain->bootloader_args = strdup("");

        ret = 1;
 out:
        return ret;
}

static int rasd_to_vdev(CMPIInstance *inst,
                        struct virt_device *dev)
{
        uint16_t type;
        char *id = NULL;
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

                free(dev->dev.disk.source);
                cu_get_str_prop(inst, "Address", &dev->dev.disk.source);
        } else if (type == VIRT_DEV_NET) {
                free(dev->dev.net.mac);
                dev->dev.net.mac = devid;

                free(dev->dev.net.type);
                cu_get_str_prop(inst, "NetworkType", &dev->dev.net.type);
                if (dev->dev.net.type == NULL)
                        dev->dev.net.type = strdup("bridge");
        } else if (type == VIRT_DEV_MEM) {
                cu_get_u64_prop(inst, "VirtualQuantity", &dev->dev.mem.size);
                cu_get_u64_prop(inst, "Reservation", &dev->dev.mem.size);
                dev->dev.mem.maxsize = dev->dev.mem.size;
                cu_get_u64_prop(inst, "Limit", &dev->dev.mem.maxsize);
                dev->dev.mem.size <<= 10;
                dev->dev.mem.maxsize <<= 10;
        }

        free(id);
        free(name);

        return 1;
 err:
        free(id);
        free(name);
        free(devid);

        return 0;
}

static int classify_resources(struct inst_list *all,
                              struct domain *domain)
{
        int i;
        uint16_t type;

        domain->dev_disk_ct = domain->dev_net_ct = 0;
        domain->dev_vcpu_ct = domain->dev_mem_ct = 0;

        domain->dev_disk = calloc(all->cur, sizeof(struct virt_device));
        domain->dev_vcpu = calloc(all->cur, sizeof(struct virt_device));
        domain->dev_mem = calloc(all->cur, sizeof(struct virt_device));
        domain->dev_net = calloc(all->cur, sizeof(struct virt_device));

        for (i = 0; i < all->cur; i++) {
                CMPIObjectPath *op;

                op = CMGetObjectPath(all->list[i], NULL);
                if (op == NULL)
                        return 0;

                if (rasd_type_from_classname(CLASSNAME(op), &type) != 
                    CMPI_RC_OK)
                        return 0;

                if (type == CIM_RASD_TYPE_PROC)
                        rasd_to_vdev(all->list[i],
                                     &domain->dev_vcpu[domain->dev_vcpu_ct++]);
                else if (type == CIM_RASD_TYPE_MEM)
                        rasd_to_vdev(all->list[i],
                                     &domain->dev_mem[domain->dev_mem_ct++]);
                else if (type == CIM_RASD_TYPE_DISK)
                        rasd_to_vdev(all->list[i],
                                     &domain->dev_disk[domain->dev_disk_ct++]);
                else if (type == CIM_RASD_TYPE_NET)
                        rasd_to_vdev(all->list[i],
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
        CMPIInstance *inst;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), s);
        if (conn == NULL) {
                CU_DEBUG("libvirt connection failed");
                return NULL;
        }

        dom = virDomainDefineXML(conn, xml);
        if (dom == NULL) {
                CU_DEBUG("Failed to define domain from XML");
                CMSetStatusWithChars(_BROKER, s,
                                     CMPI_RC_ERR_FAILED,
                                     "Failed to create domain");
                return NULL;
        }

        name = virDomainGetName(dom);
        inst = instance_from_name(_BROKER, conn, (char *)name, ref);
        if (inst == NULL) {
                CU_DEBUG("Failed to get new instance");
                CMSetStatusWithChars(_BROKER, s,
                                     CMPI_RC_ERR_FAILED,
                                     "Failed to lookup resulting system");
        }

        virConnectClose(conn);

        return inst;
}

static CMPIInstance *create_system(CMPIInstance *vssd,
                                   struct inst_list *resources,
                                   const CMPIObjectPath *ref,
                                   CMPIStatus *s)
{
        CMPIInstance *inst = NULL;
        char *xml = NULL;

        struct domain *domain;

        domain = calloc(1, sizeof(*domain));
        if (domain == NULL) {
                CMSetStatus(s, CMPI_RC_ERR_FAILED);
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

        type = get_typed_class(base_type);

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
        CMPIObjectPath *sys_op;
        struct inst_list res;
        CMPIStatus s;

        inst_list_init(&res);

        CU_DEBUG("DefineSystem");

        s = define_system_parse_args(argsin, &vssd, &res);
        if (s.rc != CMPI_RC_OK)
                goto out;

        sys = create_system(vssd, &res, reference, &s);

        inst_list_free(&res);

        CMAddArg(argsout, "ResultingSystem", &sys, CMPI_instance);

        sys_op = CMGetObjectPath(sys, NULL);
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
        char *dom_name = NULL;
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

        sys = cu_get_ref_arg(argsin, "AffectedSystem");
        if (sys == NULL) {
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
                        CMSetStatus(&status, CMPI_RC_ERR_FAILED);
                }
                virDomainFree(dom);
                trigger_indication(context,
                                   "ComputerSystemDeletedIndication",
                                   NAMESPACE(reference));
        } else {
                rc.uint32 = IM_RC_SYS_NOT_FOUND;
        }

        free(dom_name);
 error2:
        virConnectClose(conn);
 error1:
        CMReturnData(results, &rc, CMPI_uint32);
        return status;
}

static CMPIStatus update_system_settings(const CMPIObjectPath *ref,
                                         CMPIInstance *vssd)
{
        CMPIStatus s;
        char *name = NULL;
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

        conn = lv_connect(_BROKER, &s);
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
                printf("New XML is:\n%s\n", xml);
                connect_and_create(xml, ref, &s);
        }

 out:
        free(xml);
        free(name);
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
        char *inst_str;
        CMPIInstance *inst;

        inst_str = cu_get_str_arg(argsin, "SystemSettings");
        if (inst == NULL) {
                CMPIStatus s;

                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing SystemSettings");
                return s;
        }

        if (cu_parse_embedded_instance(inst_str, _BROKER, &inst)) {
                CMPIStatus s;

                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Internal Parse Error on SystemSettings");
                return s;
        }

        return update_system_settings(reference, inst);
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
        }

        return list;
}

static CMPIStatus resource_del(struct domain *dominfo,
                               CMPIInstance *rasd,
                               uint16_t type,
                               const char *devid)
{
        CMPIStatus s;
        struct virt_device **_list;
        struct virt_device *list;
        int *count;
        int i;

        _list = find_list(dominfo, type, &count);
        if (_list != NULL)
                list = *_list;
        else {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Cannot delete resources of type %" PRIu16,
                           type);
                goto out;
        }

        cu_statusf(_BROKER, &s,
                   CMPI_RC_ERR_FAILED,
                   "Device `%s' not found", devid);

        for (i = 0; i < *count; i++) {
                struct virt_device *dev = &list[i];

                if (STREQ(dev->id, devid)) {
                        dev->type = VIRT_DEV_UNKNOWN;
                        CMSetStatus(&s, CMPI_RC_OK);
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
        struct virt_device **_list;
        struct virt_device *list;
        int *count;

        _list = find_list(dominfo, type, &count);
        if (_list == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Cannot add resources of type %" PRIu16,
                           type);
                goto out;
        }

        if (*count < 0) {
                /* If count is negative, there was an error
                 * building the list for this device class
                 */
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "[TEMP] Cannot add resources of type %" PRIu16,
                           type);
                goto out;
        }

        list = realloc(*_list, ((*count)+1)*sizeof(struct virt_device));
        if (list == NULL) {
                /* No memory */
                CMSetStatus(&s, CMPI_RC_ERR_FAILED);
                goto out;
        }

        *_list = list;
        memset(&list[*count], 0, sizeof(list[*count]));

        list[*count].type = type;
        list[*count].id = strdup(devid);
        rasd_to_vdev(rasd, &list[*count]);
        (*count)++;

        CMSetStatus(&s, CMPI_RC_OK);
 out:
        return s;
}

static CMPIStatus resource_mod(struct domain *dominfo,
                               CMPIInstance *rasd,
                               uint16_t type,
                               const char *devid)
{
        CMPIStatus s;
        struct virt_device **_list;
        struct virt_device *list;
        int *count;
        int i;

        _list = find_list(dominfo, type, &count);
        if (_list != NULL)
                list = *_list;
        else {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Cannot modify resources of type %" PRIu16,
                           type);
                goto out;
        }

        cu_statusf(_BROKER, &s,
                   CMPI_RC_ERR_FAILED,
                   "Device `%s' not found", devid);

        for (i = 0; i < *count; i++) {
                struct virt_device *dev = &list[i];

                if (STREQ(dev->id, devid)) {
                        rasd_to_vdev(rasd, dev);
                        CMSetStatus(&s, CMPI_RC_OK);
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

        xml = system_to_xml(dominfo);
        if (xml != NULL) {
                printf("New XML:\n%s\n", xml);
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
                                            struct inst_list *list,
                                            resmod_fn func)
{
        int i;
        virConnectPtr conn = NULL;
        CMPIStatus s;

        conn = lv_connect(_BROKER, &s);
        if (conn == NULL)
                goto out;

        for (i = 0; i < list->cur; i++) {
                CMPIInstance *inst = list->list[i];
                char *id = NULL;
                char *name = NULL;
                char *devid = NULL;
                virDomainPtr dom = NULL;

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
                free(id);
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
        CMPIArray *array;
        CMPIStatus s;
        struct inst_list list;

        inst_list_init(&list);

        array = cu_get_array_arg(argsin, "ResourceSettings");
        if (array == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing ResourceSettings");
                goto out;
        }

        parse_str_inst_array(array, &list);

        s = _update_resource_settings(ref, &list, func);

 out:
        inst_list_free(&list);

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
        return update_resource_settings(reference, argsin, resource_del);
}

static struct method_handler DefineSystem = {
        .name = "DefineSystem",
        .handler = define_system,
        .args = {{"SystemSettings", CMPI_string},
                 {"ResourceSettings", CMPI_stringA},
                 {"ReferencedConfiguration", CMPI_string},
                 ARG_END
        }
};

static struct method_handler DestroySystem = {
        .name = "DestroySystem",
        .handler = destroy_system,
        .args = {{"AffectedSystem", CMPI_ref},
                 ARG_END
        }
};

static struct method_handler AddResourceSettings = {
        .name = "AddResourceSettings",
        .handler = add_resource_settings,
        .args = {{"AffectedConfiguration", CMPI_string},
                 {"ResourceSettings", CMPI_stringA},
                 ARG_END
        }
};

static struct method_handler ModifyResourceSettings = {
        .name = "ModifyResourceSettings",
        .handler = mod_resource_settings,
        .args = {{"ResourceSettings", CMPI_stringA},
                 ARG_END
        }
};

static struct method_handler ModifySystemSettings = {
        .name = "ModifySystemSettings",
        .handler = mod_system_settings,
        .args = {{"SystemSettings", CMPI_string},
                 ARG_END
        }
};

static struct method_handler RemoveResourceSettings = {
        .name = "RemoveResourceSettings",
        .handler = rm_resource_settings,
        .args = {{"ResourceSettings", CMPI_stringA},
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
                   _BROKER, CMNoHook, my_handlers);

static CMPIStatus _get_vsms(const CMPIObjectPath *reference,
                            CMPIInstance **_inst,
                            int name_only)
{
        CMPIStatus s;
        CMPIInstance *inst;
        CMPIInstance *host;
        char *val = NULL;

        s = get_host_cs(_BROKER, reference, &host);
        if (s.rc != CMPI_RC_OK)
                goto out;

        inst = get_typed_instance(_BROKER,
                                  "VirtualSystemManagementService",
                                  NAMESPACE(reference));
        if (inst == NULL) {
                CU_DEBUG("Failed to get typed instance");
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to create instance");
                goto out;
        }

        CMSetProperty(inst, "Name",
                      (CMPIValue *)"Management Service", CMPI_chars);

        if (cu_get_str_prop(host, "Name", &val) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get name of System");
                goto out;
        }

        CMSetProperty(inst, "SystemName",
                      (CMPIValue *)val, CMPI_chars);
        free(val);

        if (cu_get_str_prop(host, "CreationClassName", &val) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to get creation class of system");
                goto out;
        }

        CMSetProperty(inst, "SystemCreationClassName",
                      (CMPIValue *)val, CMPI_chars);
        free(val);

        CMSetStatus(&s, CMPI_RC_OK);

        *_inst = inst;
 out:
        return s;
}

static CMPIStatus return_vsms(const CMPIObjectPath *reference,
                              const CMPIResult *results,
                              int name_only)
{
        CMPIInstance *inst;
        CMPIStatus s;

        s = _get_vsms(reference, &inst, name_only);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (name_only)
                cu_return_instance_name(results, inst);
        else
                CMReturnInstance(results, inst);

        CMSetStatus(&s, CMPI_RC_OK);
 out:
        return s;
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_vsms(reference, results, 1);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{

        return return_vsms(reference, results, 0);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *ref,
                              const char **properties)
{
        CMPIInstance *inst;
        CMPIStatus s;
        const struct cu_property *prop;
        static struct cu_property props[] = {
                {"CreationClassName", 0},
                {"SystemName", 0},
                {"SystemCreationClassName", 0},
                {"Name", 1},
                {NULL, 0}
        };

        s = _get_vsms(ref, &inst, 0);
        if (s.rc != CMPI_RC_OK)
                return s;

        prop = cu_compare_ref(ref, inst, props);
        if (prop != NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s)", prop->name);
        } else {
                CMSetStatus(&s, CMPI_RC_OK);
                CMReturnInstance(results, inst);
        }

        return s;
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

/* Avoid a warning in the stub macro below */
CMPIInstanceMI *
Virt_VirtualSystemManagementService_Create_InstanceMI(const CMPIBroker *,
                                                      const CMPIContext *,
                                                      CMPIStatus *rc);

CMInstanceMIStub(, Virt_VirtualSystemManagementService, _BROKER, CMNoHook);


/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
