MODULES = deep inspect lexer mmap path pointer predicate repeater tree-walker xml

QUICKJS_PREFIX ?= /usr/local

INSTALL = install

CC = gcc
SUFFIX = so

DEFS = -DJS_SHARED_LIBRARY -DHAVE_QUICKJS_CONFIG_H
INCLUDES = -I$(QUICKJS_PREFIX)/include/quickjs

CFLAGS = $(DEFS) $(INCLUDES) -Wall -fPIC -fvisibility=hidden

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

.PHONY: bin all clean install
all: bin $(SHARED_OBJECTS)

.PHONY: bin
bin:
	mkdir -p $@

clean:
	$(RM) $(OBJECTS) $(SHARED_OBJECTS)

install: all
	$(INSTALL) -d $(QUICKJS_PREFIX)/lib/quickjs
	$(INSTALL) -m 755 $(SHARED_OBJECTS) $(QUICKJS_PREFIX)/lib/quickjs

.c.o:
	$(CC) $(CFLAGS) -c $< 

$(BUILDDIR)deep.$(SUFFIX): quickjs-deep.c vector.c pointer.c utils.c virtual-properties.c
	$(CC) $(CFLAGS) -shared -o $@ $^

$(BUILDDIR)inspect.$(SUFFIX): quickjs-inspect.c vector.c utils.c
	$(CC) $(CFLAGS) -shared -o $@ $^

$(BUILDDIR)lexer.$(SUFFIX): quickjs-lexer.c predicate.c vector.c utils.c $(BUILDDIR)predicate.$(SUFFIX)
	$(CC) $(CFLAGS) -shared -o $@ $^

$(BUILDDIR)mmap.$(SUFFIX): quickjs-mmap.c utils.c
	$(CC) $(CFLAGS) -shared -o $@ $^

$(BUILDDIR)path.$(SUFFIX): quickjs-path.c path.c utils.c
	$(CC) $(CFLAGS) -shared -o $@ $^

$(BUILDDIR)pointer.$(SUFFIX): quickjs-pointer.c pointer.c utils.c
	$(CC) $(CFLAGS) -shared -o $@ $^
	
$(BUILDDIR)predicate.$(SUFFIX): quickjs-predicate.c predicate.c vector.c utils.c
	$(CC) $(CFLAGS) -shared -o $@ $^

$(BUILDDIR)repeater.$(SUFFIX): quickjs-repeater.c utils.c
	$(CC) $(CFLAGS) -shared -o $@ $^

$(BUILDDIR)tree-walker.$(SUFFIX): quickjs-tree-walker.c vector.c utils.c
	$(CC) $(CFLAGS) -shared -o $@ $^

$(BUILDDIR)xml.$(SUFFIX): quickjs-xml.c vector.c utils.c
	$(CC) $(CFLAGS) -shared -o $@ $^
