/*
 * Copyright IBM Corp. 2007, 2013
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
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
#include <inttypes.h>
#include <sys/stat.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libcmpiutil/libcmpiutil.h>
#include <libcmpiutil/std_instance.h>

#include "misc_util.h"
#include "cs_util.h"
#include "infostore.h"

#include "Virt_RASD.h"
#include "svpc_types.h"
#include "Virt_Device.h"

#if LIBVIR_VERSION_NUMBER < 9000
/* Network QoS support */
#define QOSCMD_MAC2BANDWIDTH "_ROOT=$(tc class show dev %s | awk '($4==\"root\")\
{print $3}')\n _ID=$(tc filter show dev %s | awk 'BEGIN {RS=\"\\nfilter\"} (NR>2)\
{m1=substr($24,1,2);m2=substr($24,3,2);m3=substr($24,5,2);m4=substr($24,7,2);\
m5=substr($20,1,2);m6=substr($20,3,2);printf(\"%%s:%%s:%%s:%%s:%%s:%%s %%s\\n\",\
m1,m2,m3,m4,m5,m6,$18)}' | awk -v mm=%s '($1==mm){print $2}')\n \
if [[ -n \"$_ID\" ]]; then\n tc class show dev %s | awk -v rr=$_ROOT -v id=$_ID \
'($4==\"parent\" && $5==rr && $3==id){print \
substr($13,1,(index($13,\"Kbit\")-1))}'\n fi\n"
#endif

const static CMPIBroker *_BROKER;

static struct virt_device *_find_dev(struct virt_device *list,
                                    int count,
                                    const char *id)
{
        int i;

        for (i = 0; i < count; i++) {
                struct virt_device *dev = &list[i];

                if (STREQ(dev->id, id))
                        return virt_device_dup(dev);
        }

        return NULL;
}

int list_rasds(virConnectPtr conn,
              const uint16_t type,
              const char *host,
              struct virt_device **list)
{
        virDomainPtr dom;
        int count = 0;

        dom = virDomainLookupByName(conn, host);
        if (dom == NULL)
                return 0;

        count = get_devices(dom, list, type, 0);

        virDomainFree(dom);

        return count;
}

static struct virt_device *find_dev(virConnectPtr conn,
                                    const uint16_t type,
                                    const char *host,
                                    const char *devid)
{
        int count = -1;
        struct virt_device *list = NULL;
        struct virt_device *dev = NULL;

        count = list_rasds(conn, type, host, &list);
        if (count > 0) {
                dev = _find_dev(list, count, devid);
                cleanup_virt_devices(&list, count);
        }

        return dev;
}

char *rasd_to_xml(CMPIInstance *rasd)
{
        /* FIXME: Remove this */
        return NULL;
}

static CMPIStatus set_proc_rasd_params(const CMPIBroker *broker,
                                       const CMPIObjectPath *ref,
                                       struct virt_device *dev,
                                       const char *domain,
                                       CMPIInstance *inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        struct infostore_ctx *info = NULL;
        uint32_t weight = 0;
        uint64_t limit;
        uint64_t count = 0;

        conn = connect_by_classname(broker, CLASSNAME(ref), &s);
        if (conn == NULL)
                return s;

        dom = virDomainLookupByName(conn, domain);
        if (dom == NULL) {
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_NOT_FOUND,
                                conn,
                                "Domain `%s' not found while getting info",
                                domain);
                goto out;
        }

        if (domain_online(dom)) {
                int active_count = domain_vcpu_count(dom);
                if (active_count < 0) {
                    cu_statusf(broker, &s,
                               CMPI_RC_ERR_FAILED,
                               "Unable to get domain `%s' vcpu count",
                               domain);
                    goto out;
                }
                count = active_count;
        } else {
                count = dev->dev.vcpu.quantity;
        }

        if (count > 0)
                CMSetProperty(inst,
                              "VirtualQuantity",
                              (CMPIValue *)&count,
                              CMPI_uint64);

        info = infostore_open(dom);
        if (info == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to open domain information store");
                goto out;
        }

        /* Early versions of libvirt only support CPU cgroups for *running* KVM guests */
#if LIBVIR_VERSION_NUMBER < 9000
        if (domain_online(dom) && STREQC(virConnectGetType(conn), "QEMU")) {
#else
        if (STREQC(virConnectGetType(conn), "QEMU")) {
#endif
                char *sched;
                int nparams;
                unsigned int i;
                virSchedParameter *params;

                /* First find the number of scheduler params, in order malloc space for them all */
                sched = virDomainGetSchedulerType(dom, &nparams);
                if (sched == NULL) {
                        CU_DEBUG("Failed to get scheduler type");
                        goto out;
                }
                CU_DEBUG("domain has %d scheduler params", nparams);
                free(sched);

                /* Now retrieve all the scheduler params for this domain */
                params = calloc(nparams, sizeof(virSchedParameter));
                if (virDomainGetSchedulerParameters(dom, params, &nparams) != 0) {
                        CU_DEBUG("Failed to get scheduler params for domain");
                        goto out;
                }

                /* Look for the CPU cgroup scheduler parameter, called 'cpu_shares' */
                for (i = 0 ; i < nparams ; i++) {
                        CU_DEBUG("scheduler param #%d name is %s (type %d)",
                                 i, params[i].field, params[i].type);
                        if (STREQ(params[i].field, "cpu_shares") &&
                            (params[i].type == VIR_DOMAIN_SCHED_FIELD_ULLONG)) {
                                CU_DEBUG("scheduler param %s = %d",
                                         params[i].field, params[i].value.ul);
                                weight = (uint32_t)params[i].value.ul;
                                break; /* Found it! */
                        }
                }
                free(params);
        }
        else
                weight = (uint32_t)infostore_get_u64(info, "weight");
        limit = infostore_get_u64(info, "limit");

        CMSetProperty(inst, "Weight",
                      (CMPIValue *)&weight, CMPI_uint32);
        CMSetProperty(inst, "Limit",
                      (CMPIValue *)&limit, CMPI_uint64);

 out:
        virDomainFree(dom);
        virConnectClose(conn);
        infostore_close(info);

        return s;
}

static bool get_file_size(const CMPIBroker *broker,
                          const CMPIObjectPath *ref,
                          const char *image,
                          uint64_t *size)
{
        struct stat64 st;

        if (stat64(image, &st) == -1)
                return false;

        *size = st.st_size;

        return true;
}

#if LIBVIR_VERSION_NUMBER > 4000
static bool get_vol_size(const CMPIBroker *broker,
                         const CMPIObjectPath *ref,
                         const char *image,
                         uint64_t *size)
{
        virConnectPtr conn = NULL;
        virStorageVolPtr vol = NULL;
        virStorageVolInfo volinfo;
        CMPIStatus s;
        bool ret = false;

        *size = 0;

        conn = connect_by_classname(broker, CLASSNAME(ref), &s);
        if (conn == NULL)
                return false;

        vol = virStorageVolLookupByPath(conn, image);
        if (vol != NULL) {
                if (virStorageVolGetInfo(vol, &volinfo) != 0) {
                        CU_DEBUG("Failed to get info for volume %s", image);
                } else {
                        *size = volinfo.capacity;
                        ret = true;
                }
        } else {
                CU_DEBUG("Failed to lookup pool for volume %s", image);
        }

        virStorageVolFree(vol);
        virConnectClose(conn);

        if (!ret)
                return get_file_size(broker, ref, image, size);
        else
                return true;
}
#else
static bool get_vol_size(const CMPIBroker *broker,
                         const CMPIObjectPath *ref,
                         const char *image,
                         uint64_t *size)
{
        return get_file_size(broker, ref, image, size);
}
#endif

static CMPIStatus set_rasd_device_address(const CMPIBroker *broker,
                                          const CMPIObjectPath *ref,
                                          const struct device_address *addr,
                                          CMPIInstance *inst)
{
        int i;
        CMPIArray *arr_key;
        CMPIArray *arr_value;
        CMPIString *string;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        arr_key = CMNewArray(broker,
                             addr->ct,
                             CMPI_string,
                             &s);
        if (s.rc != CMPI_RC_OK)
                goto out;

        arr_value = CMNewArray(broker,
                               addr->ct,
                               CMPI_string,
                               &s);
        if (s.rc != CMPI_RC_OK)
                goto out;

        for (i = 0; i < addr->ct; i++) {
                string = CMNewString(broker,
                                     addr->key[i],
                                     &s);
                if (s.rc != CMPI_RC_OK)
                        goto out;

                CMSetArrayElementAt(arr_key,
                                    i,
                                    (CMPIValue *)&string,
                                    CMPI_string);

                string = CMNewString(broker,
                                     addr->value[i],
                                     &s);
                if (s.rc != CMPI_RC_OK)
                        goto out;

                CMSetArrayElementAt(arr_value,
                                    i,
                                    (CMPIValue *)&string,
                                    CMPI_string);
        }

        CMSetProperty(inst, "AddressProperties",
                      (CMPIValue *)&arr_key,
                      CMPI_stringA);

        CMSetProperty(inst, "AddressValues",
                      (CMPIValue *)&arr_value,
                      CMPI_stringA);

 out:
        return s;
}

static CMPIStatus set_disk_rasd_params(const CMPIBroker *broker,
                                       const CMPIObjectPath *ref,
                                       const struct virt_device *dev,
                                       CMPIInstance *inst)
{
        uint64_t cap = 0;
        uint16_t type;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        char *poolid = NULL;
        virConnectPtr conn = NULL;
        virStorageVolPtr vol = NULL;
        virStoragePoolPtr pool = NULL;
        const char *pool_name = NULL;
        int ret = -1;

        get_vol_size(broker, ref, dev->dev.disk.source, &cap);

        CMSetProperty(inst, "VirtualQuantity",
                      (CMPIValue *)&cap, CMPI_uint64);

        CMSetProperty(inst, "AllocationUnits",
                      (CMPIValue *)"Bytes", CMPI_chars);

        CMSetProperty(inst,
                      "VirtualDevice",
                      (CMPIValue *)dev->dev.disk.virtual_dev,
                      CMPI_chars);

        CMSetProperty(inst,
                      "Address",
                      (CMPIValue *)dev->dev.disk.source,
                      CMPI_chars);

        conn = connect_by_classname(broker, CLASSNAME(ref), &s);
        if (conn == NULL) {
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_NOT_FOUND,
                                conn,
                                "Could not get connection to hypervisor");
                goto cont;
        }

        vol = virStorageVolLookupByPath(conn, dev->dev.disk.source);
        if (vol == NULL) {
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_NOT_FOUND,
                                conn,
                                "Failed to get StorageVolPtr");
                goto cont;
        }

        pool = virStoragePoolLookupByVolume(vol);
        if (pool == NULL) {
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_NOT_FOUND,
                                conn,
                                "Failed to get StoragePoolPtr");
                goto cont;
        }

        pool_name = virStoragePoolGetName(pool);
        if (pool_name == NULL) {
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_NOT_FOUND,
                                conn,
                                "Failed to get Pool name for volume");
                goto cont;
        }

        ret = asprintf(&poolid, "DiskPool/%s", pool_name);
        if (ret == -1) {
               CU_DEBUG("Failed to get disk poolid");
               goto cont;
        }

        CMSetProperty(inst,
                      "PoolID",
                      (CMPIValue *)poolid,
                      CMPI_chars);
 cont:
        CMSetProperty(inst,
                      "BusType",
                      (CMPIValue *)dev->dev.disk.bus_type,
                      CMPI_chars);

        /* There's not much we can do here if we don't recognize the type,
         * so it seems that assuming 'disk' is a reasonable default
         */
        if ((dev->dev.disk.device != NULL) &&
            STREQ(dev->dev.disk.device, "cdrom"))
                type = VIRT_DISK_TYPE_CDROM;
        else if ((dev->dev.disk.device != NULL) &&
                STREQ(dev->dev.disk.device, "floppy"))
                type = VIRT_DISK_TYPE_FLOPPY;
        else
                type = VIRT_DISK_TYPE_DISK;

        CMSetProperty(inst,
                      "EmulatedType",
                      (CMPIValue *)&type,
                      CMPI_uint16);

        if(dev->dev.disk.readonly)
                CMSetProperty(inst,
                              "readonly",
                              (CMPIValue *)&(dev->dev.disk.readonly),
                              CMPI_boolean);

        if(dev->dev.disk.driver)
                CMSetProperty(inst,
                              "DriverName",
                              (CMPIValue *)dev->dev.disk.driver,
                              CMPI_chars);

        if(dev->dev.disk.driver_type)
                CMSetProperty(inst,
                              "DriverType",
                              (CMPIValue *)dev->dev.disk.driver_type,
                              CMPI_chars);

        if(dev->dev.disk.cache)
                CMSetProperty(inst,
                              "DriverCache",
                              (CMPIValue *)dev->dev.disk.cache,
                              CMPI_chars);

        if(dev->dev.disk.access_mode)
                CMSetProperty(inst,
                              "AccessMode",
                              (CMPIValue *)dev->dev.disk.access_mode,
                              CMPI_chars);

        if(dev->dev.disk.rawio)
                CMSetProperty(inst,
                              "rawio",
                              (CMPIValue *)dev->dev.disk.rawio,
                              CMPI_chars);

        if(dev->dev.disk.sgio)
                CMSetProperty(inst,
                              "sgio",
                              (CMPIValue *)dev->dev.disk.sgio,
                              CMPI_chars);

        if(dev->dev.disk.shareable)
                CMSetProperty(inst,
                              "shareable",
                              (CMPIValue *)&(dev->dev.disk.shareable),
                              CMPI_boolean);

        if (dev->dev.disk.address.ct > 0)
                set_rasd_device_address(broker,
                                        ref,
                                        &dev->dev.disk.address,
                                        inst);

        virStoragePoolFree(pool);
        virStorageVolFree(vol);
        virConnectClose(conn);
        return s;
}

