#include <stdio.h>
#include <inttypes.h>

#include <getopt.h>

#include <libvirt/libvirt.h>

#include "device_parsing.h"
#include "capability_parsing.h"
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

static char *get_ostype(struct domain *dom)
{
        if (dom->type == DOMAIN_XENPV) {
                return dom->os_info.pv.type;
        } else if ((dom->type == DOMAIN_XENFV) ||
                   (dom->type == DOMAIN_KVM) ||
                   (dom->type == DOMAIN_QEMU)) {
                return dom->os_info.fv.type;
        } else if (dom->type == DOMAIN_LXC) {
                return dom->os_info.lxc.type;
        } else {
                return NULL;
        }
}

static char *get_domaintype(struct domain *dom)
{
        if (dom->type == DOMAIN_XENPV || dom->type == DOMAIN_XENFV) {
                return "xen";
        } else if (dom->type == DOMAIN_KVM) {
                return "kvm";
        } else if (dom->type == DOMAIN_QEMU) {
                return "qemu";
        } else if (dom->type == DOMAIN_LXC) {
                return "lxc";
        } else {
                return NULL;
        }
}

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
        print_value(d, "Bus Type", dev->dev.disk.bus_type);
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
        print_value(d, "Graphics Port", dev->dev.graphics.dev.vnc.port);
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

static void print_cap_domain_info(struct cap_domain_info *capgdiinfo,
                                  FILE *d)
{
        struct cap_machine capgminfo;
        int i;

        if (capgdiinfo == NULL)
                return;

        if (capgdiinfo->emulator != NULL)
                print_value(d, "  Emulator", capgdiinfo->emulator);
        if (capgdiinfo->loader != NULL)
                print_value(d, "  Loader", capgdiinfo->loader);
        for (i = 0; i < capgdiinfo->num_machines; i++) {
                capgminfo = capgdiinfo->machines[i];
                fprintf(d, "  Machine name : %-15s  canonical name : %s\n",
                        capgminfo.name, capgminfo.canonical_name);
        }
        fprintf(d, "\n");
}

static void print_cap_domains(struct cap_arch caparchinfo,
                              FILE *d)
{
        struct cap_domain capgdinfo;
        int i;
        for (i = 0; i < caparchinfo.num_domains; i++) {
                capgdinfo = caparchinfo.domains[i];
                print_value(d, "  Type", capgdinfo.typestr);
                print_cap_domain_info(&capgdinfo.guest_domain_info, d);
        }
}

static void print_cap_arch(struct cap_arch caparchinfo,
                           FILE *d)
{
        print_value(d, " Arch name", caparchinfo.name);
        fprintf(d, " Arch wordsize : %i\n", caparchinfo.wordsize);
        fprintf(d, "\n  -- Default guest domain settings --\n");
        print_cap_domain_info(&caparchinfo.default_domain_info, d);
        fprintf(d, "  -- Guest domains (%i) --\n", caparchinfo.num_domains);
        print_cap_domains(caparchinfo, d);
}

static void print_cap_guest(struct cap_guest *capginfo,
                            FILE *d)
{
        print_value(d, "Guest OS type", capginfo->ostype);
        print_cap_arch(capginfo->arch, d);
}

static void print_cap_host(struct cap_host *caphinfo,
                           FILE *d)
{
        print_value(d, "Host CPU architecture", caphinfo->cpu_arch);
}

static void print_capabilities(struct capabilities *capsinfo,
                               FILE *d)
{
        int i;
        fprintf(d, "\n### Capabilities ###\n");
        fprintf(d, "-- Host --\n");
        print_cap_host(&capsinfo->host, d);
        fprintf(d, "\n-- Guest (%i) --\n", capsinfo->num_guests);
        for (i = 0; i < capsinfo->num_guests; i++)
                print_cap_guest(&capsinfo->guests[i], d);
}

static int capinfo_for_dom(const char *uri,
                           struct domain *dominfo,
                           struct capabilities **capsinfo)
{
        virConnectPtr conn = NULL;
        char *caps_xml = NULL;
        int ret = 0;

        conn = virConnectOpen(uri);
        if (conn == NULL) {
                printf("Unable to connect to libvirt\n");
                goto out;
        }

        ret = get_capabilities(conn, capsinfo);

 out:
        free(caps_xml);
        virConnectClose(conn);

        return ret;
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

        printf("XML:\n%s", xml);
        free(xml);
        fclose(file);

        return ret;
}

