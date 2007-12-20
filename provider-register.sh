#!/bin/sh
# $Id$
# ==================================================================
# Copyright IBM Corp. 2005
# ==================================================================

pegasus_repository()
{
    for p in $PEGASUS_HOME /var/lib/Pegasus /var/lib/pegasus /usr/local/var/lib/pegasus \
	/var/local/lib/pegasus /var/opt/tog-pegasus 
    do
      if test -d $p/repository
      then
	  echo $p/$1
	  return 0
      fi
    done
    return 1
}

pegasus_path()
{
    for p in $PEGASUS_HOME/bin /usr/bin /usr/sbin /usr/local/bin /usr/local/sbin \
	/opt/tog-pegasus/bin /opt/tog-pegasus/sbin 
    do
      if test -x $p/$1
      then
	  echo $p/$1
	  return 0
      fi
    done
    return 1
}

pegasus_transform()
{
    OUTFILE=$1
    shift
    regfiles=$*
    PROVIDERMODULES=`cat $regfiles 2> /dev/null | grep -v '^[[:space:]]*#.*' | cut -d ' ' -f 4 | sort | uniq`
    if test x"$PROVIDERMODULES" = x
    then
	echo Failed to read registration files >&2
	return 1
    fi
    PROVIDERS=`cat $regfiles 2> /dev/null | grep -v '^[[:space:]]*#.*' | cut -d ' ' -f 3-4 | sort | uniq`
    
# produce ProviderModules
    echo > $OUTFILE
    chatter "Processing provider modules:" $PROVIDERMODULES
    for pm in $PROVIDERMODULES
    do
      cat >> $OUTFILE <<EOFPM
instance of PG_ProviderModule
{
   Name = "$pm";
   Location = "$pm";
   Vendor = "SBLIM";
   Version = "2.0.0";
   InterfaceType = "CMPI";
   InterfaceVersion = "2.0.0";
};

EOFPM
    done
    
# produce Providers
    set -- $PROVIDERS
    while test x$1 != x
    do
      cat >> $OUTFILE <<EOFP
instance of PG_Provider
{
   Name = "$1";
   ProviderModuleName = "$2";
};

EOFP
      shift 2
    done

#produce Capabilities
    let serial=0
    for rf in $regfiles
    do
      cat $rf | grep -v '^[[:space:]]*#.*' | while read CLASSNAME NAMESPACE PROVIDERNAME PROVIDERMODULE CAPS
      do
	let serial=serial+1
	numcap=
	for cap in $CAPS
	do
	  case $cap in
	      instance) 
		  if test x$numcap = x 
		  then numcap=2
		  else numcap="$numcap,2"
		  fi;;
	      association) 
		  if test x$numcap = x 
		  then numcap=3
		  else numcap="$numcap,3"
		  fi;;
	      indication) 
		  if test x$numcap = x
		  then numcap=4
		  else numcap="$numcap,4"
		  fi;;
	      method) 
		  if test x$numcap = x 
		  then numcap=5
		  else numcap="$numcap,5"
		  fi;;
	      **) echo unknown provider type $cap >&2
		  return 1;;
	  esac	  
	done
	cat >> $OUTFILE <<EOFC
instance of PG_ProviderCapabilities
{
   ProviderModuleName = "$PROVIDERMODULE";
   ProviderName = "$PROVIDERNAME";
   ClassName = "$CLASSNAME";
   ProviderType = { $numcap };
   Namespaces = {"$NAMESPACE"};
   SupportedProperties = NULL;
   SupportedMethods = NULL;
   CapabilityID = "$CLASSNAME-$serial";
};

EOFC
      done
    done
}

pegasus_install()
{
    if ps -C cimserver > /dev/null 2>&1 
    then
	CIMMOF=`pegasus_path cimmof`
	if test $? != 0
	then
	    echo "Error: cimmof not found" >&2
	    return 1
	fi
	state=active
    else
	CIMMOF=`pegasus_path cimmofl`
	if test $? != 0
	then
	    echo "Error: cimmofl not found" >&2
	    return 1
	fi
	PEGASUSREPOSITORY=`pegasus_repository`
	if test $? != 0
	then
	    echo "Error: pegasus repository not found" >&2
	    return 1
	fi
	CIMMOF="$CIMMOF -R $PEGASUSREPOSITORY"
	state=inactive
    fi

    mofpath=
    mymofs=
    myregs=
    mofmode=1
    namespace=$1
    shift

    while test x$1 != x
    do 
      if test $1 = ":"
      then 
	  mofmode=0
	  shift
	  continue
      fi
      if test $mofmode = 1
      then
	  if test x$mofpath = x
	  then
	      mofpath=`dirname $1`
	  fi
	  mymofs="$mymofs $1"
      else
	  myregs="$myregs $1"
      fi
      shift
    done
  
    for _TEMPDIR in /var/tmp /tmp
    do
      if test -w $_TEMPDIR
      then
	  _REGFILENAME=$_TEMPDIR/$$.mof
	  break
      fi
    done

    
    trap "rm -f $_REGFILENAME" EXIT

    if pegasus_transform $_REGFILENAME $myregs
    then
	chatter Registering providers with $state cimserver
	$CIMMOF -uc -I $mofpath -n $namespace $mymofs &&
	$CIMMOF -uc -n root/PG_Interop $_REGFILENAME
    else
	echo "Failed to build pegasus registration MOF." >&2
	return 1
    fi
}