static CMPIStatus set_net_vsi_rasd_params(const CMPIBroker *broker,
                                       const CMPIObjectPath *ref,
                                       const struct vsi_device vsi,
                                       CMPIInstance *inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};

        CMSetProperty(inst,
                      "VSIType",
                      (CMPIValue *)vsi.vsi_type,
                      CMPI_chars);

        if (vsi.manager_id != NULL)
                CMSetProperty(inst,
                              "VSIManagerID",
                              (CMPIValue *)vsi.manager_id,
                              CMPI_chars);

        if (vsi.type_id != NULL)
                CMSetProperty(inst,
                              "VSITypeID",
                              (CMPIValue *)vsi.type_id,
                              CMPI_chars);

        if (vsi.type_id_version != NULL)
                CMSetProperty(inst,
                              "VSITypeIDVersion",
                              (CMPIValue *)vsi.type_id_version,
                              CMPI_chars);

        if (vsi.instance_id != NULL)
                CMSetProperty(inst,
                              "VSIInstanceID",
                              (CMPIValue *)vsi.instance_id,
                              CMPI_chars);

        if (vsi.filter_ref != NULL)
                CMSetProperty(inst,
                              "FilterRef",
                              (CMPIValue *)vsi.filter_ref,
                              CMPI_chars);

        if (vsi.profile_id != NULL)
                CMSetProperty(inst,
                              "ProfileID",
                              (CMPIValue *)vsi.profile_id,
                              CMPI_chars);

        return s;
}

