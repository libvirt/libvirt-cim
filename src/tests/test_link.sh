#!/bin/bash

test_lib() {
    LD_BIND_NOW=y LD_PRELOAD=$1 /bin/true 2>&1 

    if [ $? = 0 ]; then
	echo Link of $(basename $lib) tested OK
	return 0
    else
	echo Link of $(basename $lib) FAILED
	return 1
    fi
}

for lib in ../.libs/*.so; do
    (test_lib $lib) || exit 255
done
