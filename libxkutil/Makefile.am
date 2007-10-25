# Copyright IBM Corp. 2007
SUBDIRS = tests


CFLAGS += $(CFLAGS_STRICT)

noinst_HEADERS = cs_util.h misc_util.h device_parsing.h hostres.h

lib_LTLIBRARIES = libxkutil.la

AM_LDFLAGS = -lvirt -luuid

libxkutil_la_SOURCES = cs_util_instance.c misc_util.c device_parsing.c \
                       xmlgen.c hostres.c
