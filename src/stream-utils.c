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
  } while(0)

typedef struct {
  JSContext* ctx;
  JSValue func_obj, this_obj;
} JSFunc;

typedef struct {
  uint64_t *bytes_ptr, *characters_ptr;
  size_t buflen;
  uint8_t buf[8];
  void* parent;
} Counted;

typedef struct {
  uint8_t* buf;
  size_t len, pos;
  void* parent;
} Buffered;

typedef struct {
  Location* lo;
  size_t buflen;
  uint8_t buf[8];
  void* parent;
} Tracker;

typedef struct {
  Writer* parent;
  const char* chars;
  size_t nchars;
} Escaper;

typedef struct {
  Reader* parent;
  uint8_t pending[2];
  size_t npending;
} URLDecoder;

/* Completes the partial UTF-8 character in buf[]/(*buflen) with bytes from ptr/len.
 * Returns the number of bytes consumed from ptr once a complete character is
 * available (the buffer is reset), 0 if more input is needed (the partial bytes
 * from ptr have been added to the buffer), -1 on invalid UTF-8 (buffer unchanged). */
static inline ssize_t
buffer_character(uint8_t buf[8], size_t* buflen, const uint8_t* ptr, size_t len) {
  const uint8_t* next;
  size_t buffered;

  if((buffered = *buflen) > 0) {
    size_t needed = utf8_needed(buf[0]);

    if(needed == 0)
      return -1;

    if(buffered + len < needed) {
      memcpy(&buf[buffered], ptr, len);
      *buflen += len;
      return 0;
    }

    size_t take = needed - buffered;

    memcpy(&buf[buffered], ptr, take);

    if(unicode_from_utf8(buf, needed, &next) == -1)
      return -1;

    *buflen = 0;
    return take;
  }

  if(len == 0)
    return 0;

  size_t needed = utf8_needed(ptr[0]);

  if(needed == 0)
    return -1;

  if(needed > len) {
    memcpy(buf, ptr, len);
    *buflen = len;
    return 0;
  }

  if(unicode_from_utf8(ptr, needed, &next) == -1)
    return -1;

  return needed;
}

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

  for(size_t i = 0; i < len; i++) {
    ssize_t r;

    if(byte_chr(esc->chars, esc->nchars, x[i]) < esc->nchars)
      if((r = writer_putc(esc->parent, '\\')) <= 0)
        return i ? (ssize_t)i : r;

    if((r = writer_putc(esc->parent, x[i])) <= 0)
      return i ? (ssize_t)i : r;
  }

  return len;
}

static ssize_t
write_urlencoded(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Writer* parent = (Writer*)fd;
  const uint8_t* x = buf;
  static char const unescaped_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                        "abcdefghijklmnopqrstuvwxyz"
                                        "0123456789"
                                        "@*_+-./";

  for(size_t i = 0; i < len; i++) {
    ssize_t r;

    if(!memchr(unescaped_chars, x[i], sizeof(unescaped_chars) - 1)) {
      char esc[4] = {'%'};

      fmt_xlong0(&esc[1], x[i], 2);

      if((r = writer_write(parent, esc, 3)) != 3)
        return i ? (ssize_t)i : (r < 0 ? r : 0);
    } else if((r = writer_putc(parent, x[i])) <= 0) {
      return i ? (ssize_t)i : r;
    }
  }

  return len;
}

static ssize_t
write_counted(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Counted* c = (Counted*)fd;
  const uint8_t* ptr = buf;
  ssize_t r;

  if((r = writer_write(c->parent, ptr, len)) > 0) {
    if(c->bytes_ptr)
      (*c->bytes_ptr) += r;

    if(c->characters_ptr) {
      ssize_t bytes;
      size_t remain = r;

      while((bytes = buffer_character(c->buf, &c->buflen, ptr, remain))) {
        if(bytes < 0) {
          /* invalid UTF-8: count the bogus sequence as one character and resync */
          (*c->characters_ptr)++;

          if(c->buflen > 0) {
            c->buflen = 0;
          } else {
            ptr++;
            remain--;
          }

          continue;
        }

        (*c->characters_ptr)++;
        ptr += bytes;
        remain -= bytes;
      }
    }
  }

  return r;
}

