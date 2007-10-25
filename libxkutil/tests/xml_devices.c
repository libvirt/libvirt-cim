/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
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
#include <stdio.h>

#include "../device_parsing.h"
#include "../device_parsing.c"

#define STREQ(x, y) (strcmp(x, y) == 0)

static void print_disk(struct disk_device *dev)
{
        fprintf(stderr,
                "type:    %s\n"
                "device:  %s\n"
                "driver:  %s\n"
                "source:  %s\n"
                "vdev:    %s\n",
                dev->type, dev->device, dev->driver,
                dev->source, dev->virtual_dev);
}

static void print_net(struct net_device *dev)
{
        fprintf(stderr, 
                "type:    %s\n"
                "mac:     %s\n",
                dev->type, dev->mac);
}

static void print_dev(struct virt_device *dev)
{
        if (dev->type == VIRT_DEV_DISK)
                print_disk(&dev->dev.disk);
        else if (dev->type == VIRT_DEV_NET)
                print_net(&dev->dev.net);
        else
                printf("ERROR: Unknown device type %i\n", dev->type);
}

int main(int argc, char **argv)
{
        struct virt_device *devs;
        int count;
        char *conf1 = 
                "<domain type='xen' id='0'>\n"
                "  <name>sles10foo</name>\n"
                "  <uuid>7c42b545ffc4eb4441d1be3936516de3</uuid>\n"
                "  <bootloader>/usr/lib/xen/boot/domUloader.py</bootloader>\n"
                "  <os>\n"
                "    <type>linux</type>\n"
                "    <kernel>/var/lib/xen/tmp/kernel.0F29se</kernel>\n"
                "    <initrd>/var/lib/xen/tmp/ramdisk.qGK5v5</initrd>\n"
                "    <cmdline>TERM=xterm </cmdline>\n"
                "  </os>\n"
                "  <memory>524288</memory>\n"
                "  <vcpu>1</vcpu>\n"
                "  <on_poweroff>destroy</on_poweroff>\n"
                "  <on_reboot>restart</on_reboot>\n"
                "  <on_crash>destroy</on_crash>\n"
                "  <devices>\n"
                "    <interface type='ethernet'>\n"
                "      <mac address='00:16:3e:62:d1:bd'/>\n"
                "    </interface>\n"
                "    <disk type='file' device='disk'>\n"
                "      <driver name='file'/>\n"
                "      <source file='/var/lib/xen/images/sles10/disk0'/>\n"
                "      <target dev='xvda'/>\n"
                "    </disk>\n"
                "    <graphics type='vnc' port='-1'/>\n"
                "  </devices>\n"
                "</domain>\n";
        char *conf2 = 
                "<domain type='xen' id='0'>\n"
                "  <name>sles10foo</name>\n"
                "  <uuid>7c42b545ffc4eb4441d1be3936516de3</uuid>\n"
                "  <bootloader>/usr/lib/xen/boot/domUloader.py</bootloader>\n"
                "  <os>\n"
                "    <type>linux</type>\n"
                "    <kernel>/var/lib/xen/tmp/kernel.0F29se</kernel>\n"
                "    <initrd>/var/lib/xen/tmp/ramdisk.qGK5v5</initrd>\n"
                "    <cmdline>TERM=xterm </cmdline>\n"
                "  </os>\n"
                "  <memory>524288</memory>\n"
                "  <vcpu>1</vcpu>\n"
                "  <on_poweroff>destroy</on_poweroff>\n"
                "  <on_reboot>restart</on_reboot>\n"
                "  <on_crash>destroy</on_crash>\n"
                "  <devices>\n"
                "    <disk type='file' device='disk'>\n"
                "      <driver name='file'/>\n"
                "      <source file='/var/lib/xen/images/foo/bar'/>\n"
                "      <target dev='xvdb'/>\n"
                "    </disk>\n"
                "    <interface type='ethernet'>\n"
                "      <mac address='00:16:3e:62:d1:bd'/>\n"
                "    </interface>\n"
                "    <interface type='ethernet'>\n"
                "      <mac address='00:11:22:33:44:55'/>\n"
                "    </interface>\n"
                "    <disk type='file' device='disk'>\n"
                "      <driver name='file'/>\n"
                "      <source file='/var/lib/xen/images/sles10/disk0'/>\n"
                "      <target dev='xvda'/>\n"
                "    </disk>\n"
                "    <graphics type='vnc' port='-1'/>\n"
                "  </devices>\n"
                "</domain>\n";
        char *conf3 = "snarf";
        char *conf4 = 
                "<domain type='xen' id='0'>\n"
                "  <devices>\n"
                "    <disk type='file' device='disk'>\n"
                "      <driver name='file'/>\n"
                "      <target dev='xvdb'/>\n"
                "    </disk>\n"
                "    <interface type='ethernet'>\n"
                "      <mac address='00:16:3e:62:d1:bd'/>\n"
                "    </interface>\n"
                "    <interface type='ethernet'>\n"
                "    </interface>\n"
                "    <disk type='file' device='disk'>\n"
                "      <driver name='file'/>\n"
                "      <source file='/disk0'/>\n"
                "      <target dev='xvda'/>\n"
                "    </disk>\n"
                "    <graphics type='vnc' port='-1'/>\n"
                "  </devices>\n"
                "</domain>\n";

        count = parse_devices(conf1, &devs, VIRT_DEV_DISK);
        if (count != 1) {
                fprintf(stderr, "Failed to find disk in config\n");
                return 1;
        }

        if (! (STREQ(devs->dev.disk.type, "file")   &&
               STREQ(devs->dev.disk.device, "disk") &&
               STREQ(devs->dev.disk.driver, "file") &&
               STREQ(devs->dev.disk.source, "/var/lib/xen/images/sles10/disk0") &&
               STREQ(devs->dev.disk.virtual_dev, "xvda"))) {
                fprintf(stderr, "Failed to validate domain contents:\n");
                print_dev(devs);
                return 1;
        }
            
        cleanup_virt_device(devs);
        free(devs);

        count = parse_devices(conf2, &devs, VIRT_DEV_DISK);
        if (count != 2) {
                fprintf(stderr, "Failed to find both disks in config\n");
                return 1;
        }

        if (! (STREQ(devs[0].dev.disk.type, "file")   &&
               STREQ(devs[0].dev.disk.device, "disk") &&
               STREQ(devs[0].dev.disk.driver, "file") &&
               STREQ(devs[0].dev.disk.source, "/var/lib/xen/images/foo/bar") &&
               STREQ(devs[0].dev.disk.virtual_dev, "xvdb"))) {
                fprintf(stderr, "Failed to validate domain contents:\n");
                print_dev(&devs[0]);
                return 1;
        }

        if (! (STREQ(devs[1].dev.disk.type, "file")   &&
               STREQ(devs[1].dev.disk.device, "disk") &&
               STREQ(devs[1].dev.disk.driver, "file") &&
               STREQ(devs[1].dev.disk.source, "/var/lib/xen/images/sles10/disk0") &&
               STREQ(devs[1].dev.disk.virtual_dev, "xvda"))) {
                fprintf(stderr, "Failed to validate domain contents:\n");
                print_dev(&devs[1]);
                return 1;
        }

        cleanup_virt_devices(&devs, count);
        free(devs);

        count = parse_devices(conf4, &devs, VIRT_DEV_DISK);
        if (count != 1) {
                fprintf(stderr, "Failed to bypass non-valid disk node.%d\n", count);
                return 1;
        }

        if (! (STREQ(devs[0].dev.disk.type, "file")     &&
               STREQ(devs[0].dev.disk.device, "disk")   &&
               STREQ(devs[0].dev.disk.driver, "file")   &&
               STREQ(devs[0].dev.disk.source, "/disk0") &&
               STREQ(devs[0].dev.disk.virtual_dev, "xvda"))) {
                fprintf(stderr, "Failed to validate valide disk info:\n");
                print_dev(&devs[0]);
                return 1;
        }

        cleanup_virt_devices(&devs, count);
        free(devs);

        count = parse_devices(conf3, &devs, VIRT_DEV_DISK);
        if ((count != 0) || (devs != NULL)) {
                fprintf(stderr, "Failed to fail on bad config string\n");
                return 1;
        }

        
        count = parse_devices(conf1, &devs, VIRT_DEV_NET);
        if (count != 1) {
                fprintf(stderr, "Failed to find interface in config\n");
                return 1;
        }

        if (! (STREQ(devs->dev.net.type, "ethernet") &&
               STREQ(devs->dev.net.mac, "00:16:3e:62:d1:bd"))) {
                fprintf(stderr, "Failed to validate interface config:\n");
                print_dev(devs);
        }

        cleanup_virt_device(devs);
        free(devs);

        count = parse_devices(conf2, &devs, VIRT_DEV_NET);
        if (count != 2) {
                fprintf(stderr, "Failed to find both interfaces in config\n");
                return 1;
        }

        if (! (STREQ(devs[0].dev.net.type, "ethernet") &&
               STREQ(devs[0].dev.net.mac, "00:16:3e:62:d1:bd"))) {
                fprintf(stderr, "Failed to validate interface config:\n");
                print_dev(&devs[0]);
                return 1;
        }
        
        if (! (STREQ(devs[1].dev.net.type, "ethernet") &&
               STREQ(devs[1].dev.net.mac, "00:11:22:33:44:55"))) {
                fprintf(stderr, "Failed to validate interface config:\n");
                print_dev(&devs[1]);
                return 1;
        }

        cleanup_virt_devices(&devs, count);
        free(devs);

        count = parse_devices(conf4, &devs, VIRT_DEV_NET);
        if (count !=1) {
                fprintf(stderr, "Failed to bypass non-valid interface info.\n");
                return 1;
        }

        if (! (STREQ(devs[0].dev.net.type, "ethernet")  &&
               STREQ(devs[0].dev.net.mac, "00:16:3e:62:d1:bd"))) {
                fprintf(stderr, "Failed to validate interface config:\n");
                print_dev(&devs[0]);
                return 1;
        }
        cleanup_virt_devices(&devs, count);
        free(devs);

        count = parse_devices(conf3, &devs, VIRT_DEV_NET);
        if ((count != 0) || (devs != NULL)) {
                fprintf(stderr, "Failed to fail on bad config string\n");
                return 1;
        }
 
        return 0;
}

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
