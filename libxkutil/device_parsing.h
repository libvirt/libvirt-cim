/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *  Guolian Yun <yunguol@cn.ibm.com>
 *  Jay Gagnon <grendel@linux.vnet.ibm.com>
 *  Zhengang Li <lizg@cn.ibm.com>
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
#ifndef __DEVICE_PARSING_H
#define __DEVICE_PARSING_H

#include <stdint.h>
#include <libvirt/libvirt.h>

#include "../src/svpc_types.h"

struct disk_device {
        char *type;
        char *device;
        char *driver;
        char *source;
        char *virtual_dev;
};

struct net_device {
        char *type;
        char *mac;
        char *bridge;
};

struct mem_device {
        uint64_t size;
        uint64_t maxsize;
};

struct virt_device {
        enum {VIRT_DEV_UNKNOWN,
              VIRT_DEV_NET = CIM_RASD_TYPE_NET,
              VIRT_DEV_DISK = CIM_RASD_TYPE_DISK,
              VIRT_DEV_MEM = CIM_RASD_TYPE_MEM,
              VIRT_DEV_VCPU = CIM_RASD_TYPE_PROC,
        } type;
        union {
                struct disk_device disk;
                struct net_device net;
                struct mem_device mem;
                struct _virVcpuInfo vcpu;
        } dev;
        char *id;
};

struct os_info {
        char *type;
        char *kernel;
        char *initrd;
        char *cmdline;
};

struct domain {
        char *name;
        char *uuid;
        char *bootloader;
        char *bootloader_args;
        struct os_info os_info;
        int on_poweroff;
        int on_reboot;
        int on_crash;

        struct virt_device *dev_mem;
        int dev_mem_ct;

        struct virt_device *dev_net;
        int dev_net_ct;

        struct virt_device *dev_disk;
        int dev_disk_ct;

        struct virt_device *dev_vcpu;
        int dev_vcpu_ct;
};

struct virt_device *virt_device_dup(struct virt_device *dev);

int get_dominfo(virDomainPtr dom, struct domain **dominfo);

void cleanup_dominfo(struct domain **dominfo);

int get_disk_devices(virDomainPtr dom, struct virt_device **list);
int get_net_devices(virDomainPtr dom, struct virt_device **list);
int get_vcpu_devices(virDomainPtr dom, struct virt_device **list);
int get_mem_devices(virDomainPtr dom, struct virt_device **list);

void cleanup_virt_device(struct virt_device *dev);
void cleanup_virt_devices(struct virt_device **devs, int count);

char *get_fq_devid(char *host, char *_devid);
int parse_fq_devid(char *devid, char **host, char **device);

int attach_device(virDomainPtr dom, struct virt_device *dev);
int detach_device(virDomainPtr dom, struct virt_device *dev);

#endif

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
