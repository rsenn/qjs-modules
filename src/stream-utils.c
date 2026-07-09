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
  JSValue func_obj, this_obj;
} JSFunc;

typedef struct {
  uint8_t* buf;
  size_t len, pos;
  union {
    void* parent;
    Writer* writer;
    Reader* reader;
  };
} Buffered;

typedef struct {
  Location* lo;
  size_t buflen;
  uint8_t buf[8];
  union {
    void* parent;
    Writer* writer;
    Reader* reader;
  };
} Tracker;

typedef struct {
  Writer* parent;
  const char* chars;
  size_t nchars;
} Escaper;

static ssize_t
write_dynbuf(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  DynBuf* db = (DynBuf*)fd;

  if(dbuf_put(db, buf, len))
    return -1;

  return len;
}

static ssize_t
write_tee(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Writer* parent = (Writer*)fd;
  ssize_t written[2] = {0, 0};

  RESULT(writer_write(&parent[0], buf, len), written[0]);
  RESULT(writer_write(&parent[1], buf, len), written[1]);

  return MIN_NUM(written[0], written[1]);
}

static ssize_t
write_escaped(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Escaper* esc = (Escaper*)fd;
  const uint8_t* x = buf;
  ssize_t written = 0;

  for(size_t i = 0; i < len; i++) {
    if(byte_chr(esc->chars, esc->nchars, x[i]) < esc->nchars)
      RESULT_LOOP(writer_putc(esc->parent, '\\'), written);

    RESULT_LOOP(writer_putc(esc->parent, x[i]), written);
  }

  return written;
}

static ssize_t
write_urlencoded(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Writer* parent = (Writer*)fd;
  const uint8_t* x = buf;
  ssize_t written = 0;
  static char const unescaped_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                        "abcdefghijklmnopqrstuvwxyz"
                                        "0123456789"
                                        "@*_+-./";

  for(size_t i = 0; i < len; i++) {
    if(!memchr(unescaped_chars, x[i], sizeof(unescaped_chars) - 1)) {
      char esc[4] = {'%'};

      fmt_xlong0(&esc[1], x[i], 2);

      RESULT_LOOP(writer_write(parent, esc, 3), written);
      continue;
    }

    RESULT_LOOP(writer_putc(parent, x[i]), written);
  }

  return written;
}

static ssize_t
write_buffered(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Buffered* b = (Buffered*)fd;
  ssize_t written = 0;

  if(b->pos) {
    size_t headroom = b->len - b->pos;
    size_t remain = MIN_NUM(headroom, len);

    memcpy(&b->buf[b->pos], buf, remain);

    size_t bytes = b->pos + remain;

    if(writer_write(b->parent, b->buf, bytes) != (ssize_t)bytes)
      return -1;

    written += b->pos + remain;
    b->pos = 0;

    len -= remain;
    buf = (uint8_t*)buf + remain;
  }

  if(len > b->len) {
    if(writer_write(b->parent, buf, len) != (ssize_t)len)
      return -1;
  } else {
    memcpy(b->buf, buf, len);
    b->pos = len;
  }

  written += len;

  return written;
}

static ssize_t
write_linebuffered(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Buffered* b = (Buffered*)fd;
  const uint8_t* ptr = buf;
  ssize_t written = 0;

  for(size_t i = 0; i < len; i++) {
    if(b->pos == b->len) {
      if(writer_write(b->writer, b->buf, b->pos) != (ssize_t)b->pos)
        return -1;

      b->pos = 0;
    }

    b->buf[b->pos++] = ptr[i];

    if(ptr[i] == '\n') {
      if(writer_write(b->writer, b->buf, b->pos) != (ssize_t)b->pos)
        return -1;

      b->pos = 0;
    }

    written++;
  }

  return written;
}

static ssize_t
write_function(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  JSFunc* fw = (JSFunc*)fd;
  JSContext* ctx = fw->ctx;
  JSValueConst args[2] = {
      JS_NewArrayBuffer(ctx, (uint8_t*)buf, len, 0, 0, FALSE),
      JS_NewUint32(ctx, len),
  };
  JSValue ret = JS_Call(ctx, fw->func_obj, fw->this_obj, 2, args);

  JS_DetachArrayBuffer(ctx, args[0]);

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);

  if(JS_IsException(ret))
    return -1;

  JS_FreeValue(ctx, ret);
  return len;
}

