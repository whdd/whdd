LDLIBS := $(LDLIBS) -ldialog -lmenuw -lncursesw -lm -lrt
LDFLAGS := $(LDFLAGS) -pthread
CFLAGS := $(CFLAGS) -std=gnu99 -pthread -I./ -I./cui/ -I./libdevcheck/
INSTALL ?= install
DESTDIR ?= /usr/local

source_files := $(wildcard *.c) $(wildcard cui/*.c) $(wildcard libdevcheck/*.c)

whdd: version.h $(source_files) Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(source_files) $(LDLIBS)

version.h: FORCE
	./version.sh . version.h

install: whdd
	$(INSTALL) -D whdd $(DESTDIR)/bin/whdd

FORCE:
