#!/bin/sh
# ============================================================================
# (C) Copyright IBM Corp. 2005

die()
{
    test "$1" && echo "$1" >&2
    exit 1
}

echo "Running libtool ..."
libtoolize --copy --force --automake || die

echo "Running aclocal ..."
aclocal --force || die

echo "Running autoheader ..."
autoheader --force || die

echo "Running automake ..."
automake -i --add-missing --copy --foreign || die

echo "Running autoconf ..."
autoconf --force || die

if test -x $(which git); then
    git rev-parse --short HEAD > .changeset
    git rev-list HEAD | wc -l > .revision
else
    echo "Unknown" > .changeset
    echo "0" > .revision
fi

echo "You may now run ./configure"