static ssize_t
write_location(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Tracker* tr = (Tracker*)fd;
  const uint8_t *start = buf, *ptr = buf, *end;
  Location* lo = tr->lo;
  size_t buffered = tr->buflen;
  int cp;

  if(buffered) {
    size_t headroom = sizeof(tr->buf) - tr->buflen;
    size_t remain = MIN_NUM(len, headroom);

    memcpy(&tr->buf[tr->buflen], ptr, remain);
    tr->buflen += remain;

    if((cp = unicode_from_utf8(tr->buf, tr->buflen, &end)) == -1)
      return -1;

    size_t bytes = end - tr->buf;
    size_t charlen = location_nextchar(lo, cp);

    assert(charlen == bytes);

    if(writer_write(tr->writer, tr->buf, buffered) != (ssize_t)buffered)
      return -1;

    tr->buflen = 0;

    bytes -= buffered;

    ptr += bytes;
    len -= bytes;
  }

  while(len > 0) {
    if((cp = unicode_from_utf8(ptr, len, &end)) == -1)
      break;

    size_t bytes = end - ptr;
    size_t charlen = location_nextchar(lo, cp);

    assert(charlen == bytes);

    ptr += charlen;
    len -= charlen;
  }

  if(ptr > start)
    if(writer_write(tr->writer, start, ptr - start) < 0)
      return -1;

  if(len > 0) {
    assert(tr->buflen == 0);
    assert(len <= sizeof(tr->buf));

    size_t remain = MIN_NUM(sizeof(tr->buf), len);

    if(remain) {
      memcpy(&tr->buf[tr->buflen], ptr, remain);
      tr->buflen += remain;

      ptr += remain;
      len -= remain;
    }
  }

  return ptr - start;
}

static void
close_function(void* opaque, void* opaque2) {
  JSFunc* jsf = opaque;
  JSContext* ctx = jsf->ctx;

  JS_FreeValue(ctx, jsf->func_obj);
  JS_FreeValue(ctx, jsf->this_obj);
  free(jsf);

  JS_FreeContext(ctx);
}

