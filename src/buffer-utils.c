#include "defines.h"
#include "char-utils.h"
#include "buffer-utils.h"
#include "utils.h"
#ifdef _WIN32
#include <windows.h>
#elif defined(HAVE_TERMIOS_H)
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include "debug.h"
#include <quickjs.h>

int JS_ToInt64Clamp(JSContext*, int64_t*, JSValueConst, int64_t, int64_t, int64_t);

ssize_t
alloc_len(uintptr_t len) {
  return ((len + (len >> 2) + 30) + 31) & (~(uintptr_t)31);
}

/**
 * \addtogroup buffer-utils
 * @{
 */

int64_t
array_search(void* a, size_t m, size_t elsz, void* needle) {
  char* ptr = a;
  int64_t n, ret;

  n = m / elsz;

  for(ret = 0; ret < n; ret++) {
    if(!memcmp(ptr, needle, elsz))
      return ret;

    ptr += elsz;
  }

  return -1;
}

char*
dbuf_at_n(const DynBuf* db, size_t i, size_t* n, char sep) {
  size_t p, l = 0;

  for(p = 0; p < db->size; ++p) {
    if(l == i) {
      *n = byte_chr((const char*)&db->buf[p], db->size - p, sep);
      return (char*)&db->buf[p];
    }

    if(db->buf[p] == sep)
      ++l;
  }

  *n = 0;

  return 0;
}

const char*
dbuf_last_line(DynBuf* db, size_t* len) {
  size_t i;

  if((i = byte_rchr(db->buf, db->size, '\n')) < db->size)
    i++;
  else
    i = 0;

  if(len)
    *len = db->size - i;

  return (const char*)&db->buf[i];
}

int
dbuf_prepend(DynBuf* s, const uint8_t* data, size_t len) {
  int ret;

  if(!(ret = dbuf_reserve_start(s, len)))
    memcpy(s->buf, data, len);

  return 0;
}

void
dbuf_put_colorstr(DynBuf* db, const char* str, const char* color, int with_color) {
  if(with_color)
    dbuf_putstr(db, color);

  dbuf_putstr(db, str);

  if(with_color)
    dbuf_putstr(db, COLOR_NONE);
}

void
dbuf_put_escaped_pred(DynBuf* db, const char* str, size_t len, int (*pred)(int)) {
  size_t i = 0, j;
  char c;

  while(i < len) {
    if((j = predicate_find(&str[i], len - i, pred))) {
      dbuf_append(db, (const uint8_t*)&str[i], j);
      i += j;
    }

    if(i == len)
      break;

    dbuf_putc(db, '\\');

    if(str[i] == 0x1b) {
      dbuf_append(db, (const uint8_t*)"x1b", 3);
    } else {
      int r = pred(str[i]);

      dbuf_putc(db, (r > 1 && r <= 127) ? r : (c = escape_char_letter(str[i])) ? c : str[i]);

      if(r == 'u' || r == 'x')
        dbuf_printf(db, r == 'u' ? "%04x" : "%02x", str[i]);
    }

    i++;
  }
}

const uint8_t escape_url_tab[256] = {
    '%', '%', '%', '%', '%',  '%', '%', '%', '%', '%', '%', '%', '%', '%', '%', '%', '%', '%', '%', '%', '%', '%',
    '%', '%', '%', '%', '%',  '%', '%', '%', '%', '%', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0x5c, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   '%', 0,   0,   0,   0,
    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   '%',  '%', '%', '%', '%', '%', '%', '%', '%', '%', '%', '%', '%', '%', '%', '%', '%', '%',
    '%', '%', '%', '%', '%',  '%', '%', '%', '%', '%', '%', '%', '%', '%',
};

const uint8_t escape_noquote_tab[256] = {
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 0x62, 0x74, 0x6e, 0x76, 0x66, 0x72, 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',  'x',  'x',  0,    0,    0,    0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0x5c, 0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,
    0,   'x', 0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,
    0,   0,   'u', 'u', 'u', 'u', 'u', 'u', 'u',  'u',  'u',  'u',  'u',  'u',  'u', 'u', 'u', 'u', 'u', 'u', 'u',
    'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u',  'u',  'u',  'u',  'u',
};

