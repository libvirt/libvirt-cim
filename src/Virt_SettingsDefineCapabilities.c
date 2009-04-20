/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *  Jay Gagnon <grendel@linux.vnet.ibm.com>
 *  Richard Maciel <rmaciel@linux.vnet.ibm.com>
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/vfs.h>
#include <errno.h>
#include <uuid/uuid.h>

#include <libvirt/libvirt.h>

#include "config.h"

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include <libcmpiutil/libcmpiutil.h>
#include "misc_util.h"
#include <libcmpiutil/std_association.h>
#include "device_parsing.h"
#include "svpc_types.h"

#include "Virt_SettingsDefineCapabilities.h"
#include "Virt_DevicePool.h"
#include "Virt_RASD.h"
#include "Virt_VSMigrationCapabilities.h"
#include "Virt_VSMigrationSettingData.h"
#include "Virt_VirtualSystemManagementService.h"
#include "Virt_AllocationCapabilities.h"
#include "Virt_Device.h"

/*
 * Right now, detect support and use it, if available.
 * Later, this can be a configure option if needed
 */
#if LIBVIR_VERSION_NUMBER > 4000
# define VIR_USE_LIBVIRT_STORAGE 1
#else
# define VIR_USE_LIBVIRT_STORAGE 0
#endif

const static CMPIBroker *_BROKER;

/* These are used in more than one place so they are defined here. */
#define SDC_DISK_MIN 2000
#define SDC_DISK_DEF 5000
#define SDC_DISK_INC 250

static bool system_has_vt(virConnectPtr conn)
{
        char *caps = NULL;
        bool vt = false;

        caps = virConnectGetCapabilities(conn);
        if (caps != NULL)
                vt = (strstr(caps, "hvm") != NULL);

        free(caps);

        return vt;
}

static CMPIInstance *default_vssd_instance(const char *prefix,
                                           const char *ns)
{
        CMPIInstance *inst = NULL;
        uuid_t uuid;
        char uuidstr[37];
        char *iid = NULL;

        uuid_generate(uuid);
        uuid_unparse(uuid, uuidstr);

        if (asprintf(&iid, "%s:%s", prefix, uuidstr) == -1) {
                CU_DEBUG("Failed to generate InstanceID string");
                goto out;
        }

        inst = get_typed_instance(_BROKER,
                                  prefix,
                                  "VirtualSystemSettingData",
                                  ns);
        if (inst == NULL) {
                CU_DEBUG("Failed to create default VSSD instance");
                goto out;
        }

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)iid, CMPI_chars);

 out:
        free(iid);

        return inst;
}

static CMPIInstance *_xen_base_vssd(virConnectPtr conn,
                                    const char *ns,
                                    const char *name)
{
        CMPIInstance *inst;

        inst = default_vssd_instance(pfx_from_conn(conn), ns);
        if (inst == NULL)
                return NULL;

        CMSetProperty(inst, "VirtualSystemIdentifier",
                      (CMPIValue *)name, CMPI_chars);

        return inst;
}

static CMPIStatus _xen_vsmc_to_vssd(virConnectPtr conn,
                                    const char *ns,
                                    struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst;
        int isfv = 0;

        inst = _xen_base_vssd(conn, ns, "Xen_Paravirt_Guest");
        if (inst == NULL)
                goto error;

        CMSetProperty(inst, "Bootloader",
                      (CMPIValue *)"/usr/bin/pygrub", CMPI_chars);

        CMSetProperty(inst, "isFullVirt",
                      (CMPIValue *)&isfv, CMPI_boolean);

        inst_list_add(list, inst);

        if (system_has_vt(conn)) {
                isfv = 1;

                inst = _xen_base_vssd(conn, ns, "Xen_Fullvirt_Guest");
                if (inst == NULL)
                        goto error;

                CMSetProperty(inst, "BootDevice",
                              (CMPIValue *)"hda", CMPI_chars);

                CMSetProperty(inst, "isFullVirt",
                              (CMPIValue *)&isfv, CMPI_boolean);

                inst_list_add(list, inst);
        }

        return s;

 error:
        cu_statusf(_BROKER, &s,
                   CMPI_RC_ERR_FAILED,
                   "Unable to create %s_VSSD instance",
                   pfx_from_conn(conn));

        return s;
}

static CMPIStatus _kvm_vsmc_to_vssd(virConnectPtr conn,
                                    const char *ns,
                                    struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst;

        inst = default_vssd_instance(pfx_from_conn(conn), ns);
        if (inst == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to create %s_VSSD instance",
                           pfx_from_conn(conn));
                goto out;
        }

        CMSetProperty(inst, "VirtualSystemIdentifier",
                      (CMPIValue *)"KVM_guest", CMPI_chars);

        CMSetProperty(inst, "BootDevice",
                      (CMPIValue *)"hda", CMPI_chars);

        inst_list_add(list, inst);
 out:
        return s;
}

static CMPIStatus _lxc_vsmc_to_vssd(virConnectPtr conn,
                                    const char *ns,
                                    struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst;

        inst = default_vssd_instance(pfx_from_conn(conn), ns);
        if (inst == NULL) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to create %s_VSSD instance",
                           pfx_from_conn(conn));
                goto out;
        }

        CMSetProperty(inst, "InitPath",
                      (CMPIValue *)"/sbin/init", CMPI_chars);

        inst_list_add(list, inst);
 out:
        return s;
}

