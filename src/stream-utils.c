#include "stream-utils.h"
#include "buffer-utils.h"
#include "defines.h"
#include "queue.h"
#include "js-utils.h"

#include <assert.h>
#include <errno.h>
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
  int ref_count;
  void* rd_wr;
  int nargs;  /* 2 for (buf, len), 3 for (buf, offset, len) */
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

static JSFunc*
jsfunc_new(void) {
  JSFunc* fw;

  if((fw = malloc(sizeof(JSFunc)))) {
    fw->ctx = NULL;
    fw->func_obj = JS_UNDEFINED;
    fw->this_obj = JS_UNDEFINED;
    fw->ref_count = 1;
    fw->rd_wr = NULL;
    fw->nargs = 2;
  }

  return fw;
}

static void
jsfunc_free(void* opaque) {
  JSFunc* fw = opaque;

  if(--fw->ref_count == 0) {
    JS_FreeValue(fw->ctx, fw->func_obj);
    fw->func_obj = JS_UNDEFINED;

    JS_FreeValue(fw->ctx, fw->this_obj);
    fw->this_obj = JS_UNDEFINED;

    if(fw->ctx) {
      JS_FreeContext(fw->ctx);
      fw->ctx = NULL;
    }

    free(fw);
  }
}

static void
jsfunc_finalizer(JSRuntime* rt, void* opaque) {
  jsfunc_free(opaque);
}

static JSFunc*
jsfunc_dup(JSFunc* fw) {
  ++fw->ref_count;
  return fw;
}

/* Detect the number of parameters a function expects */
static int
jsfunc_detect_nargs(JSContext* ctx, JSValueConst func_obj) {
  JSValue length_val = JS_GetPropertyStr(ctx, func_obj, "length");
  int nargs = 2;  /* default to 2-arg signature */
  
  if(!JS_IsException(length_val) && !JS_IsUndefined(length_val)) {
    int32_t length;
    if(JS_ToInt32(ctx, &length, length_val) == 0) {
      nargs = (length >= 3) ? 3 : 2;
    }
  }
  JS_FreeValue(ctx, length_val);
  
  return nargs;
}

/* Check if a JS object is a std FILE object (from std.open) */
static BOOL
is_std_file_object(JSContext* ctx, JSValueConst obj) {
  // Check if the object has both read and write methods, which is characteristic of std FILE
  JSValue read_method = JS_GetPropertyStr(ctx, obj, "read");
  JSValue write_method = JS_GetPropertyStr(ctx, obj, "write");
  
  BOOL has_read = JS_IsFunction(ctx, read_method);
  BOOL has_write = JS_IsFunction(ctx, write_method);
  
  JS_FreeValue(ctx, read_method);
  JS_FreeValue(ctx, write_method);
  
  // Also check if it has other std FILE characteristics like close method
  JSValue close_method = JS_GetPropertyStr(ctx, obj, "close");
  BOOL has_close = JS_IsFunction(ctx, close_method);
  JS_FreeValue(ctx, close_method);
  
  // A std FILE object should have read, write, and close methods
  return has_read && has_write && has_close;
}

static JSValue
jsfunc_invoke(JSFunc* fw, void* buf, size_t len, BOOL copy) {
  JSValue ret;
  
  if (fw->nargs == 3) {
    JSValueConst args[3] = {
        copy ? JS_NewArrayBufferCopy(fw->ctx, (uint8_t*)buf, len) : JS_NewArrayBuffer(fw->ctx, (uint8_t*)buf, len, 0, 0, FALSE),
        JS_NewInt32(fw->ctx, 0),
        JS_NewInt32(fw->ctx, len),
    };
    JSAtom method = JS_ValueToAtom(fw->ctx, fw->func_obj);
    ret = JS_Invoke(fw->ctx, fw->this_obj, method, 3, args);
    JS_FreeAtom(fw->ctx, method);

    if(!copy)
      JS_DetachArrayBuffer(fw->ctx, args[0]);

    JS_FreeValue(fw->ctx, args[0]);
    JS_FreeValue(fw->ctx, args[1]);
    JS_FreeValue(fw->ctx, args[2]);
  } else {
    JSValueConst args[2] = {
        copy ? JS_NewArrayBufferCopy(fw->ctx, (uint8_t*)buf, len) : JS_NewArrayBuffer(fw->ctx, (uint8_t*)buf, len, 0, 0, FALSE),
        JS_NewInt32(fw->ctx, len),
    };
    JSAtom method = JS_ValueToAtom(fw->ctx, fw->func_obj);
    ret = JS_Invoke(fw->ctx, fw->this_obj, method, 2, args);
    JS_FreeAtom(fw->ctx, method);

    if(!copy)
      JS_DetachArrayBuffer(fw->ctx, args[0]);

    JS_FreeValue(fw->ctx, args[0]);
    JS_FreeValue(fw->ctx, args[1]);
  }
  
  return ret;
}