const uint8_t escape_singlequote_tab[256] = {
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 0x62, 0x74, 0x6e, 0x76, 0x66, 0x72, 'x', 'x', 'x', 'x', 'x',  'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',  'x',  'x',  0,    0,    0,    0,   0,   0,   0,   0x27, 0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,    0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,    0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0x5c, 0,    0,    0,    0,    0,    0,   0,   0,   0,   0,    0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,    0,   0,
    0,   'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u',  'u',  'u',  'u',  'u',  'u',  'u', 'u', 'u', 'u', 'u',  'u', 'u',
    'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u',  'u',  'u',  'u',  'u',  'u',
};

const uint8_t escape_doublequote_tab[256] = {
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 0x62, 0x74, 0x6e, 0x76, 0x66, 0x72, 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',  'x',  'x',  0,    0,    0x22, 0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0x5c, 0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,
    0,   'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u',  'u',  'u',  'u',  'u',  'u',  'u', 'u', 'u', 'u', 'u', 'u', 'u',
    'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u',  'u',  'u',  'u',  'u',  'u',
};

const uint8_t escape_backquote_tab[256] = {
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 0x62, 0x74, 0,   0x76, 0x66, 0,   'x', 'x',  'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',  'x',  'x', 0,    0,    0,   0,   0x24, 0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,   0,    0,    0,   0,   0,    0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,   0,    0,    0,   0,   0,    0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0x5c, 0,    0,   0,    0x60, 0,   0,   0,    0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,   0,    0,    0,   0,   0,    0,   0,   0,   0,   0,
    0,   'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u',  'u',  'u', 'u',  'u',  'u', 'u', 'u',  'u', 'u', 'u', 'u', 'u',
    'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u',  'u',  'u', 'u',  'u',  'u',
};

void
dbuf_put_escaped_table(DynBuf* db, const char* str, size_t len, const uint8_t table[256]) {
  size_t i = 0, clen;
  int32_t c;
  const uint8_t *pos, *end, *next;

  for(pos = (const uint8_t*)str, end = pos + len; pos < end; pos = next) {
    uint8_t r, ch;

    if((c = unicode_from_utf8(pos, end - pos, &next)) < 0)
      break;

    clen = next - pos;
    ch = c;
    r = (clen >= 2 || c > 0xff) ? 'u' : table[c];

    if(r == 'u' && clen > 1 && (c & 0xffffff00) == 0) {
      r = 'x';
      // ch = c >> 8;
    }

    if(r == '%') {
      static const char hexdigits[] = "0123456789ABCDEF";

      dbuf_putc(db, '%');
      dbuf_putc(db, hexdigits[c >> 4]);
      dbuf_putc(db, hexdigits[c & 0xf]);
    } else if(c == 0x1b) {
      dbuf_putstr(db, "\\x1b");
    } else if(r == 'u') {
      dbuf_printf(db, c > 0xffff ? "\\u{%X}" : "\\u%04x", c);
    } else if(r == 'x') {
      dbuf_printf(db, "\\x%02x", ch);
    } else if(r) {
      dbuf_putc(db, '\\');
      dbuf_putc(db, (r > 1 && r <= 127) ? r : (c = escape_char_letter(ch)) ? c : ch);
    } else {
      dbuf_put(db, pos, next - pos);
    }

    i++;
  }
}

void
dbuf_put_unescaped_pred(DynBuf* db, const char* str, size_t len, int (*pred)(const char*, size_t*)) {
  size_t i = 0, j;
  // char c;

  while(i < len) {
    int r = 0;

    if((j = byte_chr(&str[i], len - i, '\\'))) {
      dbuf_append(db, (const uint8_t*)&str[i], j);
      i += j;
    }

    if(i == len)
      break;

    size_t n = 1;

    if(pred) {
      r = pred(&str[i + 1], &n);

      if(!r && n == 1)
        dbuf_putc(db, '\\');
    }

    if(r >= 0)
      dbuf_putc(db, /*n > 1 ||*/ r ? /*(r > 1 && r < 256) ?*/ r : str[i]);

    i += n;
  }
}

static inline int
hexdigit(char c) {
  if(c >= '0' && c <= '9')
    return c - '0';

  if(c >= 'a' && c <= 'f')
    c -= 32;

  if(c >= 'A' && c <= 'F')
    return c - 'A' + 10;

  return -1;
}

