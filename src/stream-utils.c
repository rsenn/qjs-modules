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

typedef struct {
  JSContext* ctx;
  JSValue funcObj, thisObj;
} JSFunc;

typedef struct {
  uint8_t* buf;
  size_t len, pos;
  union {
    void* other;
    Writer* writer;
    Reader* reader;
  };
} Buffered;

typedef struct {
  Writer* parent;
  const char* chars;
  size_t nchars;
} EscapedWriter;

static ssize_t
write_dynbuf(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  DynBuf* db = (DynBuf*)fd;

  if(dbuf_put(db, buf, len))
    return -1;

  return len;
}

static ssize_t
write_tee(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Writer* wptr = (Writer*)fd;
  ssize_t r[2] = {0, 0};

  RESULT(writer_write(&wptr[0], buf, len), r[0]);
  RESULT(writer_write(&wptr[1], buf, len), r[1]);

  return MIN_NUM(r[0], r[1]);
}

static ssize_t
write_escaped(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  EscapedWriter* ew = (EscapedWriter*)fd;
  const uint8_t* x = buf;
  ssize_t r = 0;

  for(size_t i = 0; i < len; i++) {
    if(byte_chr(ew->chars, ew->nchars, x[i]) < ew->nchars)
      RESULT_LOOP(writer_putc(ew->parent, '\\'), r);

    RESULT_LOOP(writer_putc(ew->parent, x[i]), r);
  }

  return r;
}

static ssize_t
write_urlencoded(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Writer* parent = (Writer*)fd;
  const uint8_t* x = buf;
  ssize_t r = 0;
  static char const unescaped_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                        "abcdefghijklmnopqrstuvwxyz"
                                        "0123456789"
                                        "@*_+-./";

  for(size_t i = 0; i < len; i++) {
    if(!memchr(unescaped_chars, x[i], sizeof(unescaped_chars) - 1)) {
      char esc[4] = {'%'};

      fmt_xlong0(&esc[1], x[i], 2);

      RESULT_LOOP(writer_write(parent, esc, 3), r);
      continue;
    }

    RESULT_LOOP(writer_putc(parent, x[i]), r);
  }

  return r;
}

static ssize_t
write_buffered(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Buffered* b = (Buffered*)fd;
  ssize_t ret = 0;

  if(b->pos) {
    size_t remain = MAX_NUM(b->len - b->pos, len);

    memcpy(&b->buf[b->pos], buf, remain);

    if(writer_write(b->other, b->buf, b->pos + remain) != b->pos + remain)
      return -1;

    ret += b->pos + remain;
    b->pos = 0;

    len -= remain;
    buf = (uint8_t*)buf + remain;
  }

  if(len > b->len) {
    if(writer_write(b->other, buf, len) != len)
      return -1;
  } else {
    memcpy(b->buf, buf, len);
    b->pos = len;
  }

  ret += len;

  return len;
}

static ssize_t
write_linebuffered(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Buffered* b = (Buffered*)fd;
  const uint8_t* x = buf;
  ssize_t ret = 0;

  for(size_t i = 0; i < len; i++) {
    if(b->pos == b->len) {
      if(writer_write(b->writer, b->buf, b->pos) != (ssize_t)b->pos)
        return -1;

      b->pos = 0;
    }

    b->buf[b->pos++] = x[i];

    if(x[i] == '\n') {
      if(writer_write(b->writer, b->buf, b->pos) != (ssize_t)b->pos)
        return -1;

      b->pos = 0;
    }

    ret++;
  }

  return ret;
}

static ssize_t
write_jsfunc(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  JSFunc* jsfw = (JSFunc*)fd;
  JSContext* ctx = jsfw->ctx;
  JSValueConst args[2] = {
      JS_NewArrayBuffer(ctx, (uint8_t*)buf, len, 0, 0, FALSE),
      JS_NewUint32(ctx, len),
  };

  JSValue ret = JS_Call(ctx, jsfw->funcObj, jsfw->thisObj, 2, args);

  JS_DetachArrayBuffer(ctx, args[0]);

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);

  if(JS_IsException(ret))
    return -1;

  JS_FreeValue(ctx, ret);
  return len;
}