pegasus_uninstall()
{
    mymofs=
    myregs=
    mofmode=1
    namespace=$1
    shift

    while test x$1 != x
    do 
      if test $1 = ":"
      then 
	  mofmode=0
	  shift
	  continue
      fi
      if test $mofmode = 1
      then
	  mymofs="$mymofs $1"
      else
	  myregs="$myregs $1"
      fi
      shift
    done
  
    if ps -C cimserver > /dev/null 2>&1 
    then
	PROVIDERMODULES=`cat $myregs 2> /dev/null | grep -v '^[[:space:]]*#.*' | cut -d ' ' -f 4 | sort | uniq`
	if test x"$PROVIDERMODULES" = x
	    then
	    echo Failed to read registration files >&2
	    return 1
	fi
	CIMPROVIDER=`pegasus_path cimprovider`	
	if test $? != 0
        then
	    echo "Error: cimprovider not found" >&2
	    return 1
	fi
	for pm in $PROVIDERMODULES
	do
	  chatter "Remove provider module" $pm
	  $CIMPROVIDER -d -m $pm > /dev/null &&
	  $CIMPROVIDER -r -m $pm > /dev/null
	done
	WBEMEXEC=`pegasus_path wbemexec`	
	if test $? != 0
        then
	    echo "Error: wbemexec not found" >&2
	    return 1
	fi
	CLASSES=`cat $mymofs 2> /dev/null | grep '^class'| cut -d ' ' -f 2 | uniq`

	for _TEMPDIR in /var/tmp /tmp
	  do
	  if test -w $_TEMPDIR
	      then
	      _DELETE_NAME=$TEMPDIR/delete-class.mof
	      break
	  fi
	done

	trap "rm -f $_DELETE_NAME" EXIT

	for cls in $CLASSES
	do
	  chatter Delete CIM Class $cls
	  cat > $_DELETE_NAME <<EOFA
<?xml version="1.0" encoding="utf-8" ?>
<CIM CIMVERSION="2.0" DTDVERSION="2.0">
 <MESSAGE ID="4711" PROTOCOLVERSION="1.0">
  <SIMPLEREQ>
   <IMETHODCALL NAME="DeleteClass">
    <LOCALNAMESPACEPATH>
EOFA
	  for ns in `echo $namespace | sed 's?/? ?'g`
	  do
	    cat >> $_DELETE_NAME <<EOFX
     <NAMESPACE NAME="$ns"></NAMESPACE>
EOFX
	  done
	  cat >> $_DELETE_NAME <<EOFE
    </LOCALNAMESPACEPATH>
    <IPARAMVALUE NAME="ClassName">
     <CLASSNAME NAME="$cls"/>
    </IPARAMVALUE>
   </IMETHODCALL>
  </SIMPLEREQ>
 </MESSAGE>
</CIM>
EOFE
	  $WBEMEXEC > /dev/null $_DELETE_NAME
	done
    else
	echo "Sorry, cimserver must be running to deregister the providers." >&2
	return 1
    fi
}

sfcb_transform()
{
    OUTFILE=$1
    shift
    regfiles=$*

#produce sfcb registraion
    for rf in $regfiles
    do
      cat $rf | grep -v '^[[:space:]]*#.*' | while read CLASSNAME NAMESPACE PROVIDERNAME PROVIDERMODULE CAPS
      do
	chatter "Registering class" $CLASSNAME
	cat >> $OUTFILE <<EOFC
[$CLASSNAME]   
   provider: $PROVIDERNAME
   location: $PROVIDERMODULE
   type: $CAPS
   namespace: $NAMESPACE
#
EOFC
      done
    done
}