static CMPIStatus set_net_rasd_params(const CMPIBroker *broker,
                                       const CMPIObjectPath *ref,
                                       const struct virt_device *dev,
                                       CMPIInstance *inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};

        CMSetProperty(inst,
                      "NetworkType",
                      (CMPIValue *)dev->dev.net.type,
                      CMPI_chars);

        CMSetProperty(inst,
                      "Address",
                      (CMPIValue *)dev->dev.net.mac,
                      CMPI_chars);

        if (dev->dev.net.source != NULL)
                CMSetProperty(inst,
                              "NetworkName",
                              (CMPIValue *)dev->dev.net.source,
                              CMPI_chars);

#if LIBVIR_VERSION_NUMBER < 9000
        /* Network QoS support */
        if ((dev->dev.net.mac != NULL) && (dev->dev.net.source != NULL)) {
                FILE *pipe = NULL;
                char *cmd = NULL;
                uint64_t val = 0;
                int i;

                /* Get tc performance class bandwidth for this MAC addr */
                i = asprintf(&cmd, QOSCMD_MAC2BANDWIDTH, dev->dev.net.source,
                                                         dev->dev.net.source,
                                                         dev->dev.net.mac,
                                                         dev->dev.net.source);
                if (i == -1)
                        goto out;

                if ((pipe = popen(cmd, "r")) != NULL) {
                        if (fscanf(pipe, "%u", (unsigned int *)&val) == 1) {
                                CU_DEBUG("pipe read. val = %d", val);

                                CMSetProperty(inst,
                                        "Reservation",
                                        (CMPIValue *)&val, CMPI_uint64);
                                CMSetProperty(inst,
                                        "AllocationUnits",
                                        (CMPIValue *)"KiloBits per Second",
                                        CMPI_chars);
                        }
                        pclose(pipe);
                }
                free(cmd);
        }
