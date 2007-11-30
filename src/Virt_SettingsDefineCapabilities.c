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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/vfs.h>
#include <errno.h>

#include <libvirt.h>

#include "config.h"

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include "libcmpiutil.h"
#include "misc_util.h"
#include "std_association.h"
#include "device_parsing.h"
#include "svpc_types.h"

#include "Virt_SettingsDefineCapabilities.h"
#include "Virt_DevicePool.h"

const static CMPIBroker *_BROKER;

/* These are used in more than one place so they are defined here. */
#define SDC_DISK_MIN 2000
#define SDC_DISK_DEF 5000
#define SDC_DISK_INC 250

static bool rasd_prop_copy_value(struct sdc_rasd_prop src, 
                                 struct sdc_rasd_prop *dest)
{
        bool rc = true;

        CU_DEBUG("Copying '%s'.", src.field);
        if (src.type & CMPI_string) {
                dest->value = (CMPIValue *)strdup((char *)src.value);
        } else if (src.type & CMPI_INTEGER) {
                dest->value = malloc(sizeof(CMPIValue));
                memcpy(dest->value, src.value, sizeof(CMPIValue));
        } else {
                rc = false;
        }

        return rc;
}

static bool dup_rasd_prop_list(struct sdc_rasd_prop *src, 
                               struct sdc_rasd_prop **dest)
{
        int count, i;
        bool ret;
        *dest = NULL;
        
        for (i = 0, count = 1; src[i].field != NULL; i++, count++) {
                *dest = realloc(*dest, count * sizeof(struct sdc_rasd_prop));
                (*dest)[i].field = strdup(src[i].field);
                ret = rasd_prop_copy_value(src[i], &(*dest)[i]);
                (*dest)[i].type = src[i].type;
        }
        
        /* Make sure to terminate the list. */
        *dest = realloc(*dest, count * sizeof(struct sdc_rasd_prop));
        (*dest)[i] = (struct sdc_rasd_prop)PROP_END;

        return true;
}

static bool free_rasd_prop_list(struct sdc_rasd_prop *prop_list)
{
        int i;

        for (i = 0; prop_list[i].field != NULL; i++) {
                free(prop_list[i].field);
                free(prop_list[i].value);
        }

        free (prop_list);
        return true;
}

static struct sdc_rasd_prop *mem_max(const CMPIObjectPath *ref,
                                     CMPIStatus *s)
{
        bool ret;
        struct sdc_rasd_prop *rasd = NULL;
        uint64_t max_vq = MAX_MEM;

        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Maximum", CMPI_chars},
                {"AllocationUnits", (CMPIValue *)"MegaBytes", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&max_vq, CMPI_uint64},
                PROP_END
        };
        
        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

        return rasd;
}

static struct sdc_rasd_prop *mem_min(const CMPIObjectPath *ref,
                                     CMPIStatus *s)
{
        bool ret;
        struct sdc_rasd_prop *rasd = NULL;
        uint64_t min_vq = 64;

        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Minimum", CMPI_chars},
                {"AllocationUnits", (CMPIValue *)"MegaBytes", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&min_vq, CMPI_uint64},
                PROP_END
        };

        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

        return rasd;
}

static struct sdc_rasd_prop *mem_def(const CMPIObjectPath *ref,
                                     CMPIStatus *s)
{
        bool ret;
        struct sdc_rasd_prop *rasd = NULL;
        uint64_t def_vq = 256;

        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Default", CMPI_chars},
                {"AllocationUnits", (CMPIValue *)"MegaBytes", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&def_vq, CMPI_uint64},
                PROP_END
        };

        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

        return rasd;
}

static struct sdc_rasd_prop *mem_inc(const CMPIObjectPath *ref,
                                     CMPIStatus *s)
{
        bool ret;
        struct sdc_rasd_prop *rasd = NULL;
        uint64_t inc_vq = 1;

        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Increment", CMPI_chars},
                {"AllocationUnits", (CMPIValue *)"MegaBytes", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&inc_vq, CMPI_uint64},
                PROP_END
        };

        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

        return rasd;
}

