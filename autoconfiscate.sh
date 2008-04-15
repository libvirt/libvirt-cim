#!/bin/sh
# ============================================================================
# (C) Copyright IBM Corp. 2005

echo "Running libtool ..." &&
libtoolize --copy --force --automake && 

echo "Running aclocal ..." &&
aclocal --force &&

echo "Running autoheader ..." &&
autoheader --force &&

echo "Running automake ..." &&
automake -i --add-missing --copy --foreign &&

echo "Running autoconf ..." &&
autoconf --force &&

if test -x $(which hg); then
    hg id -i > .changeset
    hg id -n > .revision
else
    echo "Unknown" > .changeset
    echo "0" > .revision
fi

echo "You may now run ./configure"