#else
        if (dev->dev.net.reservation) {
                CMSetProperty(inst,
                              "Reservation",
                              (CMPIValue *)&(dev->dev.net.reservation),
                              CMPI_uint64);

                if (dev->dev.net.limit)
                        CMSetProperty(inst,
                                      "Limit",
                                      (CMPIValue *)&(dev->dev.net.limit),
                                      CMPI_uint64);

                CMSetProperty(inst,
                              "AllocationUnits",
                              (CMPIValue *)"KiloBytes per Second",
                              CMPI_chars);
        }
#endif

        if ((dev->dev.net.source != NULL) &&
            (STREQ(dev->dev.net.type, "direct")))
                CMSetProperty(inst,
                              "SourceDevice",
                              (CMPIValue *)dev->dev.net.source,
                              CMPI_chars);

        if (dev->dev.net.device != NULL)
                CMSetProperty(inst,
                              "VirtualDevice",
                              (CMPIValue *)dev->dev.net.device,
                              CMPI_chars);

        if (dev->dev.net.net_mode != NULL)
                CMSetProperty(inst,
                              "NetworkMode",
                              (CMPIValue *)dev->dev.net.net_mode,
                              CMPI_chars);

        if (dev->dev.net.model != NULL)
                CMSetProperty(inst,
                              "ResourceSubType",
                              (CMPIValue *)dev->dev.net.model,
                              CMPI_chars);

        if (dev->dev.net.poolid != NULL)
                CMSetProperty(inst,
                              "PoolID",
                              (CMPIValue *)dev->dev.net.poolid,
                              CMPI_chars);

        if (dev->dev.net.address.ct > 0)
                set_rasd_device_address(broker,
                                        ref,
                                        &dev->dev.net.address,
                                        inst);

#if LIBVIR_VERSION_NUMBER < 9000
out:
#endif
        return s;
}

static CMPIStatus set_graphics_rasd_params(const struct virt_device *dev,
                                           CMPIInstance *inst,
                                           const char *name,
                                           const char *classname)
{
        int rc;
        char *addr_str = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;

        CMSetProperty(inst, "ResourceSubType",
                       (CMPIValue *)dev->dev.graphics.type, CMPI_chars);

        if (STREQC(dev->dev.graphics.type, "sdl"))
                rc = asprintf(&addr_str, "%s", dev->dev.graphics.type);
        else {
                rc = asprintf(&addr_str,
                              "%s:%s",
                              dev->dev.graphics.dev.vnc.host,
                              dev->dev.graphics.dev.vnc.port);
        }

        CU_DEBUG("graphics Address = %s", addr_str);

        if (rc == -1)
                goto out;

        CMSetProperty(inst, "Address",
                (CMPIValue *)addr_str, CMPI_chars);

        if (STREQC(dev->dev.graphics.type, "vnc")) {
                CMSetProperty(inst, "KeyMap",
                             (CMPIValue *)dev->dev.graphics.dev.vnc.keymap, CMPI_chars);

                conn = connect_by_classname(_BROKER, classname, &s);
                if (conn == NULL)
                        goto out;

                dom = virDomainLookupByName(conn, name);
                if (dom == NULL) {
                        cu_statusf(_BROKER, &s,
                                   CMPI_RC_ERR_NOT_FOUND,
                                   "Domain %s not found",
                                   name);
                        goto out;
                }

                if (dev->dev.graphics.dev.vnc.passwd &&
                                strlen(dev->dev.graphics.dev.vnc.passwd)) {
                        CU_DEBUG("has password");
                        CMSetProperty(inst, "Password",
                                      (CMPIValue *)"********", CMPI_chars);
                }

                /* FIXME: Populate the IsIPv6Only */
        }

 out:
        free(addr_str);
        virDomainFree(dom);
        virConnectClose(conn);

        return s;
}