static ssize_t
write_buffered(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Buffered* b = (Buffered*)fd;
  const uint8_t* x = buf;
  size_t consumed = 0;

  while(consumed < len) {
    if(b->pos == b->len) {
      if(writer_write(b->parent, b->buf, b->pos) != (ssize_t)b->pos)
        return -1;

      b->pos = 0;
    }

    if(b->pos == 0 && len - consumed >= b->len) {
      size_t n = len - consumed;

      if(writer_write(b->parent, &x[consumed], n) != (ssize_t)n)
        return -1;

      consumed += n;
    } else {
      size_t n = MIN_NUM(b->len - b->pos, len - consumed);

      memcpy(&b->buf[b->pos], &x[consumed], n);
      b->pos += n;
      consumed += n;
    }
  }

  return consumed;
}

static ssize_t
write_linebuffered(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Buffered* b = (Buffered*)fd;
  const uint8_t* ptr = buf;
  ssize_t written = 0;

  for(size_t i = 0; i < len; i++) {
    if(b->pos == b->len) {
      if(writer_write(b->parent, b->buf, b->pos) != (ssize_t)b->pos)
        return -1;

      b->pos = 0;
    }

    b->buf[b->pos++] = ptr[i];

    if(ptr[i] == '\n') {
      if(writer_write(b->parent, b->buf, b->pos) != (ssize_t)b->pos)
        return -1;

      b->pos = 0;
    }

    written++;
  }

  return written;
}

static ssize_t
write_jsfunction(intptr_t fd, const void* buf, size_t len, Writer* wr) {
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
  int cp, invalid = 0;

  if(tr->buflen) {
    size_t buffered = tr->buflen;
    size_t needed = utf8_needed(tr->buf[0]);

    if(buffered + len < needed) { /* character still incomplete: buffer everything */
      memcpy(&tr->buf[buffered], ptr, len);
      tr->buflen += len;
      return len;
    }

    size_t take = needed - buffered;

    memcpy(&tr->buf[buffered], ptr, take);

    if((cp = unicode_from_utf8(tr->buf, needed, &end)) == -1)
      return -1;

    /* the completion bytes are part of the input and are covered by the bulk
       write below; only the prefix buffered by previous calls is written here */
    if(writer_write(tr->parent, tr->buf, buffered) != (ssize_t)buffered)
      return -1;

    location_nextchar(lo, cp);

    tr->buflen = 0;
    ptr += take;
    len -= take;
  }

  while(len > 0) {
    size_t needed = utf8_needed(*ptr);

    if(needed == 0 || needed > len) {
      invalid = needed == 0;
      break;
    }

    if((cp = unicode_from_utf8(ptr, needed, &end)) == -1) {
      invalid = 1;
      break;
    }

    location_nextchar(lo, cp);

    ptr += needed;
    len -= needed;
  }

  if(ptr > start)
    if(writer_write(tr->parent, start, ptr - start) != (ssize_t)(ptr - start))
      return -1;

  if(invalid)
    return ptr > start ? ptr - start : -1;

  if(len > 0) { /* incomplete final character: keep it for the next write */
    assert(tr->buflen == 0);
    assert(len < sizeof(tr->buf));

    memcpy(tr->buf, ptr, len);
    tr->buflen = len;
    ptr += len;
  }

  return ptr - start;
}

static void
close_dynbuf(void* opaque) {
  dbuf_free(opaque);
}

static void
close_jsfunction(void* opaque) {
  JSFunc* jsf = opaque;
  JSContext* ctx = jsf->ctx;

  JS_FreeValue(ctx, jsf->func_obj);
  JS_FreeValue(ctx, jsf->this_obj);
  free(jsf);

  JS_FreeContext(ctx);
}

static void
close_buffered(void* opaque) {
  Buffered* b = opaque;
  ssize_t r;

  if(b->pos > 0)
    if((r = writer_write(b->parent, b->buf, b->pos)) > 0)
      b->pos -= r;

  free(b);
}

static void
close_tee(void* opaque) {
  Writer* w = opaque;

  writer_free(&w[0]);
  writer_free(&w[1]);
  free(w);
}

static void
reader_jsbuf_free(void* opaque, void* opaque2) {
  InputBuffer* input = opaque;
  JSContext* ctx = opaque2;

  inputbuffer_free(input, ctx);
  js_free(ctx, input);
}

