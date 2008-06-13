# (C) Copyright IBM Corp. 2005

AC_INIT(libvirt CMPI provider, 0.5, danms@us.ibm.com, libvirt-cim)

AC_CONFIG_SRCDIR([src/Virt_ComputerSystem.c])

AC_CHECK_HEADERS([stdarg.h errno.h])
AC_CHECK_FUNCS([popen pclose fgets asprintf vfprintf fprintf snprintf sscanf])

CPPFLAGS="$CPPFLAGS -DCMPI_VERSION=100"
#CPPFLAGS="$CPPFLAGS -DSBLIM_DEBUG"

CC_WARNINGS="\
-Wall \
-Wmissing-prototypes \
-Wmissing-declarations \
-Wstrict-prototypes \
-Wpointer-arith \
-Wformat=2 \
-Wformat-security \
-Wformat-nonliteral \
-Wno-format-y2k \
-Wcast-align \
-Wno-unused-value"

CFLAGS="$CFLAGS -D_FORTIFY_SOURCE=2 -D_GNU_SOURCE"

# Configure command line options
AC_ARG_VAR([CIMSERVER],[the target CIM server (pegasus|sfcb|openwbem|sniacimom).
])
AC_ARG_VAR([PROVIDERDIR],[the directory where the CMPI providers will be install
ed.])

AC_ARG_WITH([kvm-max-nics],
	    AC_HELP_STRING([--with-kvm-max-nics=X],
			   [Maximum NICs to advertise for KVM]),
	    [test "x$withval" != "x" && KVM_MAX_NICS=$withval],
	    [KVM_MAX_NICS=8])
AC_SUBST(KVM_MAX_NICS)
AC_DEFINE_UNQUOTED(KVM_MAX_NICS, $KVM_MAX_NICS, [Maximum NIC limit for KVM])

AC_ARG_WITH([xen-max-nics],
	    AC_HELP_STRING([--with-xen-max-nics=X],
			   [Maximum NIC limit to advertise for Xen (unless known by the provider)]),
            [test "x$withval" != "x" && XEN_MAX_NICS=$withval],
	    [XEN_MAX_NICS=8])
AC_SUBST(XEN_MAX_NICS)
AC_DEFINE_UNQUOTED(XEN_MAX_NICS, $XEN_MAX_NICS, [Maximum NIC limit for Xen])

AC_ARG_WITH([namespace],
            AC_HELP_STRING([--with-namespace=ns],
                           [CIM namespace to use, default root/virt]),
            [test "x$withval" != "x" && CIM_VIRT_NS="$withval"],
            [CIM_VIRT_NS="root/virt"])
AC_SUBST(CIM_VIRT_NS)
AC_DEFINE_UNQUOTED(CIM_VIRT_NS, "$CIM_VIRT_NS", [Namespace for provider source])

AC_ARG_WITH([diskconfig], 
            [  --with-diskconfig=PATH  Set config file for disk pool. (default=/etc/libvirt/diskpool.conf)], 
            [DEFINE_DISK_CONFIG($with_diskconfig)], 
            [DEFINE_DISK_CONFIG(/etc/libvirt/diskpool.conf)]
)
AC_ARG_WITH([maxmem], 
            [  --with-maxmem=FOO	  Set max memory (KB) for a guest.], 
            [DEFINE_MAXMEM($with_maxmem)], 
            [DEFINE_MAXMEM(4194304)]
)

AC_ARG_WITH(html-dir,
            AC_HELP_STRING([--with-html-dir=path],
                           [path to html directory, default $datadir/doc/$PACKAGE-$VERSION/html]),
            [test "x$withval" != "x" && HTML_DIR="$withval"],
	    [HTML_DIR='$(datadir)/doc/$(PACKAGE)-$(VERSION)/html'])
AC_SUBST(HTML_DIR)

AC_ARG_WITH([migrate_check_timeout],
	    [  --with-migrate_check_timeout=SECS  Max runtime allowed for external migration checks],
	    [test "x$withval" != "x" && MIG_CHECKS_TIMEOUT="$withval"],
	    [MIG_CHECKS_TIMEOUT=10])