static CMPIStatus vsmc_to_vssd(const CMPIObjectPath *ref,
                               struct std_assoc_info *info,
                               struct inst_list *list)
{
        CMPIStatus s;
        virConnectPtr conn = NULL;
        const char *cn;
        const char *ns;

        cn = CLASSNAME(ref);
        ns = NAMESPACE(ref);

        conn = connect_by_classname(_BROKER, cn, &s);
        if (conn == NULL)
                goto out;

        if (STARTS_WITH(cn, "Xen"))
                s = _xen_vsmc_to_vssd(conn, ns, list);
        else if (STARTS_WITH(cn, "KVM"))
                s = _kvm_vsmc_to_vssd(conn, ns, list);
        else if (STARTS_WITH(cn, "LXC"))
                s = _lxc_vsmc_to_vssd(conn, ns, list);
        else
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Invalid reference");

 out:
        virConnectClose(conn);

        return s;
}

static CMPIInstance *sdc_rasd_inst(CMPIStatus *s,
                                   const CMPIObjectPath *ref,
                                   uint16_t resource_type)
{
        CMPIInstance *inst = NULL;
        const char *base = NULL;

        if (rasd_classname_from_type(resource_type, &base) != CMPI_RC_OK) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Resource type not known");
                goto out;
        }

        inst = get_typed_instance(_BROKER,
                                  CLASSNAME(ref),
                                  base,
                                  NAMESPACE(ref));

        if (inst == NULL) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to create instance of type %s",
                           base);
                goto out;
        }

        CMSetProperty(inst, "ResourceType", &resource_type, CMPI_uint16);

 out:
        return inst;
}

static CMPIStatus mem_template(const CMPIObjectPath *ref,
                               int template_type,
                               struct inst_list *list)
{
        uint64_t mem_size;
        const char *id;
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        switch (template_type) {
        case SDC_RASD_MIN:
                mem_size = 64 << 10;
                id = "Minimum";
                break;
        case SDC_RASD_MAX:
                mem_size = MAX_MEM;
                id = "Maximum";
                break;
        case SDC_RASD_INC:
                mem_size = 1 << 10;
                id = "Increment";
                break;
        case SDC_RASD_DEF:
                mem_size = 256 << 10;
                id = "Default";
                break;
        default:
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unsupported sdc_rasd type");
                goto out;
        }

        inst = sdc_rasd_inst(&s, ref, CIM_RES_TYPE_MEM); 
        if ((inst == NULL) || (s.rc != CMPI_RC_OK))
                goto out;

        CMSetProperty(inst, "InstanceID", (CMPIValue *)id, CMPI_chars);
        CMSetProperty(inst, "AllocationUnits", 
                      (CMPIValue *)"KiloBytes", CMPI_chars);
        CMSetProperty(inst, "VirtualQuantity", 
                      (CMPIValue *)&mem_size, CMPI_uint64);
        CMSetProperty(inst, "Limit", 
                      (CMPIValue *)&mem_size, CMPI_uint64);

        inst_list_add(list, inst);

 out:
        return s;
}

static bool get_max_procs(const CMPIObjectPath *ref,
                          uint64_t *num_procs,
                          CMPIStatus *s)
{
        bool ret = false;
        virConnectPtr conn;
        int max;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), s);
        if (conn == NULL) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Could not connect to hypervisor");
                goto out;
        }

        max = virConnectGetMaxVcpus(conn, NULL);
        if (max == -1) {
                CU_DEBUG("GetMaxVcpus not supported, assuming 1");
                *num_procs = 1;
        } else {
                *num_procs = max;
                CU_DEBUG("libvirt says %d max vcpus", *num_procs);
        }
        ret = true;

 out:
       virConnectClose(conn);
       return ret;
}

static CMPIStatus proc_template(const CMPIObjectPath *ref,
                                int template_type,
                                struct inst_list *list)
{
        bool ret;
        uint64_t num_procs;
        uint64_t limit;
        uint32_t weight;
        const char *id;
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        switch (template_type) {
        case SDC_RASD_MIN:
                num_procs = 0;
                limit = 1;
                weight = MIN_XEN_WEIGHT;
                id = "Minimum";
                break;
        case SDC_RASD_MAX:
                ret = get_max_procs(ref, &num_procs, &s);
                if (!ret)
                    goto out;
                limit = 0;
                weight = MAX_XEN_WEIGHT;
                id = "Maximum";
                break;
        case SDC_RASD_INC:
                num_procs = 1;
                limit = 50;
                weight = INC_XEN_WEIGHT;
                id = "Increment";
                break;
        case SDC_RASD_DEF:
                num_procs = 1;
                limit = 0;
                weight = DEFAULT_XEN_WEIGHT;
                id = "Default";
                break;
        default:
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unsupported sdc_rasd type");
                goto out;
        }

        inst = sdc_rasd_inst(&s, ref, CIM_RES_TYPE_PROC); 
        if ((inst == NULL) || (s.rc != CMPI_RC_OK))
                goto out;

        CMSetProperty(inst, "InstanceID", (CMPIValue *)id, CMPI_chars);
        CMSetProperty(inst, "AllocationUnits", 
                      (CMPIValue *)"Processors", CMPI_chars);
        CMSetProperty(inst, "VirtualQuantity", 
                      (CMPIValue *)&num_procs, CMPI_uint64);