static ssize_t
read_dynbuf(intptr_t fd, void* buf, size_t len, Reader* rd) {
  DynBuf* db = (DynBuf*)fd;

  size_t pos = (size_t)rd->opaque2;
  ssize_t n = MAX_NUM(len, db->size - pos);

  if(n) {
    memcpy(buf, &db->buf[pos], n);
    pos += n;
  }

  if(pos == db->size)
    pos = 0;

  rd->opaque2 = (void*)pos;

  return n;
}

static ssize_t
read_urldecoded(intptr_t fd, void* buf, size_t len, struct StreamReader* rd) {
  Reader* parent = (Reader*)fd;
  uint8_t* x = buf;
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
read_bytes(intptr_t fd, void* buf, size_t len, struct StreamReader* rd) {
  const uint8_t *start = rd->opaque, *end = rd->opaque2;
  size_t remain = end - start;

  if(len > remain)
    len = remain;

  if(len)
    memcpy(buf, start, len);

  start += len;
  rd->opaque = (void*)start;

  return len;
}

static ssize_t
read_jsfunc(intptr_t fd, void* buf, size_t len, Reader* rd) {
  JSFunc* jsfr = (JSFunc*)fd;
  JSContext* ctx = jsfr->ctx;
  JSValueConst args[2] = {
      JS_NewArrayBuffer(ctx, buf, len, 0, 0, FALSE),
      JS_NewUint32(ctx, len),
  };

  JSValue ret = JS_Call(ctx, jsfr->funcObj, jsfr->thisObj, 2, args);

  JS_DetachArrayBuffer(ctx, args[0]);

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);

  if(JS_IsException(ret))
    return -1;

  int32_t r = -1;
  JS_ToInt32(ctx, &r, ret);
  JS_FreeValue(ctx, ret);

  return r;
}

static ssize_t
read_buffered(intptr_t fd, void* buf, size_t len, Reader* rd) {
  Buffered* b = (Buffered*)fd;
  ssize_t ret = 0;

  if(b->pos < b->len) {
    size_t remain = b->len - b->pos;

    if(reader_read(b->reader, b->buf, remain) != remain)
      return -1;

    buf = (uint8_t*)buf + remain;
    len -= remain;
  }

  if(b->pos > 0) {
    memcpy(buf, b->buf, b->pos);
    ret = b->pos;
    b->pos = 0;
  }

  return ret;
}

static ssize_t
read_linebuffered(intptr_t fd, void* buf, size_t len, Reader* rd) {
  Buffered* b = (Buffered*)fd;
  size_t eol;

  for(;;) {
    if((eol = byte_chr(b->buf, b->pos, '\n')) < b->pos) {
      eol++;
      break;
    }

    if(b->pos == b->len) {
      eol = b->pos;
      break;
    }

    ssize_t r = reader_read(b->reader, &b->buf[b->pos], b->len - b->pos);

    if(r < 0)
      return r;

    if(r == 0) {
      eol = b->pos;
      break;
    }

    b->pos += r;
  }

  if(eol > len)
    eol = len;

  memcpy(buf, b->buf, eol);
  memmove(b->buf, &b->buf[eol], b->pos - eol);
  b->pos -= eol;

  return eol;
}

static void
close_jsfunc(void* opaque, void* opaque2) {
  JSFunc* jsf = opaque;
  JSContext* ctx = jsf->ctx;

  JS_FreeValue(ctx, jsf->funcObj);
  JS_FreeValue(ctx, jsf->thisObj);
  free(jsf);

  JS_FreeContext(ctx);
}

static void
close_buffered(void* opaque, void* opaque2) {
  Buffered* b = opaque;

  if(b->pos > 0) {
    writer_write(b->other, b->buf, b->pos);
    b->pos = 0;
  }

  free(b);
}

/**
 * \addtogroup stream-utils
 * @{
 */
Writer
writer_from_dynbuf(DynBuf* db) {
  return (Writer){&write_dynbuf, db, (WriterFinalizer*)(void*)&dbuf_free};
}

Writer
writer_from_buf(OutputBuffer* buf) {
  return (Writer){(WriteFunction*)&outputbuffer_write, buf, NULL};
}