static struct sdc_rasd_prop *proc_min(const CMPIObjectPath *ref,
                                      CMPIStatus *s)
{
        bool ret;
        uint16_t num_procs = 1;
        struct sdc_rasd_prop *rasd = NULL;
 
        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Minimum", CMPI_chars},
                {"AllocationUnits", (CMPIValue *)"Processors", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&num_procs, CMPI_uint16},
                PROP_END
        };
 
        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

        return rasd;
}

static struct sdc_rasd_prop *proc_max(const CMPIObjectPath *ref,
                                      CMPIStatus *s)
{
        bool ret;
        virConnectPtr conn;
        uint16_t num_procs = 0;
        struct sdc_rasd_prop *rasd = NULL;
        
        CU_DEBUG("In proc_max()");

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), s);
        if (conn == NULL) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not connect to hypervisor.");
                goto out;
        }

        num_procs = virConnectGetMaxVcpus(conn, NULL);
        CU_DEBUG("libvirt says %d max vcpus", num_procs);

        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Maximum", CMPI_chars},
                {"AllocationUnits", (CMPIValue *)"Processors", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&num_procs, CMPI_uint16},
                PROP_END
        };
 
        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

 out:
        return rasd;
}

static struct sdc_rasd_prop *proc_def(const CMPIObjectPath *ref,
                                      CMPIStatus *s)
{
        bool ret;
        uint16_t num_procs = 1;
        struct sdc_rasd_prop *rasd = NULL;
 
        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Default", CMPI_chars},
                {"AllocationUnits", (CMPIValue *)"Processors", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&num_procs, CMPI_uint16},
                PROP_END
        };
 
        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

        return rasd;
}

static struct sdc_rasd_prop *proc_inc(const CMPIObjectPath *ref,
                                      CMPIStatus *s)
{
        bool ret;
        uint16_t num_procs = 1;
        struct sdc_rasd_prop *rasd = NULL;
 
        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Increment", CMPI_chars},
                {"AllocationUnits", (CMPIValue *)"Processors", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&num_procs, CMPI_uint16},
                PROP_END
        };
 
        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

        return rasd;
}

static struct sdc_rasd_prop *net_min(const CMPIObjectPath *ref,
                                     CMPIStatus *s)
{
        bool ret;
        uint16_t num_nics = 0;
        struct sdc_rasd_prop *rasd = NULL;
 
        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Minimum", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&num_nics, CMPI_uint16},
                PROP_END
        };
 
        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

        return rasd;
}

static uint16_t net_max_kvm(const CMPIObjectPath *ref,
                            CMPIStatus *s)
{
        /* This appears to not require anything dynamic. */
        return KVM_MAX_NICS;
}
static uint16_t net_max_xen(const CMPIObjectPath *ref,
                            CMPIStatus *s)
{
        int rc;
        virConnectPtr conn;
        unsigned long version;
        uint16_t num_nics = -1;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), s);
        if (s->rc != CMPI_RC_OK) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not get connection.");
                goto out;
        }

        rc = virConnectGetVersion(conn, &version);
        CU_DEBUG("libvir : version=%ld, rc=%d", version, rc);
        if (rc != 0) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not get xen version.");
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
 
static struct sdc_rasd_prop *net_max(const CMPIObjectPath *ref,
                                     CMPIStatus *s)
{
        bool ret;
        char *prefix;
        uint16_t num_nics;
        struct sdc_rasd_prop *rasd = NULL;

        prefix = class_prefix_name(CLASSNAME(ref));
        if (prefix == NULL) {
                cu_statusf(_BROKER, s,
                           CMPI_RC_ERR_FAILED,
                           "Could not get prefix from reference.");
                goto out;
        }

        if (STREQC(prefix, "Xen")) {
                num_nics = net_max_xen(ref, s);
        } else if (STREQC(prefix, "KVM")) {
                num_nics = net_max_kvm(ref, s);
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
        }
 
        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Maximum", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&num_nics, CMPI_uint16},
                PROP_END
        };
 
        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }
 out:
        free(prefix);
        return rasd;
}

