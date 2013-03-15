/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *  Guolian Yun <yunguol@cn.ibm.com>
 *  Heidi Eckhart <heidieck@linux.vnet.ibm.com>
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
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_instance.h>
#include "cs_util.h"
#include "misc_util.h"
#include "device_parsing.h"

#include "Virt_Device.h"

#define CIM_NET_UNKNOWN  0
#define CIM_NET_ETHERNET 2

#define CIM_INPUT_UNKNOWN  2
#define CIM_INPUT_MOUSE    3

const static CMPIBroker *_BROKER;
const static uint64_t XEN_MEM_BLOCKSIZE = 4096;

static int net_set_type(CMPIInstance *instance,
                        struct net_device *dev)
{
        uint16_t cim_type;

        if (STREQC(dev->type, "ethernet") ||
            STREQC(dev->type, "network") ||
            STREQC(dev->type, "bridge"))
                cim_type = CIM_NET_ETHERNET;
        else
                cim_type = CIM_NET_UNKNOWN;

        CMSetProperty(instance, "LinkTechnology",
                      (CMPIValue *)&cim_type, CMPI_uint16);

        return 1;
}

static int net_set_hwaddr(CMPIInstance *instance,
                          struct net_device *dev,
                          const CMPIBroker *broker)
{
        CMPIArray *array;
        CMPIStatus s;
        CMPIString *str;

        array = CMNewArray(broker, 1, CMPI_string, &s);
        if ((s.rc != CMPI_RC_OK) || (CMIsNullObject(array)))
                return 0;

        str = CMNewString(broker, dev->mac, &s);
        if ((s.rc != CMPI_RC_OK) || (CMIsNullObject(str)))
                return 0;

        CMSetArrayElementAt(array, 0, &str, CMPI_string);

        CMSetProperty(instance, "NetworkAddresses",
                      (CMPIValue *)&array, CMPI_stringA);

        return 1;
}

static CMPIInstance *net_instance(const CMPIBroker *broker,
                                  struct net_device *dev,
                                  const virDomainPtr dom,
                                  const char *ns)
{
        CMPIInstance *inst;
        virConnectPtr conn;

        conn = virDomainGetConnect(dom);
        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "NetworkPort",
                                  ns,
                                  true);

        if (inst == NULL) {
                CU_DEBUG("Failed to get instance for NetworkPort");
                return NULL;
        }

        if (!net_set_type(inst, dev))
                return NULL;

        if (!net_set_hwaddr(inst, dev, broker))
                return NULL;

        return inst;        
}

static int disk_set_name(CMPIInstance *instance,
                         struct disk_device *dev)
{
        CMSetProperty(instance, "Name",
                      (CMPIValue *)dev->virtual_dev, CMPI_chars);

        return 1;
}

static CMPIInstance *disk_instance(const CMPIBroker *broker,
                                   struct disk_device *dev,
                                   const virDomainPtr dom,
                                   const char *ns)
{
        CMPIInstance *inst;
        virConnectPtr conn;
        uint16_t state;

        conn = virDomainGetConnect(dom);
        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "LogicalDisk",
                                  ns,
                                  true);

        if (inst == NULL) {
                CU_DEBUG("Failed to get instance for LogicalDisk");
                return NULL;
        }

        if (!disk_set_name(inst, dev))
                return NULL;

        //Set HealthState to "OK"
        state = 5;
        CMSetProperty(inst, "HealthState", (CMPIValue *)&state, CMPI_uint16);

        return inst;
}

static int mem_set_size(CMPIInstance *instance,
                        struct mem_device *dev)
{
        uint64_t consumableblocks, numberofblocks;

        consumableblocks = (dev->size << 10) / XEN_MEM_BLOCKSIZE;
        numberofblocks = (dev->maxsize << 10) / XEN_MEM_BLOCKSIZE;