sfcb_rebuild()
{
    if ps -C sfcbd > /dev/null 2>&1
    then
        # sfcb is running -- need to restart
	for INITSCRIPT in /etc/init.d/sfcb /usr/local/etc/init.d/sfcb none
	do
	  if test -x $INITSCRIPT
	  then
	      break;
	  fi
	done
	chatter "Shutting down sfcb."
	if test $INITSCRIPT = none
	then
	    killall sfcbd
	else
	    $INITSCRIPT stop
	fi
	t=0
	while ps -C sfcbd > /dev/null 2>&1
	do
	  sleep 1
	  t=`expr $t + 1`
	  if test $t -gt 10
	  then
	      echo "Timed out waiting for sfcb shutdown..." >&2
	      echo "Please stop sfcb manually and rebuild the repository using sfcbrepos." >&2
	      return 1
	  fi
	done
	chatter "Rebuilding repository."
	sfcbrepos -f $endian
	if test $? != 0
	then
	    echo "Repository rebuild failed." >&2
	    return 1
	fi
	
	if test $INITSCRIPT = none
	then
	    echo "No init script found - you need to start sfcbd manually." >&2
	    return 1
	else
	    chatter "Restarting sfcb."
	    $INITSCRIPT start
	fi
    else
	# Not running - rebuild repository
	chatter "Rebuilding repository."
	sfcbrepos -f $endian
    fi
}

sfcb_install()
{    
    mymofs=
    myregs=
    mofmode=1
    namespace=$1
    shift
    
    while test x$1 != x
    do 
      if test $1 = ":"
      then 
	  mofmode=0
	  shift
	  baseregname=`basename $1 .registration`
	  continue
      fi
      if test $mofmode = 1
      then
	  mymofs="$mymofs $1"
      else
	  myregs="$myregs $1"
      fi
      shift
    done

    for _TEMPDIR in /var/tmp /tmp
    do
      if test -w $_TEMPDIR
      then
	  _REGFILENAME=$_TEMPDIR/$baseregname.reg
	  break
      fi
    done

    trap "rm -f $_REGFILENAME" EXIT

    if sfcb_transform $_REGFILENAME $myregs
    then
	chatter "Staging provider registration."
	sfcbstage -n $namespace -r $_REGFILENAME $mymofs
	if test $? != 0 
	then
	    echo "Failed to stage provider registration." >&2
	    return 1
	fi
	sfcb_rebuild
    else
	echo "Failed to build sfcb registration file." >&2
	return 1
    fi
}

sfcb_uninstall()
{    
    mymofs=
    namespace=$1
    shift

    while test x$1 != x
    do 
      if test $1 = ":"
      then 
	  shift
	  baseregname=`basename $1 .registration`
	  break
      fi
      mymofs="$mymofs `basename $1`"
      shift
    done
    
    # "Unstage" MOFs and the registration file
    chatter "Unstaging provider registrations."
    sfcbunstage -n $namespace -r $baseregname.reg $mymofs

    # Rebuild repository
    sfcb_rebuild
}

openwbem_transform()
{
    OUTFILE=$1
    shift
    moffiles=$*
    
    if rm -f $OUTFILE
    then
	for _f in $moffiles
	do
	  cat $_f >> $OUTFILE
	done
    fi
}

openwbem_repository()
{
    for p in /var/lib/openwbem /usr/local/var/openwbem
    do
      if test -f $p/schema.dat
      then
	  echo $p
	  return 0
      fi
    done
    return 1
}

openwbem_install()
{
    namespace=$1
    shift

    CIMMOF=`which owmofc 2> /dev/null`
    if test $? != 0
    then
	echo "Error: cimmof not found" >&2
	return 1
    fi

    if ps -C owcimomd > /dev/null 2>&1 
    then
	state=active
    else
	CIMMOF="$CIMMOF -d `openwbem_repository`"
	if test $? != 0
	then
	    echo "Error: OpenWBEM repository not found" >&2
	    return 1
	fi
	state=inactive
    fi

    for _TEMPDIR in /var/tmp /tmp
    do
      if test -w $_TEMPDIR
      then
	  _REGFILENAME=$_TEMPDIR/$$.mof
	  break
      fi
    done

    trap "rm -f $_REGFILENAME" EXIT

    if openwbem_transform $_REGFILENAME $*
    then
	chatter Registering providers with $state owcimomd
	$CIMMOF -u http://localhost/cimom -n $namespace $_REGFILENAME > /dev/null
    else
	echo "Failed to build OpenWBEM registration MOF." >&2
	return 1
    fi       
}

