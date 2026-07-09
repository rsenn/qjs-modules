#ifndef STREAM_UTILS_H
#define STREAM_UTILS_H

#include "cutils.h"
#include <sys/types.h>
#include <string.h>
#include <stdbool.h>
#include "buffer-utils.h"
#include "location.h"

/**
 * \defgroup stream-utils stream-utils: Utilities for stream I/O
 * @{
 */
struct StreamWriter;
struct StreamReader;

enum {
  STREAM_EOF = -2,
  STREAM_ERROR = -1,
};

typedef ssize_t WriteFunction(intptr_t, const void*, size_t, struct StreamWriter*);
typedef ssize_t ReadFunction(intptr_t, void*, size_t, struct StreamReader*);
typedef void WriterFinalizer(void*);
typedef void ReaderFinalizer(void*, void*);

typedef struct StreamWriter {
  WriteFunction* write;
  void* opaque;
  WriterFinalizer* finalizer;
} Writer;

typedef struct StreamReader {
  ReadFunction* read;
  void *opaque, *opaque2;
  ReaderFinalizer* finalizer;
} Reader;

Writer writer_from_dynbuf(DynBuf*);
Writer writer_from_buf(OutputBuffer*);
Writer writer_from_fd(intptr_t, bool);
Writer writer_from_function(JSContext*, JSValueConst);
Writer writer_from_method(JSContext*, JSValueConst, JSValueConst);
Writer writer_counted(Writer*, uint64_t*, uint64_t*);
Writer writer_buffered(Writer*, size_t);
Writer writer_linebuffered(Writer*, size_t);
Writer writer_tee(const Writer, const Writer);
Writer writer_escaped(Writer*, const char*, size_t);
Writer writer_urlencode(Writer*);
Writer writer_location(Writer*, Location*);
ssize_t writer_write(Writer*, const void*, size_t);
void writer_free(Writer*);
ssize_t writer_flush(Writer*);
Reader reader_from_dynbuf(DynBuf*);
Reader reader_from_buf(InputBuffer*);
Reader reader_from_bytes(const void*, size_t);
Reader reader_from_fd(intptr_t, bool);
Reader reader_from_function(JSContext*, JSValueConst);
Reader reader_from_method(JSContext*, JSValueConst, JSValueConst);
Reader reader_counted(Reader*, uint64_t*, uint64_t*);
Reader reader_buffered(Reader*, size_t);
Reader reader_linebuffered(Reader*, size_t);
Reader reader_urldecode(Reader*);
Reader reader_location(Reader*, Location*);
ssize_t reader_read(Reader*, void*, size_t);
void reader_free(Reader*);

static inline ssize_t
writer_puts(Writer* wr, const void* s) {
  return writer_write(wr, s, strlen(s));
}

static inline ssize_t
writer_putc(Writer* wr, int c) {
  char ch = c;
  return writer_write(wr, &ch, 1);
}

static inline void
reader_jsbuf_free(void* opaque, void* opaque2) {
  InputBuffer* input = opaque;
  JSContext* ctx = opaque2;

  inputbuffer_free(input, ctx);
  js_free(ctx, input);
}

static inline Reader
reader_from_jsbuf(JSContext* ctx, JSValueConst value) {
  InputBuffer* input;

  if(!(input = js_mallocz(ctx, sizeof(InputBuffer))))
    return (Reader){};

  *input = js_input_chars(ctx, value);

  Reader rd = reader_from_buf(input);
  rd.opaque2 = ctx;
  rd.finalizer = &reader_jsbuf_free;
  return rd;
}

static inline int
reader_getc(Reader* rd) {
  uint8_t ch;
  ssize_t ret;

  if((ret = reader_read(rd, &ch, 1)) == 1)
    return (unsigned int)ch;

  return ret == 0 ? STREAM_EOF : STREAM_ERROR;
}

ssize_t transform_urldecode(Reader*, Writer*);

/**
 * @}
 */
#endif /* defined(STREAM_UTILS_H) */
