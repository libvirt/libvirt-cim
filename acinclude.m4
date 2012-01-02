dnl
dnl $Id$
dnl
dnl 
dnl © Copyright IBM Corp. 2004, 2005, 2007

dnl CHECK_CMPI: Check for CMPI headers and set the CPPFLAGS
dnl with the -I<directory>
dnl 
dnl CHECK_PEGASUS_2_3_2: Check for Pegasus 2.3.2 and set
dnl the HAVE_PEGASUS_2_3_2 
dnl flag 
dnl

AC_DEFUN([CHECK_PEGASUS_2_3_2],
	[
	AC_MSG_CHECKING(for Pegasus 2.3.2)
	if which cimserver > /dev/null 2>&1 
	then
	   test_CIMSERVER=`cimserver -v`
	fi	
	if test "$test_CIMSERVER" == "2.3.2"; then
		AC_MSG_RESULT(yes)		
		AC_DEFINE_UNQUOTED(HAVE_PEGASUS_2_3_2,1,[Defined to 1 if Pegasus 2.3.2 is used])
	else
		AC_MSG_RESULT(no)

	fi
	]
)

dnl
dnl CHECK_PEGASUS_2_4: Check for Pegasus 2.4 and set the
dnl the -DPEGASUS_USE_EXPERIMENTAL_INTERFACES flag
dnl
AC_DEFUN([CHECK_PEGASUS_2_4],
	[
	AC_MSG_CHECKING(for Pegasus 2.4)
	if which cimserver > /dev/null 2>&1 
	then
	   test_CIMSERVER=`cimserver -v`
	fi	
	if test "$test_CIMSERVER" == "2.4"; then
		AC_MSG_RESULT(yes)		
		CPPFLAGS="$CPPFLAGS -DPEGASUS_USE_EXPERIMENTAL_INTERFACES"
		AC_DEFINE_UNQUOTED(HAVE_PEGASUS_2_4,1,[Defined to 1 if Pegasus 2.4 is used])
	else
		AC_MSG_RESULT(no)

	fi
	]
)
	
	
dnl
dnl Helper functions
dnl
AC_DEFUN([_CHECK_CMPI],
	[
	AC_MSG_CHECKING($1)
	AC_TRY_LINK(
	[
		#include <cmpimacs.h>
		#include <cmpidt.h>
		#include <cmpift.h>
	],
	[
		CMPIBroker broker;
		CMPIStatus status = {CMPI_RC_OK, NULL};
		CMPIString *s = CMNewString(&broker, "TEST", &status);
	],
	[
		have_CMPI=yes
	],
	[
		have_CMPI=no
	])

])

dnl
dnl The main function to check for CMPI headers
dnl Modifies the CPPFLAGS with the right include directory and sets
dnl the 'have_CMPI' to either 'no' or 'yes'
dnl

AC_DEFUN([CHECK_CMPI],
	[
	AC_MSG_NOTICE([checking for CMPI headers...])
	dnl Check just with the standard include paths
	CMPI_CPP_FLAGS="$CPPFLAGS"
	_CHECK_CMPI(standard)
	if test "$have_CMPI" == "yes"; then
		dnl The standard include paths worked.
		AC_MSG_RESULT(yes)
	else
		AC_MSG_RESULT(no)
	  _DIRS_="/usr/include/cmpi \
                  /usr/local/include/cmpi \
		  $PEGASUS_ROOT/src/Pegasus/Provider/CMPI \
		  /opt/tog-pegasus/include/Pegasus/Provider/CMPI \
		  /usr/include/Pegasus/Provider/CMPI \
		  /usr/include/openwbem \
		  /usr/sniacimom/include"
	  for _DIR_ in $_DIRS_
	  do
		 _cppflags=$CPPFLAGS
		 _include_CMPI="$_DIR_"
		 CPPFLAGS="$CPPFLAGS -I$_include_CMPI"
		 _CHECK_CMPI($_DIR_)
		 if test "$have_CMPI" == "yes"; then
		  	dnl Found it
		  	AC_MSG_RESULT(yes)
			dnl Save the new -I parameter  
			CMPI_CPP_FLAGS="$CPPFLAGS"
			break
                 else
                        AC_MSG_RESULT(no)
		 fi
	         CPPFLAGS=$_cppflags
	  done
	fi	
	CPPFLAGS="$CMPI_CPP_FLAGS"	
	if test "$have_CMPI" == "no"; then
		AC_MSG_ERROR(Cannot find CMPI headers files.)
	fi
	]
)

dnl
dnl The check for the CMPI provider directory
dnl Sets the PROVIDERDIR  variable.
dnl