static struct sdc_rasd_prop *net_def(const CMPIObjectPath *ref,
                                     CMPIStatus *s)
{
        bool ret;
        uint16_t num_nics = 1;
        struct sdc_rasd_prop *rasd = NULL;
 
        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Default", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&num_nics, CMPI_uint16},
                PROP_END
        };
 
        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

        return rasd;
}
 
static struct sdc_rasd_prop *net_inc(const CMPIObjectPath *ref,
                                     CMPIStatus *s)
{
        bool ret;
        uint16_t num_nics = 1;
        struct sdc_rasd_prop *rasd = NULL;
 
        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Increment", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&num_nics, CMPI_uint16},
                PROP_END
        };
 
        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

        return rasd;
}

static struct sdc_rasd_prop *disk_min(const CMPIObjectPath *ref,
                                      CMPIStatus *s)
{
        bool ret;
        uint16_t disk_size = SDC_DISK_MIN;
        struct sdc_rasd_prop *rasd = NULL;

        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Minimum", CMPI_chars},
                {"AllocationQuantity", (CMPIValue *)"MegaBytes", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&disk_size, CMPI_uint16},
                PROP_END
        };

        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

        return rasd;
}

static struct sdc_rasd_prop *disk_max(const CMPIObjectPath *ref,
                                      CMPIStatus *s)
{
        bool ret;
        const char *inst_id;
        CMPIrc prop_ret;
        uint16_t free_space;
        uint64_t free_64;
        virConnectPtr conn;
        CMPIInstance *pool_inst;
        struct sdc_rasd_prop *rasd = NULL;
        
        if (cu_get_str_path(ref, "InstanceID", &inst_id) != CMPI_RC_OK) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not get InstanceID.");
                goto out;
        }

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), s);
        if (s->rc != CMPI_RC_OK) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not get connection.");
                goto out;
        }

        /* Getting the relevant resource pool directly finds the free space 
           for us.  It is in the Capacity field. */
        pool_inst = get_pool_by_id(_BROKER, conn, inst_id, NAMESPACE(ref));
        if (pool_inst == NULL) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not get pool instance.");
                goto out;
        }

        prop_ret = cu_get_u64_prop(pool_inst, "Capacity", &free_64);
        if (prop_ret != CMPI_RC_OK) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not get capacity from instance.");
                goto out;
        }
        CU_DEBUG("Got capacity from pool_inst: %lld", free_64);
        
        free_space = (uint16_t)free_64;
        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Maximum", CMPI_chars},
                {"AllocationQuantity", (CMPIValue *)"MegaBytes", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&free_space, CMPI_uint16},
                PROP_END
        };

        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

 out:
        return rasd;
}

static struct sdc_rasd_prop *disk_def(const CMPIObjectPath *ref,
                                      CMPIStatus *s)
{
        bool ret;
        uint16_t disk_size = SDC_DISK_DEF;
        struct sdc_rasd_prop *rasd = NULL;

        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Default", CMPI_chars},
                {"AllocationQuantity", (CMPIValue *)"MegaBytes", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&disk_size, CMPI_uint16},
                PROP_END
        };

        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

        return rasd;
}

static struct sdc_rasd_prop *disk_inc(const CMPIObjectPath *ref,
                                      CMPIStatus *s)
{
        bool ret;
        uint16_t disk_size = SDC_DISK_INC;
        struct sdc_rasd_prop *rasd = NULL;

        struct sdc_rasd_prop tmp[] = {
                {"InstanceID", (CMPIValue *)"Increment", CMPI_chars},
                {"AllocationQuantity", (CMPIValue *)"MegaBytes", CMPI_chars},
                {"VirtualQuantity", (CMPIValue *)&disk_size, CMPI_uint16},
                PROP_END
        };

        ret = dup_rasd_prop_list(tmp, &rasd);
        if (!ret) {
                cu_statusf(_BROKER, s, 
                           CMPI_RC_ERR_FAILED,
                           "Could not copy RASD.");
        }

        return rasd;
}

static struct sdc_rasd mem = {
        .resource_type = CIM_RASD_TYPE_MEM,
        .min = mem_min,
        .max = mem_max,
        .def = mem_def,
        .inc = mem_inc
};

