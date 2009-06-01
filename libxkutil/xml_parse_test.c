#include <stdio.h>
#include <inttypes.h>

#include <getopt.h>

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

#if 0
static void print_u32(FILE *d, const char *name, uint32_t val)
{
        fprintf(d, "%-15s: %" PRIu32 "\n", name, val);
}
#endif

static void print_os(struct domain *dom,
                     FILE *d)
{
        int i;

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

                for (i = 0; i < dom->os_info.fv.bootlist_ct; i++) {
                        print_value(d, "Boot", dom->os_info.fv.bootlist[i]);
                }
        } else if ((dom->type == DOMAIN_KVM) || (dom->type == DOMAIN_QEMU)) {
                print_value(d, "Domain Type", "KVM/QEMU");
                print_value(d, "Type", dom->os_info.fv.type);
                print_value(d, "Loader", dom->os_info.fv.loader);

                for (i = 0; i < dom->os_info.fv.bootlist_ct; i++) {
                        print_value(d, "Boot", dom->os_info.fv.bootlist[i]);
                }
        } else if (dom->type == DOMAIN_LXC) {
                print_value(d, "Init", dom->os_info.lxc.init);
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
        print_value(d, "Source", dev->dev.net.source);
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
        print_u64(d, "Virtual CPU", dev->dev.vcpu.quantity);
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

static char *read_from_file(FILE *file)
{
        char *xml = NULL;
        char buf[256];
        int size = 0;

        while (fgets(buf, sizeof(buf) - 1, file) != NULL) {
                xml = realloc(xml, size + strlen(buf) + 1);
                if (xml == NULL) {
                        printf("Out of memory\n");
                        return NULL;
                }

                strcat(xml, buf);
                size += strlen(buf);
        }

        return xml;
}

static int dominfo_from_dom(const char *uri,
                            const char *domain,
                            struct domain **d)
{
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;
        int ret = 0;

        conn = virConnectOpen(uri);
        if (conn == NULL) {
                printf("Unable to connect to libvirt\n");
                goto out;
        }

        dom = virDomainLookupByName(conn, domain);
        if (dom == NULL) {
                printf("Unable to find domain `%s'\n", domain);
                goto out;
        }

        ret = get_dominfo(dom, d);

 out:
        virDomainFree(dom);
        virConnectClose(conn);

        return ret;
}

static int dominfo_from_file(const char *fname, struct domain **d)
{
        char *xml;
        FILE *file;
        int ret;

        if (fname[0] == '-')
                file = stdin;
        else
                file = fopen(fname, "r");

        if (file == NULL) {
                printf("Unable to open `%s'\n", fname);
                return 0;
        }

        xml = read_from_file(file);
        if (xml == NULL) {
                printf("Unable to read from `%s'\n", fname);
                return 0;
        }

        ret = get_dominfo_from_xml(xml, d);

        free(xml);
        fclose(file);

        printf("XML:\n%s", xml);

        return ret;
}

static void usage(void)
{
        printf("xml_parse_test -f [FILE | -] [--xml]\n"
               "xml_parse_test -d domain [--uri URI] [--xml]\n"
               "\n"
               "-f,--file FILE    Parse domain XML from file (or stdin if -)\n"
               "-d,--domain DOM   Display dominfo for a domain from libvirt\n"
               "-u,--uri URI      Connect to libvirt with URI\n"
               "-x,--xml          Dump generated XML instead of summary\n"
               "-h,--help         Display this help message\n");
}

int main(int argc, char **argv)
{
        int c;
        char *domain = NULL;
        char *uri = "xen";
        char *file = NULL;
        bool xml = false;
        struct domain *dominfo = NULL;
        int ret;

        static struct option lopts[] = {
                {"domain", 1, 0, 'd'},
                {"uri",    1, 0, 'u'},
                {"xml",    0, 0, 'x'},
                {"file",   1, 0, 'f'},
                {"help",   0, 0, 'h'},
                {0,        0, 0, 0}};

        while (1) {
                int optidx = 0;

                c = getopt_long(argc, argv, "d:u:f:xh", lopts, &optidx);
                if (c == -1)
                        break;

                switch (c) {
                case 'd':
                        domain = optarg;
                        break;

                case 'u':
                        uri = optarg;
                        break;

                case 'f':
                        file = optarg;
                        break;

                case 'x':
                        xml = true;
                        break;

                case '?':
                case 'h':
                        usage();
                        return c == '?';

                };
        }

        if (file != NULL)
                ret = dominfo_from_file(file, &dominfo);
        else if (domain != NULL)
                ret = dominfo_from_dom(uri, domain, &dominfo);
        else {
                printf("Need a data source (--domain or --file)\n");
                return 1;
        }

        if (ret == 0) {
                printf("Unable to get dominfo\n");
                return 2;
        }

        if (xml)
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