        if (STARTS_WITH(CLASSNAME(ref), "Xen")) {
                CMSetProperty(inst, "Limit", (CMPIValue *)&limit, CMPI_uint64); 
                CMSetProperty(inst, "Weight", 
                              (CMPIValue *)&weight, CMPI_uint32); 
        }

        inst_list_add(list, inst);

 out:
        return s;
}

static uint64_t net_max_xen(const CMPIObjectPath *ref,
                            CMPIStatus *s)
{
        int rc;
        virConnectPtr conn;
        unsigned long version;
        uint64_t num_nics = -1;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), s);
        if (s->rc != CMPI_RC_OK) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not get connection");
                goto out;
        }

        rc = virConnectGetVersion(conn, &version);
        CU_DEBUG("libvir : version=%ld, rc=%d", version, rc);
        if (rc != 0) {
                virt_set_status(_BROKER, s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "Unable to get xen version");
                goto out;
        }

        if (version >= 3001000)
                num_nics = XEN_MAX_NICS;
        else
                num_nics = 4;
        
 out:
        virConnectClose(conn);
        return num_nics;
}

static bool get_max_nics(const CMPIObjectPath *ref,
                         uint64_t *num_nics,
                         CMPIStatus *s)
{
        char *prefix;
        bool ret = false;

        prefix = class_prefix_name(CLASSNAME(ref));
        if (prefix == NULL) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Could not get prefix from reference");
                goto out;
        }

        if (STREQC(prefix, "Xen")) {
                *num_nics = net_max_xen(ref, s);
        } else if (STREQC(prefix, "KVM")) {
                /* This appears to not require anything dynamic. */
                *num_nics = KVM_MAX_NICS;
        } else {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_NOT_SUPPORTED,
                           "Unsupported hypervisor: '%s'", prefix);
                goto out;
        }

        if (s->rc != CMPI_RC_OK) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Could not get max nic count");
                goto out;
        } else {
                ret = true;
        }

 out:
        free(prefix);

        return ret;
}

static CMPIStatus set_net_props(int type,
                                const CMPIObjectPath *ref,
                                const char *id,
                                uint64_t num_nics,
                                const char *model,
                                struct inst_list *list)
{
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        inst = sdc_rasd_inst(&s, ref, CIM_RES_TYPE_NET);
        if ((inst == NULL) || (s.rc != CMPI_RC_OK))
                goto out;

        CMSetProperty(inst, "InstanceID", (CMPIValue *)id, CMPI_chars);
        CMSetProperty(inst, "VirtualQuantity",
                      (CMPIValue *)&num_nics, CMPI_uint64);

        if (model != NULL)
                CMSetProperty(inst, "ResourceSubType", 
                             (CMPIValue *)model, CMPI_chars);

        inst_list_add(list, inst);

 out:
        return s;
}

static CMPIStatus net_template(const CMPIObjectPath *ref,
                               int template_type,
                               struct inst_list *list)
{
        bool ret;
        uint64_t num_nics;
        const char *id;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        switch (template_type) {
        case SDC_RASD_MIN:
                num_nics = 0;
                id = "Minimum";
                break;
        case SDC_RASD_MAX:
                ret = get_max_nics(ref, &num_nics, &s);
                if (!ret)
                    goto out;
                id = "Maximum";
                break;
        case SDC_RASD_INC:
                num_nics = 1;
                id = "Increment";
                break;
        case SDC_RASD_DEF:
                num_nics = 1;
                id = "Default";
                break;
        default:
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unsupported sdc_rasd type");
                goto out;
        }

        
        s = set_net_props(template_type, ref, id, num_nics, "e1000", list);
        if (s.rc != CMPI_RC_OK)
                goto out;
     
        s = set_net_props(template_type, ref, id, num_nics, NULL, list);

 out:
        return s;
}

static CMPIStatus set_disk_props(int type,
                                 const CMPIObjectPath *ref,
                                 const char *id,
                                 const char *disk_path,
                                 uint64_t disk_size,
                                 uint16_t emu_type,
                                 struct inst_list *list)
{
        const char *dev;
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        if (type == DOMAIN_LXC) {
                dev = "/lxc_mnt/tmp";
        }
        else {
                dev = "hda";
        }

        inst = sdc_rasd_inst(&s, ref, CIM_RES_TYPE_DISK);
        if ((inst == NULL) || (s.rc != CMPI_RC_OK))
                goto out;

        CMSetProperty(inst, "InstanceID", (CMPIValue *)id, CMPI_chars);
        CMSetProperty(inst, "AllocationQuantity",
                      (CMPIValue *)"MegaBytes", CMPI_chars);
        CMSetProperty(inst, "Address", (CMPIValue *)disk_path, CMPI_chars);

        if (type == DOMAIN_LXC)
                CMSetProperty(inst, "MountPoint", (CMPIValue *)dev, CMPI_chars);
        else {
                if (emu_type == 0)
                        CMSetProperty(inst, "VirtualQuantity",
                                      (CMPIValue *)&disk_size, CMPI_uint64);

                if (type == DOMAIN_XENPV) {
                        dev = "xvda";
                        CMSetProperty(inst, "Caption",
                                      (CMPIValue *)"PV disk", CMPI_chars);
                } else if (type == DOMAIN_XENFV) {
                        CMSetProperty(inst, "Caption",
                                      (CMPIValue *)"FV disk", CMPI_chars);
                }

                CMSetProperty(inst, "VirtualDevice",
                              (CMPIValue *)dev, CMPI_chars);
                CMSetProperty(inst, "EmulatedType",
                              (CMPIValue *)&emu_type, CMPI_uint16);
        }