static char* _build_console_url(const char *protocol,
                                const char *host,
                                const char *port)
{
        char* result = NULL;

        if (host == NULL)
                goto out;

        if (protocol != NULL && STREQC("file", protocol)) {
                /* The host string contains the file name.
                   Even if the file name does not start with a '/'
                   it is treated by libvirt as a full qualified path.
                */
                if (host[0] == '/') {
                        if (asprintf(&result, "file://%s", host) < 0)
                                result = NULL;
                        goto out;
                } else {
                        if (asprintf(&result, "file:///%s", host) < 0)
                                result = NULL;
                        goto out;
                }
        }
        /* The assumption is that the host does not contain a port.
           If the host string contains a ':',
           the host is treated as an IPv6 address.
        */
        if (strchr(host, ':') == NULL) {
                if (port == NULL) {
                        if (asprintf(&result,"%s://%s", protocol, host) < 0)
                                result = NULL;
                        goto out;
                } else {
                        if (asprintf(&result,"%s://%s:%s", protocol,
                                     host,port) < 0)
                                result = NULL;
                        goto out;
                }
        }
 out:
        return result;
}


static CMPIStatus set_console_rasd_params(const struct virt_device *vdev,
                                          CMPIInstance *inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        const struct console_device *cdev = NULL;
        char* tmp = NULL;

        cdev = &vdev->dev.console;

        CMSetProperty(inst, "OtherResourceType", "console", CMPI_chars);
        CMSetProperty(inst, "SourceType",
                      (CMPIValue *)&cdev->source_type, CMPI_uint16);
        CMSetProperty(inst, "TargetType",
                      (CMPIValue *)cdev->target_type, CMPI_chars);

        switch (cdev->source_type) {
        case CIM_CHARDEV_SOURCE_TYPE_PTY:
                CMSetProperty(inst, "SourcePath",
                              (CMPIValue *)cdev->source_dev.pty.path,
                              CMPI_chars);
                break;
        case CIM_CHARDEV_SOURCE_TYPE_DEV:
                CMSetProperty(inst, "SourcePath",
                              (CMPIValue *)cdev->source_dev.dev.path,
                              CMPI_chars);
                break;
        case CIM_CHARDEV_SOURCE_TYPE_FILE:
                CMSetProperty(inst, "SourcePath",
                              (CMPIValue *)cdev->source_dev.file.path,
                              CMPI_chars);
                break;
        case CIM_CHARDEV_SOURCE_TYPE_PIPE:
                CMSetProperty(inst, "SourcePath",
                              (CMPIValue *)cdev->source_dev.pipe.path,
                              CMPI_chars);
                break;
        case CIM_CHARDEV_SOURCE_TYPE_UNIXSOCK:
                tmp = _build_console_url("file",
                                         cdev->source_dev.unixsock.path, NULL);
                if (cdev->source_dev.unixsock.mode != NULL) {
                        if (STREQC(cdev->source_dev.unixsock.mode, "bind"))
                                CMSetProperty(inst, "BindURL",
                                              (CMPIValue *)tmp, CMPI_chars);
                        else if (STREQC(cdev->source_dev.unixsock.mode,
                                        "connect"))
                                CMSetProperty(inst, "ConnectURL",
                                              (CMPIValue *)tmp, CMPI_chars);
                }
                free(tmp);
                break;
        case CIM_CHARDEV_SOURCE_TYPE_UDP:
                tmp = _build_console_url("udp",
                                         cdev->source_dev.udp.bind_host,
                                         cdev->source_dev.udp.bind_service);
                CMSetProperty(inst, "BindURL",
                              (CMPIValue *)tmp, CMPI_chars);
                free(tmp);

                tmp = _build_console_url("udp",
                                         cdev->source_dev.udp.connect_host,
                                         cdev->source_dev.udp.connect_service);
                CMSetProperty(inst, "ConnectURL", (CMPIValue *)tmp, CMPI_chars);
                free(tmp);
                break;
        case CIM_CHARDEV_SOURCE_TYPE_TCP:
                tmp = _build_console_url(cdev->source_dev.tcp.protocol,
                                         cdev->source_dev.tcp.host,
                                         cdev->source_dev.tcp.service);
                if (cdev->source_dev.tcp.mode != NULL) {
                        if (STREQC(cdev->source_dev.tcp.mode, "bind"))
                                CMSetProperty(inst, "BindURL",
                                              (CMPIValue *)tmp, CMPI_chars);
                        else if (STREQC(cdev->source_dev.tcp.mode, "connect"))
                                CMSetProperty(inst, "ConnectURL",
                                              (CMPIValue *)tmp, CMPI_chars);
                }
                free(tmp);
                break;

        default:
                /* Nothing to do for :
                   CIM_CHARDEV_SOURCE_TYPE_STDIO
                   CIM_CHARDEV_SOURCE_TYPE_NULL
                   CIM_CHARDEV_SOURCE_TYPE_VC
                   CIM_CHARDEV_SOURCE_TYPE_SPICEVMC
                */
                break;
        }

        return s;
}