openwbem_uninstall()
{
    namespace=$1
    shift

    CIMMOF=`which owmofc 2> /dev/null`
    if test $? != 0
    then
	echo "Error: cimmof not found" >&2
	return 1
    fi

    if ps -C owcimomd > /dev/null 2>&1 
    then
	state=active
    else
	CIMMOF="$CIMMOF -d `openwbem_repository`"
	if test $? != 0
	then
	    echo "Error: OpenWBEM repository not found" >&2
	    return 1
	fi
	state=inactive
    fi

    for _TEMPDIR in /var/tmp /tmp
    do
      if test -w $_TEMPDIR
      then
	  _REGFILENAME=$_TEMPDIR/$$.mof
	  break
      fi
    done

    trap "rm -f $_REGFILENAME" EXIT

    if openwbem_transform $_REGFILENAME $*
    then
	chatter Deregistering providers with $state owcimomd
	$CIMMOF -r $_REGFILENAME > /dev/null
    else
	echo "Failed to build OpenWBEM registration MOF." >&2
	return 1
    fi       
}

cim_server()
{
    for exname in sfcbd cimserver owcimomd
    do
      if pegasus_path $exname > /dev/null
      then
	  case $exname in
	      sfcbd) echo sfcb; return 0;;
	      cimserver) echo pegasus; return 0;;
	      owcimomd) echo openwbem; return 0;;
	  esac
	  break;
      fi
    done
    echo unknown
    return 1
}

usage() 
{
    echo "usage: $0 [-h] [-v] [-d] [-t <cimserver>] [-n <namespace>] -r regfile ... -m mof ..."
}

chatter()
{
    if test x$verbose != x
    then
	echo $*
    fi
}

gb_getopt()
{
    rmode=0
    mmode=0
    options=
    moffiles=
    registrations=
    while [ -n "$1" ]
    do
      case $1 in
	  -r) mmode=0;
	      rmode=1;
	      shift;;
	  -m) mmode=1;
	      rmode=0;
	      shift;;
	  -*) mmode=0;
	      rmode=0;
	      options="$options $1";
	      shift;;
	  **) if [ $mmode = 1 ] 
	      then moffiles="$moffiles $1"       
	      elif [ $rmode = 1 ]
	      then registrations="$registrations -r $1" 
	      else options="$options $1";
	      fi; 
	      shift;;
      esac
    done
    echo $options $registrations $moffiles 
}

prepargs=`gb_getopt $*`
args=`getopt dvhX:t:r:n: $prepargs`
rc=$?

if [ $rc = 127 ]
then
    echo "warning: getopt not found ...continue without syntax check"
    args=$prepargs
elif [ $rc != 0 ]
then
    usage $0
    exit 1
fi

namespace="root/cimv2"

set -- $args

while [ -n "$1" ]
do
  case $1 in
      -h) help=1; 
	  shift;
	  break;;
      -v) verbose=1; 
	  shift;;
      -X) endian="-X $2"; 
	  shift 2;;
      -d) deregister=1; 
	  shift;;
      -t) cimserver=$2;
	  shift 2;;
      -n) namespace=$2;
	  shift 2;;
      -r) regs="$regs $2";
	  shift 2;;
      --) shift;
	  break;;
      **) break;;
  esac
done

mofs=$*

if [ "$help" = "1" ]
then
    usage
    echo -e "\t-h display help message"
    echo -e "\t-v verbose mode"
    echo -e "\t-d deregister provider and uninstall schema"
    echo -e "\t-t specify cimserver type (pegasus|sfcb|openwbem|sniacimom)"
    echo -e "\t-r specify registration files"
    echo -e "\t-m specify schema mof files"
    echo -e "\t-X create repository for alternate platform (sfcb only at the moment)."
    echo -e "\t-n target namespace definition (default: root/cimv2)."
    echo
    echo Use this command to install schema mofs and register providers.
    echo CIM Server Type is required as well as at least one registration file and one mof.
    exit 0
fi

if test x"$mofs" = x || test x"$regs" = x
then
    usage $0
    exit 1
fi

if test x$cimserver = x
then
    cimserver=`cim_server`
    if test $? = 0
    then
	chatter "Autoselected CIM server type:" $cimserver
    else
	echo "CIM server type could not be determined, specify with -t." >&2
	exit 1
    fi
fi

if test x$deregister = x
then
    case $cimserver in
	pegasus) pegasus_install $namespace $mofs ":" $regs;;
	sfcb)    sfcb_install $namespace $mofs ":" $regs;;
	openwbem) openwbem_install $namespace $mofs ;;
	sniacimom) echo sniacimom not yet supported && exit 1 ;;
	**)	echo "Invalid CIM Server Type " $cimserver && exit 1;;
    esac
else
    case $cimserver in
	pegasus) pegasus_uninstall $namespace $mofs ":" $regs;;
	sfcb)    sfcb_uninstall $namespace $mofs ":" $regs;;
	openwbem) openwbem_uninstall $namespace $mofs ;;
	sniacimom) echo sniacimom not yet supported && exit 1 ;;
	**)	echo "Invalid CIM Server Type " $cimserver && exit 1;;
    esac
fi    