void
dbuf_put_unescaped_table(DynBuf* db, const char* str, size_t len, const uint8_t table[256]) {
  size_t i = 0, j;
  char escape_char = table == escape_url_tab ? '%' : '\\';

  while(i < len) {
    if((j = byte_chr(&str[i], len - i, escape_char))) {
      dbuf_append(db, (const uint8_t*)&str[i], j);
      i += j;
    }

    if(i == len)
      break;

    if(escape_char == '%') {
      int hi = hexdigit(str[i + 1]), lo = hexdigit(str[i + 2]);
      uint8_t c = (hi << 4) | (lo & 0xf);
      dbuf_putc(db, c);

      i += 2;

    } else {
      ++i;

      uint8_t c;

      switch(str[i]) {
        case 'b': c = '\b'; break;
        case 't': c = '\t'; break;
        case 'n': c = '\n'; break;
        case 'v': c = '\v'; break;
        case 'f': c = '\f'; break;
        case 'r': c = '\r'; break;
        default: c = str[i]; break;
      }

      uint8_t r = table[c];

      if(!(r && r != 'x' && r != 'u')) {
        dbuf_putc(db, '\\');
        dbuf_putc(db, c);
      } else {
        dbuf_putc(db, str[i] == r ? c : r);
      }
    }

    ++i;
  }
}

void
dbuf_put_escaped(DynBuf* db, const char* str, size_t len) {
  return dbuf_put_escaped_table(db, str, len, escape_noquote_tab);
}

void
dbuf_put_value(DynBuf* db, JSContext* ctx, JSValueConst value) {
  const char* str;
  size_t len;

  str = JS_ToCStringLen(ctx, &len, value);
  dbuf_append(db, str, len);
  JS_FreeCString(ctx, str);
}

void
dbuf_put_uint32(DynBuf* db, uint32_t num) {
  char buf[FMT_ULONG];

  dbuf_put(db, (const uint8_t*)buf, fmt_ulong(buf, num));
}

void
dbuf_put_atom(DynBuf* db, JSContext* ctx, JSAtom atom) {
  const char* str;

  str = JS_AtomToCString(ctx, atom);
  dbuf_putstr(db, str);
  JS_FreeCString(ctx, str);
}

int
dbuf_reserve_start(DynBuf* s, size_t len) {
  if(unlikely((s->size + len) > s->allocated_size)) {
    if(dbuf_realloc(s, s->size + len))
      return -1;
  }

  if(s->size > 0)
    memcpy(s->buf + len, s->buf, s->size);

  s->size += len;
  return 0;
}

uint8_t*
dbuf_reserve(DynBuf* s, size_t len) {
  if(unlikely((s->size + len) > s->allocated_size))
    if(dbuf_realloc(s, s->size + len))
      return 0;

  return &s->buf[s->size];
}

size_t
dbuf_token_pop(DynBuf* db, char delim) {
  size_t n, p, len;
  len = db->size;
  for(n = db->size; n > 0;) {
    if((p = byte_rchr(db->buf, n, delim)) == n) {
      db->size = 0;
      break;
    }

    if(p > 0 && db->buf[p - 1] == '\\') {
      n = p - 1;
      continue;
    }

    db->size = p;
    break;
  }

  return len - db->size;
}

size_t
dbuf_token_push(DynBuf* db, const char* str, size_t len, char delim) {
  size_t pos;

  if(db->size)
    dbuf_putc(db, delim);

  pos = db->size;
  dbuf_put_escaped_pred(db, str, len, is_dot_char);

  return db->size - pos;
}

JSValue
dbuf_tostring_free(DynBuf* s, JSContext* ctx) {
  JSValue r = JS_NewStringLen(ctx, s->buf ? (const char*)s->buf : "", s->buf ? s->size : 0);

  dbuf_free(s);
  return r;
}

ssize_t
dbuf_load(DynBuf* s, const char* filename) {
  FILE* fp;
  size_t nbytes = 0;

  if((fp = fopen(filename, "rb"))) {
    char buf[4096];
    size_t r;

    while(!feof(fp)) {
      if((r = fread(buf, 1, sizeof(buf), fp)) == 0) {
        fclose(fp);
        return -1;
      }

      dbuf_put(s, (uint8_t const*)buf, r);
      nbytes += r;
    }

    fclose(fp);
  }

  return nbytes;
}

int
dbuf_vprintf(DynBuf* s, const char* fmt, va_list ap) {

  s->size += vsnprintf((char*)(s->buf + s->size), s->allocated_size - s->size, fmt, ap);

  return 0;
}

