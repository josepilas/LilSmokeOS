CC ?= cc
CFLAGS ?= -O2
SQFS_CFLAGS ?=
SQFS_LIBS ?= -lsquashfs
CPPFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -Ivendor/libini $(SQFS_CFLAGS)
WARN = -Wall -Wextra -Werror -std=c99
SOURCES = src/runtime.c src/common.c src/desktop.c src/elf.c src/opk.c src/sha256.c
all: lilsmoke-opendingux
libini.so.0: vendor/libini/libini.c vendor/libini/ini.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -fPIC -shared -Wl,-soname,libini.so.0 $< -o $@
lilsmoke-opendingux: $(SOURCES) src/od.h libini.so.0
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(SOURCES) $(LDFLAGS) $(SQFS_LIBS) -lsodium -L. -l:libini.so.0 -o $@
clean:
	rm -f lilsmoke-opendingux libini.so.0
.PHONY: all clean