        CMSetProperty(instance, "BlockSize",
                      (CMPIValue *)&XEN_MEM_BLOCKSIZE, CMPI_uint64);
        CMSetProperty(instance, "ConsumableBlocks",
                      (CMPIValue *)&consumableblocks, CMPI_uint64);
        CMSetProperty(instance, "NumberOfBlocks",
                      (CMPIValue *)&numberofblocks, CMPI_uint64);

        return 1;
}

static CMPIInstance *mem_instance(const CMPIBroker *broker,
                                  struct mem_device *dev,
                                  const virDomainPtr dom,
                                  const char *ns)
{
        CMPIInstance *inst;
        virConnectPtr conn;

        conn = virDomainGetConnect(dom);
        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "Memory",
                                  ns,
                                  true);

        if (inst == NULL) {
                CU_DEBUG("Failed to get instance for Memory");
                return NULL;
        }

        if (!mem_set_size(inst, dev))
                return NULL;

        return inst;
}

static int graphics_set_attr(CMPIInstance *instance,
                             struct graphics_device *dev)
{
        int rc;
        char *vp_str = NULL;

        if (STREQC(dev->type, "sdl"))
                rc = asprintf(&vp_str, "%s", dev->type);
        else 
                rc = asprintf(&vp_str, "%s/%s:%s", 
                              dev->type, 
                              dev->dev.vnc.host,
                              dev->dev.vnc.port);
        if (rc == -1)
                return 0;

        CMSetProperty(instance, "VideoProcessor",
                      (CMPIValue *)vp_str, CMPI_chars);

        free(vp_str);

        return 1;
}

static CMPIInstance *graphics_instance(const CMPIBroker *broker,
                                       struct graphics_device *dev,
                                       const virDomainPtr dom,
                                       const char *ns)
{
        CMPIInstance *inst;
        virConnectPtr conn;

        conn = virDomainGetConnect(dom);
        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "DisplayController",
                                  ns,
                                  true);

        if (inst == NULL) {
                CU_DEBUG("Failed to get instance for DisplayController");
                return NULL;
        }

        if (!graphics_set_attr(inst, dev))
                return NULL;

        return inst;
}

int get_input_dev_caption(const char *type,
                          const char *bus,
                          char **cap)
{
        int ret;
        const char *type_str;
        const char *bus_str;

        if (STREQC(type, "mouse"))
                type_str = "Mouse";
        else if (STREQC(type, "tablet"))
                type_str = "Tablet";
        else
                type_str = "Unknown device type";

        if (STREQC(bus, "usb")) 
                bus_str = "USB";
        else if (STREQC(bus, "ps2"))
                bus_str = "PS2";
        else if (STREQC(bus, "xen"))
                bus_str = "Xen";
        else
                bus_str = "Unknown bus";

        ret = asprintf(cap, "%s %s", bus_str, type_str);
        if (ret == -1) {
                CU_DEBUG("Failed to create input id string");
                return 0;
        }

        return 1;
}

static int input_set_attr(CMPIInstance *instance,
                          struct input_device *dev)
{
        uint16_t cim_type;
        char *cap;
        int rc;

        if ((STREQC(dev->type, "mouse")) || (STREQC(dev->type, "tablet"))) 
                cim_type = CIM_INPUT_MOUSE;
        else
                cim_type = CIM_INPUT_UNKNOWN;

        rc = get_input_dev_caption(dev->type, dev->bus, &cap);
        if (rc != 1) {
                free(cap);
                return 0;
        }            

        CMSetProperty(instance, "PointingType",
                      (CMPIValue *)&cim_type, CMPI_uint16);

        CMSetProperty(instance, "Caption", (CMPIValue *)cap, CMPI_chars);

        free(cap);

        return 1;
}

static CMPIInstance *input_instance(const CMPIBroker *broker,
                                    struct input_device *dev,
                                    const virDomainPtr dom,
                                    const char *ns)
{
        CMPIInstance *inst;
        virConnectPtr conn;

        conn = virDomainGetConnect(dom);
        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "PointingDevice",
                                  ns,
                                  true);
        if (inst == NULL) {
                CU_DEBUG("Failed to get instance of %s_PointingDevice",
                         pfx_from_conn(conn));
                return NULL;
        }

        if (!input_set_attr(inst, dev))
                return NULL;

        return inst;
}