InputBuffer
js_input_buffer(JSContext* ctx, JSValueConst value) {
  InputBuffer ret = {
      {BLOCK_INIT()},
      0,
      &input_buffer_free_default,
      JS_UNDEFINED,
      OFFSETLENGTH_INIT(),
  };

  if(js_is_typedarray(ctx, value)) {
    ret.value = offsetlength_typedarray(&ret.range, value, ctx);
  } else if(js_is_arraybuffer(ctx, value) || js_is_sharedarraybuffer(ctx, value)) {
    ret.value = JS_DupValue(ctx, value);
  }

  if(js_is_arraybuffer(ctx, ret.value) || js_is_sharedarraybuffer(ctx, ret.value)) {
    block_arraybuffer(&ret.block, ret.value, ctx);
  } else {
    JS_ThrowTypeError(ctx, "Invalid type (%s) for input buffer", js_value_typestr(ctx, ret.value));
    JS_FreeValue(ctx, ret.value);
    ret.value = JS_EXCEPTION;
  }

  return ret;
}

#undef free

InputBuffer
js_input_chars(JSContext* ctx, JSValueConst value) {
  InputBuffer ret = {
      {{0, 0}},
      0,
      &input_buffer_free_default,
      JS_UNDEFINED,
      OFFSETLENGTH_INIT(),
  };

  BOOL is_buffer = js_is_arraybuffer(ctx, value) || js_is_sharedarraybuffer(ctx, value) || js_is_typedarray(ctx, value);

  if(!is_buffer) {
    ret.data = (uint8_t*)JS_ToCStringLen(ctx, &ret.size, value);
    ret.value = JS_DupValue(ctx, value);
    ret.free = &input_buffer_free_default;
  } else {
    ret = js_input_buffer(ctx, value);
  }

  return ret;
}

InputBuffer
js_input_args(JSContext* ctx, int argc, JSValueConst argv[]) {
  InputBuffer input = js_input_chars(ctx, argv[0]);

  if(argc > 1)
    js_offset_length(ctx, input.size, argc, argv, 1, &input.range);

  return input;
}

InputBuffer
js_output_args(JSContext* ctx, int argc, JSValueConst argv[]) {
  InputBuffer output = js_input_buffer(ctx, argv[0]);

  if(argc > 1)
    js_offset_length(ctx, output.size, argc, argv, 1, &output.range);

  return output;
}

int
range_overlap(const PointerRange* a, const PointerRange* b) {
  return range_in(a, b->start) || range_in(a, b->end);
}

PointerRange
range_null() {
  return (PointerRange){0, 0};
}

PointerRange
range_frombuf(const void* x, uintptr_t n) {
  return (PointerRange){(char*)x, (char*)x + n};
}

PointerRange
range_fromstr(const char* s) {
  return range_frombuf(s, strlen(s));
}

int
range_resize(PointerRange* r, uintptr_t newlen) {
  uintptr_t len = range_len(r);

  if(newlen > len) {
    uintptr_t res = alloc_len(len + 1);

    if(newlen < res)
      return -1;
  }

  r->end = r->start + newlen;
  *(char*)r->end = '\0';
  return 0;
}

int
range_write(PointerRange* r, const void* x, uintptr_t n) {
  ssize_t len = range_len(r);
  ssize_t a = alloc_len(len + n + 1);

  if(!r->start || a != alloc_len(len + 1))
    if(!(r->start = realloc(r->start, a)))
      return -1;

  byte_copy(range_begin(r) + len, n, x);
  r->end = r->start + len + n;
  *(char*)r->end = '\0';

  return 0;
}

int
range_puts(PointerRange* r, const void* x) {
  return range_write(r, x, strlen(x));
}

int
range_append(PointerRange* r, PointerRange other) {
  return range_write(r, other.start, range_len(&other));
}

BOOL
input_buffer_valid(const InputBuffer* in) {
  return !JS_IsException(in->value);
}

InputBuffer
input_buffer_clone(const InputBuffer* in, JSContext* ctx) {
  InputBuffer ret = js_input_buffer(ctx, in->value);

  ret.pos = in->pos;
  ret.size = in->size;
  ret.free = in->free;

  return ret;
}

void
input_buffer_dump(const InputBuffer* in, DynBuf* db) {
  dbuf_printf(db,
              "(InputBuffer){ .data = %p, .size = %lu, .pos = %lu, .free = %p }",
              in->data,
              (unsigned long)in->size,
              (unsigned long)in->pos,
              in->free);
}

void
input_buffer_free(InputBuffer* in, JSContext* ctx) {
  if(in->data) {
    in->free(ctx, (const char*)in->data, in->value);
    in->data = 0;
    in->size = 0;
    in->pos = 0;
    in->value = JS_UNDEFINED;
  }
}

