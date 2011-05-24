# Copyright IBM Corp. 2007
SUBDIRS = tests

CFLAGS += $(CFLAGS_STRICT)

noinst_HEADERS = cs_util.h misc_util.h device_parsing.h xmlgen.h infostore.h \
                 pool_parsing.h

lib_LTLIBRARIES = libxkutil.la

libxkutil_la_SOURCES = cs_util_instance.c misc_util.c device_parsing.c \
                       xmlgen.c infostore.c pool_parsing.c

libxkutil_la_LIBADD = @LIBVIRT_LIBS@ \
		      @LIBUUID_LIBS@

noinst_PROGRAMS = xml_parse_test

xml_parse_test_SOURCES = xml_parse_test.c
xml_parse_test_LDADD = libxkutil.la \
		       @LIBVIRT_LIBS@
