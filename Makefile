MODULES = mmap inspect
QUICKJS_PREFIX ?= /usr/local

INSTALL = install

CC = gcc
SUFFIX = so

DEFS = -DJS_SHARED_LIBRARY
INCLUDES = -I$(QUICKJS_PREFIX)/include/quickjs

CFLAGS = $(DEFS) $(INCLUDES) -O3 -Wall

ifneq ($(DEBUG),)
CFLAGS += -g -ggdb
endif
ifneq ($(WEXTRA),)
CFLAGS += -Wextra -Wno-unused -Wno-cast-function-type
endif

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

mmap.$(SUFFIX): mmap.o
	$(CC) $(CFLAGS) -shared -o $@ $^

inspect.$(SUFFIX): inspect.o
	$(CC) $(CFLAGS) -shared -o $@ $^
