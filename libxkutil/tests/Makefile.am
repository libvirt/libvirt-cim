# Copyright IBM Corp. 2007
TESTS = xml_tag.test xml_devices.test xmlgen.test xml_dominfo.test

CFLAGS += -g

%.test: %.c
	$(CC) -o $@ $^ $(CFLAGS) -lvirt `xml2-config --libs`

clean-local:
	rm -f *.test