static JSValue
jsfunc_call(JSFunc* fw, void* buf, size_t len, BOOL copy) {
  JSValue ret;
  
  if (fw->nargs == 3) {
    JSValueConst args[3] = {
        copy ? JS_NewArrayBufferCopy(fw->ctx, (uint8_t*)buf, len) : JS_NewArrayBuffer(fw->ctx, (uint8_t*)buf, len, 0, 0, FALSE),
        JS_NewInt32(fw->ctx, 0),
        JS_NewInt32(fw->ctx, len),
    };
    ret = JS_Call(fw->ctx, fw->func_obj, fw->this_obj, 3, args);

    if(!copy)
      JS_DetachArrayBuffer(fw->ctx, args[0]);

    JS_FreeValue(fw->ctx, args[0]);
    JS_FreeValue(fw->ctx, args[1]);
    JS_FreeValue(fw->ctx, args[2]);
  } else {
    JSValueConst args[2] = {
        copy ? JS_NewArrayBufferCopy(fw->ctx, (uint8_t*)buf, len) : JS_NewArrayBuffer(fw->ctx, (uint8_t*)buf, len, 0, 0, FALSE),
        JS_NewInt32(fw->ctx, len),
    };
    ret = JS_Call(fw->ctx, fw->func_obj, fw->this_obj, 2, args);

    if(!copy)
      JS_DetachArrayBuffer(fw->ctx, args[0]);

    JS_FreeValue(fw->ctx, args[0]);
    JS_FreeValue(fw->ctx, args[1]);
  }
  
  return ret;
}

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

ssize_t
write_stubborn(WriteFunction* wr, intptr_t fd, const char* x, size_t n, void* b) {
  errno = 0;

  while(n) {
    ssize_t w;

    if((w = wr(fd, (void*)x, n, b)) <= 0) {
      if(errno == EINTR)
        continue;

      return -1;
    }

    x += w;
    n -= (size_t)w;
  }

  return 0;
}

static ssize_t
empty_buffered(Buffered* b) {
  ssize_t r;

  if((r = writer_write(b->parent, b->buf, b->pos)) <= 0)
    return r;

  if(r < b->pos)
    memmove(b->buf, &b->buf[r], b->pos - r);

  b->pos -= r;
  return r;
}

