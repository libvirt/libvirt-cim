#!/bin/bash
#
# A script to register base classes with the CIMOM
# 
# Copyright IBM Corp. 2007
# Author: Dan Smith <danms@us.ibm.com>
#
# Usage: 
#
#  $ register_base.sh (sfcb|pegasus) [MOF...]
#
# FIXME: Need to make pegasus location and namespace variable

CIMOM=$1

if [ -z "$CIMOM" ]; then
    echo "Usage: $0 (pegasus|sfcb)"
    exit 1
fi

shift

if [ "$CIMOM" = "pegasus" ]; then
    for i in $*; do
	cimmofl -W -uc -aEV -R/var/lib/Pegasus -n /root/ibmsd $i
    done
elif [ "$CIMOM" = "sfcb" ]; then
    for i in $*; do
	sfcbstage -n /root/ibmsd $i
    done
else
    echo "Unknown CIMOM type: $CIMOM"
fi
