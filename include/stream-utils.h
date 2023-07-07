#ifndef STREAM_UTILS_H
#define STREAM_UTILS_H

#include "cutils.h"
#include <sys/types.h>
#include <string.h>
#include <stdbool.h>

/**
 * \defgroup stream-utils stream-utils: Utilities for stream I/O
 * @{
 */
typedef ssize_t WriteFunction(intptr_t, const void*, size_t);
typedef ssize_t WriterFinalizer(void*);

typedef struct {
  WriteFunction* write;
  void* opaque;
  WriterFinalizer* finalizer;
} Writer;

Writer writer_from_dynbuf(DynBuf*);
Writer writer_from_fd(intptr_t fd, bool close_on_end);
Writer writer_tee(const Writer a, const Writer b);

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

/**
 * @}
 */
#endif /* defined(STREAM_UTILS_H) */