static int device_set_devid(CMPIInstance *instance,
                            struct virt_device *dev,
                            const virDomainPtr dom)
{
        char *id;

        id = get_fq_devid((char *)virDomainGetName(dom), dev->id);
        if (id == NULL)
                return 0;

        CMSetProperty(instance, "DeviceID",
                      (CMPIValue *)id, CMPI_chars);

        free(id);

        return 1;
}

static int device_set_systemname(CMPIInstance *instance,
                                 const virDomainPtr dom)
{
        virConnectPtr conn = NULL;

        CMSetProperty(instance, "SystemName",
                      (CMPIValue *)virDomainGetName(dom), CMPI_chars);

        conn = virDomainGetConnect(dom);
        if (conn) {
                char *sccn = NULL;
                sccn = get_typed_class(pfx_from_conn(conn), "ComputerSystem");
                if (sccn != NULL)
                        CMSetProperty(instance, "SystemCreationClassName",
                                      (CMPIValue *)sccn, CMPI_chars);
                free(sccn);
        }

        return 1;
}

static char *get_vcpu_inst_id(const virDomainPtr dom,
                              int proc_num)
{
        int rc;
        char *id_num = NULL;
        char *dev_id = NULL;

        rc = asprintf(&id_num, "%d", proc_num);
        if (rc == -1) {
                free(dev_id);
                dev_id = NULL;
                goto out;
        }
        
        dev_id = get_fq_devid((char *)virDomainGetName(dom), id_num);
        free(id_num);

 out:
        return dev_id;
}                    

static bool vcpu_inst(const CMPIBroker *broker,
                      const virDomainPtr dom,
                      const char *ns,
                      int dev_id_num,
                      struct inst_list *list)
{
        char *dev_id;
        CMPIInstance *inst;
        virConnectPtr conn = NULL;

        conn = virDomainGetConnect(dom);
        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "Processor",
                                  ns,
                                  true);
        if (inst == NULL)
                return false;

        dev_id = get_vcpu_inst_id(dom, dev_id_num);
        CMSetProperty(inst, "DeviceID",
                      (CMPIValue *)dev_id, CMPI_chars);
        free(dev_id);
                
        device_set_systemname(inst, dom);
        inst_list_add(list, inst);

        return true;
}

static bool vcpu_instances(const CMPIBroker *broker,
                           const virDomainPtr dom,
                           const char *ns,
                           uint64_t proc_count,
                           struct inst_list *list)
{
        int i;
        bool rc;

        for (i = 0; i < proc_count; i++) {
                rc = vcpu_inst(broker, dom, ns, i, list);
                if (!rc)
                        return false;
        }

        return true;
}

static bool device_instances(const CMPIBroker *broker,
                             struct virt_device *devs,
                             int count,
                             const virDomainPtr dom,
                             const char *ns,
                             struct inst_list *list)
{
        int i;
        uint64_t proc_count = 0;
        CMPIInstance *instance = NULL;

        for (i = 0; i < count; i++) {
                struct virt_device *dev = &devs[i];

                if (dev->type == CIM_RES_TYPE_NET)
                        instance = net_instance(broker,
                                                &dev->dev.net,
                                                dom,
                                                ns);
                else if (dev->type == CIM_RES_TYPE_DISK)
                        instance = disk_instance(broker,
                                                 &dev->dev.disk,
                                                 dom,
                                                 ns);
                else if (dev->type == CIM_RES_TYPE_MEM)
                        instance = mem_instance(broker,
                                                &dev->dev.mem,
                                                dom,
                                                ns);
                else if (dev->type == CIM_RES_TYPE_PROC) {
                        proc_count = dev->dev.vcpu.quantity;
                        continue;
                } else if (dev->type == CIM_RES_TYPE_GRAPHICS)
                        instance = graphics_instance(broker,
                                                     &dev->dev.graphics,
                                                     dom,
                                                     ns);
                 else if (dev->type == CIM_RES_TYPE_INPUT)
                        instance = input_instance(broker,
                                                  &dev->dev.input,
                                                  dom,
                                                  ns);
                else
                        return false;

                if (!instance)
                        return false;
                
                device_set_devid(instance, dev, dom);
                device_set_systemname(instance, dom);
                inst_list_add(list, instance);
        }

