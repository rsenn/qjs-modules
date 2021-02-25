MODULES = inspect mmap pointer tree-walker xml

QUICKJS_PREFIX ?= /usr/local

INSTALL = install

CC = gcc
SUFFIX = so

DEFS = -DJS_SHARED_LIBRARY
INCLUDES = -I$(QUICKJS_PREFIX)/include/quickjs

CFLAGS = $(DEFS) $(INCLUDES) -O3 -Wall -fPIC

ifneq ($(DEBUG),)
CFLAGS += -g -ggdb
endif
ifneq ($(WEXTRA),)
CFLAGS += -Wextra -Wno-cast-function-type
endif
CFLAGS += -Wno-unused 

OBJECTS = $(MODULES:%=%.o)
SHARED_OBJECTS = $(MODULES:%=%.$(SUFFIX))

all: $(SHARED_OBJECTS)

clean:
	$(RM) $(OBJECTS) $(SHARED_OBJECTS)

install:
	$(INSTALL) -d $(QUICKJS_PREFIX)/lib/quickjs
	$(INSTALL) -m 755 $(SHARED_OBJECTS) $(QUICKJS_PREFIX)/lib/quickjs

.c.o:
	$(CC) $(CFLAGS) -c $< 

inspect.$(SUFFIX): inspect.o
	$(CC) $(CFLAGS) -shared -o $@ $^

mmap.$(SUFFIX): mmap.o
	$(CC) $(CFLAGS) -shared -o $@ $^

pointer.$(SUFFIX): pointer.o
	$(CC) $(CFLAGS) -shared -o $@ $^

tree-walker.$(SUFFIX): tree-walker.o vector.o
	$(CC) $(CFLAGS) -shared -o $@ $^

xml.$(SUFFIX): xml.o
	$(CC) $(CFLAGS) -shared -o $@ $^