static void
close_buffered(void* opaque, void* opaque2) {
  Buffered* b = opaque;

  if(b->pos > 0) {
    writer_write(b->parent, b->buf, b->pos);
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
  return (Writer){&write_dynbuf, db, (WriterFinalizer*)&dbuf_free};
}

Writer
writer_from_buf(OutputBuffer* buf) {
  return (Writer){(WriteFunction*)&outputbuffer_write, buf, NULL};
}

Writer
writer_from_fd(intptr_t fd, bool close_on_end) {
  return (Writer){
      (WriteFunction*)&write,
      (void*)fd,
      close_on_end ? (WriterFinalizer*)&close : NULL,
  };
}

Writer
writer_from_function(JSContext* ctx, JSValueConst fn) {
  return writer_from_method(ctx, fn, JS_UNDEFINED);
}

Writer
writer_from_method(JSContext* ctx, JSValueConst func_obj, JSValueConst this_obj) {
  JSFunc* fw = malloc(sizeof(JSFunc));

  assert(fw);

  *fw = (JSFunc){JS_DupContext(ctx), JS_DupValue(ctx, func_obj), JS_DupValue(ctx, this_obj)};

  return (Writer){
      &write_function,
      fw,
      (WriterFinalizer*)&close_function,
  };
}

Writer
writer_buffered(Writer* parent, size_t buf_size) {
  Buffered* b = malloc(sizeof(Buffered) + buf_size);

  assert(b);

  *b = (Buffered){(uint8_t*)&b[1], buf_size, 0, {parent}};

  return (Writer){
      &write_buffered,
      b,
      (WriterFinalizer*)&close_buffered,
  };
}

Writer
writer_linebuffered(Writer* parent, size_t buf_size) {
  Buffered* b = malloc(sizeof(Buffered) + buf_size);

  assert(b);

  *b = (Buffered){(uint8_t*)&b[1], buf_size, 0, {parent}};

  return (Writer){
      &write_linebuffered,
      b,
      (WriterFinalizer*)&close_buffered,
  };
}

Writer
writer_tee(const Writer a, const Writer b) {
  Writer* parent;

  if((parent = malloc(sizeof(Writer) * 2))) {
    parent[0] = a;
    parent[1] = b;
  }

  assert(parent);

  return (Writer){
      &write_tee,
      parent,
      (WriterFinalizer*)&orig_free,
  };
}

Writer
writer_escaped(Writer* out, const char* chars, size_t nchars) {
  Escaper* esc;

  if((esc = malloc(sizeof(Escaper)))) {
    esc->parent = out;
    esc->chars = chars;
    esc->nchars = nchars;
  }

  return (Writer){
      &write_escaped,
      esc,
      (WriterFinalizer*)&orig_free,
  };
}

Writer
writer_urlencode(Writer* out) {
  return (Writer){
      &write_urlencoded,
      out,
      NULL,
  };
}

Writer
writer_location(Writer* parent, Location* lo) {
  Tracker* tr = malloc(sizeof(Tracker));

  assert(tr);

  *tr = (Tracker){lo, 0, {}, {parent}};

  return (Writer){
      &write_location,
      tr,
      (WriterFinalizer*)&orig_free,
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

/**
 * @}
 */

static ssize_t
read_dynbuf(intptr_t fd, void* buf, size_t len, Reader* rd) {
  DynBuf* db = (DynBuf*)fd;
  size_t pos = (size_t)rd->opaque2;
  size_t headroom = db->size - pos;
  size_t remain = MIN_NUM(len, headroom);

  if(remain) {
    memcpy(buf, &db->buf[pos], remain);
    pos += remain;
  }

  if(pos == db->size)
    pos = 0;

  rd->opaque2 = (void*)pos;

  return remain;
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
read_function(intptr_t fd, void* buf, size_t len, Reader* rd) {
  JSFunc* fr = (JSFunc*)fd;
  JSContext* ctx = fr->ctx;
  JSValue args[2] = {
      JS_NewArrayBuffer(ctx, buf, len, 0, 0, FALSE),
      JS_NewUint32(ctx, len),
  };
  JSValue ret = JS_Call(ctx, fr->func_obj, fr->this_obj, 2, args);

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
  uint8_t* ptr = buf;
  ssize_t remain, bytes;

  while(len) {
    if(b->pos > 0) {
      size_t n = MIN_NUM(b->pos, len);

      memcpy(ptr, b->buf, n);

      ptr += n;
      len -= n;

      if((remain = b->pos - n) > 0)
        memmove(b->buf, &b->buf[n], remain);

      b->pos = remain;
    }

    if(len == 0)
      break;

    if((remain = b->len - b->pos) > 0) {
      if((bytes = reader_read(b->reader, &b->buf[b->pos], remain)) < 0)
        if(ptr == (uint8_t*)buf)
          return -1;

      b->pos += bytes;

      if(bytes <= 0)
        break;
    }
  }

  return ptr - (uint8_t*)buf;
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

static ssize_t
read_location(intptr_t fd, void* buf, size_t len, Reader* rd) {
  Tracker* tr = (Tracker*)fd;
  const uint8_t* next;
  uint8_t* ptr = buf;
  ssize_t ret = 0, bytes;
  Location* lo = tr->lo;

  while(len > 0) {
    size_t headroom = sizeof(tr->buf) - tr->buflen;
    size_t remain = MIN_NUM(len, headroom);

    if((bytes = reader_read(tr->reader, &tr->buf[tr->buflen], remain)) < 0)
      return -1;

    if(bytes == 0 && tr->buflen == 0)
      break;

    tr->buflen += bytes;

    int cp = unicode_from_utf8(tr->buf, tr->buflen, &next);

    if(cp == -1 || !(bytes = next - tr->buf))
      break;

    memcpy(ptr, tr->buf, bytes);

    ptr += bytes;
    len -= bytes;

    memmove(tr->buf, &tr->buf[bytes], sizeof(tr->buf) - bytes);
    tr->buflen -= bytes;

    location_nextchar(lo, cp);

    ret += bytes;
  }

  return ret;
}

/**
 * \addtogroup stream-utils
 * @{
 */
Reader
reader_from_dynbuf(DynBuf* db) {
  return (Reader){&read_dynbuf, db, NULL, (ReaderFinalizer*)&dbuf_free};
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
      (ReadFunction*)&read,
      (void*)fd,
      NULL,
      close_on_end ? (ReaderFinalizer*)&close : NULL,
  };
}

Reader
reader_from_function(JSContext* ctx, JSValueConst func_obj) {
  return reader_from_method(ctx, func_obj, JS_UNDEFINED);
}

Reader
reader_from_method(JSContext* ctx, JSValueConst func_obj, JSValueConst this_obj) {
  JSFunc* fr = malloc(sizeof(JSFunc));

  assert(fr);

  *fr = (JSFunc){JS_DupContext(ctx), JS_DupValue(ctx, func_obj), JS_DupValue(ctx, this_obj)};

  return (Reader){
      &read_function,
      fr,
      NULL,
      &close_function,
  };
}

Reader
reader_buffered(Reader* parent, size_t buf_size) {
  Buffered* b = malloc(sizeof(Buffered) + buf_size);

  assert(b);

  *b = (Buffered){(uint8_t*)&b[1], buf_size, 0, {parent}};

  return (Reader){
      &read_buffered,
      b,
      NULL,
      &close_buffered,
  };
}

Reader
reader_linebuffered(Reader* parent, size_t buf_size) {
  Buffered* b = malloc(sizeof(Buffered) + buf_size);

  assert(b);

  *b = (Buffered){(uint8_t*)&b[1], buf_size, 0, {parent}};

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

Reader
reader_location(Reader* parent, Location* lo) {
  Tracker* tr = malloc(sizeof(Tracker));

  assert(tr);

  *tr = (Tracker){lo, 0, {}, {parent}};

  return (Reader){
      &read_location,
      tr,
      NULL,
      (ReaderFinalizer*)&orig_free,
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
