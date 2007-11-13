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

const static CMPIBroker *_BROKER;

static bool rasd_prop_copy_value(struct sdc_rasd_prop src, 
                                 struct sdc_rasd_prop *dest)
{
        bool rc = true;

        CU_DEBUG("Copying '%s'.\n", src.field);
        if (src.type & CMPI_string) {
                CU_DEBUG("String type.\n");
                dest->value = (CMPIValue *)strdup((char *)src.value);
        } else if (src.type & CMPI_INTEGER) {
                CU_DEBUG("Integer type.\n");
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
                CU_DEBUG("count: %d, i: %d.  reallocing.\n", count, i);
                *dest = realloc(*dest, count * sizeof(struct sdc_rasd_prop));
                (*dest)[i].field = strdup(src[i].field);
                ret = rasd_prop_copy_value(src[i], &(*dest)[i]);
                (*dest)[i].type = src[i].type;
        }
        
        /* Make sure to terminate the list. */
        CU_DEBUG("Terminating list. count: %d, i: %d\n", count, i);
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

static struct sdc_rasd mem = {
        .resource_type = CIM_RASD_TYPE_MEM,
        .min = mem_min,
        .max = mem_max,
        .def = mem_def,
        .inc = mem_inc
};

static struct sdc_rasd *sdc_rasd_list[] = {
        &mem,
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
                                  "ResourceAllocationSettingData",
                                  NAMESPACE(ref));
        
        CMSetProperty(inst, "InstanceID", inst_id, CMPI_chars);
        CMSetProperty(inst, "PropertyPolicy", &policy, CMPI_uint16);
        CMSetProperty(inst, "ValueRole", &role, CMPI_uint16);
        CMSetProperty(inst, "ValueRange", &range, CMPI_uint16);

        resource_type = rasd->resource_type;
        CMSetProperty(inst, "ResourceType", &resource_type, CMPI_uint16);

        for (i = 0; prop_list[i].field != NULL; i++) {
                CU_DEBUG("Setting property '%s'.\n", prop_list[i].field);
                CMSetProperty(inst, prop_list[i].field, 
                              prop_list[i].value, prop_list[i].type);
                CU_DEBUG("Set.\n");
        }

        CU_DEBUG("freeing prop_list.\n");
        free_rasd_prop_list(prop_list);
 out:
        CU_DEBUG("Returning inst.\n");
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
                        CU_DEBUG("Got inst.\n");
                        if (inst != NULL) {
                                inst_list_add(list, inst);
                                CU_DEBUG("Added inst.\n");
                        } else {
                                CU_DEBUG("Inst is null, not added.\n");
                        }
                }
                
        } else {
                CU_DEBUG("Unsupported type.\n");
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

        CU_DEBUG("Getting ResourceType.\n");

        ret = cu_get_u16_path(ref, "ResourceType", &type);
        if (ret != 1) {
                CMSetStatusWithChars(_BROKER, &s, CMPI_RC_ERR_FAILED,
                                     "Could not get ResourceType.");
                goto out;
        }
        
        CU_DEBUG("ResourceType: %hi.\n", type);

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
        CMPIInstance *refinst;
        char *base;

        base = class_base_name(assoc->assoc_class);
        if (base == NULL)
                return NULL;

        refinst = get_typed_instance(_BROKER,
                                     base,
                                     NAMESPACE(ref));

        if (refinst != NULL) {
                CMPIObjectPath *instop;

                instop = CMGetObjectPath(inst, NULL);

                CMSetProperty(refinst, assoc->source_prop,
                              (CMPIValue *)&ref, CMPI_ref);
                CMSetProperty(refinst, assoc->target_prop,
                              (CMPIValue *)&instop, CMPI_ref);
        }

        free(base);

        return refinst;
}

struct std_assoc _alloc_cap_to_rasd = {
        .source_class = "CIM_AllocationCapabilities",
        .source_prop = "GroupComponent",

        .target_class = "CIM_ResourceAllocationSettingData",
        .target_prop = "PartComponent",

        .assoc_class = "CIM_SettingsDefineCapabilities",

        .handler = alloc_cap_to_rasd,
        .make_ref = make_ref
};

struct std_assoc _rasd_to_alloc_cap = {
        .source_class = "CIM_ResourceAllocationSettingData",
        .source_prop = "PartComponent",

        .target_class = "CIM_AllocationCapabilities",
        .target_prop = "GroupComponent",

        .assoc_class = "CIM_SettingsDefineCapabilities",

        .handler = rasd_to_alloc_cap,
        .make_ref = make_ref
};

struct std_assoc *assoc_handlers[] = {
        &_alloc_cap_to_rasd,
        &_rasd_to_alloc_cap,
        NULL
};


STDA_AssocMIStub(, Xen_SettingsDefineCapabilitiesProvider, _BROKER, CMNoHook, assoc_handlers);
STDA_AssocMIStub(, KVM_SettingsDefineCapabilitiesProvider, _BROKER, CMNoHook, assoc_handlers);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
