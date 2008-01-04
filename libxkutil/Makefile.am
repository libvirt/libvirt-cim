# Copyright IBM Corp. 2007
SUBDIRS = tests


CFLAGS += $(CFLAGS_STRICT)

noinst_HEADERS = cs_util.h misc_util.h device_parsing.h xmlgen.h

lib_LTLIBRARIES = libxkutil.la

AM_LDFLAGS = -lvirt -luuid

libxkutil_la_SOURCES = cs_util_instance.c misc_util.c device_parsing.c \
                       xmlgen.c

noinst_PROGRAMS = xml_parse_test

xml_parse_test_SOURCES = xml_parse_test.c
#xml_parse_test_HEADERS = device_parsing.h
xml_parse_test_LDADD = -lvirt -lxkutil