        inst_list_add(list, inst);

 out:
        return s;
}

static CMPIStatus cdrom_template(const CMPIObjectPath *ref,
                                  int template_type,
                                  struct inst_list *list)
{
        char *pfx = NULL;
        const char *id;
        const char *vol_path = "/dev/null";
        uint64_t vol_size = 0;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        uint16_t emu_type = 1;

        switch(template_type) {
        case SDC_RASD_MIN:
                id = "Minimum CDROM";
                break;
        case SDC_RASD_MAX:
                id = "Maximum CDROM";
                break;
        case SDC_RASD_INC:
                id = "Increment CDROM";
                break;
        case SDC_RASD_DEF:
                id = "Default CDROM";
                break;
        default:
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unsupported sdc_rasd type");
                goto out;
        }

        pfx = class_prefix_name(CLASSNAME(ref));
        if (STREQ(pfx, "Xen")) {
                int xen_type[2] = {DOMAIN_XENFV, DOMAIN_XENPV};
                int i = 0;
 
                for (; i < 2; i++) {
                        s = set_disk_props(xen_type[i],
                                           ref, 
                                           id,
                                           vol_path, 
                                           vol_size, 
                                           emu_type, 
                                           list); 
                }
        } else if (STREQ(pfx, "KVM")) {
                s = set_disk_props(DOMAIN_KVM,
                                   ref, 
                                   id,
                                   vol_path, 
                                   vol_size, 
                                   emu_type, 
                                   list); 

        } else if (!STREQ(pfx, "LXC")){
                cu_statusf(_BROKER, &s, 
                            CMPI_RC_ERR_FAILED,
                           "Unsupported virtualization type");
       }

 out:
        free(pfx);

        return s;
}

static int get_disk_freespace(const CMPIObjectPath *ref,
                              CMPIStatus *s,
                              uint64_t *free_space)
{
        bool ret = false;
        const char *inst_id;
        CMPIrc prop_ret;
        virConnectPtr conn = NULL;
        CMPIInstance *pool_inst;

        if (cu_get_str_path(ref, "InstanceID", &inst_id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Could not get InstanceID");
                goto out;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), s);
        if (s->rc != CMPI_RC_OK) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Could not get connection");
                goto out;
        }

        /* Getting the relevant resource pool directly finds the free space
 *            for us.  It is in the Capacity field. */
        *s = get_pool_by_name(_BROKER, ref, inst_id, &pool_inst);
        if (s->rc != CMPI_RC_OK)
                goto out;

        prop_ret = cu_get_u64_prop(pool_inst, "Capacity", free_space);
        if (prop_ret != CMPI_RC_OK) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Could not get capacity from instance");
                goto out;
        }

        CU_DEBUG("Got capacity from pool_inst: %lld", *free_space);
        ret = true;

 out:
        virConnectClose(conn);
        return ret;
}

static CMPIStatus default_disk_template(const CMPIObjectPath *ref,
                                        int template_type,
                                        struct inst_list *list)
{
        uint64_t disk_size = 0;
        int emu_type = 0;
        char *pfx = NULL;
        const char *disk_path = "/dev/null";
        const char *id;
        int type = 0;
        bool ret;

        CMPIStatus s = {CMPI_RC_OK, NULL};

        pfx = class_prefix_name(CLASSNAME(ref));

        switch(template_type) {
        case SDC_RASD_MIN:
                if (!STREQ(pfx, "LXC")) {
                        ret = get_disk_freespace(ref, &s, &disk_size);
                        if (!ret)
                                goto out;
                        if (SDC_DISK_MIN < disk_size)
                                disk_size = SDC_DISK_MIN;
                }
                id = "Minimum";
                break;
        case SDC_RASD_MAX:
                if (!STREQ(pfx, "LXC")) {
                        ret = get_disk_freespace(ref, &s, &disk_size);
                        if (!ret)
                                goto out;
                }
                id = "Maximum";
                break;
        case SDC_RASD_INC:
                disk_size = SDC_DISK_INC;
                id = "Increment";
                break;
        case SDC_RASD_DEF:
                disk_size = SDC_DISK_DEF;
                id = "Default";
                break;
        default:
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unsupported sdc_rasd type");
                goto out;
        }

        if (STREQ(pfx, "Xen")) {
                int xen_type[2] = {DOMAIN_XENFV, DOMAIN_XENPV};
                int i = 0;

                for (; i < 2; i++) {
                        s = set_disk_props(xen_type[i],
                                           ref,
                                           id,
                                           disk_path,
                                           disk_size,
                                           emu_type,
                                           list);
                        if (s.rc != CMPI_RC_OK)
                                goto out;
                }
        } else {
                if (STREQ(pfx, "KVM")) {
                        type = DOMAIN_KVM;
                } else if (STREQ(pfx, "LXC")) {
                        type = DOMAIN_LXC;
                        disk_path = "/tmp";
                        disk_size = 0;
                }

                s = set_disk_props(type,
                                   ref,
                                   id,
                                   disk_path,
                                   disk_size,
                                   emu_type,
                                   list);
        }

 out:
        free(pfx);
        return s;
}