Writer
writer_from_fd(intptr_t fd, bool close_on_end) {
  return (Writer){
      (WriteFunction*)(void*)&write,
      (void*)fd,
      close_on_end ? (WriterFinalizer*)(void*)&close : NULL,
  };
}

Writer
writer_from_jsfunc(JSContext* ctx, JSValueConst fn) {
  return writer_from_jsfunc2(ctx, fn, JS_UNDEFINED);
}

Writer
writer_from_jsfunc2(JSContext* ctx, JSValueConst funcObj, JSValueConst thisObj) {
  JSFunc* jsfw = malloc(sizeof(JSFunc));

  assert(jsfw);

  *jsfw = (JSFunc){JS_DupContext(ctx), JS_DupValue(ctx, funcObj), JS_DupValue(ctx, thisObj)};

  return (Writer){
      &write_jsfunc,
      jsfw,
      (WriterFinalizer*)(void*)&close_jsfunc,
  };
}

Writer
writer_buffered(Writer* other, size_t buf_size) {
  Buffered* b = malloc(sizeof(Buffered) + buf_size);

  assert(b);

  *b = (Buffered){(uint8_t*)&b[1], buf_size, 0, {other}};

  return (Writer){
      &write_buffered,
      b,
      (WriterFinalizer*)(void*)&close_buffered,
  };
}

Writer
writer_linebuffered(Writer* other, size_t buf_size) {
  Buffered* b = malloc(sizeof(Buffered) + buf_size);

  assert(b);

  *b = (Buffered){(uint8_t*)&b[1], buf_size, 0, {other}};

  return (Writer){
      &write_linebuffered,
      b,
      (WriterFinalizer*)(void*)&close_buffered,
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
      &write_tee,
      opaque,
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
      &write_escaped,
      ew,
      (WriterFinalizer*)(void*)&orig_free,
  };
}

Writer
writer_urlencode(Writer* out) {
  return (Writer){
      &write_urlencoded,
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
reader_from_dynbuf(DynBuf* db) {
  return (Reader){&read_dynbuf, db, NULL, (ReaderFinalizer*)(void*)&dbuf_free};
}

Reader
reader_from_buf(InputBuffer* buf, JSContext* ctx) {
  return (Reader){
      (ReadFunction*)&inputbuffer_read,
      buf,
      ctx,
      0,
  };
}

Reader
reader_from_bytes(const void* start, size_t len) {
  return (Reader){
      &read_bytes,
      (void*)start,
      ((uint8_t*)start) + len,
      NULL,
  };
}

Reader
reader_from_fd(intptr_t fd, bool close_on_end) {
  return (Reader){
      (ReadFunction*)(void*)&read,
      (void*)(intptr_t)fd,
      NULL,
      close_on_end ? (ReaderFinalizer*)(void*)&close : NULL,
  };
}

Reader
reader_from_jsfunc(JSContext* ctx, JSValueConst funcObj) {
  return reader_from_jsfunc2(ctx, funcObj, JS_UNDEFINED);
}

Reader
reader_from_jsfunc2(JSContext* ctx, JSValueConst funcObj, JSValueConst thisObj) {
  JSFunc* jsfr = malloc(sizeof(JSFunc));

  assert(jsfr);

  *jsfr = (JSFunc){JS_DupContext(ctx), JS_DupValue(ctx, funcObj), JS_DupValue(ctx, thisObj)};

  return (Reader){
      &read_jsfunc,
      jsfr,
      NULL,
      &close_jsfunc,
  };
}

Reader
reader_buffered(Reader* other, size_t buf_size) {
  Buffered* b = malloc(sizeof(Buffered) + buf_size);

  assert(b);

  *b = (Buffered){(uint8_t*)&b[1], buf_size, 0, {other}};

  return (Reader){
      &read_buffered,
      b,
      NULL,
      &close_buffered,
  };
}

Reader
reader_linebuffered(Reader* other, size_t buf_size) {
  Buffered* b = malloc(sizeof(Buffered) + buf_size);

  assert(b);

  *b = (Buffered){(uint8_t*)&b[1], buf_size, 0, {other}};

  return (Reader){
      &read_linebuffered,
      b,
      NULL,
      &close_buffered,
  };
}

Reader
reader_urldecode(Reader* parent) {
  return (Reader){
      &read_urldecoded,
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
