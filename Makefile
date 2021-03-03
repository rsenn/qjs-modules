MODULES = inspect mmap path pointer tree-walker xml

QUICKJS_PREFIX ?= /usr/local

INSTALL = install

CC = gcc
SUFFIX = so

DEFS = -DJS_SHARED_LIBRARY -DHAVE_QUICKJS_CONFIG_H
INCLUDES = -I$(QUICKJS_PREFIX)/include/quickjs

CFLAGS = $(DEFS) $(INCLUDES) -Wall -fPIC

ifneq ($(DEBUG),)
CFLAGS += -g -ggdb -O0
else
CFLAGS += -O3
endif
ifneq ($(WEXTRA),)
CFLAGS += -Wextra -Wno-cast-function-type
endif
CFLAGS += -Wno-unused 

OBJECTS = $(MODULES:%=%.o)
BUILDDIR := bin/
SHARED_OBJECTS = $(MODULES:%=$(BUILDDIR)%.$(SUFFIX))

all: bin $(SHARED_OBJECTS)

.PHONY: bin
bin:
	mkdir -p $@

clean:
	$(RM) $(OBJECTS) $(SHARED_OBJECTS)

install:
	$(INSTALL) -d $(QUICKJS_PREFIX)/lib/quickjs
	$(INSTALL) -m 755 $(SHARED_OBJECTS) $(QUICKJS_PREFIX)/lib/quickjs

.c.o:
	$(CC) $(CFLAGS) -c $< 

$(BUILDDIR)inspect.$(SUFFIX): quickjs-inspect.c vector.c
	$(CC) $(CFLAGS) -shared -o $@ $^

$(BUILDDIR)mmap.$(SUFFIX): quickjs-mmap.c
	$(CC) $(CFLAGS) -shared -o $@ $^

$(BUILDDIR)path.$(SUFFIX): quickjs-path.c path.c
	$(CC) $(CFLAGS) -shared -o $@ $^

$(BUILDDIR)pointer.$(SUFFIX): quickjs-pointer.c
	$(CC) $(CFLAGS) -shared -o $@ $^

$(BUILDDIR)tree-walker.$(SUFFIX): quickjs-tree-walker.c vector.c
	$(CC) $(CFLAGS) -shared -o $@ $^

$(BUILDDIR)xml.$(SUFFIX): quickjs-xml.c vector.c
	$(CC) $(CFLAGS) -shared -o $@ $^