static CMPIStatus set_input_rasd_params(const struct virt_device *dev,
                                        CMPIInstance *inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        char *cap;
        int ret;

        ret = get_input_dev_caption(dev->dev.input.type,
                                    dev->dev.input.bus,
                                    &cap);
        if (ret != 1) {
                free(cap);
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "Unable to build input caption");
                return s;
        }

        CMSetProperty(inst, "ResourceSubType",
                      (CMPIValue *)dev->dev.input.type, CMPI_chars);

        CMSetProperty(inst, "BusType",
                      (CMPIValue *)dev->dev.input.bus, CMPI_chars);

        CMSetProperty(inst, "Caption", (CMPIValue *)cap, CMPI_chars);

        free(cap);

        return s;
}

CMPIInstance *rasd_from_vdev(const CMPIBroker *broker,
                                    struct virt_device *dev,
                                    const char *host,
                                    const CMPIObjectPath *ref,
                                    const char **properties)
{
        CMPIStatus s;
        CMPIInstance *inst;
        uint16_t type;
        const char *base;
        char *id;
        const char *keys[] = {"InstanceID", NULL};

        if (dev->type == CIM_RES_TYPE_DISK) {
                type = CIM_RES_TYPE_DISK;
                base = "DiskResourceAllocationSettingData";
        } else if (dev->type == CIM_RES_TYPE_NET) {
                type = CIM_RES_TYPE_NET;
                base = "NetResourceAllocationSettingData";
        } else if (dev->type == CIM_RES_TYPE_PROC) {
                type = CIM_RES_TYPE_PROC;
                base = "ProcResourceAllocationSettingData";
        } else if (dev->type == CIM_RES_TYPE_MEM) {
                type = CIM_RES_TYPE_MEM;
                base = "MemResourceAllocationSettingData";
        } else if (dev->type == CIM_RES_TYPE_GRAPHICS) {
                type = CIM_RES_TYPE_GRAPHICS;
                base = "GraphicsResourceAllocationSettingData";
        } else if (dev->type == CIM_RES_TYPE_CONSOLE) {
                type = CIM_RES_TYPE_OTHER;
                base = "ConsoleResourceAllocationSettingData";
        } else if (dev->type == CIM_RES_TYPE_INPUT) {
                type = CIM_RES_TYPE_INPUT;
                base = "InputResourceAllocationSettingData";
        } else {
                return NULL;
        }

        inst = get_typed_instance(broker,
                                  CLASSNAME(ref),
                                  base,
                                  NAMESPACE(ref),
                                  false);
        if (inst == NULL)
                return inst;

        s = CMSetPropertyFilter(inst, properties, keys);

        if (s.rc != CMPI_RC_OK)
                CU_DEBUG("Unable to set property filter: %d", s.rc);

        id = get_fq_devid((char *)host, dev->id);

        CMSetProperty(inst, "InstanceID",
                      (CMPIValue *)id, CMPI_chars);

        CMSetProperty(inst, "ResourceType",
                      (CMPIValue *)&type, CMPI_uint16);

        if (dev->type == CIM_RES_TYPE_DISK) {
                s = set_disk_rasd_params(broker, ref, dev, inst);
        } else if (dev->type == CIM_RES_TYPE_NET) {
                s = set_net_rasd_params(broker, ref, dev, inst);
                if ((s.rc == CMPI_RC_OK) &&
                     (dev->dev.net.vsi.vsi_type != NULL))
                        s = set_net_vsi_rasd_params(broker,
                                                    ref,
                                                    dev->dev.net.vsi,
                                                    inst);

        } else if (dev->type == CIM_RES_TYPE_MEM) {
                const char *units = "KiloBytes";

                CMSetProperty(inst, "AllocationUnits",
                              (CMPIValue *)units, CMPI_chars);
                CMSetProperty(inst, "VirtualQuantity",
                              (CMPIValue *)&dev->dev.mem.size, CMPI_uint64);
                CMSetProperty(inst, "Reservation",
                              (CMPIValue *)&dev->dev.mem.size, CMPI_uint64);
                CMSetProperty(inst, "Limit",
                              (CMPIValue *)&dev->dev.mem.maxsize, CMPI_uint64);
        } else if (dev->type == CIM_RES_TYPE_PROC) {
                set_proc_rasd_params(broker, ref, dev, host, inst);
        } else if (dev->type == CIM_RES_TYPE_GRAPHICS) {
                s = set_graphics_rasd_params(dev, inst, host, CLASSNAME(ref));
        } else if (dev->type == CIM_RES_TYPE_INPUT) {
                s = set_input_rasd_params(dev, inst);
        } else if (dev->type == CIM_RES_TYPE_CONSOLE) {
                s = set_console_rasd_params(dev, inst);
        }

        /* FIXME: Put the HostResource in place */

        free(id);

        return inst;
}

