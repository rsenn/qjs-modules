#include "stream-utils.h"
#include "buffer-utils.h"
#include "defines.h"

#include <assert.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#define RESULT(r, acc) \
  do { \
    ssize_t n = (r); \
    if(n < 0) \
      return n; \
    (acc) += (n); \
  } while(0);

#define RESULT_LOOP(r, acc) \
  if(1) { \
    ssize_t n = (r); \
    if(n < 0) \
      return n; \
    if(n == 0) \
      break; \
    (acc) += (n); \
  }

static ssize_t
write_dbuf(DynBuf* db, const void* buf, size_t len, Writer* wr) {
  if(dbuf_put(db, buf, len))
    return -1;

  return len;
}

static ssize_t
write_tee(Writer* wptr, const void* buf, size_t len) {
  ssize_t r[2] = {0, 0};

  RESULT(writer_write(&wptr[0], buf, len), r[0]);
  RESULT(writer_write(&wptr[1], buf, len), r[1]);

  return MIN_NUM(r[0], r[1]);
}

typedef struct {
  Writer* parent;
  const char* chars;
  size_t nchars;
} EscapedWriter;

static ssize_t
write_escaped(EscapedWriter* ew, const uint8_t* x, size_t len) {
  ssize_t r = 0;

  for(size_t i = 0; i < len; i++) {
    if(byte_chr(ew->chars, ew->nchars, x[i]) < ew->nchars)
      RESULT_LOOP(writer_putc(ew->parent, '\\'), r);

    RESULT_LOOP(writer_putc(ew->parent, x[i]), r);
  }

  return r;
}

static ssize_t
write_urlencoded(Writer* parent, const uint8_t* x, size_t len) {
  ssize_t r = 0;
  static char const unescaped_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                        "abcdefghijklmnopqrstuvwxyz"
                                        "0123456789"
                                        "@*_+-./";

  for(size_t i = 0; i < len; i++) {

    if(!memchr(unescaped_chars, x[i], sizeof(unescaped_chars) - 1)) {
      char buf[4] = {'%'};

      fmt_xlong0(&buf[1], x[i], 2);

      RESULT_LOOP(writer_write(parent, buf, 3), r);
      continue;
    }

    RESULT_LOOP(writer_putc(parent, x[i]), r);
  }

  return r;
}

static ssize_t
read_urldecoded(Reader* parent, uint8_t* x, size_t len) {
  uint8_t c, *y = x;
  ssize_t r = 0;

  while(len > 0) {
    RESULT_LOOP(reader_read(parent, &c, 1), r);

    if(c == '%') {
      uint8_t hi, lo;

      RESULT_LOOP(reader_read(parent, &hi, 1), r);

      if(hi != '%') {

        RESULT_LOOP(reader_read(parent, &lo, 1), r);

        c = (scan_fromhex(hi) << 4) | scan_fromhex(lo);
      }
    }

    *x++ = c;
    len--;
  }

  return x - y;
}

static ssize_t
read_inputbuffer(InputBuffer* ib, void* buf, size_t len, Reader* rd) {
  size_t remain = ib->size - ib->pos;

  if(len > remain)
    len = remain;

  if(len)
    memcpy(buf, &ib->data[ib->pos], len);

  ib->pos += len;
  return len;
}

static ssize_t
read_range(const uint8_t* start, void* buf, size_t len, Reader* rd) {
  const uint8_t* end = rd->opaque2;
  size_t remain = end - start;

  if(len > remain)
    len = remain;

  if(len)
    memcpy(buf, start, len);

  start += len;
  rd->opaque = (void*)start;

  return len;
}

/**
 * \addtogroup stream-utils
 * @{
 */
Writer
writer_from_dynbuf(DynBuf* db) {
  return (Writer){(WriteFunction*)&write_dbuf, db, (WriterFinalizer*)(void*)&dbuf_free};
}

Writer
writer_from_fd(intptr_t fd, bool close_on_end) {
  return (Writer){
      (WriteFunction*)&write,
      (void*)fd,
      close_on_end ? (WriterFinalizer*)(void*)&close : NULL,
  };
}

Writer
writer_tee(const Writer a, const Writer b) {
  Writer* opaque;

  if((opaque = malloc(sizeof(Writer) * 2))) {
    opaque[0] = a;
    opaque[1] = b;
  }

  assert(opaque);

  return (Writer){
      (WriteFunction*)&write_tee,
      (void*)opaque,
      (WriterFinalizer*)(void*)&orig_free,
  };
}

Writer
writer_escaped(Writer* out, const char chars[], size_t nchars) {
  EscapedWriter* ew;

  if((ew = malloc(sizeof(EscapedWriter)))) {
    ew->parent = out;
    ew->chars = chars;
    ew->nchars = nchars;
  }

  return (Writer){
      (WriteFunction*)&write_escaped,
      (void*)ew,
      (WriterFinalizer*)(void*)&orig_free,
  };
}

Writer
writer_urlencode(Writer* out) {
  return (Writer){
      (WriteFunction*)&write_urlencoded,
      (void*)out,
      NULL,
  };
}

ssize_t
writer_write(Writer* wr, const void* buf, size_t len) {
  return wr->write((intptr_t)wr->opaque, buf, len, wr);
}

void
writer_free(Writer* wr) {
  if(wr->finalizer)
    wr->finalizer(wr->opaque);
}

Reader
reader_from_buf(InputBuffer* ib, JSContext* ctx) {
  return (Reader){
      (ReadFunction*)&read_inputbuffer,
      ib,
      ctx,
      (ReaderFinalizer*)(void*)&input_buffer_free,
  };
}

Reader
reader_from_range(const void* start, size_t len) {
  return (Reader){
      (ReadFunction*)&read_range,
      (void*)start,
      ((uint8_t*)start) + len,
      NULL,
  };
}

Reader
reader_from_fd(intptr_t fd, bool close_on_end) {
  return (Reader){
      (ReadFunction*)&read,
      (void*)fd,
      NULL,
      close_on_end ? (ReaderFinalizer*)(void*)&close : NULL,
  };
}

Reader
reader_urldecode(Reader* parent) {
  return (Reader){
      (ReadFunction*)&read_urldecoded,
      parent,
      NULL,
      NULL,
  };
}

ssize_t
reader_read(Reader* rd, void* buf, size_t len) {
  return rd->read((intptr_t)rd->opaque, buf, len, rd);
}

void
reader_free(Reader* rd) {
  if(rd->finalizer)
    rd->finalizer(rd->opaque, rd->opaque2);
}

ssize_t
transform_urldecode(Reader* rd, Writer* wr) {
  int c;
  ssize_t ret = 0;

  while((c = reader_getc(rd)) != -1) {
    if(c == '%') {
      int hi, lo;

      if((hi = reader_getc(rd)) == -1)
        return -1;

      if(hi != '%') {

        if((lo = reader_getc(rd)) == -1)
          return -1;

        c = (scan_fromhex(hi) << 4) | scan_fromhex(lo);
      }
    }

    RESULT(writer_putc(wr, c), ret);
  }

  return ret;
}

/**
 * @}
 */
