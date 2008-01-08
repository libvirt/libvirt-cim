#include <stdio.h>
#include <inttypes.h>

#include <libvirt/libvirt.h>

#include "device_parsing.h"
#include "xmlgen.h"

static void print_value(FILE *d, const char *name, const char *val)
{
        fprintf(d, "%-15s: %s\n", name, val);
}

static void print_u64(FILE *d, const char *name, uint64_t val)
{
        fprintf(d, "%-15s: %" PRIu64 "\n", name, val);
}

static void print_u32(FILE *d, const char *name, uint32_t val)
{
        fprintf(d, "%-15s: %" PRIu32 "\n", name, val);
}

static void print_os(struct domain *dom,
                     FILE *d)
{

        if (dom->type == DOMAIN_XENPV) {
                print_value(d, "Domain Type", "Xen PV");
                print_value(d, "Type", dom->os_info.pv.type);
                print_value(d, "Kernel", dom->os_info.pv.kernel);
                print_value(d, "Ramdisk", dom->os_info.pv.initrd);
                print_value(d, "Args", dom->os_info.pv.cmdline);
        } else if (dom->type == DOMAIN_XENFV) {
                print_value(d, "Domain Type", "Xen FV");
                print_value(d, "Type", dom->os_info.fv.type);
                print_value(d, "Loader", dom->os_info.fv.loader);
                print_value(d, "Boot", dom->os_info.fv.boot);

        } else if (dom->type == DOMAIN_KVM) {
                print_value(d, "Domain Type", "KVM/QEMU");
                print_value(d, "Type", dom->os_info.fv.type);
                print_value(d, "Loader", dom->os_info.fv.loader);
                print_value(d, "Boot", dom->os_info.fv.boot);
        } else {
                fprintf(d, "[ Unknown domain type %i ]\n", dom->type);
        }
}

static void print_dominfo(struct domain *dominfo,
                          FILE *d)
{
        print_value(d, "Name", dominfo->name);
        print_value(d, "UUID", dominfo->uuid);
        print_value(d, "Bootloader", dominfo->bootloader);
        print_value(d, "  args", dominfo->bootloader_args);

        fprintf(d, "Actions:       : P:%i R:%i C:%i\n",
                dominfo->on_poweroff,
                dominfo->on_reboot,
                dominfo->on_crash);

        print_os(dominfo, d);
}

static void print_dev_mem(struct virt_device *dev,
                          FILE *d)
{
        print_u64(d, "Memory", dev->dev.mem.size);
        print_u64(d, "Maximum", dev->dev.mem.maxsize);
}
static void print_dev_net(struct virt_device *dev,
                          FILE *d)
{
        print_value(d, "Type", dev->dev.net.type);
        print_value(d, "MAC", dev->dev.net.mac);
}

static void print_dev_disk(struct virt_device *dev,
                           FILE *d)
{
        print_value(d, "Type", dev->dev.disk.type);
        print_value(d, "Device", dev->dev.disk.device);
        print_value(d, "Driver", dev->dev.disk.driver);
        print_value(d, "Source", dev->dev.disk.source);
        print_value(d, "Virt Device", dev->dev.disk.virtual_dev);
}

static void print_dev_vcpu(struct virt_device *dev,
                           FILE *d)
{
        print_u32(d, "Virtual CPU", dev->dev.vcpu.number);
}

static void print_dev_emu(struct virt_device *dev,
                          FILE *d)
{
        print_value(d, "Emulator", dev->dev.emu.path);
}

static void print_dev_graphics(struct virt_device *dev,
                               FILE *d)
{
        print_value(d, "Graphics Type", dev->dev.graphics.type);
        print_value(d, "Graphics Port", dev->dev.graphics.port);
}

static void print_devices(struct domain *dominfo,
                          FILE *d)
{
        int i;

        fprintf(d, "\n-- Memory (%i) --\n", dominfo->dev_mem_ct);
        for (i = 0; i < dominfo->dev_mem_ct; i++)
                print_dev_mem(&dominfo->dev_mem[i], d);

        fprintf(d, "\n-- Network (%i) --\n", dominfo->dev_net_ct);
        for (i = 0; i < dominfo->dev_net_ct; i++)
                print_dev_net(&dominfo->dev_net[i], d);

        fprintf(d, "\n-- Disk (%i) --\n", dominfo->dev_disk_ct);
        for (i = 0; i < dominfo->dev_disk_ct; i++)
                print_dev_disk(&dominfo->dev_disk[i], d);

        fprintf(d, "\n-- VCPU (%i) -- \n", dominfo->dev_vcpu_ct);
        for (i = 0; i < dominfo->dev_vcpu_ct; i++)
                print_dev_vcpu(&dominfo->dev_vcpu[i], d);

        if ((dominfo->type != DOMAIN_XENPV) && (dominfo->dev_emu)) {
                fprintf(d, "\n-- Emulator --\n");
                print_dev_emu(dominfo->dev_emu, d);
        }

        if (dominfo->dev_graphics) {
                fprintf(d, "\n-- Graphics --\n");
                print_dev_graphics(dominfo->dev_graphics, d);
        }
}

static void print_domxml(struct domain *dominfo,
                         FILE *d)
{
        char *xml;

        xml = system_to_xml(dominfo);
        if (xml == NULL)
                printf("Failed to create system XML\n");
        else
                printf("%s\n", xml);
}

int main(int argc, char **argv)
{
        virConnectPtr conn;
        virDomainPtr dom;
        struct domain *dominfo;

        if (argc < 2) {
                printf("Usage: %s domain [URI] [xml]\n", argv[0]);
                return 1;
        }

        if (argc > 2)
                conn = virConnectOpen(argv[2]);
        else
                conn = virConnectOpen("xen:///");
        if (conn == NULL) {
                printf("Unable to connect to libvirt\n");
                return 2;
        }

        dom = virDomainLookupByName(conn, argv[1]);
        if (dom == NULL) {
                printf("Unable to lookup domain `%s'\n", argv[1]);
                return 3;
        }

        if (get_dominfo(dom, &dominfo) == 0) {
                printf("Failed to parse domain info\n");
                return 4;
        }

        printf("Parsed domain info\n");

        if ((argc > 3) && (argv[3][0] == 'x'))
                print_domxml(dominfo, stdout);
        else {
                print_dominfo(dominfo, stdout);
                print_devices(dominfo, stdout);
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