static ssize_t
write_buffered(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  Buffered* b = (Buffered*)fd;
  const uint8_t* x = buf;
  size_t consumed = 0;

  while(consumed < len) {
    ssize_t r;

    if(b->pos == b->len)
      if((r = empty_buffered(b)) <= 0)
        return consumed ? consumed : r;

    size_t remain = len - consumed;

    /* if Buffered* is empty and remaining bytes are bigger
       than Buffered, then write directly to the parent */
    if(b->pos == 0 && remain >= b->len) {
      if((r = writer_write(b->parent, &x[consumed], remain)) <= 0)
        return consumed ? consumed : r;

      consumed += r;
    } else {
      /* Buffered* is partially filled */
      size_t headroom = b->len - b->pos;
      size_t n = MIN_NUM(headroom, remain);

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
  ssize_t consumed = 0;

  for(size_t i = 0; i < len; i++) {
    ssize_t r;

    if(b->pos == b->len)
      if((r = empty_buffered(b)) <= 0)
        return consumed ? consumed : r;

    b->buf[b->pos++] = ptr[i];

    if(ptr[i] == '\n')
      if((r = empty_buffered(b)) <= 0)
        return consumed ? consumed : r;

    consumed++;
  }

  return consumed;
}

static ssize_t
write_jsinvoke(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  JSFunc* fw = (JSFunc*)fd;
  JSValue ret = jsfunc_invoke(fw, (void*)buf, len, TRUE);

  if(JS_IsException(ret)) {
    JS_FreeValue(fw->ctx, JS_GetException(fw->ctx));
    return -1;
  }

  int32_t written;
  if(JS_ToInt32(fw->ctx, &written, ret)) {
    JS_FreeValue(fw->ctx, ret);
    return -1;
  }
  JS_FreeValue(fw->ctx, ret);

  return written;
}

static ssize_t
write_jsfunction(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  JSFunc* fw = (JSFunc*)fd;
  JSValue ret = jsfunc_call(fw, (void*)buf, len, TRUE);

  if(JS_IsException(ret)) {
    JS_FreeValue(fw->ctx, JS_GetException(fw->ctx));
    return -1;
  }

  int32_t written;
  if(JS_ToInt32(fw->ctx, &written, ret)) {
    JS_FreeValue(fw->ctx, ret);
    return -1;
  }
  JS_FreeValue(fw->ctx, ret);

  return written;
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

int
writer_from_js(JSContext* ctx, JSValueConst value, Writer* wr) {
  if(JS_IsObject(value)) {
    if(JS_IsFunction(ctx, value)) {
      *wr = writer_from_jsfunction(ctx, value);
      return 1;
    }

    if(is_std_file_object(ctx, value)) {
      *wr = writer_from_jsstd(ctx, value);
      return 1;
    }

    if(js_has_propertystr(ctx, value, "getWriter")) {
      *wr = writer_from_jsstream(ctx, value);
      return 1;
    }
    if(js_has_propertystr(ctx, value, "write")) {
      *wr = writer_from_jsinvoke(ctx, "write", value);
      return 1;
    }

    if(js_is_typedarray(ctx, value) || js_is_arraybuffer(ctx, value) || js_is_dataview(ctx, value)) {
      *wr = writer_from_jsbuf(ctx, value);
      return 1;
    }

  } else if(JS_IsNumber(value)) {
    intptr_t fd = js_toint64(ctx, value);

    *wr = writer_from_fd(fd, FALSE);
    return 1;
  }

  return 0;
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
writer_from_jsinvoke(JSContext* ctx, const char* method, JSValueConst this_obj) {
  JSFunc* fw = jsfunc_new();

  assert(fw);

  *fw = (JSFunc){JS_DupContext(ctx), JS_NewString(ctx, method), JS_DupValue(ctx, this_obj)};

  return (Writer){
      &write_jsinvoke,
      fw,
      &jsfunc_free,
  };
}

Writer
writer_from_jsfunction(JSContext* ctx, JSValueConst fn) {
  return writer_from_jsmethod(ctx, fn, JS_UNDEFINED);
}

Writer
writer_from_jsmethod(JSContext* ctx, JSValueConst func_obj, JSValueConst this_obj) {
  JSFunc* fw = jsfunc_new();

  assert(fw);

  fw->ctx = JS_DupContext(ctx);
  fw->func_obj = JS_DupValue(ctx, func_obj);
  fw->this_obj = JS_DupValue(ctx, this_obj);
  fw->nargs = jsfunc_detect_nargs(ctx, func_obj);

  return (Writer){
      &write_jsfunction,
      fw,
      &jsfunc_free,
  };
}

Writer
writer_from_jsstd(JSContext* ctx, JSValueConst file_obj) {
  JSFunc* fw = jsfunc_new();

  assert(fw);

  *fw = (JSFunc){JS_DupContext(ctx), JS_NewString(ctx, "write"), JS_DupValue(ctx, file_obj), 3};

  return (Writer){
      &write_jsinvoke,
      fw,
      &jsfunc_free,
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
    ((ReaderFinalizer*)wr->finalizer)(wr->opaque, NULL);
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

/**
 * \addtogroup stream-utils
 * @{
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
read_jsinvoke(intptr_t fd, void* buf, size_t len, Reader* rd) {
  JSFunc* fr = (JSFunc*)fd;
  JSValue ret = jsfunc_invoke(fr, buf, len, FALSE);

  if(JS_IsException(ret)) {
    JS_FreeValue(fr->ctx, JS_GetException(fr->ctx));
    return -1;
  }

  int32_t n = js_toint32_free(fr->ctx, ret);

  return n;
}

static ssize_t
read_jsfunction(intptr_t fd, void* buf, size_t len, Reader* rd) {
  JSFunc* fr = (JSFunc*)fd;
  JSValue ret = jsfunc_call(fr, buf, len, FALSE);

  if(JS_IsException(ret)) {
    JS_FreeValue(fr->ctx, JS_GetException(fr->ctx));
    return -1;
  }

  int32_t n = js_toint32_free(fr->ctx, ret);

  return n;
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
/* input is either a buffer (string/ArrayBuffer/TypedArray), a pull function
 * called as fn(buf, len) -> bytesRead, or an object exposing such a function as
 * its "read" method (called with the object as `this`) - same convention as
 * JsonParser's constructor (quickjs-json.c). */
int
reader_from_js(JSContext* ctx, JSValueConst value, Reader* rd) {
  if(JS_IsObject(value)) {
    /* a pull function called as fn(buf, len) -> bytesRead */
    if(JS_IsFunction(ctx, value)) {
      *rd = reader_from_jsfunction(ctx, value);
      return 1;
    }

    /* a std FILE object (e.g., from std.open) with read(buf, offset, len) signature */
    if(is_std_file_object(ctx, value)) {
      *rd = reader_from_jsstd(ctx, value);
      return 1;
    }

    /*  an object exposing such a function as its "read" method (called with the object as `this`) */
    if(js_has_propertystr(ctx, value, "read")) {
      *rd = reader_from_jsinvoke(ctx, "read", value);
      return 1;
    }

    /* a ReadableStream */
    if(js_has_propertystr(ctx, value, "getReader")) {
      *rd = reader_from_jsstream(ctx, value);
      return 1;
    }
  }

  /* a buffer (string/ArrayBuffer/TypedArray) */
  if(JS_IsString(value) || js_is_typedarray(ctx, value) || js_is_arraybuffer(ctx, value) || js_is_dataview(ctx, value)) {
    *rd = reader_from_jsbuf(ctx, value);
    return 1;
  }

  /* an fd number */
  if(JS_IsNumber(value)) {
    *rd = reader_from_fd(js_toint64(ctx, value), FALSE);
    return 1;
  }

  return 0;
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
reader_from_jsinvoke(JSContext* ctx, const char* method, JSValueConst this_obj) {
  JSFunc* fw = jsfunc_new();

  assert(fw);

  *fw = (JSFunc){JS_DupContext(ctx), JS_NewString(ctx, method), JS_DupValue(ctx, this_obj)};

  return (Reader){
      &read_jsinvoke,
      fw,
      NULL,
      (ReaderFinalizer*)&jsfunc_free,
  };
}

Reader
reader_from_jsfunction(JSContext* ctx, JSValueConst func_obj) {
  return reader_from_jsmethod(ctx, func_obj, JS_UNDEFINED);
}

Reader
reader_from_jsmethod(JSContext* ctx, JSValueConst func_obj, JSValueConst this_obj) {
  JSFunc* fr = jsfunc_new();

  assert(fr);

  *fr = (JSFunc){JS_DupContext(ctx), JS_DupValue(ctx, func_obj), JS_DupValue(ctx, this_obj)};

  return (Reader){
      &read_jsfunction,
      fr,
      NULL,
      (ReaderFinalizer*)&jsfunc_free,
  };
}

Reader
reader_from_jsstd(JSContext* ctx, JSValueConst file_obj) {
  JSFunc* fr = jsfunc_new();

  assert(fr);

  *fr = (JSFunc){JS_DupContext(ctx), JS_NewString(ctx, "read"), JS_DupValue(ctx, file_obj), 3};

  return (Reader){
      &read_jsinvoke,
      fr,
      NULL,
      (ReaderFinalizer*)&jsfunc_free,
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

/**
 * \addtogroup stream-utils-async
 * @{
 *
 * Interfaces WritableStream/ReadableStream from JS. Implication is that their method calls are async
 */
static ssize_t
write_jsstream(intptr_t fd, const void* buf, size_t len, Writer* wr) {
  JSFunc* fr = (JSFunc*)fd;
  JSValue ret = jsfunc_call(fr, (void*)buf, len, TRUE);

  ssize_t r = JS_IsException(ret) ? -1 : len;
  JS_FreeValue(fr->ctx, ret);
  return r;
}

static void
close_jsstream(void* opaque, void* opaque2) {
  JSFunc* jsf = opaque;
  JSContext* ctx = jsf->ctx;

  JSAtom release_method = JS_NewAtom(ctx, "releaseLock");
  JS_FreeValue(ctx, JS_Invoke(ctx, jsf->this_obj, release_method, 0, 0));
  JS_FreeAtom(ctx, release_method);

  if(opaque2) {
    queue_clear(opaque2);
    js_free(ctx, opaque2);
  }

  jsfunc_free(opaque);
}

Writer
writer_from_jsstream(JSContext* ctx, JSValueConst stream) {
  JSFunc* fr = jsfunc_new();

  assert(fr);

  JSValue writer = JS_GetPropertyStr(ctx, stream, "getWriter");

  *fr = (JSFunc){JS_DupContext(ctx), JS_GetPropertyStr(ctx, writer, "write"), writer, 1, NULL};

  Writer ret = (Writer){
      &write_jsstream,
      fr,
      (WriterFinalizer*)&close_jsstream,
  };

  return ret;
}

static JSValue
then_jsstream(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  JSFunc* fr = opaque;
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case JS_PROMISE_FULFILLED: {
      InputBuffer input = js_input_chars(ctx, argv[0]);
      size_t len = inputbuffer_length(&input);

      if(len > 0) {
        ssize_t r;

        if((r = queue_write(fr->rd_wr, inputbuffer_data(&input), len)) <= 0)
          ret = JS_ThrowInternalError(ctx, "queue_write() returned %zd", r);
      }

      inputbuffer_free(&input, ctx);
      break;
    }

    case JS_PROMISE_REJECTED: {
      JS_FreeValue(ctx, fr->func_obj);
      fr->func_obj = JS_DupValue(ctx, argv[0]);
      break;
    }
  }

  return ret;
}

static void
invoke_jsstream(JSFunc* fr) {
  JSValue promise = JS_Call(fr->ctx, fr->func_obj, fr->this_obj, 0, 0);

  JSValue then = js_function_cclosure(fr->ctx, then_jsstream, 1, JS_PROMISE_FULFILLED, jsfunc_dup(fr), jsfunc_finalizer);
  JSValue reject = js_function_cclosure(fr->ctx, then_jsstream, 1, JS_PROMISE_REJECTED, jsfunc_dup(fr), jsfunc_finalizer);

  JS_FreeValue(fr->ctx, promise_then2(fr->ctx, promise, then, reject));
  JS_FreeValue(fr->ctx, then);
  JS_FreeValue(fr->ctx, reject);
}

static ssize_t
read_jsstream(intptr_t fd, void* buf, size_t len, Reader* rd) {
  JSFunc* fr = (JSFunc*)fd;
  ssize_t r = -1;

  if(queue_size(fr->rd_wr) > 0)
    r = queue_read(fr->rd_wr, buf, len);

  if(queue_empty(fr->rd_wr) && JS_IsFunction(fr->ctx, fr->func_obj))
    invoke_jsstream(fr);

  return r;
}

Reader
reader_from_jsstream(JSContext* ctx, JSValueConst stream) {
  JSFunc* fr = jsfunc_new();

  assert(fr);

  JSValue reader = JS_GetPropertyStr(ctx, stream, "getReader");
  Queue* q;

  if((q = js_malloc(ctx, sizeof(Queue))))
    queue_init(q);

  *fr = (JSFunc){JS_DupContext(ctx), JS_GetPropertyStr(ctx, reader, "read"), reader, 1, q};

  Reader ret = (Reader){
      &read_jsstream,
      fr,
      q,
      (ReaderFinalizer*)&close_jsstream,
  };

  reader_read(&ret, 0, 0);
  return ret;
}
/**
 * @}
 */