static void usage(void)
{
        printf("xml_parse_test -f [FILE | -] [--xml]\n"
               "xml_parse_test -d domain [--uri URI] [--xml] [--cap]\n"
               "\n"
               "-f,--file FILE    Parse domain XML from file (or stdin if -)\n"
               "-d,--domain DOM   Display dominfo for a domain from libvirt\n"
               "-u,--uri URI      Connect to libvirt with URI\n"
               "-x,--xml          Dump generated XML instead of summary\n"
               "-c,--cap          Display the libvirt default capability values for the specified domain\n"
               "-h,--help         Display this help message\n");
}

int main(int argc, char **argv)
{
        int c;
        char *domain = NULL;
        char *uri = "xen";
        char *file = NULL;
        bool xml = false;
        bool cap = false;
        struct domain *dominfo = NULL;
        struct capabilities *capsinfo = NULL;
        struct cap_domain_info *capgdinfo = NULL;
        int ret;

        static struct option lopts[] = {
                {"domain", 1, 0, 'd'},
                {"uri",    1, 0, 'u'},
                {"xml",    0, 0, 'x'},
                {"file",   1, 0, 'f'},
                {"cap",    0, 0, 'c'},
                {"help",   0, 0, 'h'},
                {0,        0, 0, 0}};

        while (1) {
                int optidx = 0;

                c = getopt_long(argc, argv, "d:u:f:xch", lopts, &optidx);
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

                case 'c':
                        cap = true;
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

        if (cap && file == NULL) {
                ret = capinfo_for_dom(uri, dominfo, &capsinfo);
                if (ret == 0) {
                        printf("Unable to get capsinfo\n");
                        return 3;
                } else {
                        print_capabilities(capsinfo, stdout);
                        const char *os_type = get_ostype(dominfo);
                        const char *dom_type = get_domaintype(dominfo);
                        const char *def_arch = get_default_arch(capsinfo, os_type);

                        fprintf(stdout, "-- KVM is used: %s\n\n", (use_kvm(capsinfo)?"true":"false"));
                        fprintf(stdout, "-- For all following default OS type=%s\n", os_type);
                        fprintf(stdout, "-- Default Arch : %s\n", def_arch);

                        fprintf(stdout,
                                "-- Default Machine for arch=NULL : %s\n",
                                get_default_machine(capsinfo, os_type, NULL, NULL));
                        fprintf(stdout,
                                "-- Default Machine for arch=%s and domain type=NULL : %s\n",
                                def_arch,
                                get_default_machine(capsinfo, os_type, def_arch, NULL));
                        fprintf(stdout,
                                "-- Default Machine for arch=%s and domain type=%s : %s\n",
                                def_arch, dom_type,
                                get_default_machine(capsinfo, os_type, def_arch, dom_type));
                        fprintf(stdout,
                                "-- Default Machine for arch=NULL and domain type=%s : %s\n",
                                dom_type,
                                get_default_machine(capsinfo, os_type, NULL, dom_type));

                        fprintf(stdout,
                                "-- Default Emulator for arch=NULL : %s\n",
                                get_default_emulator(capsinfo, os_type, NULL, NULL));
                        fprintf(stdout,
                                "-- Default Emulator for arch=%s and domain type=NULL : %s\n",
                                def_arch,
                                get_default_emulator(capsinfo, os_type, def_arch, NULL));
                        fprintf(stdout,
                                "-- Default Emulator for arch=%s and domain type=%s : %s\n",
                                def_arch, dom_type,
                                get_default_emulator(capsinfo, os_type, def_arch, dom_type));
                        fprintf(stdout,
                                "-- Default Emulator for arch=NULL and domain type=%s : %s\n",
                                dom_type,
                                get_default_emulator(capsinfo, os_type, NULL, dom_type));

                        fprintf(stdout, "\n-- Default Domain Search for: \n"
                                "guest type=hvm - guest arch=* - guest domain type=kvm\n");
                        capgdinfo = findDomainInfo(capsinfo, "hvm", NULL, "kvm");
                        print_cap_domain_info(capgdinfo, stdout);

                        fprintf(stdout, "-- Default Domain Search for: \n"
                                "guest type=* - guest arch=* - guest domain type=*\n");
                        capgdinfo = findDomainInfo(capsinfo, NULL, NULL, NULL);
                        print_cap_domain_info(capgdinfo, stdout);

                        cleanup_capabilities(&capsinfo);
                }
        } else if (cap) {
                printf("Need a data source (--domain) to get default capabilities\n");
                return 4;
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