CMPIStatus get_rasd_by_name(const CMPIBroker *broker,
                            const CMPIObjectPath *reference,
                            const char *name,
                            const uint16_t type,
                            const char **properties,
                            CMPIInstance **_inst)
{
        CMPIInstance *inst = NULL;
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int ret;
        char *host = NULL;
        char *devid = NULL;
        virConnectPtr conn = NULL;
        struct virt_device *dev = NULL;

        conn = connect_by_classname(broker, CLASSNAME(reference), &s);
        if (conn == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance");
                goto out;
        }

        ret = parse_fq_devid((char *)name, &host, &devid);
        if (ret != 1) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_NOT_FOUND,
                           "No such instance (%s)",
                           name);
                goto out;
        }

        dev = find_dev(conn, type, host, devid);
        if (!dev) {
                virt_set_status(broker, &s,
                                CMPI_RC_ERR_NOT_FOUND,
                                conn,
                                "No such instance (%s)",
                                name);
                goto out;
        }

        inst = rasd_from_vdev(broker, dev, host, reference, properties);
        if (inst == NULL)
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to set instance properties");
        else
                *_inst = inst;

        cleanup_virt_devices(&dev, 1);

 out:
        virConnectClose(conn);
        free(host);
        free(devid);

        return s;
}

CMPIStatus get_rasd_by_ref(const CMPIBroker *broker,
                           const CMPIObjectPath *reference,
                           const char **properties,
                           CMPIInstance **_inst)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;
        const char *name = NULL;
        uint16_t type;

        if (cu_get_str_path(reference, "InstanceID", &name) != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Missing InstanceID");
                goto out;
        }

        if (res_type_from_rasd_classname(CLASSNAME(reference), &type) != CMPI_RC_OK) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to determine RASD type");
                goto out;
        }

        s = get_rasd_by_name(broker, reference, name, type, properties, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        s = cu_validate_ref(broker, reference, inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        *_inst = inst;

 out:
        return s;
}

CMPIrc res_type_from_rasd_classname(const char *cn, uint16_t *type)
{
       char *base = NULL;
       CMPIrc rc = CMPI_RC_ERR_FAILED;

       base = class_base_name(cn);
       if (base == NULL)
                goto out;

       if ((STREQ(base, "DiskResourceAllocationSettingData")) ||
                (STREQ(base, "DiskPoolResourceAllocationSettingData")))
               *type = CIM_RES_TYPE_DISK;
       else if ((STREQ(base, "NetResourceAllocationSettingData")) ||
                (STREQ(base, "NetPoolResourceAllocationSettingData")))
               *type = CIM_RES_TYPE_NET;
       else if (STREQ(base, "ProcResourceAllocationSettingData"))
               *type = CIM_RES_TYPE_PROC;
       else if (STREQ(base, "MemResourceAllocationSettingData"))
               *type = CIM_RES_TYPE_MEM;
       else if (STREQ(base, "GraphicsResourceAllocationSettingData"))
               *type = CIM_RES_TYPE_GRAPHICS;
       else if (STREQ(base, "InputResourceAllocationSettingData"))
               *type = CIM_RES_TYPE_INPUT;
       else if (STREQ(base, "StorageVolumeResourceAllocationSettingData"))
               *type = CIM_RES_TYPE_IMAGE;
       else if (STREQ(base, "ConsoleResourceAllocationSettingData"))
               *type = CIM_RES_TYPE_CONSOLE;
       else
               goto out;

       rc = CMPI_RC_OK;

 out:
       free(base);

       return rc;
}

CMPIrc rasd_classname_from_type(uint16_t type, const char **classname)
{
        CMPIrc rc = CMPI_RC_OK;

        switch(type) {
        case CIM_RES_TYPE_MEM:
                *classname = "MemResourceAllocationSettingData";
                break;
        case CIM_RES_TYPE_PROC:
                *classname = "ProcResourceAllocationSettingData";
                break;
        case CIM_RES_TYPE_NET:
                *classname = "NetResourceAllocationSettingData";
                break;
        case CIM_RES_TYPE_DISK:
                *classname = "DiskResourceAllocationSettingData";
                break;
        case CIM_RES_TYPE_GRAPHICS:
                *classname = "GraphicsResourceAllocationSettingData";
                break;
        case CIM_RES_TYPE_CONSOLE:
                *classname = "ConsoleResourceAllocationSettingData";
                break;
        case CIM_RES_TYPE_INPUT:
                *classname = "InputResourceAllocationSettingData";
                break;
        default:
                rc = CMPI_RC_ERR_FAILED;
        }

        return rc;
}

CMPIrc pool_rasd_classname_from_type(uint16_t type, const char **classname)
{
        CMPIrc rc = CMPI_RC_OK;

        switch(type) {
        case CIM_RES_TYPE_DISK:
                *classname = "DiskPoolResourceAllocationSettingData";
                break;
        case CIM_RES_TYPE_NET:
                *classname = "NetPoolResourceAllocationSettingData";
                break;
        default:
                rc = CMPI_RC_ERR_FAILED;
        }

        return rc;
}

static CMPIStatus _get_rasds(const CMPIBroker *broker,
                             const CMPIObjectPath *reference,
                             const virDomainPtr dom,
                             const uint16_t type,
                             const char **properties,
                             struct inst_list *list)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        int count;
        int i;
        struct virt_device *devs = NULL;
        const char *host = NULL;

        count = get_devices(dom, &devs, type, 0);
        if (count <= 0)
                goto out;

        /* Bit hackish, but for proc we need to cut list down to one. */
        if (type == CIM_RES_TYPE_PROC) {
                struct virt_device *tmp_dev = NULL;
                tmp_dev = virt_device_dup(&devs[count - 1]);
                if (tmp_dev == NULL) {
                        cu_statusf(broker, &s,
                                   CMPI_RC_ERR_FAILED,
                                   "Failed to allocate memory for proc RASD");
                        goto out;
                }

                tmp_dev->id = strdup("proc");

                cleanup_virt_devices(&devs, count);
                devs = tmp_dev;
                count = 1;
        }

        host = virDomainGetName(dom);
        if (host == NULL) {
                cu_statusf(broker, &s,
                           CMPI_RC_ERR_FAILED,
                           "Failed to get domain name");

                goto out;
        }

        for (i = 0; i < count; i++) {
                CMPIInstance *dev = NULL;

                dev = rasd_from_vdev(broker,
                                     &devs[i],
                                     host,
                                     reference,
                                     properties);
                if (dev)
                        inst_list_add(list, dev);
        }

 out:
        cleanup_virt_devices(&devs, count);
        return s;
}

