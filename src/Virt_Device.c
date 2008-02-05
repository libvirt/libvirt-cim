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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
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

const static CMPIBroker *_BROKER;
const static uint64_t XEN_MEM_BLOCKSIZE = 4096;

static int net_set_type(CMPIInstance *instance,
                        struct net_device *dev)
{
        uint16_t cim_type;

        if (STREQC(dev->type, "ethernet") ||
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
        if ((s.rc = CMPI_RC_OK) || (CMIsNullObject(str)))
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
                                  ns);

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

        conn = virDomainGetConnect(dom);
        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "LogicalDisk",
                                  ns);

        if (!disk_set_name(inst, dev))
                return NULL;

        return inst;
}

static int mem_set_size(CMPIInstance *instance,
                        struct mem_device *dev)
{
        uint64_t consumableblocks, numberofblocks;

        consumableblocks = dev->size/XEN_MEM_BLOCKSIZE;
        numberofblocks = dev->maxsize/XEN_MEM_BLOCKSIZE;

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
                                  ns);

        if (!mem_set_size(inst, dev))
                return NULL;

        return inst;
}

static CMPIInstance *vcpu_instance(const CMPIBroker *broker,
                                   struct vcpu_device *dev,
                                   const virDomainPtr dom,
                                   const char *ns)
{
        CMPIInstance *inst;
        virConnectPtr conn;

        conn = virDomainGetConnect(dom);
        inst = get_typed_instance(broker,
                                  pfx_from_conn(conn),
                                  "Processor",
                                  ns);

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
                CMSetProperty(instance, "SystemCreationClassName",
                              (CMPIValue *)sccn, CMPI_chars);
                free(sccn);
        }

        return 1;
}

static CMPIInstance *device_instance(const CMPIBroker *broker,
                                     struct virt_device *dev,
                                     const virDomainPtr dom,
                                     const char *ns)
{
        CMPIInstance *instance;

        if (dev->type == VIRT_DEV_NET)
                instance = net_instance(broker,
                                        &dev->dev.net,
                                        dom,
                                        ns);
        else if (dev->type == VIRT_DEV_DISK)
                instance = disk_instance(broker,
                                         &dev->dev.disk,
                                         dom,
                                         ns);
        else if (dev->type == VIRT_DEV_MEM)
                instance = mem_instance(broker,
                                        &dev->dev.mem,
                                        dom,
                                        ns);
        else if (dev->type == VIRT_DEV_VCPU)
                instance = vcpu_instance(broker,
                                         &dev->dev.vcpu,
                                         dom,
                                         ns);
        else
                return NULL;

        if (!instance)
                return NULL;

        device_set_devid(instance, dev, dom);
        device_set_systemname(instance, dom);

        return instance;
}

int device_type_from_classname(const char *classname)
{
        if (strstr(classname, "NetworkPort"))
                return VIRT_DEV_NET;
        else if (strstr(classname, "LogicalDisk"))
                return VIRT_DEV_DISK;
        else if (strstr(classname, "Memory"))
                return VIRT_DEV_MEM;
        else if (strstr(classname, "Processor"))
                return VIRT_DEV_VCPU;
        else
                return VIRT_DEV_UNKNOWN;
}

int dom_devices(const CMPIBroker *broker,
                virDomainPtr dom,
                const char *ns,
                int type,
                struct inst_list *list)
{
        int count;
        int i;
        struct virt_device *devs = NULL;

        count = get_devices(dom, &devs, type);
        if (count <= 0)
                goto out;

        for (i = 0; i < count; i++) {
                CMPIInstance *dev = NULL;

                dev = device_instance(broker, &devs[i], dom, ns);
                if (dev)
                        inst_list_add(list, dev);

                cleanup_virt_device(&devs[i]);
        }

 out:
        free(devs);

        return 1;
}

