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
		dnl AC_MSG_RESULT(yes)
	],
	[
		have_CMPI=no
		dnl AC_MSG_RESULT(no)
	])

])

dnl
dnl The main function to check for CMPI headers
dnl Modifies the CPPFLAGS with the right include directory and sets
dnl the 'have_CMPI' to either 'no' or 'yes'
dnl

AC_DEFUN([CHECK_CMPI],
	[
	AC_MSG_CHECKING(for CMPI headers)
	dnl Check just with the standard include paths
	CMPI_CPP_FLAGS="$CPPFLAGS"
	_CHECK_CMPI(standard)
	if test "$have_CMPI" == "yes"; then
		dnl The standard include paths worked.
		AC_MSG_RESULT(yes)
	else
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
		 fi
	         CPPFLAGS=$_cppflags
	  done
	fi	
	CPPFLAGS="$CMPI_CPP_FLAGS"	
	if test "$have_CMPI" == "no"; then
		AC_MSG_ERROR(no. Sorry cannot find CMPI headers files.)
	fi
	]
)

dnl
dnl The check for the CMPI provider directory
dnl Sets the PROVIDERDIR  variable.
dnl

AC_DEFUN([CHECK_PROVIDERDIR],
	[
	AC_MSG_CHECKING(for CMPI provider directory)
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
		AC_MSG_CHECKING( $_dir )
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
	AC_MSG_CHECKING(for CIM servers)
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
           done
	PATH=$_SAVE_PATH
	if test x"$CIMSERVER" == x ; then
		CIMSERVER=sfcb
		AC_MSG_RESULT(implied: $CIMSERVER)
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

dnl
dnl The check for the libxml2 library
dnl Sets the LIBXML2DIR variable
dnl

AC_DEFUN([_CHECK_LIBXML2],
[
   AC_MSG_CHECKING($1)
   AC_TRY_LINK(
   [
      #include <libxml/tree.h>
   ],
   [
      xmlNodePtr nodeptr;
   ],
   [
      have_LIBXML2=yes
      dnl AC_MSG_RESULT(yes)
   ],
   [
      have_LIBXML2=no
      dnl AC_MSG_RESULT(no)
   ])
])

AC_DEFUN([CHECK_LIBXML2],
	[
	AC_MSG_CHECKING(for libxml2 package)
	CPPFLAGS="$CPPFLAGS `xml2-config --cflags` "
	LDFLAGS="$LDFLAGS `xml2-config --libs` "
	dnl The standard include paths worked.
	_CHECK_LIBXML2(standard)	
	if test "$have_LIBXML2" == "no"; then
        	AC_MSG_ERROR(no. The required libxml2 package is missing.)
	fi 
	]
)

AC_DEFUN([_CHECK_LIBCU_PC],
	[
	if pkg-config --exists libcmpiutil; then
		CPPFLAGS="$CPPFLAGS `pkg-config --cflags libcmpiutil`"
		LDFLAGS="$LDFLAGS `pkg-config --libs libcmpiutil`"
		found_libcu=yes
	fi
	]
)

AC_DEFUN([_CHECK_LIBCU_NOPC],
	[
	DIRS="/usr /usr/local"
	for dir in $DIRS; do
		if test -f "${dir}/include/libcmpiutil/libcmpiutil.h"; then
			CPPFLAGS="$CPPFLAGS -I${dir}/include/libcmpiutil"
			LDFLAGS="$LDFLAGS -lcmpiutil -L${dir}/lib"
			found_libcu=yes
		fi
	done
	]
)

AC_DEFUN([CHECK_LIBCU],
	[
	_CHECK_LIBCU_PC
	if test "x$found_libcu" != "xyes"; then
		_CHECK_LIBCU_NOPC
	fi
	AC_CHECK_LIB(cmpiutil, cu_check_args, [], [
		     AC_MSG_ERROR(libcmpiutil not found)
                     ])
	AC_CHECK_HEADER([libcmpiutil.h], [], [
                        AC_MSG_ERROR([libcmpiutil.h not found])
		     ])
	]
)

dnl
dnl The check for the libvirt library
dnl Sets the LIBVIRTDIR variable
dnl

AC_DEFUN([_CHECK_LIBVIRT],
[
   AC_MSG_CHECKING($1)
   AC_TRY_LINK(
   [
      #include <libvirt.h>
      #include <virterror.h>
   ],
   [
      virConnectPtr connectPtr;
   ],
   [
      have_LIBVIRT=yes
      dnl AC_MSG_RESULT(yes)
   ],
   [
      have_LIBVIRT=no
      dnl AC_MSG_RESULT(no)
   ])
])

AC_DEFUN([CHECK_LIBVIRT],
	[
	AC_MSG_CHECKING(for libvirt package)
        LIBVIRT_CPP_FLAGS="$CPPFLAGS"
	dnl The standard include paths worked.
	_CHECK_LIBVIRT(standard)
	if test x"$LIBVIRTDIR" == x ; then
		_DIRS_="/usr/include/libvirt \
        		/usr/local/include/libvirt"
	else
		_DIRS_="$LIBVIRTDIR/include/libvirt"
	fi
	for _DIR_ in $_DIRS_
	do
		_cppflags=$CPPFLAGS
		_include_LIBVIRT="$_DIR_"
		CPPFLAGS="$CPPFLAGS -I$_include_LIBVIRT"
		_CHECK_LIBVIRT($_DIR_)
		if test "$have_LIBVIRT" == "yes"; then
		 	dnl Found it
		  	AC_MSG_RESULT(yes)
			dnl Save the new -I parameter  
			LIBVIRT_CPP_FLAGS="$CPPFLAGS"	
			LIBLIBVIRT=-lvirt
			break
		fi
		CPPFLAGS=$_cppflags
	done	
	CPPFLAGS=$LIBVIRT_CPP_FLAGS
	AC_SUBST(LIBLIBVIRT)
	if test "$have_LIBVIRT" == "no"; then
		AC_MSG_ERROR(no. The required libvirt package is missing.)
        fi
	]
)

dnl
dnl The check for the SBLIM test suite
dnl Sets the TESTSUITEDIR variable and the TESTSUITE conditional
dnl

AC_DEFUN([CHECK_TESTSUITE],
	[
	AC_MSG_CHECKING(for SBLIM testsuite)
	_DIRS="$datadir/sblim-testsuite"
	save_exec_prefix=${exec_prefix}
	save_prefix=${prefix}
	if test xNONE == x${prefix}; then
		prefix=/usr/local
	fi
	if test xNONE == x${exec_prefix}; then
		exec_prefix=$prefix
	fi
	for _name in $_DIRS
	do
	 	AC_MSG_CHECKING( $_name )
		_xname=`eval echo $_name`
		if test -x $_xname/run.sh ; then
		  dnl Found it
		  AC_MSG_RESULT(yes)
		  if test x"$TESTSUITEDIR" == x; then
		  	TESTSUITEDIR=$_name
		  fi
		  AC_SUBST(TESTSUITEDIR)
		  break;
		fi
        done
	if test x"$TESTSUITEDIR" == x ; then
		AC_MSG_RESULT(no)
	fi
	AM_CONDITIONAL(TESTSUITE,[test x"$TESTSUITEDIR" != x])
	exec_prefix=$save_exec_prefix
	prefix=$save_prefix
	]
)


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
