# (C) Copyright IBM Corp. 2005

AC_INIT(libvirt CMPI provider, 0.1, danms@us.ibm.com, libvirt-cim)

AC_CONFIG_SRCDIR([src/Virt_ComputerSystem.c])
#AC_CONFIG_FILES([xen-kvm-cmpi.spec])

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
AC_ARG_VAR([LIBVIRTDIR],[the directory where the libvirt package is installed.])
AC_ARG_VAR([TESTSUITEDIR],[the directory where the SBLIM testsuite is installed.
])

AC_ARG_WITH([diskconfig], 
            [  --with-diskconfig=PATH  Set config file for disk pool. (default=/tmp/diskpool.conf)], 
            [DEFINE_DISK_CONFIG($with_diskconfig)], 
            [DEFINE_DISK_CONFIG(/tmp/diskpool.conf)]
)
AC_ARG_WITH([maxmem], 
            [  --with-maxmem=FOO	  Set max memory (MB) for a guest.], 
            [DEFINE_MAXMEM($with_maxmem)], 
            [DEFINE_MAXMEM(4096)]
)

# Autogenerate the autoconf header file to store build settings
AC_CONFIG_HEADER([config.h])

# We need a absolute here for beam to work
topdir=`pwd`
AC_SUBST(topdir)

AC_CHECK_PROG([found_beam_compile],[beam_compile],[yes])

BEAMFILES="parser-messages BEAM_STATS BEAM-complaints MISC_ERRORS *.beam"
AC_SUBST(BEAMFILES)

# Autogenerate runbeam-c, which must be executable
#AC_CONFIG_FILES([beam/runbeam-c],[chmod a+x beam/runbeam-c])

# Autogenerate the Makefile
AC_CONFIG_FILES([
    libxkutil/Makefile
    libxkutil/tests/Makefile
    src/Makefile
    src/tests/Makefile
    doc/Makefile
    Makefile 
    ])

AM_INIT_AUTOMAKE

# Check for the required C compiler
AC_PROG_CC

# Check for LEX/YACC
AC_PROG_LEX
AC_PROG_YACC

# Check if the C compiler supports 'const'
AC_C_CONST

# Check for the required libtool
AC_PROG_LIBTOOL

# Check for the required install program 
AC_PROG_INSTALL

# Check for some common required headers
AC_CHECK_HEADERS([stdlib.h string.h unistd.h])

# Check if the system headers conform to ANSI C
AC_HEADER_STDC

# Check for specific add on libraries that we know we'll need
#  - libuuid
#AC_CHECK_LIB([uuid], [uuid_generate], [have_uuid=yes], [
#    AC_CHECK_FAIL(libuuid,e2fsprogs-devel)
#    ])

#  - libxenstore
#AC_CHECK_LIB([xenstore], [xs_read], [have_xen=yes], [
#    AC_CHECK_FAIL(libxenstore,xen-devel)
#    ])

# Check for the required CMPI header files (this macro is defined in acinclude.m4)
CHECK_CMPI
CHECK_IND_VOID

# Check for where to install CMPI libraries (this macro is defined in acinclude.m4)
CHECK_PROVIDERDIR

# Check for presense of a CIM server (this macro is defined in acinclude.m4)
CHECK_CIMSERVER

# Check for presense of libxml2 (this macro defined in acinclude.m4)
AC_CHECK_PROG([found_xml2_config],[xml2-config],[yes])
AM_CONDITIONAL([found_xml2_config], [test x$found_xml2_config = xyes])
CHECK_LIBXML2

# Check for presense of libvirt (this macro is defined in acinclude.m4)
CHECK_LIBVIRT

# Check for the SBLIM testsuite (this macro is defined in acinclude.m4)
CHECK_TESTSUITE
if test x"$TESTSUITEDIR" == x; then
   TESTSUITE_SUPPORT=Disabled
else
   TESTSUITE_SUPPORT=Enabled
fi

#
# Enable the beam source analysis system.  This requires that beam's
# bin directory is in your path
#

CFLAGS_STRICT="-Werror"

CHECK_LIBCU

AC_ARG_ENABLE([werror],
    [  --enable-werror         enable werror on builds [[default=yes]]],
    [if test "x$enableval" = "xno"; then
        CFLAGS_STRICT=""
     fi])

AC_SUBST(CFLAGS_STRICT)

AC_ARG_ENABLE([beam],
    [  --enable-beam           use beam to compile source [[default=no]]],
    [if test "x$enableval" = "xyes"; then
        if test "x$found_beam_compile" = "xyes"; then 
            AC_MSG_NOTICE(Building beam configuration) 
            beam_configure --c $CC -o beam/compiler_c_config.tcl
            CC="\$(top_srcdir)/beam/runbeam-c"
        else
            AC_CHECK_FAIL(beam,beam,http://w3.eda.ibm.com/beam,
                Can not find beam_compile in path! Can not enable beam analysis!
)
        fi
    fi],
    )

CFLAGS="$CFLAGS $CC_WARNINGS"

# Display configuration options
echo "----------------------------------------------------------"
echo "Configuration for $PACKAGE complete."
echo ""
echo "The following configuration options have been selected:"
echo "   CIMSERVER: " $CIMSERVER
echo "   PROVIDERDIR: " $PROVIDERDIR
echo "   LIBVIRTDIR: " $LIBVIRTDIR
echo "   TESTSUITE_SUPPORT: " $TESTSUITE_SUPPORT
if test x"$TESTSUITEDIR" != x; then
   echo "   TESTSUITEDIR: " $TESTSUITEDIR
fi
echo "   CPPFLAGS:" $CPPFLAGS
echo
echo "You can override these values by setting the corresponding"
echo "configure command-line option(s); e.g."
echo "   ./configure PROVIDERDIR=/home/me/lib/sblim/"
echo "----------------------------------------------------------"

# Generate configure scripts for the Makefile
AC_OUTPUT(libvirt-cim.spec)

echo "You may now run make"