static CMPIStatus _enum_rasds(const CMPIBroker *broker,
                              const CMPIObjectPath *reference,
                              const virDomainPtr dom,
                              const uint16_t type,
                              const char **properties,
                              struct inst_list *list)
{
        CMPIStatus s;
        int i;

        if (type == CIM_RES_TYPE_ALL) {
                for (i=0; i<CIM_RES_TYPE_COUNT; i++)
                        s = _get_rasds(broker,
                                       reference,
                                       dom,
                                       cim_res_types[i],
                                       properties,
                                       list);
        }
        else
                s = _get_rasds(broker,
                               reference,
                               dom,
                               type,
                               properties,
                               list);

        return s;
}

CMPIStatus enum_rasds(const CMPIBroker *broker,
                      const CMPIObjectPath *ref,
                      const char *domain,
                      const uint16_t type,
                      const char **properties,
                      struct inst_list *list)
{
        virConnectPtr conn = NULL;
        virDomainPtr *domains = NULL;
        int count = 1;
        int i;
        CMPIStatus s = {CMPI_RC_OK, NULL};

        conn = connect_by_classname(_BROKER, CLASSNAME(ref), &s);
        if (conn == NULL)
                goto out;

        if (domain) {
                domains = calloc(1, sizeof(virDomainPtr));
                domains[0] = virDomainLookupByName(conn, domain);
        }
        else
                count = get_domain_list(conn, &domains);

        for (i = 0; i < count; i++) {
                _enum_rasds(broker,
                            ref,
                            domains[i],
                            type,
                            properties,
                            list);
                virDomainFree(domains[i]);
        }

 out:
        virConnectClose(conn);
        free(domains);

        return s;
}

static CMPIStatus return_enum_rasds(const CMPIObjectPath *ref,
                                    const CMPIResult *results,
                                    const char **properties,
                                    const bool names_only)
{
        struct inst_list list;
        CMPIStatus s;
        uint16_t type;

        inst_list_init(&list);

        if (res_type_from_rasd_classname(CLASSNAME(ref), &type) != CMPI_RC_OK) {
                cu_statusf(_BROKER, &s,
                           CMPI_RC_ERR_FAILED,
                           "Unable to determine RASD type");
                goto out;
        }

        s = enum_rasds(_BROKER, ref, NULL,
                       type, properties, &list);
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

static CMPIStatus EnumInstanceNames(CMPIInstanceMI *self,
                                    const CMPIContext *context,
                                    const CMPIResult *results,
                                    const CMPIObjectPath *reference)
{
        return return_enum_rasds(reference, results, NULL, true);
}

static CMPIStatus EnumInstances(CMPIInstanceMI *self,
                                const CMPIContext *context,
                                const CMPIResult *results,
                                const CMPIObjectPath *reference,
                                const char **properties)
{

        return return_enum_rasds(reference, results, properties, false);
}

static CMPIStatus GetInstance(CMPIInstanceMI *self,
                              const CMPIContext *context,
                              const CMPIResult *results,
                              const CMPIObjectPath *ref,
                              const char **properties)
{
        CMPIStatus s = {CMPI_RC_OK, NULL};
        CMPIInstance *inst = NULL;

        s = get_rasd_by_ref(_BROKER, ref, properties, &inst);
        if (s.rc != CMPI_RC_OK)
                goto out;

        CMReturnInstance(results, inst);

 out:
        return s;
}

DEFAULT_CI();
DEFAULT_MI();
DEFAULT_DI();
DEFAULT_INST_CLEANUP();
DEFAULT_EQ();

STD_InstanceMIStub(,
                   Virt_RASD,
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
