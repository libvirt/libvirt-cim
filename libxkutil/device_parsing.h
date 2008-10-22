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
#include <stdbool.h>
#include <libvirt/libvirt.h>

#include "../src/svpc_types.h"

struct disk_device {
        char *type;
        char *device;
        char *driver;
        char *source;
        char *virtual_dev;
        enum {DISK_UNKNOWN, DISK_PHY, DISK_FILE, DISK_FS} disk_type;
        bool readonly;
        bool shareable;
};

struct net_device {
        char *type;
        char *mac;
        char *source;
};

struct mem_device {
        uint64_t size;
        uint64_t maxsize;
};

struct vcpu_device {
        uint64_t quantity;
        uint32_t weight;
        uint64_t limit;
};

struct emu_device {
        char *path;
};

struct graphics_device {
        char *type;
        char *port;
        char *host;
        char *keymap;
};

struct input_device {
        char *type;
        char *bus;
};

struct virt_device {
        uint16_t type;
        union {
                struct disk_device disk;
                struct net_device net;
                struct mem_device mem;
                struct vcpu_device vcpu;
                struct emu_device emu;
                struct graphics_device graphics;
                struct input_device input;
        } dev;
        char *id;
};

struct pv_os_info {
        char *type;
        char *kernel;
        char *initrd;
        char *cmdline;
};

struct fv_os_info {
        char *type; /* Should always be 'hvm' */
        char *loader;
        char *boot;
};

struct lxc_os_info {
        char *type; /* Should always be 'exe' */
        char *init;
};

struct domain {
        enum { DOMAIN_XENPV, DOMAIN_XENFV, DOMAIN_KVM, DOMAIN_LXC } type;
        char *name;
        char *typestr; /*xen, kvm, etc */
        char *uuid;
        char *bootloader;
        char *bootloader_args;

        union {
                struct pv_os_info pv;
                struct fv_os_info fv;
                struct lxc_os_info lxc;
        } os_info;

        int on_poweroff;
        int on_reboot;
        int on_crash;

        struct virt_device *dev_graphics;
        struct virt_device *dev_emu;

        struct virt_device *dev_input;
        int dev_input_ct;

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

int disk_type_from_file(const char *path);

int get_dominfo(virDomainPtr dom, struct domain **dominfo);
int get_dominfo_from_xml(const char *xml, struct domain **dominfo);

void cleanup_dominfo(struct domain **dominfo);

int get_devices(virDomainPtr dom, struct virt_device **list, int type);

void cleanup_virt_device(struct virt_device *dev);
void cleanup_virt_devices(struct virt_device **devs, int count);

char *get_fq_devid(char *host, char *_devid);
int parse_fq_devid(const char *devid, char **host, char **device);

int attach_device(virDomainPtr dom, struct virt_device *dev);
int detach_device(virDomainPtr dom, struct virt_device *dev);
int change_device(virDomainPtr dom, struct virt_device *dev);

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