        if (proc_count) {
                vcpu_instances(broker, dom, ns, proc_count, list);
        }

        return true;
}

uint16_t res_type_from_device_classname(const char *classname)
{
        if (strstr(classname, "NetworkPort"))
                return CIM_RES_TYPE_NET;
        else if (strstr(classname, "LogicalDisk"))
                return CIM_RES_TYPE_DISK;
        else if (strstr(classname, "Memory"))
                return CIM_RES_TYPE_MEM;
        else if (strstr(classname, "Processor"))
                return CIM_RES_TYPE_PROC;
        else if (strstr(classname, "DisplayController"))
                return CIM_RES_TYPE_GRAPHICS;
        else if (strstr(classname, "PointingDevice"))
                return CIM_RES_TYPE_INPUT;
        else
                return CIM_RES_TYPE_UNKNOWN;
}

static CMPIStatus _get_devices(const CMPIBroker *broker,
                               const CMPIObjectPath *reference,
                               const virDomainPtr dom,
                               const uint16_t type,
                               struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int count;
        bool rc;
        struct virt_device *devs = NULL;

        count = get_devices(dom, &devs, type, 0);
        if (count <= 0)
                goto out;

        rc = device_instances(broker, 
                              devs, 
                              count,
                              dom, 
                              NAMESPACE(reference),
                              list);

        if (!rc) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Couldn't get device instances");
        }

        cleanup_virt_devices(&devs, count);

 out:
        free(devs);
        return s;
}

static CMPIStatus _enum_devices(const CMPIBroker *broker,
                                const CMPIObjectPath *reference,
                                const virDomainPtr dom,
                                const uint16_t type,
                                struct inst_list *list)
{
        CMPIStatus s;
        int i;

        if (type == CIM_RES_TYPE_ALL) {
                for (i=0; i<CIM_RES_TYPE_COUNT; i++)
                        s = _get_devices(broker,
                                         reference,
                                         dom,
                                         cim_res_types[i],
                                         list);
        }
        else
                s = _get_devices(broker,
                                 reference,
                                 dom,
                                 type,
                                 list);

        return s;
}

CMPIStatus enum_devices(const CMPIBroker *broker,
                        const CMPIObjectPath *reference,
                        const char *domain,
                        const uint16_t type,
                        struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;
        virDomainPtr *doms = NULL;
        int count = 1;
        int i;

        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL)
                goto out;

        if (domain) {
                doms = calloc(1, sizeof(virDomainPtr));
                doms[0] = virDomainLookupByName(conn, domain);
        }
        else
                count = get_domain_list(conn, &doms);

        for (i = 0; i < count; i++) {
                s = _enum_devices(broker,
                                  reference,
                                  doms[i],
                                  type,
                                  list);

                virDomainFree(doms[i]);
        }

 out:
        virConnectClose(conn);
        free(doms);

        return s;
}

static CMPIStatus return_enum_devices(const CMPIObjectPath *reference,
                                      const CMPIResult *results,
                                      int names_only)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        struct inst_list list;

        if (!provider_is_responsible(_BROKER, reference, &s))
                goto out;

        inst_list_init(&list);

        s = enum_devices(_BROKER,
                         reference,
                         NULL, 
                         res_type_from_device_classname(CLASSNAME(reference)),
                         &list);
        if (s.rc != CMPI_RC_OK)
                goto out;

        if (names_only)
                cu_return_instance_names(results, &list);
        else
                cu_return_instances(results, &list);

 out:
        inst_list_free(&list);

        return s;
}