static int dom_list_devices(virConnectPtr conn,
                            const CMPIObjectPath *ref,
                            struct inst_list *list)
{
        virDomainPtr *doms;
        int ndom;
        int i;
        int type;

        type = device_type_from_classname(CLASSNAME(ref));

        ndom = get_domain_list(conn, &doms);
        if (ndom == 0)
                return 1;
        else if (ndom < 0)
                return 0;

        for (i = 0; i < ndom; i++) {
                dom_devices(_BROKER, doms[i], NAMESPACE(ref), type, list);
        }

        return 1;
}

static CMPIStatus enum_devices(const CMPIObjectPath *reference,
                               const CMPIResult *results,
                               int names_only)
{
        CMPIStatus s;
        virConnectPtr conn;
        struct inst_list list;

        if (!provider_is_responsible(_BROKER, reference, &s))
                return s;

        inst_list_init(&list);

        conn = connect_by_classname(_BROKER, CLASSNAME(reference), &s);
        if (conn == NULL)
                return s;

        if (!dom_list_devices(conn, reference, &list)) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to list domains");
                return s;
        }

        if (list.cur == 0)
                goto out;

        if (names_only)
                cu_return_instance_names(results, &list);
        else
                cu_return_instances(results, &list);

        inst_list_free(&list);

 out:
        CMSetStatus(&s, CMPI_RC_OK);
        virConnectClose(conn);

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

static struct virt_device *find_dom_dev(virDomainPtr dom, 
                                        char *device,
                                        int type)
{
        struct virt_device *list = NULL;
        struct virt_device *dev = NULL;
        int count;
        int i;

        count = get_devices(dom, &list, type);
        if (!count)
                goto out;

        for (i = 0; i < count; i++) {
                if (STREQC(device, list[i].id))
                        dev = virt_device_dup(&list[i]);

                cleanup_virt_device(&list[i]);
        }

 out:
        free(list);

        return dev;
}

CMPIInstance *instance_from_devid(const CMPIBroker *broker,
                                  virConnectPtr conn,
                                  const char *devid,
                                  const char *ns,
                                  int type)
{
        char *domain = NULL;
        char *device = NULL;
        CMPIInstance *instance = NULL;
        virDomainPtr dom = NULL;
        struct virt_device *dev = NULL;

        if (!parse_devid(devid, &domain, &device))
                return NULL;

        dom = virDomainLookupByName(conn, domain);
        if (!dom)
                goto out;

        dev = find_dom_dev(dom, device, type);
        if (!dev)
                goto out;

        instance = device_instance(broker, dev, dom, ns);
        cleanup_virt_device(dev);

 out:
        virDomainFree(dom);
        free(domain);
        free(device);

        return instance;        
}                                       

CMPIStatus get_device(const CMPIBroker *broker,
                      const CMPIObjectPath *reference,
                      CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn;
        CMPIInstance *inst;
        const char *cn;
        const char *devid;

        cn = CLASSNAME(reference);

        conn = connect_by_classname(broker, cn, &s);
        if (conn == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance");
                goto out;
        }        

        if (cu_get_str_path(reference, "DeviceID", &devid) != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "No DeviceID specified");
                goto out;
        }

        inst = instance_from_devid(broker,
                                   conn,
                                   devid,
                                   NAMESPACE(reference),
                                   device_type_from_classname(cn));
        if (inst == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s)", devid);
                goto out;
        }

        s = cu_validate_ref(broker, reference, inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        *_inst = inst;

 out:
        virConnectClose(conn);

        return s;                
}

static CMPIStatus return_device(const CMPIResult *results,
                                const CMPIObjectPath *reference)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        s = get_device(_BROKER, reference, &inst);
        if (s.rc != CMPI_RC_OK || inst == NULL)
                return s;

        CMReturnInstance(results, inst);

        return s;   
}

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return enum_devices(reference, results, 1);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{
        return enum_devices(reference, results, 0);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *reference,
                              const char **properties)
{
        return return_device(results, reference);
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