#if VIR_USE_LIBVIRT_STORAGE
static CMPIStatus volume_template(const CMPIObjectPath *ref,
                                  int template_type,
                                  virStorageVolPtr volume_ptr,
                                  struct inst_list *list)
{
        char *pfx = NULL;
        const char *id;
        char *vol_path = NULL;
        uint64_t vol_size;
        virStorageVolInfo vol_info;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int ret;
        uint16_t emu_type = 0;

        ret = virStorageVolGetInfo(volume_ptr, &vol_info);
        if (ret == -1) {
                CU_DEBUG("Unable to get volume information");
                goto out;
        }

        switch(template_type) {
        case SDC_RASD_MIN:
                if (SDC_DISK_MIN > (uint64_t)vol_info.capacity)
                        vol_size = (uint64_t)vol_info.capacity;
                else
                        vol_size = SDC_DISK_MIN;
                id = "Minimum";
                break;
        case SDC_RASD_MAX:
                vol_size = (uint64_t)vol_info.capacity;
                id = "Maximum";
                break;
        case SDC_RASD_INC:
                vol_size = SDC_DISK_INC;
                id = "Increment";
                break;
        case SDC_RASD_DEF:
                vol_size = (uint64_t)vol_info.allocation;
                id = "Default";
                break;
        default:
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unsupported sdc_rasd type");
                goto out;
        }

        vol_path = virStorageVolGetPath(volume_ptr);
        if (vol_path == NULL) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                virStorageVolGetConnect(volume_ptr),
                                "Unable to get volume path");
                goto out;
        }

        pfx = class_prefix_name(CLASSNAME(ref));

        if (STREQ(pfx, "Xen")) {
                int xen_type[2] = {DOMAIN_XENFV, DOMAIN_XENPV};
                int i = 0;

                for (; i < 2; i++) {
                        s = set_disk_props(xen_type[i],
                                           ref,
                                           id,
                                           vol_path,
                                           vol_size,
                                           emu_type,
                                           list);
                }
        } else if (STREQ(pfx, "KVM")) {
                s = set_disk_props(DOMAIN_KVM,
                                   ref,
                                   id,
                                   vol_path,
                                   vol_size,
                                   emu_type,
                                   list);
        } else {
                cu_statusf(_BROKER, &s,
                            CMPI_RC_ERR_FAILED,
                           "Unsupported virtualization type");
       }

 out:
        free(pfx);
        free(vol_path);

        return s;
}

static CMPIStatus disk_template(const CMPIObjectPath *ref,
                                int template_type,
                                struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;
        virStoragePoolPtr poolptr = NULL;
        virStorageVolPtr volptr = NULL;
        const char *instid = NULL;
        char *host = NULL;
        const char *poolname = NULL;
        char **volnames = NULL;
        int numvols = 0;
        int numvolsret = 0;
        int i;
        char *pfx = NULL;

        pfx = class_prefix_name(CLASSNAME(ref));
        if (STREQ(pfx, "LXC")) {
                s = default_disk_template(ref, template_type, list);
                goto out;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);

        if (cu_get_str_path(ref, "InstanceID", &instid) != CMPI_RC_OK) {
               cu_statusf(_BROKER, &s,
                          CMPI_RC_ERR_FAILED,
                          "Unable to get InstanceID for disk device");
               goto out;
        }

        if (parse_fq_devid(instid, &host, (char **)&poolname) != 1) {
                cu_statusf(_BROKER, &s, 
                           CMPI_RC_ERR_FAILED,
                           "Unable to get pool device id");
                goto out;
        }

        if ((poolptr = virStoragePoolLookupByName(conn, poolname)) == NULL) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_NOT_FOUND,
                                conn,
                                "Storage pool `%s' not found",
                                poolname);
                goto out;
        }

        if ((numvols = virStoragePoolNumOfVolumes(poolptr)) == -1) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "Unable to get the number of volumes \
                                of storage pool `%s'",
                                poolname);
                goto out;
        }

        volnames = (char **)malloc(sizeof(char *) * numvols);
        if (volnames == NULL) {
               cu_statusf(_BROKER, &s,
                          CMPI_RC_ERR_FAILED,
                          "Could not allocate space for list of volumes \
                          of storage pool `%s'",
                          poolname);
               goto out;
        }

        numvolsret = virStoragePoolListVolumes(poolptr, volnames, numvols);

        if (numvolsret == -1) {
                virt_set_status(_BROKER, &s,
                                CMPI_RC_ERR_FAILED,
                                conn,
                                "Unable to get a pointer to volumes \
                                of storage pool `%s'",
                                poolname);
                goto out;
        }

        for (i = 0; i < numvolsret; i++) {
                volptr = virStorageVolLookupByName(poolptr, volnames[i]);
                if (volptr == NULL) {
                        virt_set_status(_BROKER, &s,
                                        CMPI_RC_ERR_NOT_FOUND,
                                        conn,
                                        "Storage Volume `%s' not found",
                                        volnames[i]);
                        goto out;
                }         
                
                s = volume_template(ref, template_type, volptr, list);

                virStorageVolFree(volptr);

                if (s.rc != CMPI_RC_OK)
                        goto out;            
        }

        s = cdrom_template(ref, template_type, list);

 out:
        free(pfx);
        free(volnames);
        free(host);
        virStoragePoolFree(poolptr);
        virConnectClose(conn);

        return s;
}
#else
static CMPIStatus disk_template(const CMPIObjectPath *ref,
                                int template_type,
                                struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        char *pfx = NULL;

        s = default_disk_template(ref, template_type, list);
        if (s.rc != CMPI_RC_OK)
                goto out;

        pfx = class_prefix_name(CLASSNAME(ref));
        if (STREQ(pfx, "LXC"))
                goto out;

        s = cdrom_template(ref, template_type, list);

 out:
        free(pfx);

        return s;
}
#endif