static int parse_devid(const char *devid, char **dom, char **dev)
{
        int ret;

        ret = sscanf(devid, "%a[^/]/%as", dom, dev);
        if (ret != 2) {
                free(*dom);
                free(*dev);
                return 0;
        }

        return 1;
}

static int proc_dev_list(uint64_t quantity,
                         struct virt_device **list)
{
        int i;

        *list = (struct virt_device *)calloc(quantity, 
                                             sizeof(struct virt_device));

        for (i = 0; i < quantity; i++) {
                char *dev_num;
                int ret;

                ret = asprintf(&dev_num, "%d", i);
                if (ret == -1)
                        CU_DEBUG("asprintf error %d" , ret);

                (*list)[i].id = strdup(dev_num);

                free(dev_num);
        }

        return quantity;
}

static struct virt_device *find_dom_dev(virDomainPtr dom, 
                                        char *device,
                                        int type)
{
        struct virt_device *list = NULL;
        struct virt_device *dev = NULL;
        int count;
        int i;

        count = get_devices(dom, &list, type, 0);
        if (!count) {
                CU_DEBUG("No devices for %i", type);
                goto out;
        }

        if (type == CIM_RES_TYPE_PROC) {
                struct virt_device *tmp_list;
                int tmp_count;

                tmp_count = proc_dev_list(list[0].dev.vcpu.quantity,
                                          &tmp_list);
                cleanup_virt_devices(&list, count);
                list = tmp_list;
                count = tmp_count;
        }

        for (i = 0; i < count; i++) {
                if (STREQC(device, list[i].id)) {
                        dev = virt_device_dup(&list[i]);
                        break;
                }

        }

        cleanup_virt_devices(&list, count);
 out:

        return dev;
}

CMPIStatus get_device_by_name(const CMPIBroker *broker,
                              const CMPIObjectPath *reference,
                              const char *name,
                              const uint16_t type,
                              CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        char *domain = NULL;
        char *device = NULL;
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        struct virt_device *dev = NULL;
        struct inst_list tmp_list;

        inst_list_init(&tmp_list);

        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance");
                goto out;
        }

        if (parse_devid(name, &domain, &device) != 1) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (bad id %s)", 
                           name);
                goto out;
        }

        dom = virDomainLookupByName(conn, domain);
        if (dom == NULL) {
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_NOT_FOUND,
                                conn,
                                "No such instance (no domain for %s)",
                                name);
                goto err;
        }

        dev = find_dom_dev(dom, device, type);
        if (!dev) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (no device %s)",
                           name);
                goto err;
        }

        if (type == CIM_RES_TYPE_PROC) {
                int dev_id_num;
                sscanf(dev->id, "%d", &dev_id_num);

                vcpu_inst(broker, dom, NAMESPACE(reference),
                          dev_id_num, &tmp_list);
        } else {
                device_instances(broker, dev, 1, dom,
                                 NAMESPACE(reference), &tmp_list);
        }

        cleanup_virt_devices(&dev, 1);

        *_inst = tmp_list.list[0];

 err:
        virDomainFree(dom);
        free(domain);
        free(device);

 out:
        inst_list_free(&tmp_list);
        virConnectClose(conn);

        return s;
}

CMPIStatus get_device_by_ref(const CMPIBroker *broker,
                             const CMPIObjectPath *reference,
                             CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        const char *name = NULL;

        if (cu_get_str_path(reference, "DeviceID", &name) != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "No DeviceID specified");
                goto out;
        }
        
        s = get_device_by_name(broker, 
                               reference, 
                               name, 
                               res_type_from_device_classname(CLASSNAME(reference)), 
                               &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;
        
        s = cu_validate_ref(broker, reference, inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        *_inst = inst;

 out:
        return s;                
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_enum_devices(reference, results, true);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{
        return return_enum_devices(reference, results, false);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        s = get_device_by_ref(_BROKER, reference, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;
        
        CMReturnInstance(results, inst);
        
 out:
        return s;
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_EQ();
DEFAULT_INST_CLEANUP();

STD_InstanceMIStub(,
                   Virt_Device,
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