static struct sdc_rasd processor = {
         .resource_type = CIM_RASD_TYPE_PROC,
         .min = proc_min,
         .max = proc_max,
         .def = proc_def,
         .inc = proc_inc
};

static struct sdc_rasd network = {
        .resource_type = CIM_RASD_TYPE_NET,
        .min = net_min,
        .max = net_max,
        .def = net_def,
        .inc = net_inc
};

static struct sdc_rasd disk = {
        .resource_type = CIM_RASD_TYPE_DISK,
        .min = disk_min,
        .max = disk_max,
        .def = disk_def,
        .inc = disk_inc
};

static struct sdc_rasd *sdc_rasd_list[] = {
        &mem,
        &processor,
        &network,
        &disk,
        NULL
};

static CMPIInstance *sdc_rasd_inst(const CMPIBroker *broker,
                                   CMPIStatus *s,
                                   const CMPIObjectPath *ref,
                                   struct sdc_rasd *rasd,
                                   sdc_rasd_type type)
{
        CMPIInstance *inst = NULL;
        struct sdc_rasd_prop *prop_list;
        int i;
        char *inst_id;
        uint16_t resource_type;
        /* Defaults for the following are from 
           CIM_SettingsDefineCapabilities.mof. */
        uint16_t policy = SDC_POLICY_INDEPENDENT;
        uint16_t role = SDC_ROLE_SUPPORTED;

        switch(type) {
        case SDC_RASD_MIN:
                if (rasd->min == NULL)
                        goto out;
                prop_list = rasd->min(ref, s);
                inst_id = "Minimum";
                range = SDC_RANGE_MIN;
                break;
        case SDC_RASD_MAX:
                if (rasd->max == NULL)
                        goto out;
                prop_list = rasd->max(ref, s);
                inst_id = "Maximum";
                range = SDC_RANGE_MAX;
                break;
        case SDC_RASD_INC:
                if (rasd->inc == NULL)
                        goto out;
                prop_list = rasd->inc(ref, s);
                inst_id = "Increment";
                range = SDC_RANGE_INC;
                break;
        case SDC_RASD_DEF:
                if (rasd->def == NULL)
                        goto out;
                prop_list = rasd->def(ref, s);
                inst_id = "Default";
                role = SDC_ROLE_DEFAULT;
                range = SDC_RANGE_POINT;
                break;
        default:
                CMSetStatusWithChars(broker, s, CMPI_RC_ERR_FAILED,
                                     "Unsupported sdc_rasd type.");
                goto out;
        }

        if (s->rc != CMPI_RC_OK) 
                goto out;

        inst = get_typed_instance(broker,
                                  CLASSNAME(ref),
                                  "ResourceAllocationSettingData",
                                  NAMESPACE(ref));
        
        CMSetProperty(inst, "InstanceID", inst_id, CMPI_chars);
        CMSetProperty(inst, "PropertyPolicy", &policy, CMPI_uint16);
        CMSetProperty(inst, "ValueRole", &role, CMPI_uint16);
        CMSetProperty(inst, "ValueRange", &range, CMPI_uint16);

        resource_type = rasd->resource_type;
        CMSetProperty(inst, "ResourceType", &resource_type, CMPI_uint16);

        for (i = 0; prop_list[i].field != NULL; i++) {
                CU_DEBUG("Setting property '%s'.", prop_list[i].field);
                CMSetProperty(inst, prop_list[i].field, 
                              prop_list[i].value, prop_list[i].type);
        }

        CU_DEBUG("freeing prop_list.");
        free_rasd_prop_list(prop_list);
 out:
        CU_DEBUG("Returning inst.");
        return inst;
}