const uint8_t*
input_buffer_peek(InputBuffer* in, size_t* lenp) {
  input_buffer_peekc(in, lenp);
  return input_buffer_data(in) + in->pos;
}

const uint8_t*
input_buffer_get(InputBuffer* in, size_t* lenp) {
  const uint8_t* ret = input_buffer_peek(in, lenp);

  in->pos += *lenp;

  return ret;
}

const char*
input_buffer_currentline(InputBuffer* in, size_t* len) {
  size_t i;

  if((i = byte_rchr(input_buffer_data(in), in->pos, '\n')) < in->pos)
    i++;

  if(len)
    *len = in->pos - i;

  return (const char*)&input_buffer_begin(in)[i];
}

size_t
input_buffer_column(InputBuffer* in, size_t* len) {
  size_t i;

  if((i = byte_rchr(input_buffer_data(in), in->pos, '\n')) < in->pos)
    i++;

  return in->pos - i;
}

int
offsetlength_from_argv(OffsetLength* ol, int64_t size, int argc, JSValueConst argv[], JSContext* ctx) {
  int64_t offset = 0, length = size;
  int i = 0;

  if(i < argc) {
    if(JS_IsNumber(argv[i]))
      if(JS_ToInt64Clamp(ctx, &offset, argv[i], 0, size, size))
        return -1;

    if(++i < argc) {
      if(JS_IsNumber(argv[i]))
        if(JS_ToInt64Clamp(ctx, &length, argv[i], 0, size - offset, size))
          return -2;

      ++i;
    }
  }

  if(ol) {
    ol->offset = offset;
    ol->length = length;
  }

  return i;
}

int
indexrange_from_argv(IndexRange* ir, int64_t size, int argc, JSValueConst argv[], JSContext* ctx) {
  int i = 0;

  ir->start = 0;
  ir->end = size;

  if(i < argc) {
    if(JS_ToInt64Clamp(ctx, &ir->start, argv[i], 0, size, size))
      return -1;

    if(++i < argc) {
      if(JS_ToInt64Clamp(ctx, &ir->end, argv[i], ir->start, size, size))
        return -2;

      ++i;
    }
  }

  return i;
}

int
screen_size(int size[2]) {
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO csbi;

  GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
  size[0] = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  size[1] = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  return 0;

#elif defined(HAVE_TERMIOS_H)
  {
    struct winsize w = {.ws_col = -1, .ws_row = -1};

    if(isatty(STDIN_FILENO))
      ioctl(STDIN_FILENO, TIOCGWINSZ, &w);
    else if(isatty(STDOUT_FILENO))
      ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    else if(isatty(STDERR_FILENO))
      ioctl(STDERR_FILENO, TIOCGWINSZ, &w);

    size[0] = w.ws_col;
    size[1] = w.ws_row;
    return 0;
  }
#else
  size[0] = 80;
  size[1] = 25;
  return 0;
#endif
  return -1;
}

#undef js_realloc_rt
void
js_dbuf_allocator(JSContext* ctx, DynBuf* s) {
  dbuf_init2(s, JS_GetRuntime(ctx), (DynBufReallocFunc*)js_realloc_rt);
}

inline int
input_buffer_peekc(InputBuffer* in, size_t* lenp) {
  const uint8_t *pos, *end, *next;
  int cp;

  pos = input_buffer_data(in) + in->pos;
  end = input_buffer_data(in) + input_buffer_length(in);
  cp = unicode_from_utf8(pos, end - pos, &next);

  *lenp = next - pos;

  return cp;
}

inline int
input_buffer_putc(InputBuffer* in, unsigned int c, JSContext* ctx) {
  int len;

  if(in->pos + UTF8_CHAR_LEN_MAX > in->size)
    if(block_realloc(&in->block, in->pos + UTF8_CHAR_LEN_MAX, ctx))
      return -1;

  len = unicode_to_utf8(&in->data[in->pos], c);

  in->pos += len;

  return len;
}

size_t
dbuf_bitflags(DynBuf* db, uint32_t bits, const char* const names[]) {
  size_t i, n = 0;

  for(i = 0; i < sizeof(bits) * 8; i++) {
    if(bits & (1 << i)) {
      size_t len = strlen(names[i]);

      if(n) {
        n++;
        dbuf_putstr(db, "|");
      }

      dbuf_append(db, names[i], len);
      n += len;
    }
  }

  return n;
}

/**
 * @}
 */
