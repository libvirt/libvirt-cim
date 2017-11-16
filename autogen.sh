#!/bin/sh
# ============================================================================
# (C) Copyright IBM Corp. 2005

die()
{
    test "$1" && echo "$1" >&2
    exit 1
}

curdir=$(pwd)
test "$curdir" || curdir=.

srcdir=$(dirname "$0")
test "$srcdir" || srcdir=.

cd "$srcdir" || die "Failed to cd into $srcdir"

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

cd "$curdir" || die "Failed to cd into $curdir"

if test "$#" -eq 0; then
    echo "Running configure without arguments ..."
    echo "(If you want to pass any, specify them on the $0 command line)"
else
    echo "Running configure with $@ ..."
fi

"$srcdir/configure" "$@" || die

echo
echo "You may now run make"