static CMPIStatus sdc_rasds_for_type(const CMPIObjectPath *ref,
                                     struct inst_list *list,
                                     uint16_t type)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct sdc_rasd *rasd = NULL;
        CMPIInstance *inst;
        int i;

        for (i = 0; sdc_rasd_list[i] != NULL; i++) {
                if (sdc_rasd_list[i]->resource_type == type) {
                        rasd = sdc_rasd_list[i];
                        break;
                }
        }

        if (rasd) {
                for (i = SDC_RASD_MIN; i <= SDC_RASD_INC; i++) {
                        inst = sdc_rasd_inst(_BROKER, &s, ref, rasd, i);
                        if (s.rc != CMPI_RC_OK) {
                                CU_DEBUG("Problem getting inst.");
                                goto out;
                        }
                        CU_DEBUG("Got inst.");
                        if (inst != NULL) {
                                inst_list_add(list, inst);
                                CU_DEBUG("Added inst.");
                        } else {
                                CU_DEBUG("Inst is null, not added.");
                        }
                }
                
        } else {
                CU_DEBUG("Unsupported type.");
                CMSetStatusWithChars(_BROKER, &s, CMPI_RC_ERR_FAILED,
                                     "Unsupported device type.");
        }

 out:
        return s;
}

static CMPIStatus alloc_cap_to_rasd(const CMPIObjectPath *ref,
                                    struct std_assoc_info *info,
                                    struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK};
        int ret;
        uint16_t type;

        CU_DEBUG("Getting ResourceType.");

        ret = cu_get_u16_path(ref, "ResourceType", &type);
        if (ret != CMPI_RC_OK) {
                CMSetStatusWithChars(_BROKER, &s, CMPI_RC_ERR_FAILED,
                                     "Could not get ResourceType.");
                goto out;
        }
        
        CU_DEBUG("ResourceType: %hi.", type);

        s = sdc_rasds_for_type(ref, list, type);

 out:
        return s;
}

static CMPIStatus rasd_to_alloc_cap(const CMPIObjectPath *ref,
                                    struct std_assoc_info *info,
                                    struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK};
        
        /* This direction of the association currently not supported. */
        
        return s;
}
static CMPIInstance *make_ref(const CMPIObjectPath *ref,
                              const CMPIInstance *inst,
                              struct std_assoc_info *info,
                              struct std_assoc *assoc)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *refinst = NULL;
        virConnectPtr conn = NULL;

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                return NULL;

        refinst = get_typed_instance(_BROKER,
                                     pfx_from_conn(conn),
                                     "SettingsDefineCapabilities",
                                     NAMESPACE(ref));

        if (refinst != NULL) {
                CMPIObjectPath *instop;

                instop = CMGetObjectPath(inst, NULL);

                CMSetProperty(refinst, assoc->source_prop,
                              (CMPIValue *)&ref, CMPI_ref);
                CMSetProperty(refinst, assoc->target_prop,
                              (CMPIValue *)&instop, CMPI_ref);
        }

        virConnectClose(conn);

        return refinst;
}

char* group_component[] = {
        "Xen_AllocationCapabilities",
        "KVM_AllocationCapabilities",
        NULL
};

char* part_component[] = {
        "Xen_ResourceAllocationSettingData",
        "KVM_ResourceAllocationSettingData",
        NULL
};

char* assoc_classname[] = {
        "Xen_SettingsDefineCapabilities",
        "KVM_SettingsDefineCapabilities",        
        NULL
};

struct std_assoc _alloc_cap_to_rasd = {
        .source_class = (char**)&group_component,
        .source_prop = "GroupComponent",

        .target_class = (char**)&part_component,
        .target_prop = "PartComponent",

        .assoc_class = (char**)&assoc_classname,

        .handler = alloc_cap_to_rasd,
        .make_ref = make_ref
};

struct std_assoc _rasd_to_alloc_cap = {
        .source_class = (char**)&part_component,
        .source_prop = "PartComponent",

        .target_class = (char**)&group_component,
        .target_prop = "GroupComponent",

        .assoc_class = (char**)&assoc_classname,

        .handler = rasd_to_alloc_cap,
        .make_ref = make_ref
};

struct std_assoc *assoc_handlers[] = {
        &_alloc_cap_to_rasd,
        &_rasd_to_alloc_cap,
        NULL
};


STDA_AssocMIStub(, Xen_SettingsDefineCapabilitiesProvider, _BROKER, libvirt_cim_init(), assoc_handlers);
STDA_AssocMIStub(, KVM_SettingsDefineCapabilitiesProvider, _BROKER, libvirt_cim_init(), assoc_handlers);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