static CMPIStatus graphics_template(const CMPIObjectPath *ref,
                                    int template_type,
                                    struct inst_list *list)
{
        const char *id;
        const char *addr;
        CMPIInstance *inst;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        switch(template_type) {
        case SDC_RASD_MIN:
                id = "Minimum";
                break;
        case SDC_RASD_MAX:
                id = "Maximum";
                break;
        case SDC_RASD_INC:
                id = "Increment";
                break;
        case SDC_RASD_DEF:
                id = "Default";
                break;
        default:
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unsupported sdc_rasd type");
                goto out;
        }

        inst = sdc_rasd_inst(&s, ref, CIM_RES_TYPE_GRAPHICS);

        CMSetProperty(inst, "InstanceID", (CMPIValue *)id, CMPI_chars);

        addr = "127.0.0.1:-1";
        CMSetProperty(inst, "Address", (CMPIValue *)addr, CMPI_chars);

        CMSetProperty(inst, "KeyMap", (CMPIValue *)"en-us", CMPI_chars);
        CMSetProperty(inst, "ResourceSubType", (CMPIValue *)"vnc", CMPI_chars);

        inst_list_add(list, inst);

 out:
        return s;
}

static CMPIStatus set_input_props(const CMPIObjectPath *ref,
                                  const char *id,
                                  const char *type,
                                  const char *bus,
                                  const char *caption,
                                  struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst;
        char *cap;

        if (get_input_dev_caption(type, bus, &cap) != 1) {
                free(cap);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "Unable to build input caption");
                return s;
        }

        if (caption != NULL) {
                if (asprintf(&cap, "%s %s", caption, cap) == -1) {
                        cu_statusf(_BROKER, &s,
                                   CMPI_RC_ERR_NOT_FOUND,
                                   "Unable to build input caption");
                        goto out;
                }
        }

        inst = sdc_rasd_inst(&s, ref, CIM_RES_TYPE_INPUT);
        if ((inst == NULL) || (s.rc != CMPI_RC_OK))
                goto out;

        CMSetProperty(inst, "InstanceID", (CMPIValue *)id, CMPI_chars);

        CMSetProperty(inst, "ResourceSubType", (CMPIValue *)type, CMPI_chars);

        CMSetProperty(inst, "BusType", (CMPIValue *)bus, CMPI_chars);

        CMSetProperty(inst, "Caption", (CMPIValue *)cap, CMPI_chars);

        inst_list_add(list, inst);

 out:
        free(cap);

        return s;
}

static CMPIStatus input_template(const CMPIObjectPath *ref,
                                 int template_type,
                                 struct inst_list *list)
{
        const char *id;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        const char *xen_inputs[4][3] = { {"mouse", "ps2", "FV"},
                                         {"mouse", "usb", "FV"},
                                         {"mouse", "xen", "PV"},
                                         {NULL, NULL, NULL}
                                       };
        const char *kvm_inputs[4][3] = { {"mouse", "ps2", NULL},
                                         {"mouse", "usb", NULL},
                                         {"tablet", "usb", NULL},
                                         {NULL, NULL, NULL}
                                       };
        const char *lxc_inputs[4][3] = { {"mouse", "usb", NULL},
                                         {NULL, NULL, NULL}
                                       };
        char *pfx = NULL;
        int i;

        switch(template_type) {
        case SDC_RASD_MIN:
                id = "Minimum";
                break;
        case SDC_RASD_MAX:
                id = "Maximum";
                break;
        case SDC_RASD_INC:
                id = "Increment";
                break;
        case SDC_RASD_DEF:
                id = "Default";
                break;
        default:
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unsupported sdc_rasd type");
                goto out;
        }

        pfx = class_prefix_name(CLASSNAME(ref));

        if (STREQ(pfx, "Xen")) {
                for(i = 0; xen_inputs[i][0] != NULL; i++) {
                        s = set_input_props(ref, 
                                            id, 
                                            xen_inputs[i][0], 
                                            xen_inputs[i][1], 
                                            xen_inputs[i][2], 
                                            list);
                        if (s.rc != CMPI_RC_OK)
                                goto out;
                }
        } else if (STREQ(pfx, "KVM")) {
                for(i = 0; kvm_inputs[i][0] != NULL; i++) {
                        s = set_input_props(ref, 
                                            id, 
                                            kvm_inputs[i][0], 
                                            kvm_inputs[i][1], 
                                            kvm_inputs[i][2], 
                                            list);
                        if (s.rc != CMPI_RC_OK)
                                goto out;
                }
        } else if (STREQ(pfx, "LXC")) {
                for(i = 0; lxc_inputs[i][0] != NULL; i++) {
                        s = set_input_props(ref, 
                                            id, 
                                            lxc_inputs[i][0], 
                                            lxc_inputs[i][1], 
                                            lxc_inputs[i][2], 
                                            list);
                        if (s.rc != CMPI_RC_OK)
                                goto out;
                }
        } else {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unsupported guest type");
                goto out;
        }