static void
writer_jsbuf_free(void* opaque) {
  OutputBuffer* output = opaque;
  outputbuffer_free(output, 0);
  free(output);
}

/**
 * \addtogroup stream-utils
 * @{
 */
Writer
writer_from_dynbuf(DynBuf* db) {
  return (Writer){&write_dynbuf, db, &close_dynbuf};
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
writer_from_jsbuf(JSContext* ctx, JSValueConst value) {
  OutputBuffer* output = malloc(sizeof(OutputBuffer));

  assert(output);

  *output = js_output_typedarray(ctx, value);

  Writer wr = writer_from_buf(output);
  wr.finalizer = &writer_jsbuf_free;
  return wr;
}

Writer
writer_from_jsfunction(JSContext* ctx, JSValueConst fn) {
  return writer_from_jsmethod(ctx, fn, JS_UNDEFINED);
}

Writer
writer_from_jsmethod(JSContext* ctx, JSValueConst func_obj, JSValueConst this_obj) {
  JSFunc* fw = malloc(sizeof(JSFunc));

  assert(fw);

  *fw = (JSFunc){JS_DupContext(ctx), JS_DupValue(ctx, func_obj), JS_DupValue(ctx, this_obj)};

  return (Writer){
      &write_jsfunction,
      fw,
      &close_jsfunction,
  };
}

Writer
writer_counted(Writer* parent, uint64_t* bytes_ptr, uint64_t* characters_ptr) {
  Counted* c = malloc(sizeof(Counted));

  assert(c);

  *c = (Counted){bytes_ptr, characters_ptr, 0, {}, parent};

  return (Writer){
      &write_counted,
      c,
      (WriterFinalizer*)&orig_free,
  };
}

Writer
writer_buffered(Writer* parent, size_t buf_size) {
  Buffered* b = malloc(sizeof(Buffered) + buf_size);

  assert(b);

  *b = (Buffered){(uint8_t*)&b[1], buf_size, 0, parent};

  return (Writer){
      &write_buffered,
      b,
      &close_buffered,
  };
}

Writer
writer_linebuffered(Writer* parent, size_t buf_size) {
  Buffered* b = malloc(sizeof(Buffered) + buf_size);

  assert(b);

  *b = (Buffered){(uint8_t*)&b[1], buf_size, 0, parent};

  return (Writer){
      &write_linebuffered,
      b,
      &close_buffered,
  };
}

Writer
writer_tee(const Writer a, const Writer b) {
  Writer* parent = malloc(sizeof(Writer) * 2);

  assert(parent);

  parent[0] = a;
  parent[1] = b;

  return (Writer){
      &write_tee,
      parent,
      &close_tee,
  };
}

Writer
writer_escaped(Writer* out, const char* chars, size_t nchars) {
  Escaper* esc = malloc(sizeof(Escaper));

  assert(esc);

  *esc = (Escaper){out, chars, nchars};

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

  *tr = (Tracker){lo, 0, {}, parent};

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

ssize_t
writer_flush(Writer* wr) {
  if(wr->finalizer == &close_buffered) {
    Buffered* b = wr->opaque;
    ssize_t ret = 0;

    if(b->pos > 0)
      if((ret = writer_write(b->parent, b->buf, b->pos)) > 0)
        b->pos -= ret;
  }

  return -1;
}

/**
 * @}
 */

static ssize_t
read_dynbuf(intptr_t fd, void* buf, size_t len, Reader* rd) {
  DynBuf* db = (DynBuf*)fd;
  size_t remain, pos = (size_t)rd->opaque2;
  size_t headroom = db->size - pos;

  if((remain = MIN_NUM(len, headroom))) {
    memcpy(buf, &db->buf[pos], remain);
    pos += remain;
    rd->opaque2 = (void*)pos;
  }

  return remain;
}

static ssize_t
read_urldecoded(intptr_t fd, void* buf, size_t len, struct StreamReader* rd) {
  URLDecoder* u = (URLDecoder*)fd;
  uint8_t *x = buf, *y = x;

  while(len > 0) {
    uint8_t c;
    ssize_t r;

    if(u->npending) {
      *x++ = u->pending[0];
      u->pending[0] = u->pending[1];
      u->npending--;
      len--;
      continue;
    }

    if((r = reader_read(u->parent, &c, 1)) <= 0)
      return x > y ? x - y : r;

    if(c == '%') {
      uint8_t hi, lo;

      if((r = reader_read(u->parent, &hi, 1)) < 0)
        return x > y ? x - y : r;

      if(r == 0) {
        *x++ = '%';
        len--;
        continue;
      }

      if(hi != '%') {
        if((r = reader_read(u->parent, &lo, 1)) < 0)
          return x > y ? x - y : r;

        int h = scan_fromhex(hi);
        int l = r == 0 ? -1 : scan_fromhex(lo);

        if(h >= 0 && l >= 0) {
          c = (h << 4) | l;
        } else {
          /* not a valid escape: emit it literally */
          u->pending[0] = hi;
          u->npending = 1;

          if(r > 0) {
            u->pending[1] = lo;
            u->npending = 2;
          }

          c = '%';
        }
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
  size_t remain;

  if(len > (remain = end - start))
    len = remain;

  if(len)
    memcpy(buf, start, len);

  start += len;
  rd->opaque = (void*)start;

  return len;
}

static ssize_t
read_jsfunction(intptr_t fd, void* buf, size_t len, Reader* rd) {
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

  if(r > 0 && (size_t)r > len)
    r = len;

  return r;
}

static ssize_t
read_counted(intptr_t fd, void* buf, size_t len, Reader* rd) {
  Counted* c = (Counted*)fd;
  uint8_t* ptr = buf;
  ssize_t r;

  if((r = reader_read(c->parent, ptr, len)) > 0) {
    if(c->bytes_ptr)
      (*c->bytes_ptr) += r;

    if(c->characters_ptr) {
      ssize_t bytes;
      size_t remain = r;

      while((bytes = buffer_character(c->buf, &c->buflen, ptr, remain))) {
        if(bytes < 0) {
          /* invalid UTF-8: count the bogus sequence as one character and resync */
          (*c->characters_ptr)++;

          if(c->buflen > 0) {
            c->buflen = 0;
          } else {
            ptr++;
            remain--;
          }

          continue;
        }

        (*c->characters_ptr)++;
        ptr += bytes;
        remain -= bytes;
      }
    }
  }

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
      if((bytes = reader_read(b->parent, &b->buf[b->pos], remain)) < 0)
        return ptr > (uint8_t*)buf ? ptr - (uint8_t*)buf : bytes;

      if(bytes == 0)
        break;

      b->pos += bytes;
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

    ssize_t r = reader_read(b->parent, &b->buf[b->pos], b->len - b->pos);

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
  Location* lo = tr->lo;
  const uint8_t* end;
  ssize_t r = reader_read(tr->parent, buf, len);
  int cp;

  if(r <= 0)
    return r;

  const uint8_t* ptr = buf;
  size_t remain = r;

  while(remain > 0) {
    if(tr->buflen > 0) { /* complete a character split across reads */
      size_t buffered = tr->buflen;
      size_t needed = utf8_needed(tr->buf[0]);
      size_t take = needed - buffered;

      if(take > remain) {
        memcpy(&tr->buf[buffered], ptr, remain);
        tr->buflen += remain;
        break;
      }

      memcpy(&tr->buf[buffered], ptr, take);
      tr->buflen = 0;

      if((cp = unicode_from_utf8(tr->buf, needed, &end)) == -1) {
        /* bad continuation: count the buffered prefix bytes, rescan the new ones */
        lo->char_offset += buffered;
        lo->column += buffered;
        lo->byte_offset += buffered;
        continue;
      }

      location_nextchar(lo, cp);

      ptr += take;
      remain -= take;
      continue;
    }

    size_t needed = utf8_needed(*ptr);

    if(needed > remain) { /* character split across reads: keep the prefix */
      memcpy(tr->buf, ptr, remain);
      tr->buflen = remain;
      break;
    }

    if(needed == 0 || (cp = unicode_from_utf8(ptr, needed, &end)) == -1) {
      /* invalid byte: count it individually */
      lo->char_offset++;
      lo->column++;
      lo->byte_offset++;
      ptr++;
      remain--;
      continue;
    }

    location_nextchar(lo, cp);

    ptr += needed;
    remain -= needed;
  }

  return r;
}

/**
 * \addtogroup stream-utils
 * @{
 */
Reader
reader_from_dynbuf(DynBuf* db) {
  return (Reader){&read_dynbuf, db, NULL, (ReaderFinalizer*)&close_dynbuf};
}

Reader
reader_from_buf(InputBuffer* buf) {
  return (Reader){
      (ReadFunction*)&inputbuffer_read,
      buf,
      NULL,
      NULL,
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
reader_from_jsbuf(JSContext* ctx, JSValueConst value) {
  InputBuffer* input = js_mallocz(ctx, sizeof(InputBuffer));

  assert(input);

  *input = js_input_chars(ctx, value);

  Reader rd = reader_from_buf(input);
  rd.opaque2 = ctx;
  rd.finalizer = &reader_jsbuf_free;
  return rd;
}

Reader
reader_from_jsfunction(JSContext* ctx, JSValueConst func_obj) {
  return reader_from_jsmethod(ctx, func_obj, JS_UNDEFINED);
}

Reader
reader_from_jsmethod(JSContext* ctx, JSValueConst func_obj, JSValueConst this_obj) {
  JSFunc* fr = malloc(sizeof(JSFunc));

  assert(fr);

  *fr = (JSFunc){JS_DupContext(ctx), JS_DupValue(ctx, func_obj), JS_DupValue(ctx, this_obj)};

  return (Reader){
      &read_jsfunction,
      fr,
      NULL,
      (ReaderFinalizer*)&close_jsfunction,
  };
}

Reader
reader_counted(Reader* parent, uint64_t* bytes_ptr, uint64_t* characters_ptr) {
  Counted* c = malloc(sizeof(Counted));

  assert(c);

  *c = (Counted){bytes_ptr, characters_ptr, 0, {}, parent};

  return (Reader){
      &read_counted,
      c,
      NULL,
      (ReaderFinalizer*)&orig_free,
  };
}

Reader
reader_buffered(Reader* parent, size_t buf_size) {
  Buffered* b = malloc(sizeof(Buffered) + buf_size);

  assert(b);

  *b = (Buffered){(uint8_t*)&b[1], buf_size, 0, parent};

  return (Reader){
      &read_buffered,
      b,
      NULL,
      (ReaderFinalizer*)&orig_free,
  };
}

Reader
reader_linebuffered(Reader* parent, size_t buf_size) {
  Buffered* b = malloc(sizeof(Buffered) + buf_size);

  assert(b);

  *b = (Buffered){(uint8_t*)&b[1], buf_size, 0, parent};

  return (Reader){
      &read_linebuffered,
      b,
      NULL,
      (ReaderFinalizer*)&orig_free,
  };
}

Reader
reader_urldecode(Reader* parent) {
  URLDecoder* u = malloc(sizeof(URLDecoder));

  assert(u);

  *u = (URLDecoder){parent, {0, 0}, 0};

  return (Reader){
      &read_urldecoded,
      u,
      NULL,
      (ReaderFinalizer*)&orig_free,
  };
}

Reader
reader_location(Reader* parent, Location* lo) {
  Tracker* tr = malloc(sizeof(Tracker));

  assert(tr);

  *tr = (Tracker){lo, 0, {}, parent};

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

  while((c = reader_getc(rd)) >= 0) {
    if(c == '%') {
      int hi, lo;

      if((hi = reader_getc(rd)) == STREAM_ERROR)
        return -1;

      if(hi == STREAM_EOF) {
        RESULT(writer_putc(wr, '%'), ret);
        break;
      }

      if(hi != '%') {
        if((lo = reader_getc(rd)) == STREAM_ERROR)
          return -1;

        int h = scan_fromhex(hi);
        int l = lo == STREAM_EOF ? -1 : scan_fromhex(lo);

        if(h >= 0 && l >= 0) {
          c = (h << 4) | l;
        } else {
          /* not a valid escape: emit it literally */
          RESULT(writer_putc(wr, '%'), ret);
          RESULT(writer_putc(wr, hi), ret);

          if(lo == STREAM_EOF)
            break;

          c = lo;
        }
      }
    }

    RESULT(writer_putc(wr, c), ret);
  }

  if(c == STREAM_ERROR)
    return -1;

  return ret;
}

/**
 * @}
 */