AC_DEFUN([CHECK_PROVIDERDIR],
	[
	AC_MSG_NOTICE([checking for CMPI provider directory])
	_DIRS="$libdir/cmpi"
	save_exec_prefix=${exec_prefix}
	save_prefix=${prefix}
	if test xNONE == x${prefix}; then
		prefix=/usr/local
	fi
	if test xNONE == x${exec_prefix}; then
		exec_prefix=$prefix
	fi
	for _dir in $_DIRS
	do
		_xdir=`eval echo $_dir`
		AC_MSG_CHECKING([for $_dir])
		if test -d $_xdir ; then
		  dnl Found it
		  AC_MSG_RESULT(yes)
		  if test x"$PROVIDERDIR" == x ; then
			PROVIDERDIR=$_dir
		  fi
		  break
		fi
        done
	if test x"$PROVIDERDIR" == x ; then
		PROVIDERDIR="$libdir"/cmpi
		AC_MSG_RESULT(implied: $PROVIDERDIR)
	fi
	exec_prefix=$save_exec_prefix
	prefix=$save_prefix
	]
)

dnl
dnl The "check" for the CIM server type
dnl Sets the CIMSERVER variable.
dnl

AC_DEFUN([CHECK_CIMSERVER],
	[
	AC_MSG_NOTICE([checking for CIM servers])
	if test x"$CIMSERVER" = x
	then
	_SERVERS="sfcbd cimserver owcimomd"
	_SAVE_PATH=$PATH
	PATH=/usr/sbin:/usr/local/sbin:$PATH
	for _name in $_SERVERS
	do
	 	AC_MSG_CHECKING( $_name )
		for _path in `echo $PATH | sed "s/:/ /g"`
                do
		  if test -f $_path/$_name ; then
		  dnl Found it
		  AC_MSG_RESULT(yes)
		  if test x"$CIMSERVER" == x ; then
			case $_name in
			   sfcbd) CIMSERVER=sfcb;;
			   cimserver) CIMSERVER=pegasus;;
			   owcimomd) CIMSERVER=openwbem;;
			esac
		  fi
		  break;
                  fi
                done
          if test x"$CIMSERVER" == x; then
            AC_MSG_RESULT(no)
          else
            break
          fi
        done
	PATH=$_SAVE_PATH
	if test x"$CIMSERVER" == x ; then
		CIMSERVER=sfcb
		AC_MSG_WARN([CIM server implied: $CIMSERVER])
	fi
	fi
	# Cross platform only needed for sfcb currently
	if test $CIMSERVER = sfcb
	then
		AC_REQUIRE([AC_CANONICAL_HOST])
		AC_CHECK_SIZEOF(int)
	 	case "$build_cpu" in
			i*86) case "$host_cpu" in
				powerpc*) if test $ac_cv_sizeof_int == 4
					  then
						REGISTER_FLAGS="-X P32"
					  fi ;;
			      esac ;;
		esac
		AC_SUBST(REGISTER_FLAGS)
	fi
	]
)

AC_DEFUN([CHECK_LIBXML2],
	[
	PKG_CHECK_MODULES([LIBXML], [libxml-2.0])
	CPPFLAGS="$CPPFLAGS $LIBXML_CFLAGS"
	LDFLAGS="$LDFLAGS $LIBXML_LDFLAGS"
	])

AC_DEFUN([CHECK_LIBCU],
	[
	PKG_CHECK_MODULES([LIBCU], [libcmpiutil >= 0.1])
	CPPFLAGS="$CPPFLAGS $LIBCU_CFLAGS"
	LDFLAGS="$LDFLAGS $LIBCU_LIBS"
	])

AC_DEFUN([CHECK_LIBVIRT],
	[
	PKG_CHECK_MODULES([LIBVIRT], [libvirt >= 0.9.0])
	AC_SUBST([LIBVIRT_CFLAGS])
	AC_SUBST([LIBVIRT_LIBS])
	CPPFLAGS="$CPPFLAGS $LIBVIRT_CFLAGS"
	LDFLAGS="$LDFLAGS $LIBVIRT_LIBS"
	])

AC_DEFUN([CHECK_LIBUUID],
	[
	PKG_CHECK_MODULES([LIBUUID], [uuid >= 1.41.2],
			  [LIBUUID_FOUND=yes], [LIBUUID_FOUND=no])
	if test "$LIBUUID_FOUND" = "no" ; then
	    PKG_CHECK_MODULES([LIBUUID], [uuid],
			      [LIBUUID_FOUND=yes], [LIBUUID_FOUND=no])
	    if test "$LIBUUID_FOUND" = "no" ; then
		AC_MSG_ERROR([libuuid development files required])
	    else
		LIBUUID_INCLUDEDIR=$(pkg-config --variable=includedir uuid)
		LIBUUID_CFLAGS+=" -I$LIBUUID_INCLUDEDIR/uuid "
	    fi
	fi
	AC_SUBST([LIBUUID_CFLAGS])
	AC_SUBST([LIBUUID_LIBS])
	CPPFLAGS="$CPPFLAGS $LIBUUID_CFLAGS"
	LDFLAGS="$LDFLAGS $LIBUUID_LIBS"
	])