 out:
        free(pfx);
        return s;
}

static CMPIStatus sdc_rasds_for_type(const CMPIObjectPath *ref,
                                     struct inst_list *list,
                                     uint16_t type)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int i;

        for (i = SDC_RASD_MIN; i <= SDC_RASD_INC; i++) {
                if (type == CIM_RES_TYPE_MEM)
                        s = mem_template(ref, i, list);
                else if (type == CIM_RES_TYPE_PROC)
                        s = proc_template(ref, i, list);
                else if (type == CIM_RES_TYPE_NET)
                        s = net_template(ref, i, list);
                else if (type == CIM_RES_TYPE_DISK)
                        s = disk_template(ref, i, list);
                else if (type == CIM_RES_TYPE_GRAPHICS)
                        s = graphics_template(ref, i, list);
                else if (type == CIM_RES_TYPE_INPUT)
                        s = input_template(ref, i, list);
                else {
                        cu_statusf(_BROKER, &s,
                                   CMPI_RC_ERR_FAILED,
                                   "Unsupported resource type");
                }

                if (s.rc != CMPI_RC_OK) {
                        CU_DEBUG("Problem getting inst list");
                        goto out;
                }
        }
                
 out:
        return s;
}

static CMPIStatus alloc_cap_to_rasd(const CMPIObjectPath *ref,
                                    struct std_assoc_info *info,
                                    struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK};
        CMPIInstance *inst;
        uint16_t type;
        const char *id = NULL;
        int i;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        if (cu_get_str_path(ref, "InstanceID", &id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing InstanceID");
                goto out;
        }

        s = get_alloc_cap_by_id(_BROKER, ref, id, &inst);
        if ((inst == NULL) || (s.rc != CMPI_RC_OK))
                goto out;
 
        type = res_type_from_pool_id(id);

        if (type == CIM_RES_TYPE_UNKNOWN) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to determine resource type");
                goto out;
        }

        s = sdc_rasds_for_type(ref, list, type);

        for (i = 0; i < list->cur; i++)
                CMSetProperty(list->list[i], "PoolID",
                              (CMPIValue *)id, CMPI_chars);

 out:
        return s;
}

static CMPIStatus rasd_to_alloc_cap(const CMPIObjectPath *ref,
                                    struct std_assoc_info *info,
                                    struct inst_list *list)
{
        return (CMPIStatus){CMPI_RC_OK, NULL};
}

static CMPIStatus migrate_cap_to_vsmsd(const CMPIObjectPath *ref,
                                       struct std_assoc_info *info,
                                       struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK};
        CMPIInstance *inst;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        s = get_migration_caps(ref, &inst, _BROKER, true);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = get_migration_sd(ref, &inst, _BROKER, false);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, inst);

 out:
        return s;
}

static CMPIStatus vsmsd_to_migrate_cap(const CMPIObjectPath *ref,
                                       struct std_assoc_info *info,
                                       struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK};
        CMPIInstance *inst;

        if (!match_hypervisor_prefix(ref, info))
                return s;

        s = get_migration_sd(ref, &inst, _BROKER, true);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = get_migration_caps(ref, &inst, _BROKER, false);
        if (s.rc == CMPI_RC_OK)
                inst_list_add(list, inst);

 out:
        return s;
}

static CMPIInstance *make_ref_valuerole(const CMPIObjectPath *source_ref,
                                        const CMPIInstance *target_inst,
                                        struct std_assoc_info *info,
                                        struct std_assoc *assoc)
{
        CMPIInstance *ref_inst = NULL;
        uint16_t valuerole = SDC_ROLE_SUPPORTED;
        uint16_t valuerange;
        uint16_t ppolicy = SDC_POLICY_INDEPENDENT;
        const char *iid = NULL;

        ref_inst = make_reference(_BROKER,
                                  source_ref,
                                  target_inst,
                                  info,
                                  assoc);

        if (cu_get_str_prop(target_inst, "InstanceID", &iid) != CMPI_RC_OK) {
                CU_DEBUG("Target instance does not have an InstanceID");
                goto out;
        }

        if (strstr("Default", iid) != NULL)
                valuerange = SDC_RANGE_POINT;
        else if (strstr("Increment", iid) != NULL)
                valuerange = SDC_RANGE_INC;
        else if (strstr("Maximum", iid) != NULL)
                valuerange = SDC_RANGE_MAX;
        else if (strstr("Minimum", iid) != NULL)
                valuerange = SDC_RANGE_MIN;
        else
                CU_DEBUG("Unknown default RASD type: `%s'", iid);

        if (valuerange == SDC_RANGE_POINT)
                valuerole = SDC_ROLE_DEFAULT;

        CMSetProperty(ref_inst, "ValueRole",
                      (CMPIValue *)&valuerole, CMPI_uint16);
        CMSetProperty(ref_inst, "ValueRange",
                      (CMPIValue *)&valuerange, CMPI_uint16);
        CMSetProperty(ref_inst, "PropertyPolicy",
                      (CMPIValue *)&ppolicy, CMPI_uint16);
 out:
        return ref_inst;
}

static CMPIInstance *make_ref_msd(const CMPIObjectPath *source_ref,
                                  const CMPIInstance *target_inst,
                                  struct std_assoc_info *info,
                                  struct std_assoc *assoc)
{
        CMPIInstance *ref_inst = NULL;
        uint16_t ppolicy = SDC_POLICY_INDEPENDENT;
        uint16_t valuerole = SDC_ROLE_DEFAULT;
        uint16_t valuerange = SDC_RANGE_POINT;