AC_SUBST(MIG_CHECKS_TIMEOUT)
AC_DEFINE_UNQUOTED(MIG_CHECKS_TIMEOUT, $MIG_CHECKS_TIMEOUT, [External migration check timeout])

AC_ARG_WITH([migrate_check_dir],
	    [  --with-migrate_check_dir=dir  Location of external migration checks],
	    [test "x$withval" != "x" && MIG_CHECKS_DIR="$withval"],
	    [MIG_CHECKS_DIR=/usr/libexec/libvirt-cim/extchecks])
AC_SUBST(MIG_CHECKS_DIR)
AC_DEFINE_UNQUOTED(MIG_CHECKS_DIR, "$MIG_CHECKS_DIR", [External migration check timeout])

if test -x /usr/lib64/xen/bin/qemu-dm; then
   default_xen_emulator=/usr/lib64/xen/bin/qemu-dm
elif test -x /usr/lib/xen/bin/qemu-dm; then
   default_xen_emulator=/usr/lib/xen/bin/qemu-dm
else
   default_xen_emulator=/bin/false
fi

AC_ARG_WITH([xen_emulator],
	    [  --with-xen_emulator=emu  Location of Xen FullVirt emulator],
	    [test "x$withval" != "x" && XEN_EMULATOR="$withval"],
	    [XEN_EMULATOR=$default_xen_emulator])
AC_SUBST(XEN_EMULATOR)
AC_DEFINE_UNQUOTED(XEN_EMULATOR, "$XEN_EMULATOR", [Location of Xen FullVirt emulator])

# Autogenerate the autoconf header file to store build settings
AC_CONFIG_HEADER([config.h])

topdir=`pwd`
AC_SUBST(topdir)

AC_PATH_PROG(XSLTPROC, xsltproc, /usr/bin/xsltproc)

# Autogenerate the Makefile
AC_CONFIG_FILES([
    base_schema/install_base_schema.sh
    base_schema/Makefile
    libxkutil/Makefile
    libxkutil/tests/Makefile
    src/Makefile
    src/tests/Makefile
    doc/Makefile
    Makefile 
    ])

AM_INIT_AUTOMAKE

AC_PROG_CC
AC_C_CONST
AC_PROG_LIBTOOL
AC_PROG_INSTALL
AC_CHECK_HEADERS([stdlib.h string.h unistd.h])
AC_HEADER_STDC

# Check for the required CMPI header files (this macro is defined in acinclude.m4)
CHECK_CMPI
CHECK_IND_VOID

# Check for where to install CMPI libraries (this macro is defined in acinclude.m4)
CHECK_PROVIDERDIR

# Check for presense of a CIM server (this macro is defined in acinclude.m4)
CHECK_CIMSERVER

# Check for presence of libraries
CHECK_LIBVIRT
CHECK_LIBXML2
CHECK_LIBCU

CFLAGS_STRICT="-Werror"

AC_ARG_ENABLE([werror],
    [  --enable-werror         enable werror on builds [[default=yes]]],
    [if test "x$enableval" = "xno"; then
        CFLAGS_STRICT=""
     fi])

AC_SUBST(CFLAGS_STRICT)

CFLAGS="$CFLAGS $CC_WARNINGS"
SET_CSET

# Display configuration options
echo "----------------------------------------------------------"
echo "Configuration for $PACKAGE complete."
echo ""
echo "The following configuration options have been selected:"
echo "   CIMSERVER: " $CIMSERVER
echo "   PROVIDERDIR: " $PROVIDERDIR
echo "   CPPFLAGS:" $CPPFLAGS
echo
echo "You can override these values by setting the corresponding"
echo "configure command-line option(s); e.g."
echo "   ./configure PROVIDERDIR=/home/me/lib/sblim/"
echo "----------------------------------------------------------"

# Generate configure scripts for the Makefile
AC_OUTPUT(libvirt-cim.spec)

echo "You may now run make"