AC_DEFUN([CHECK_LIBCONFIG],
	[
	PKG_CHECK_MODULES([LIBCONFIG], [libconfig],
			  [LIBCONFIG_FOUND=yes], [LIBCONFIG_FOUND=no])
	if test "$LIBCONFIG_FOUND" = "yes" ; then
        AC_DEFINE(HAVE_LIBCONFIG, 1, [Define if libconfig development files were found])
    fi
	AC_SUBST([LIBCONFIG_CFLAGS])
	AC_SUBST([LIBCONFIG_LIBS])
	CPPFLAGS="$CPPFLAGS $LIBCONFIG_CFLAGS"
	LDFLAGS="$LDFLAGS $LIBCONFIG_LIBS"
	])

# A convenience macro that spits out a fail message for a particular test
#
# AC_CHECK_FAIL($LIBNAME,$PACKAGE_SUGGEST,$URL,$EXTRA)
#

AC_DEFUN([AC_CHECK_FAIL],
    [
    ERR_MSG=`echo -e "- $1 not found!\n"`
    if test "x" != "x$4"; then
        ERR_MSG=`echo -e "$ERR_MSG\n- $4"`
    fi
    if test "x$2" != "x"; then
        ERR_MSG=`echo -e "$ERR_MSG\n- Try installing the $2 package\n"`
    fi
    if test "x$3" != "x"; then
        ERR_MSG=`echo -e "$ERR_MSG\n- or get the latest software from $3\n"`
    fi
    
    AC_MSG_ERROR(
!
************************************************************
$ERR_MSG
************************************************************
)
    ]
)

#
# Check for void EnableIndications return
#
AC_DEFUN([CHECK_IND_VOID], [
	AH_TEMPLATE([CMPI_EI_VOID],
		    [Defined if return type of EnableIndications 
		     should be void])
	AC_MSG_CHECKING([return type for indications])
	CFLAGS_TMP=$CFLAGS
	CFLAGS="-Werror"
	AC_TRY_COMPILE([
		  #include <cmpift.h>
		  static void ei(CMPIIndicationMI *mi, const CMPIContext *c) {
		       return;
		  }
		],[ 
		  struct _CMPIIndicationMIFT ft;
		  ft.enableIndications = ei;
		  return 0;
	], [
		echo "void"
		AC_DEFINE_UNQUOTED([CMPI_EI_VOID], [yes])
	], [
		echo "CMPIStatus"
	])
	CFLAGS=$CFLAGS_TMP
])

#
# Define max mem.
#
AC_DEFUN([DEFINE_MAXMEM],
    [
    AC_DEFINE_UNQUOTED([MAX_MEM], $1, [Max memory for a guest.])
    ]
)

#
# Define disk pool config.
#
AC_DEFUN([DEFINE_DISK_CONFIG],
    [
    AC_DEFINE_UNQUOTED([DISK_POOL_CONFIG], "$1", [Disk pool config filepath.])
    DISK_POOL_CONFIG=$1
    AC_SUBST(DISK_POOL_CONFIG)
    ]
)

#
# Define info store location.
#
AC_DEFUN([DEFINE_INFO_STORE],
    [
    AC_DEFINE_UNQUOTED([INFO_STORE], "$1", [Info store location])
    INFO_STORE=$1
    AC_SUBST(INFO_STORE)
    ]
)

AC_DEFUN([SET_CSET],
	[
	if test -d .hg && test -x $(which hg); then
	   cs='-DLIBVIRT_CIM_CS=\"`hg id -i`\"'
	   rv='-DLIBVIRT_CIM_RV=\"`hg id -n`\"'
	elif test -f .changeset; then
	   cset=$(cat .changeset)
	   revn=$(cat .revision)
	   cs="-DLIBVIRT_CIM_CS=\\\"$cset\\\""
	   rv="-DLIBVIRT_CIM_RV=\\\"$revn\\\""
	else
	   cs='-DLIBVIRT_CIM_CS=\"Unknown\"'
	   rv='-DLIBVIRT_CIM_RV=\"0\"'
 	fi

	CFLAGS="$CFLAGS $cs $rv"
	]
)
