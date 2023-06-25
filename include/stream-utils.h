#ifndef STREAM_UTILS_H
#define STREAM_UTILS_H

#include "cutils.h"
#include <sys/types.h>
#include <string.h>

typedef ssize_t WriteFunction(intptr_t, const void*, size_t);
typedef ssize_t WriterFinalizer(void*);

typedef struct {
  WriteFunction* write;
  void* opaque;
  WriterFinalizer* finalizer;
} Writer;

Writer writer_from_dynbuf(DynBuf*);
ssize_t writer_write(Writer*, const void*, size_t);
void writer_free(Writer*);

static inline ssize_t
writer_puts(Writer* wr, const void* s) {
  return writer_write(wr, s, strlen(s));
}

static inline ssize_t
writer_putc(Writer* wr, int c) {
  char ch = c;
  return writer_write(wr, &ch, 1);
}

#endif /* defined(STREAM_UTILS_H) */