        ref_inst = make_reference(_BROKER,
                                  source_ref,
                                  target_inst,
                                  info,
                                  assoc);

        CMSetProperty(ref_inst, "ValueRole",
                      (CMPIValue *)&valuerole, CMPI_uint16);
        CMSetProperty(ref_inst, "ValueRange",
                      (CMPIValue *)&valuerange, CMPI_uint16);
        CMSetProperty(ref_inst, "PropertyPolicy",
                      (CMPIValue *)&ppolicy, CMPI_uint16);

        return ref_inst;
}

LIBVIRT_CIM_DEFAULT_MAKEREF()

static char* group_component[] = {
        "Xen_AllocationCapabilities",
        "KVM_AllocationCapabilities",
        "LXC_AllocationCapabilities",
        NULL
};

static char* part_component[] = {
        "Xen_DiskResourceAllocationSettingData",
        "Xen_MemResourceAllocationSettingData",
        "Xen_NetResourceAllocationSettingData",
        "Xen_ProcResourceAllocationSettingData",
        "Xen_GraphicsResourceAllocationSettingData",
        "Xen_InputResourceAllocationSettingData",
        "KVM_DiskResourceAllocationSettingData",
        "KVM_MemResourceAllocationSettingData",
        "KVM_NetResourceAllocationSettingData",
        "KVM_ProcResourceAllocationSettingData",
        "KVM_GraphicsResourceAllocationSettingData",
        "KVM_InputResourceAllocationSettingData",
        "LXC_DiskResourceAllocationSettingData",
        "LXC_MemResourceAllocationSettingData",
        "LXC_NetResourceAllocationSettingData",
        "LXC_ProcResourceAllocationSettingData",
        "LXC_GraphicsResourceAllocationSettingData",
        "LXC_InputResourceAllocationSettingData",
        NULL
};

static char* assoc_classname[] = {
        "Xen_SettingsDefineCapabilities",
        "KVM_SettingsDefineCapabilities",
        "LXC_SettingsDefineCapabilities",
        NULL
};

static struct std_assoc _alloc_cap_to_rasd = {
        .source_class = (char**)&group_component,
        .source_prop = "GroupComponent",

        .target_class = (char**)&part_component,
        .target_prop = "PartComponent",

        .assoc_class = (char**)&assoc_classname,

        .handler = alloc_cap_to_rasd,
        .make_ref = make_ref_valuerole
};

static struct std_assoc _rasd_to_alloc_cap = {
        .source_class = (char**)&part_component,
        .source_prop = "PartComponent",

        .target_class = (char**)&group_component,
        .target_prop = "GroupComponent",

        .assoc_class = (char**)&assoc_classname,

        .handler = rasd_to_alloc_cap,
        .make_ref = make_ref
};

static char* migrate_cap[] = {
        "Xen_VirtualSystemMigrationCapabilities",
        "KVM_VirtualSystemMigrationCapabilities",
        "LXC_VirtualSystemMigrationCapabilities",
        NULL
};

static char* migrate_sd[] = {
        "Xen_VirtualSystemMigrationSettingData",
        "KVM_VirtualSystemMigrationSettingData",
        "LXC_VirtualSystemMigrationSettingData",
        NULL
};

static struct std_assoc _migrate_cap_to_vsmsd = {
        .source_class = (char**)&migrate_cap,
        .source_prop = "GroupComponent",

        .target_class = (char**)&migrate_sd,
        .target_prop = "PartComponent",

        .assoc_class = (char**)&assoc_classname,

        .handler = migrate_cap_to_vsmsd,
        .make_ref = make_ref_msd
};

static struct std_assoc _vsmsd_to_migrate_cap = {
        .source_class = (char**)&migrate_sd,
        .source_prop = "PartComponent",

        .target_class = (char**)&migrate_cap,
        .target_prop = "GroupComponent",

        .assoc_class = (char**)&assoc_classname,

        .handler = vsmsd_to_migrate_cap,
        .make_ref = make_ref
};

static char *vsmc[] = {
        "Xen_VirtualSystemManagementCapabilities",
        "KVM_VirtualSystemManagementCapabilities",
        "LXC_VirtualSystemManagementCapabilities",
        NULL
};

static char *vssd[] = {
        "Xen_VirtualSystemSettingData",
        "KVM_VirtualSystemSettingData",
        "LXC_VirtualSystemSettingData",
        NULL
};

static struct std_assoc _vsmc_to_vssd = {
        .source_class = (char**)&vsmc,
        .source_prop = "GroupComponent",

        .target_class = (char**)&vssd,
        .target_prop = "PartComponent",

        .assoc_class = (char**)&assoc_classname,

        .handler = vsmc_to_vssd,
        .make_ref = make_ref
};

static struct std_assoc *assoc_handlers[] = {
        &_alloc_cap_to_rasd,
        &_rasd_to_alloc_cap,
        &_migrate_cap_to_vsmsd,
        &_vsmsd_to_migrate_cap,
        &_vsmc_to_vssd,
        NULL
};

STDA_AssocMIStub(,
                 Virt_SettingsDefineCapabilities,
                 _BROKER, 
                 libvirt_cim_init(), 
                 assoc_handlers);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
