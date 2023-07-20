#ifndef STREAM_UTILS_H
#define STREAM_UTILS_H

#include "cutils.h"
#include <sys/types.h>
#include <string.h>
#include <stdbool.h>
#include "buffer-utils.h"

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

typedef ssize_t ReadFunction(intptr_t, void*, size_t);
typedef ssize_t ReaderFinalizer(void*, void*);

typedef struct {
  ReadFunction* read;
  void *opaque, *opaque2;
  ReaderFinalizer* finalizer;
} Reader;

Reader reader_from_buf(InputBuffer*, JSContext*);
Reader reader_from_fd(intptr_t, _Bool);

ssize_t reader_read(Reader*, void*, size_t);
void reader_free(Reader*);

static inline int
reader_getc(Reader* rd) {
  uint8_t ch;
  ssize_t ret;

  if((ret = reader_read(rd, &ch, 1)) == 1)
    return (unsigned int)ch;

  return -1;
}

/**
 * @}
 */
#endif /* defined(STREAM_UTILS_H) */
