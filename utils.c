#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#undef _ISOC99_SOURCE
#define _ISOC99_SOURCE 1

#include "utils.h"
#include "cutils.h"
#include "vector.h"
#include "libregexp.h"
#include "quickjs-internal.h"
#include <time.h>
#include <math.h>

#ifndef INFINITY
#define INFINITY __builtin_inf()
#endif

#if defined(__linux__) || defined(__APPLE__)
uint64_t
time_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000 + (ts.tv_nsec / 1000);
}
#else
/* more portable, but does not work if the date is updated */
uint64_t
time_us(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}
#endif

size_t
ansi_length(const char* str, size_t len) {
  size_t i, n = 0, p;
  for(i = 0; i < len;) {
    if(str[i] == 0x1b && (p = ansi_skip(&str[i], len - i)) > 0) {
      i += p;
      continue;
    }
    n++;
    i++;
  }
  return n;
}

size_t
ansi_skip(const char* str, size_t len) {
  size_t pos = 0;
  if(str[pos] == 0x1b) {
    if(++pos < len && str[pos] == '[') {
      while(++pos < len)
        if(is_alphanumeric_char(str[pos]))
          break;
      if(++pos < len && str[pos] == '~')
        ++pos;
      return pos;
    }
  }
  return 0;
}

size_t
ansi_truncate(const char* str, size_t len, size_t limit) {
  size_t i, n = 0, p;
  for(i = 0; i < len;) {
    if((p = ansi_skip(&str[i], len - i)) > 0) {
      i += p;
      continue;
    }
    n += is_escape_char(str[i]) ? 2 : 1;

    i++;
    if(n > limit)
      break;
  }
  return i;
}

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
str_escape(const char* s) {
  DynBuf dbuf;
  dbuf_init2(&dbuf, 0, 0);
  dbuf_put_escaped(&dbuf, s, strlen(s));
  dbuf_0(&dbuf);
  return (char*)dbuf.buf;
}

char*
byte_escape(const char* s, size_t n) {
  DynBuf dbuf;
  dbuf_init2(&dbuf, 0, 0);
  dbuf_put_escaped(&dbuf, s, n);
  dbuf_0(&dbuf);
  return (char*)dbuf.buf;
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

void
dbuf_put_escaped_table(DynBuf* db, const char* str, size_t len, const char table[256]) {
  size_t i = 0, j;
  char c;
  while(i < len) {
    if((j = lookup_find(&str[i], len - i, table))) {
      dbuf_append(db, (const uint8_t*)&str[i], j);
      i += j;
    }
    if(i == len)
      break;
    dbuf_putc(db, '\\');

    if(str[i] == 0x1b) {
      dbuf_append(db, (const uint8_t*)"x1b", 3);
    } else {
      int r = table[(unsigned char)str[i]];

      dbuf_putc(db, (r > 1 && r <= 127) ? r : (c = escape_char_letter(str[i])) ? c : str[i]);

      if(r == 'u' || r == 'x')
        dbuf_printf(db, r == 'u' ? "%04x" : "%02x", str[i]);
    }
    i++;
  }
}

void
dbuf_put_unescaped_pred(DynBuf* db, const char* str, size_t len, int (*pred)(int)) {
  size_t i = 0, j;
  char c;
  int r;
  while(i < len) {
    if((j = byte_chr(&str[i], len - i, '\\'))) {
      dbuf_append(db, (const uint8_t*)&str[i], j);
      i += j;
    }
    if(i == len)
      break;

    if(!(r = pred(str[++i])))
      dbuf_putc(db, '\\');

    dbuf_putc(db, (r > 1 && r < 256) ? r : str[i]);
    i++;
  }
}

void
dbuf_put_escaped(DynBuf* db, const char* str, size_t len) {
  return dbuf_put_escaped_table(
      db, str, len, (const char[256]){'x', 'x', 'x',  'x', 'x', 'x', 'x', 'x', 0x62, 0x74, 0x6e, 0x76, 0x66, 0x72, 'x',
                                      'x', 'x', 'x',  'x', 'x', 'x', 'x', 'x', 'x',  'x',  'x',  'x',  'x',  'x',  'x',
                                      'x', 'x', 0,    0,   0,   0,   0,   0,   0,    0x27, 0,    0,    0,    0,    0,
                                      0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,
                                      0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,
                                      0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,
                                      0,   0,   0x5c, 0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,
                                      0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,
                                      0,   0,   0,    0,   0,   0,   0,   'x', 0,    0,    0,    0,    0,    0,    0,
                                      0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,
                                      0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,
                                      0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,
                                      0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,
                                      0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,
                                      0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,
                                      0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,
                                      0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,
                                      0});
}

void
dbuf_put_value(DynBuf* db, JSContext* ctx, JSValueConst value) {
  const char* str;
  size_t len;
  str = JS_ToCStringLen(ctx, &len, value);
  dbuf_append(db, str, len);
  js_cstring_free(ctx, str);
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
  JSValue r;
  r = JS_NewStringLen(ctx, s->buf ? (const char*)s->buf : "", s->buf ? s->size : 0);
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
      if((r = fread(buf, 1, sizeof(buf), fp)) == 0)
        return -1;
      dbuf_put(s, (uint8_t const*)buf, r);
      nbytes += r;
    }
    fclose(fp);
  }
  return nbytes;
}

int
regexp_flags_fromstring(const char* s) {
  int flags = 0;

  if(str_contains(s, 'g'))
    flags |= LRE_FLAG_GLOBAL;
  if(str_contains(s, 'i'))
    flags |= LRE_FLAG_IGNORECASE;
  if(str_contains(s, 'm'))
    flags |= LRE_FLAG_MULTILINE;
  if(str_contains(s, 's'))
    flags |= LRE_FLAG_DOTALL;
  if(str_contains(s, 'u'))
    flags |= LRE_FLAG_UTF16;
  if(str_contains(s, 'y'))
    flags |= LRE_FLAG_STICKY;

  return flags;
}

int
regexp_flags_tostring(int flags, char* buf) {
  char* out = buf;

  if(flags & LRE_FLAG_GLOBAL)
    *out++ = 'g';
  if(flags & LRE_FLAG_IGNORECASE)
    *out++ = 'i';
  if(flags & LRE_FLAG_MULTILINE)
    *out++ = 'm';
  if(flags & LRE_FLAG_DOTALL)
    *out++ = 's';
  if(flags & LRE_FLAG_UTF16)
    *out++ = 'u';
  if(flags & LRE_FLAG_STICKY)
    *out++ = 'y';

  *out = '\0';
  return out - buf;
}

RegExp
regexp_from_argv(int argc, JSValueConst argv[], JSContext* ctx) {
  RegExp re = {0, 0, 0};
  const char* flagstr;
  assert(argc > 0);
  if(js_is_regexp(ctx, argv[0])) {
    re.source = js_get_propertystr_stringlen(ctx, argv[0], "source", &re.len);
    re.flags = regexp_flags_fromstring((flagstr = js_get_propertystr_cstring(ctx, argv[0], "flags")));
    js_cstring_free(ctx, flagstr);
  } else {
    re.source = js_tostringlen(ctx, &re.len, argv[0]);
    if(argc > 1 && JS_IsString(argv[1])) {
      re.flags = regexp_flags_fromstring((flagstr = JS_ToCString(ctx, argv[1])));
      js_cstring_free(ctx, flagstr);
    }
  }
  return re;
}

RegExp
regexp_from_dbuf(DynBuf* dbuf, int flags) {
  RegExp re = {(char*)dbuf->buf, dbuf->size, flags};
  dbuf->buf = 0;
  dbuf->allocated_size = 0;
  dbuf->size = 0;
  return re;
}

uint8_t*
regexp_compile(RegExp re, JSContext* ctx) {
  char error_msg[64];
  int len = 0;
  uint8_t* bytecode;
  if(!(bytecode = lre_compile(&len, error_msg, sizeof(error_msg), re.source, re.len, re.flags, ctx)))
    JS_ThrowInternalError(ctx, "Error compiling regex /%.*s/: %s", (int)re.len, re.source, error_msg);

  return bytecode;
}

JSValue
regexp_to_value(RegExp re, JSContext* ctx) {
  char flagstr[32] = {0};
  size_t flaglen = regexp_flags_tostring(re.flags, flagstr);
  JSValueConst args[2] = {JS_NewStringLen(ctx, re.source, re.len), JS_NewStringLen(ctx, flagstr, flaglen)};
  JSValue regex, ctor = js_global_get(ctx, "RegExp");
  regex = JS_CallConstructor(ctx, ctor, 2, args);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  return regex;
}

InputBuffer
js_input_buffer(JSContext* ctx, JSValueConst value) {
  InputBuffer ret = {0, 0, 0, &input_buffer_free_default, JS_UNDEFINED};

  if(js_value_isclass(ctx, value, JS_CLASS_ARRAY_BUFFER) || js_is_arraybuffer(ctx, value)) {
    ret.value = JS_DupValue(ctx, value);
    ret.data = JS_GetArrayBuffer(ctx, &ret.size, ret.value);
  } else if(JS_IsString(value)) {
    ret.data = (uint8_t*)JS_ToCStringLen(ctx, &ret.size, value);
    ret.value = js_cstring_value((const char*)ret.data);
  } else {
    ret.value = JS_EXCEPTION;
    //    JS_ThrowTypeError(ctx, "Invalid type for input buffer");
  }
  return ret;
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
  dbuf_printf(
      db, "(InputBuffer){ .data = %p, .size = %zu, .pos = %zu, .free = %p }", in->data, in->size, in->pos, in->free);
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
  return in->data + in->pos;
}

const uint8_t*
input_buffer_get(InputBuffer* in, size_t* lenp) {
  size_t n;
  const uint8_t* ret;
  if(lenp == 0)
    lenp = &n;
  ret = input_buffer_peek(in, lenp);
  in->pos += *lenp;
  return ret;
}

const char*
input_buffer_currentline(InputBuffer* in, size_t* len) {
  size_t i;

  if((i = byte_rchr(in->data, in->pos, '\n')) < in->pos)
    i++;

  if(len)
    *len = in->pos - i;

  return (const char*)&in->data[i];
}

size_t
input_buffer_column(InputBuffer* in, size_t* len) {
  size_t i;

  if((i = byte_rchr(in->data, in->pos, '\n')) < in->pos)
    i++;

  return in->pos - i;
}

int64_t
js_array_length(JSContext* ctx, JSValueConst array) {
  int64_t len = -1;
  if(JS_IsArray(ctx, array) || js_is_typedarray(array)) {
    JSValue length = JS_GetPropertyStr(ctx, array, "length");
    JS_ToInt64(ctx, &len, length);
    JS_FreeValue(ctx, length);
  }
  return len;
}

char**
js_array_to_argv(JSContext* ctx, int* argcp, JSValueConst array) {
  int i, len = js_array_length(ctx, array);
  char** ret = js_mallocz(ctx, sizeof(char*) * (len + 1));
  for(i = 0; i < len; i++) {
    JSValue item = JS_GetPropertyUint32(ctx, array, i);
    ret[i] = js_tostring(ctx, item);
    JS_FreeValue(ctx, item);
  }
  if(argcp)
    *argcp = len;
  return ret;
}

void
js_atom_dump(JSContext* ctx, JSAtom atom, DynBuf* db, BOOL color) {
  const char* str;
  BOOL is_int;
  str = JS_AtomToCString(ctx, atom);
  is_int = js_atom_isint(atom) || is_integer(str);
  if(color)
    dbuf_putstr(db, is_int ? "\x1b[33m" : "\x1b[1;30m");

  dbuf_putstr(db, str);
  if(color)
    dbuf_putstr(db, "\x1b[1;36m");

  if(!is_int)
    dbuf_printf(db, "(0x%x)", js_atom_tobinary(atom));

  if(color)
    dbuf_putstr(db, "\x1b[m");
}

unsigned int
js_atom_tobinary(JSAtom atom) {
  ssize_t ret;
  if(js_atom_isint(atom)) {
    ret = js_atom_toint(atom);
    ret = -ret;
  } else {
    ret = atom;
  }
  return ret;
}

const char*
js_atom_to_cstringlen(JSContext* ctx, size_t* len, JSAtom atom) {
  JSValue v;
  const char* s;
  v = JS_AtomToValue(ctx, atom);
  s = JS_ToCStringLen(ctx, len, v);
  JS_FreeValue(ctx, v);
  return s;
}

int32_t
js_atom_toint32(JSContext* ctx, JSAtom atom) {
  if(!js_atom_isint(atom)) {
    int64_t i = INT64_MIN;
    js_atom_toint64(ctx, &i, atom);
    return i;
  }
  return -atom;
}

int
js_atom_toint64(JSContext* ctx, int64_t* i, JSAtom atom) {
  int ret;
  JSValue value;
  *i = INT64_MAX;
  value = JS_AtomToValue(ctx, atom);
  ret = !JS_ToInt64(ctx, i, value);
  JS_FreeValue(ctx, value);
  return ret;
}

BOOL
js_atom_is_index(JSContext* ctx, int64_t* pval, JSAtom atom) {
  JSValue value;
  BOOL ret = FALSE;
  int64_t index;

  if(atom & (1U << 31)) {
    *pval = atom & (~(1U << 31));
    return TRUE;
  }

  value = JS_AtomToValue(ctx, atom);

  if(JS_IsNumber(value)) {
    JS_ToInt64(ctx, &index, value);
    ret = TRUE;
  } else if(JS_IsString(value)) {
    const char* s = JS_ToCString(ctx, value);
    if(s[0] == '-' && is_digit_char(s[s[0] == '-'])) {
      index = atoi(s);
      ret = TRUE;
    }
    JS_FreeCString(ctx, s);
  }

  if(ret == TRUE)
    *pval = index;

  return ret;
}

BOOL
js_atom_is_length(JSContext* ctx, JSAtom atom) {
  const char* str = JS_AtomToCString(ctx, atom);
  BOOL ret = !strcmp(str, "length");
  JS_FreeCString(ctx, str);
  return ret;
}

const char*
js_function_name(JSContext* ctx, JSValueConst value) {
  JSAtom atom;
  JSValue str, name, args[2], idx;
  const char* s = 0;
  int32_t i = -1;
  str = js_value_tostring(ctx, "Function", value);
  atom = JS_NewAtom(ctx, "indexOf");
  args[0] = JS_NewString(ctx, "function ");
  idx = JS_Invoke(ctx, str, atom, 1, args);
  JS_FreeValue(ctx, args[0]);
  JS_ToInt32(ctx, &i, idx);
  if(i != 0) {
    JS_FreeAtom(ctx, atom);
    JS_FreeValue(ctx, str);
    return 0;
  }
  args[0] = JS_NewString(ctx, "(");
  idx = JS_Invoke(ctx, str, atom, 1, args);
  JS_FreeValue(ctx, args[0]);
  JS_FreeAtom(ctx, atom);
  atom = JS_NewAtom(ctx, "substring");
  args[0] = JS_NewUint32(ctx, 9);
  args[1] = idx;
  name = JS_Invoke(ctx, str, atom, 2, args);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, str);
  JS_FreeAtom(ctx, atom);
  s = JS_ToCString(ctx, name);
  JS_FreeValue(ctx, name);
  return s;
}

JSValue
js_global_get(JSContext* ctx, const char* prop) {
  JSValue global_obj, ret;
  global_obj = JS_GetGlobalObject(ctx);
  ret = JS_GetPropertyStr(ctx, global_obj, prop);
  JS_FreeValue(ctx, global_obj);
  return ret;
}

JSValue
js_global_prototype(JSContext* ctx, const char* class_name) {
  JSValue ctor, ret;
  ctor = js_global_get(ctx, class_name);
  ret = JS_GetPropertyStr(ctx, ctor, "prototype");
  JS_FreeValue(ctx, ctor);
  return ret;
}

JSValue
js_iterator_method(JSContext* ctx, JSValueConst obj) {
  JSAtom atom;
  JSValue ret = JS_UNDEFINED;
  atom = js_symbol_static_atom(ctx, "iterator");
  if(JS_HasProperty(ctx, obj, atom))
    ret = JS_GetProperty(ctx, obj, atom);

  JS_FreeAtom(ctx, atom);
  if(!JS_IsFunction(ctx, ret)) {
    atom = js_symbol_static_atom(ctx, "asyncIterator");
    if(JS_HasProperty(ctx, obj, atom))
      ret = JS_GetProperty(ctx, obj, atom);

    JS_FreeAtom(ctx, atom);
  }
  return ret;
}

JSValue
js_iterator_new(JSContext* ctx, JSValueConst obj) {
  JSValue fn, ret;
  fn = js_iterator_method(ctx, obj);
  ret = JS_Call(ctx, fn, obj, 0, 0);
  JS_FreeValue(ctx, fn);
  return ret;
}

JSValue
js_iterator_next(JSContext* ctx, JSValueConst obj, BOOL* done_p) {
  JSValue fn, result, done, value;
  fn = JS_GetPropertyStr(ctx, obj, "next");
  result = JS_Call(ctx, fn, obj, 0, 0);
  JS_FreeValue(ctx, fn);
  done = JS_GetPropertyStr(ctx, result, "done");
  value = JS_GetPropertyStr(ctx, result, "value");
  JS_FreeValue(ctx, result);
  *done_p = JS_ToBool(ctx, done);
  JS_FreeValue(ctx, done);
  return value;
}

JSValue
js_object_constructor(JSContext* ctx, JSValueConst value) {
  JSValue ctor = JS_UNDEFINED;
  if(JS_IsObject(value))
    ctor = JS_GetPropertyStr(ctx, value, "constructor");
  return ctor;
}

char*
js_object_classname(JSContext* ctx, JSValueConst value) {
  JSValue proto, ctor;
  const char* str;
  char* name = 0;
  int namelen;
  ctor = js_object_constructor(ctx, value);
  if(!JS_IsFunction(ctx, ctor)) {
    proto = JS_GetPrototype(ctx, value);
    ctor = js_object_constructor(ctx, proto);
  }
  if((str = JS_ToCString(ctx, ctor))) {
    if(!strncmp(str, "function ", 9)) {
      namelen = byte_chr(str + 9, strlen(str) - 9, '(');
      name = js_strndup(ctx, str + 9, namelen);
    }
  }
  if(!name) {
    if(str)
      js_cstring_free(ctx, str);
    if((str = JS_ToCString(ctx, JS_GetPropertyStr(ctx, ctor, "name"))))
      name = js_strdup(ctx, str);
  }
  if(str)
    js_cstring_free(ctx, str);
  return name;
}

BOOL
js_object_equals(JSContext* ctx, JSValueConst a, JSValueConst b) {
  JSPropertyEnum *atoms_a, *atoms_b;
  uint32_t i, natoms_a, natoms_b;
  int32_t ta, tb;
  ta = js_value_type(ctx, a);
  tb = js_value_type(ctx, b);
  assert(ta == TYPE_OBJECT);
  assert(tb == TYPE_OBJECT);
  if(JS_GetOwnPropertyNames(ctx, &atoms_a, &natoms_a, a, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY))
    return FALSE;

  if(JS_GetOwnPropertyNames(ctx, &atoms_b, &natoms_b, b, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY))
    return FALSE;

  if(natoms_a != natoms_b)
    return FALSE;

  qsort_r(&atoms_a, natoms_a, sizeof(JSPropertyEnum), &js_propenum_cmp, ctx);
  qsort_r(&atoms_b, natoms_b, sizeof(JSPropertyEnum), &js_propenum_cmp, ctx);
  for(i = 0; i < natoms_a; i++)
    if(atoms_a[i].atom != atoms_b[i].atom)
      return FALSE;
  return TRUE;
}

int
js_object_is(JSContext* ctx, JSValueConst value, const char* cmp) {
  int ret;
  const char* str;
  str = js_object_tostring(ctx, value);
  ret = strcmp(str, cmp) == 0;
  js_cstring_free(ctx, str);
  return ret;
}

JSValue
js_object_construct(JSContext* ctx, JSValueConst ctor) {
  JSValueConst args[] = {JS_UNDEFINED};
  return JS_CallConstructor(ctx, ctor, 0, args);
}

JSValue
js_object_error(JSContext* ctx, const char* message) {
  JSValueConst ctor = js_global_get(ctx, "Error");
  JSValueConst args[] = {JS_NewString(ctx, message)};
  JSValue ret;
  ret = JS_CallConstructor(ctx, ctor, 1, args);
  JS_FreeValue(ctx, ctor);
  JS_FreeValue(ctx, args[0]);
  return ret;
}

JSValue
js_object_stack(JSContext* ctx) {
  JSValue error = js_object_error(ctx, "");
  JSValue stack = JS_GetPropertyStr(ctx, error, "stack");
  JS_FreeValue(ctx, error);
  return stack;
}

BOOL
js_has_propertystr(JSContext* ctx, JSValueConst obj, const char* str) {
  JSAtom atom;
  BOOL ret = FALSE;
  atom = JS_NewAtom(ctx, str);
  ret = JS_HasProperty(ctx, obj, atom);
  JS_FreeAtom(ctx, atom);
  return ret;
}

BOOL
js_get_propertystr_bool(JSContext* ctx, JSValueConst obj, const char* str) {
  BOOL ret = FALSE;
  JSValue value;
  value = JS_GetPropertyStr(ctx, obj, str);
  if(!JS_IsException(value))
    ret = JS_ToBool(ctx, value);

  JS_FreeValue(ctx, value);
  return ret;
}

const char*
js_get_propertystr_cstring(JSContext* ctx, JSValueConst obj, const char* prop) {
  JSValue value;
  const char* ret;
  value = JS_GetPropertyStr(ctx, obj, prop);
  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;

  ret = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);
  return ret;
}

const char*
js_get_propertystr_cstringlen(JSContext* ctx, JSValueConst obj, const char* prop, size_t* lenp) {
  JSValue value;
  const char* ret;
  value = JS_GetPropertyStr(ctx, obj, prop);
  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;

  ret = JS_ToCStringLen(ctx, lenp, value);
  JS_FreeValue(ctx, value);
  return ret;
}

const char*
js_get_propertyint_cstring(JSContext* ctx, JSValueConst obj, uint32_t prop) {
  JSValue value;
  char* ret;
  value = JS_GetPropertyUint32(ctx, obj, prop);
  /* if(JS_IsUndefined(value) || JS_IsException(value))
     return 0;*/

  ret = js_tostring(ctx, value);
  JS_FreeValue(ctx, value);
  return ret;
}

char*
js_get_propertystr_string(JSContext* ctx, JSValueConst obj, const char* prop) {
  JSValue value;
  char* ret;
  value = JS_GetPropertyStr(ctx, obj, prop);
  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;

  ret = js_tostring(ctx, value);
  JS_FreeValue(ctx, value);
  return ret;
}

char*
js_get_propertystr_stringlen(JSContext* ctx, JSValueConst obj, const char* prop, size_t* lenp) {
  JSValue value;
  char* ret;
  value = JS_GetPropertyStr(ctx, obj, prop);
  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;

  ret = js_tostringlen(ctx, lenp, value);
  JS_FreeValue(ctx, value);
  return ret;
}

int32_t
js_get_propertystr_int32(JSContext* ctx, JSValueConst obj, const char* prop) {
  JSValue value;
  int32_t ret;
  value = JS_GetPropertyStr(ctx, obj, prop);
  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;
  JS_ToInt32(ctx, &ret, value);
  JS_FreeValue(ctx, value);
  return ret;
}

uint64_t
js_get_propertystr_uint64(JSContext* ctx, JSValueConst obj, const char* prop) {
  JSValue value;
  uint64_t ret;
  value = JS_GetPropertyStr(ctx, obj, prop);
  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;
  JS_ToIndex(ctx, &ret, value);
  JS_FreeValue(ctx, value);
  return ret;
}

void
js_set_propertyint_string(JSContext* ctx, JSValueConst obj, uint32_t i, const char* str) {
  JSValue value;
  value = JS_NewString(ctx, str);
  JS_SetPropertyUint32(ctx, obj, i, value);
}

void
js_set_propertyint_int(JSContext* ctx, JSValueConst obj, uint32_t i, int32_t value) {
  JS_SetPropertyUint32(ctx, obj, i, JS_NewInt32(ctx, value));
}

void
js_set_propertystr_string(JSContext* ctx, JSValueConst obj, const char* prop, const char* str) {
  JSValue value;
  value = JS_NewString(ctx, str);
  JS_SetPropertyStr(ctx, obj, prop, value);
}

void
js_set_propertystr_stringlen(JSContext* ctx, JSValueConst obj, const char* prop, const char* str, size_t len) {
  JSValue value;
  value = JS_NewStringLen(ctx, str, len);
  JS_SetPropertyStr(ctx, obj, prop, value);
}

int
js_get_propertydescriptor(JSContext* ctx, JSPropertyDescriptor* desc, JSValueConst value, JSAtom prop) {
  JSValue obj, proto;
  obj = JS_DupValue(ctx, value);
  do {
    if(JS_GetOwnProperty(ctx, desc, obj, prop) == TRUE)
      return TRUE;
    proto = JS_GetPrototype(ctx, obj);
    if(JS_VALUE_GET_OBJ(proto) == JS_VALUE_GET_OBJ(obj))
      break;
    JS_FreeValue(ctx, obj);
    obj = proto;
  } while(JS_IsObject(obj));
  return FALSE;
}

JSClassID
js_class_id(JSContext* ctx, int id) {
  return ctx->rt->class_array[id].class_id;
}

JSClassID
js_class_newid(void) {
  JSClassID id;
  JS_NewClassID(&id);
  return id;
}

JSClass*
js_class_get(JSContext* ctx, JSClassID id) {
  JSClass* ret = &ctx->rt->class_array[id];
  return ret->class_id == id ? ret : 0;
}

JSAtom
js_class_atom(JSContext* ctx, JSClassID id) {
  JSAtom atom = 0;
  if(id > 0 && id < (JSClassID)ctx->rt->class_count)
    atom = ctx->rt->class_array[id].class_name;
  return atom;
}

const char*
js_class_name(JSContext* ctx, JSClassID id) {
  JSAtom atom = ctx->rt->class_array[id].class_name;
  return JS_AtomToCString(ctx, atom);
}

const char*
js_object_tostring(JSContext* ctx, JSValueConst value) {
  JSValue str = js_value_tostring(ctx, "Object", value);
  const char* s = JS_ToCString(ctx, str);
  JS_FreeValue(ctx, str);
  return s;
}

const char*
js_function_tostring(JSContext* ctx, JSValueConst value) {
  JSValue str = js_value_tostring(ctx, "Function", value);
  const char* s = JS_ToCString(ctx, str);
  JS_FreeValue(ctx, str);
  return s;
}

BOOL
js_function_isnative(JSContext* ctx, JSValueConst value) {
  const char* fn = js_function_tostring(ctx, value);
  BOOL ret = !!strstr(fn, "\n    [native code]\n");
  JS_FreeCString(ctx, fn);
  return ret;
}

BOOL
js_is_input(JSContext* ctx, JSValueConst value) {
  return JS_IsString(value) || js_value_isclass(ctx, value, JS_CLASS_ARRAY_BUFFER);
}

int
js_propenum_cmp(const void* a, const void* b, void* ptr) {
  JSContext* ctx = ptr;
  const char *stra, *strb;
  int ret;
  stra = JS_AtomToCString(ctx, ((const JSPropertyEnum*)a)->atom);
  strb = JS_AtomToCString(ctx, ((const JSPropertyEnum*)b)->atom);
  ret = strverscmp(stra, strb);
  js_cstring_free(ctx, stra);
  js_cstring_free(ctx, strb);
  return ret;
}

void
js_propertyenums_free(JSContext* ctx, JSPropertyEnum* props, size_t len) {
  uint32_t i;
  for(i = 0; i < len; i++) JS_FreeAtom(ctx, props[i].atom);
  // js_free(ctx, props);
}

void
js_argv_free(JSContext* ctx, char** strv) {
  size_t i;
  if(strv == 0)
    return;

  for(i = 0; strv[i]; i++) { js_free(ctx, strv[i]); }
  js_free(ctx, strv);
}

void
js_argv_free_rt(JSRuntime* rt, char** strv) {
  size_t i;
  if(strv == 0)
    return;

  for(i = 0; strv[i]; i++) { js_free_rt(rt, strv[i]); }
  js_free_rt(rt, strv);
}

JSValue
js_argv_to_array(JSContext* ctx, char** strv) {
  JSValue ret = JS_NewArray(ctx);
  if(strv) {
    size_t i;
    for(i = 0; strv[i]; i++) JS_SetPropertyUint32(ctx, ret, i, JS_NewString(ctx, strv[i]));
  }
  return ret;
}

size_t
js_argv_length(char** strv) {
  size_t i;
  for(i = 0; strv[i]; i++) {}
  return i;
}

char**
js_argv_dup(JSContext* ctx, char** strv) {
  char** ret;
  size_t i, len = js_argv_length(strv);
  ret = js_malloc(ctx, (len + 1) * sizeof(char*));
  for(i = 0; i < len; i++) { ret[i] = js_strdup(ctx, strv[i]); }
  ret[i] = 0;
  return ret;
}

JSValue
js_intv_to_array(JSContext* ctx, int* intv) {
  JSValue ret = JS_NewArray(ctx);
  if(intv) {
    size_t i;
    for(i = 0; intv[i]; i++) JS_SetPropertyUint32(ctx, ret, i, JS_NewInt32(ctx, intv[i]));
  }
  return ret;
}

JSAtom
js_symbol_static_atom(JSContext* ctx, const char* name) {
  JSValue sym = js_symbol_static_value(ctx, name);
  JSAtom ret = JS_ValueToAtom(ctx, sym);
  JS_FreeValue(ctx, sym);
  return ret;
}

JSValue
js_symbol_static_value(JSContext* ctx, const char* name) {
  JSValue symbol_ctor, ret;
  symbol_ctor = js_symbol_ctor(ctx);
  ret = JS_GetPropertyStr(ctx, symbol_ctor, name);
  JS_FreeValue(ctx, symbol_ctor);
  return ret;
}

JSValue
js_symbol_ctor(JSContext* ctx) {
  return js_global_get(ctx, "Symbol");
}

JSValue
js_symbol_invoke_static(JSContext* ctx, const char* name, JSValueConst arg) {
  JSValue ret;
  JSAtom method_name = JS_NewAtom(ctx, name);
  ret = JS_Invoke(ctx, js_symbol_ctor(ctx), method_name, 1, &arg);
  JS_FreeAtom(ctx, method_name);
  return ret;
}

JSValue
js_symbol_for(JSContext* ctx, const char* sym_for) {
  JSValue key, sym;
  JSAtom atom;
  key = JS_NewString(ctx, sym_for);
  sym = js_symbol_invoke_static(ctx, "for", key);
  JS_FreeValue(ctx, key);
  return sym;
}

JSAtom
js_symbol_for_atom(JSContext* ctx, const char* sym_for) {
  JSValue sym = js_symbol_for(ctx, sym_for);
  JSAtom atom = JS_ValueToAtom(ctx, sym);
  JS_FreeValue(ctx, sym);
  return atom;
}

JSValue
js_symbol_to_string(JSContext* ctx, JSValueConst sym) {
  JSValue value, str;
  JSAtom atom;
  value = js_symbol_invoke_static(ctx, "keyFor", sym);
  if(!JS_IsUndefined(value))
    return value;

  atom = JS_ValueToAtom(ctx, sym);
  str = JS_AtomToString(ctx, atom);
  JS_FreeAtom(ctx, atom);
  return str;
}

const char*
js_symbol_to_cstring(JSContext* ctx, JSValueConst sym) {
  JSValue value = js_symbol_to_string(ctx, sym);
  const char* str;
  str = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);
  return str;
}

JSValue*
js_values_dup(JSContext* ctx, int nvalues, JSValueConst* values) {
  JSValue* ret = js_mallocz_rt(ctx->rt, sizeof(JSValue) * nvalues);
  int i;
  for(i = 0; i < nvalues; i++) ret[i] = JS_DupValueRT(ctx->rt, values[i]);
  return ret;
}
/*
void
js_values_free(JSContext* ctx, int nvalues, JSValueConst* values) {
  int i;
  for(i = 0; i < nvalues; i++) JS_FreeValue(ctx, values[i]);
  js_free(ctx, values);
}
*/
void
js_values_free(JSRuntime* rt, int nvalues, JSValueConst* values) {
  int i;
  for(i = 0; i < nvalues; i++) JS_FreeValueRT(rt, values[i]);
  js_free_rt(rt, values);
}

JSValue
js_values_toarray(JSContext* ctx, int nvalues, JSValueConst* values) {
  int i;
  JSValue ret = JS_NewArray(ctx);
  for(i = 0; i < nvalues; i++) JS_SetPropertyUint32(ctx, ret, i, JS_DupValue(ctx, values[i]));
  return ret;
}

const char*
js_value_type_name(int32_t type) {
  int32_t flag = js_value_type2flag(type);
  const char* const* types = js_value_types();
  return types[flag];
}

const char*
js_value_typestr(JSContext* ctx, JSValueConst value) {
  int32_t type = js_value_type(ctx, value);
  return js_value_type_name(type);
}

void*
js_value_get_ptr(JSValueConst v) {
  return JS_VALUE_GET_PTR(v);
}

JSObject*
js_value_get_obj(JSValueConst v) {
  return JS_VALUE_GET_TAG(v) == JS_TAG_OBJECT ? JS_VALUE_GET_OBJ(v) : 0;
}

int32_t
js_value_get_tag(JSValueConst v) {
  return JS_VALUE_GET_TAG(v);
}

BOOL
js_value_has_ref_count(JSValue v) {
  return ((unsigned)js_value_get_tag(v) >= (unsigned)JS_TAG_FIRST);
}

enum value_mask
js_value_type(JSContext* ctx, JSValueConst value) {
  int32_t flag;
  uint32_t type = 0;
  if((flag = js_value_type_get(ctx, value)) == -1)
    return 0;

  if(flag == FLAG_ARRAY /*|| flag == FLAG_FUNCTION*/)
    type |= TYPE_OBJECT;

  type |= 1 << flag;

  return type;
}

int32_t
js_value_type_get(JSContext* ctx, JSValueConst value) {
  if(JS_IsArray(ctx, value))
    return FLAG_ARRAY;

  if(JS_IsFunction(ctx, value))
    return FLAG_FUNCTION;

  if(JS_VALUE_IS_NAN(value))
    return FLAG_NAN;

  return js_value_type_flag(value);
}

int32_t
js_value_type_flag(JSValueConst value) {
  switch(JS_VALUE_GET_TAG(value)) {
    case JS_TAG_BIG_DECIMAL: return FLAG_BIG_DECIMAL;
    case JS_TAG_BIG_INT: return FLAG_BIG_INT;
    case JS_TAG_BIG_FLOAT: return FLAG_BIG_FLOAT;
    case JS_TAG_SYMBOL: return FLAG_SYMBOL;
    case JS_TAG_STRING: return FLAG_STRING;
    case JS_TAG_MODULE: return FLAG_MODULE;
    case JS_TAG_FUNCTION_BYTECODE: return FLAG_FUNCTION_BYTECODE;
    case JS_TAG_OBJECT: return FLAG_OBJECT;
    case JS_TAG_INT: return FLAG_INT;
    case JS_TAG_BOOL: return FLAG_BOOL;
    case JS_TAG_NULL: return FLAG_NULL;
    case JS_TAG_UNDEFINED: return FLAG_UNDEFINED;
    case JS_TAG_UNINITIALIZED: return FLAG_UNINITIALIZED;
    case JS_TAG_CATCH_OFFSET: return FLAG_CATCH_OFFSET;
    case JS_TAG_EXCEPTION: return FLAG_EXCEPTION;
    case JS_TAG_FLOAT64: return FLAG_FLOAT64;
  }
  return -1;
}

void
js_value_free(JSContext* ctx, JSValue v) {
  if(js_value_has_ref_count(v)) {
    JSRefCountHeader* p = (JSRefCountHeader*)js_value_get_ptr(v);
    if(p->ref_count > 0) {
      --p->ref_count;
      if(p->ref_count == 0)
        __JS_FreeValue(ctx, v);
    }
  }
}

JSValue
js_value_clone(JSContext* ctx, JSValueConst value) {
  enum value_mask type = 1 << js_value_type_get(ctx, value);
  JSValue ret = JS_UNDEFINED;
  switch(type) {
    /*case TYPE_STRING: {
     size_t len;
     const char* str;
     str = JS_ToCStringLen(ctx, &len, value);
     ret = JS_NewStringLen(ctx, str, len);
     js_cstring_free(ctx, str);
     break;
     }*/
    case TYPE_INT: {
      ret = JS_NewInt32(ctx, JS_VALUE_GET_INT(value));
      break;
    }
    case TYPE_FLOAT64: {
      ret = JS_NewFloat64(ctx, JS_VALUE_GET_FLOAT64(value));
      break;
    }
    case TYPE_BOOL: {
      ret = JS_NewBool(ctx, JS_VALUE_GET_BOOL(value));
      break;
    }
    case TYPE_FUNCTION:
    case TYPE_ARRAY:
    case TYPE_OBJECT: {
      JSPropertyEnum* tab_atom;
      uint32_t tab_atom_len;
      ret = JS_IsArray(ctx, value) ? JS_NewArray(ctx) : JS_NewObject(ctx);
      if(!JS_GetOwnPropertyNames(
             ctx, &tab_atom, &tab_atom_len, value, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY)) {
        uint32_t i;
        for(i = 0; i < tab_atom_len; i++) {
          JSValue prop;
          prop = JS_GetProperty(ctx, value, tab_atom[i].atom);
          JS_SetProperty(ctx, ret, tab_atom[i].atom, js_value_clone(ctx, prop));
        }
      }
      break;
    }
    case TYPE_UNDEFINED:
    case TYPE_NULL:
    case TYPE_STRING:
    case TYPE_SYMBOL:
    case TYPE_BIG_DECIMAL:
    case TYPE_BIG_INT:
    case TYPE_BIG_FLOAT: {
      ret = JS_DupValue(ctx, value);
      break;
    }
    default: {
      ret = JS_ThrowTypeError(ctx, "No such type: %s (0x%08x)\n", js_value_type_name(type), type);
      break;
    }
  }
  return ret;
}

void
js_value_dump(JSContext* ctx, JSValueConst value, DynBuf* db) {
  const char* str;
  size_t len;
  if(JS_IsObject(value)) {
    const char* str = js_object_tostring(ctx, value);
    dbuf_putstr(db, str);
    js_cstring_free(ctx, str);
    if(db->size && db->buf[db->size - 1] == '\n')
      db->size--;
  } else {
    int is_string = JS_IsString(value);

    if(is_string)
      dbuf_putc(db, '"');

    str = JS_ToCStringLen(ctx, &len, value);
    dbuf_append(db, (const uint8_t*)str, len);

    js_cstring_free(ctx, str);

    if(is_string)
      dbuf_putc(db, '"');
    else if(JS_IsBigFloat(value))
      dbuf_putc(db, 'l');
    else if(JS_IsBigDecimal(value))
      dbuf_putc(db, 'm');
    else if(JS_IsBigInt(ctx, value))
      dbuf_putc(db, 'n');
  }
}

BOOL
js_value_equals(JSContext* ctx, JSValueConst a, JSValueConst b) {
  int32_t ta, tb;
  BOOL ret = FALSE;
  ta = js_value_type(ctx, a);
  tb = js_value_type(ctx, b);

  if(ta != tb) {
    ret = FALSE;
  } else if(ta & tb & (TYPE_NULL | TYPE_UNDEFINED | TYPE_NAN)) {
    ret = TRUE;
  } else if(ta & tb & (TYPE_BIG_INT | TYPE_BIG_FLOAT | TYPE_BIG_DECIMAL)) {
    const char *astr, *bstr;

    astr = JS_ToCString(ctx, a);
    bstr = JS_ToCString(ctx, b);

    ret = !strcmp(astr, bstr);

    JS_FreeCString(ctx, astr);
    JS_FreeCString(ctx, bstr);

  } else if(ta & TYPE_INT) {
    int32_t inta, intb;

    inta = JS_VALUE_GET_INT(a);
    intb = JS_VALUE_GET_INT(b);
    ret = inta == intb;
  } else if(ta & TYPE_BOOL) {
    BOOL boola, boolb;

    boola = !!JS_VALUE_GET_BOOL(a);
    boolb = !!JS_VALUE_GET_BOOL(b);
    ret = boola == boolb;

  } else if(ta & TYPE_FLOAT64) {
    double flta, fltb;

    flta = JS_VALUE_GET_FLOAT64(a);
    fltb = JS_VALUE_GET_FLOAT64(b);
    ret = flta == fltb;

  } else if(ta & TYPE_OBJECT) {
    ret = js_object_equals(ctx, a, b);
    /*void *obja, *objb;

    obja = JS_VALUE_GET_OBJ(a);
    objb = JS_VALUE_GET_OBJ(b);

    ret = obja == objb;*/
  } else if(ta & TYPE_STRING) {
    const char *stra, *strb;

    stra = JS_ToCString(ctx, a);
    strb = JS_ToCString(ctx, b);

    ret = !strcmp(stra, strb);

    js_cstring_free(ctx, stra);
    js_cstring_free(ctx, strb);
  }

  return ret;
}

JSValue
js_value_from_char(JSContext* ctx, int c) {
  uint8_t buf[16];
  size_t len = unicode_to_utf8(buf, c);
  return JS_NewStringLen(ctx, (const char*)buf, len);
}

void
js_value_print(JSContext* ctx, JSValueConst value) {
  DynBuf dbuf;
  js_dbuf_init(ctx, &dbuf);
  js_value_dump(ctx, value, &dbuf);
  dbuf_0(&dbuf);
  fputs((const char*)dbuf.buf, stdout);
  dbuf_free(&dbuf);
}

int
js_value_tosize(JSContext* ctx, size_t* sz, JSValueConst value) {
  uint64_t u64 = 0;
  int r;
  r = JS_ToIndex(ctx, &u64, value);
  *sz = u64;
  return r;
}

void
js_value_free_rt(JSRuntime* rt, JSValue v) {
  if(js_value_has_ref_count(v)) {
    JSRefCountHeader* p = (JSRefCountHeader*)js_value_get_ptr(v);
    --p->ref_count;
    if(p->ref_count == 0)
      __JS_FreeValueRT(rt, v);
  }
}

char*
js_cstring_ptr(JSValueConst v) {
  JSString* p;

  if(JS_IsString(v)) {
    p = JS_VALUE_GET_PTR(v);
    return (char*)p->u.str8;
  }
  return 0;
}
char*
js_cstring_dup(JSContext* ctx, const char* str) {
  JSString* p;
  if(!str)
    return 0;
  /* purposely removing constness */
  p = (JSString*)(void*)(str - offsetof(JSString, u));
  JS_DupValue(ctx, JS_MKPTR(JS_TAG_STRING, p));
  return (char*)str;
}

JSValueConst
js_cstring_value(const char* ptr) {
  JSString* p;
  if(!ptr)
    return JS_UNDEFINED;

  p = (JSString*)(void*)(ptr - offsetof(JSString, u));
  return JS_MKPTR(JS_TAG_STRING, p);
}

void
js_cstring_dump(JSContext* ctx, JSValueConst value, DynBuf* db) {
  const char* str;
  size_t len;

  str = JS_ToCStringLen(ctx, &len, value);
  dbuf_append(db, (const uint8_t*)str, len);

  js_cstring_free(ctx, str);
}

size_t
token_length(const char* str, size_t len, char delim) {
  const char *s, *e;
  size_t pos;
  for(s = str, e = s + len; s < e; s += pos + 1) {
    pos = byte_chr(s, e - s, delim);
    if(s + pos == e)
      break;

    if(pos == 0 || s[pos - 1] != '\\') {
      s += pos;
      break;
    }
  }
  return s - str;
}

JSValue
js_module_name(JSContext* ctx, JSValueConst value) {
  JSModuleDef* module;
  JSValue name = JS_UNDEFINED;

  if(JS_VALUE_GET_TAG(value) == JS_TAG_MODULE && (module = JS_VALUE_GET_PTR(value)))
    name = JS_AtomToValue(ctx, module->module_name);

  return name;
}

char*
js_module_namestr(JSContext* ctx, JSValueConst value) {
  JSValue name = js_module_name(ctx, value);
  char* s = js_tostring(ctx, name);
  JS_FreeValue(ctx, name);
  return s;
}

BOOL
js_is_arraybuffer(JSContext* ctx, JSValueConst value) {
  BOOL ret = FALSE;
  if(!JS_IsObject(value))
    return ret;
  if(!ret)
    ret |= js_value_isclass(ctx, value, JS_CLASS_ARRAY_BUFFER);
  // if(!ret) ret |= js_object_is(ctx, value, "[object ArrayBuffer]");
  /*  if(!ret) {
      JSObject* obj;
      if((obj = js_value_get_obj(value)) && obj->class_id) {
        JSValue ctor = js_global_get(ctx, "ArrayBuffer");
        ret = JS_IsInstanceOf(ctx, value, ctor);
        JS_FreeValue(ctx, ctor);
      }
    }*/
  return ret;
}

BOOL
js_is_sharedarraybuffer(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) && (js_value_isclass(ctx, value, JS_CLASS_SHARED_ARRAY_BUFFER)/* ||
                                js_object_is(ctx, value, "[object SharedArrayBuffer]")*/);
}

BOOL
js_is_map(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) &&
         (js_value_isclass(ctx, value, JS_CLASS_MAP) /*|| js_object_is(ctx, value, "[object Map]")*/);
}

BOOL
js_is_set(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) &&
         (js_value_isclass(ctx, value, JS_CLASS_SET) /* || js_object_is(ctx, value, "[object Set]")*/);
}

BOOL
js_is_generator(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) &&
         (js_value_isclass(ctx, value, JS_CLASS_GENERATOR) /*|| js_object_is(ctx, value, "[object Generator]")*/);
}

BOOL
js_is_regexp(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) &&
         (js_value_isclass(ctx, value, JS_CLASS_REGEXP) /*|| js_object_is(ctx, value, "[object RegExp]")*/);
}

BOOL
js_is_promise(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) &&
         (js_value_isclass(ctx, value, JS_CLASS_PROMISE) || js_object_is(ctx, value, "[object Promise]"));
}

BOOL
js_is_iterable(JSContext* ctx, JSValueConst obj) {
  JSAtom atom;
  BOOL ret = FALSE;
  atom = js_symbol_static_atom(ctx, "iterator");
  if(JS_HasProperty(ctx, obj, atom))
    ret = TRUE;

  JS_FreeAtom(ctx, atom);
  if(!ret) {
    atom = js_symbol_static_atom(ctx, "asyncIterator");
    if(JS_HasProperty(ctx, obj, atom))
      ret = TRUE;

    JS_FreeAtom(ctx, atom);
  }
  return ret;
}

BOOL
js_is_iterator(JSContext* ctx, JSValueConst obj) {
  if(JS_IsObject(obj)) {
    JSValue next = JS_GetPropertyStr(ctx, obj, "next");

    if(JS_IsFunction(ctx, next))
      return TRUE;
  }
  return FALSE;
}

JSValue
js_typedarray_prototype(JSContext* ctx) {
  JSValue u8arr_proto = js_global_prototype(ctx, "Uint8Array");
  JSValue typedarr_proto = JS_GetPrototype(ctx, u8arr_proto);
  JS_FreeValue(ctx, u8arr_proto);
  return typedarr_proto;
}

JSValue
js_typedarray_constructor(JSContext* ctx) {
  JSValue typedarr_proto = js_typedarray_prototype(ctx);
  JSValue typedarr_ctor = JS_GetPropertyStr(ctx, typedarr_proto, "constructor");
  JS_FreeValue(ctx, typedarr_proto);
  return typedarr_ctor;
}

JSValue
js_invoke(JSContext* ctx, JSValueConst this_obj, const char* method, int argc, JSValueConst argv[]) {
  JSAtom atom;
  JSValue ret;
  atom = JS_NewAtom(ctx, method);
  ret = JS_Invoke(ctx, this_obj, atom, argc, argv);
  JS_FreeAtom(ctx, atom);
  return ret;
}

JSValue
js_symbol_operatorset_value(JSContext* ctx) {
  return js_symbol_static_value(ctx, "operatorSet");
}

JSAtom
js_symbol_operatorset_atom(JSContext* ctx) {
  JSValue operator_set = js_symbol_operatorset_value(ctx);
  JSAtom atom = JS_ValueToAtom(ctx, operator_set);
  JS_FreeValue(ctx, operator_set);
  return atom;
}

JSValue
js_operators_create(JSContext* ctx, JSValue* this_obj) {
  JSValue operators = js_global_get(ctx, "Operators");
  JSValue create_fun = JS_GetPropertyStr(ctx, operators, "create");
  if(this_obj)
    *this_obj = operators;
  else
    JS_FreeValue(ctx, operators);
  return create_fun;
}

JSValue
js_number_new(JSContext* ctx, int32_t n) {
  if(n == INT32_MAX)
    return JS_NewFloat64(ctx, INFINITY);

  return JS_NewInt32(ctx, n);
}

JSValue
js_date_new(JSContext* ctx, JSValueConst arg) {
  JSValue ctor = js_global_get(ctx, "Date");
  JSValue ret = JS_CallConstructor(ctx, ctor, 1, &arg);
  JS_FreeValue(ctx, ctor);
  return ret;
}

JSValue
js_date_from_ms(JSContext* ctx, int64_t ms) {
  JSValue arg = JS_NewInt64(ctx, ms);
  JSValue ret = js_date_new(ctx, arg);
  JS_FreeValue(ctx, arg);
  return ret;
}

JSValue
js_date_from_time_ns(JSContext* ctx, time_t t, long ns) {
  return js_date_from_ms(ctx, t * 1000ull + ns / 1000000ull);
}

JSValue
js_date_from_timespec(JSContext* ctx, const struct timespec ts) {
  return js_date_from_time_ns(ctx, ts.tv_sec, ts.tv_nsec);
}

int64_t
js_date_gettime(JSContext* ctx, JSValueConst arg) {
  int64_t r = -1;
  JSAtom method = JS_NewAtom(ctx, "getTime");
  JSValue value = JS_Invoke(ctx, arg, method, 0, 0);
  JS_FreeAtom(ctx, method);
  if(JS_IsNumber(value))
    JS_ToInt64(ctx, &r, value);
  JS_FreeValue(ctx, value);
  return r;
}

int64_t
js_date_time(JSContext* ctx, JSValue arg) {
  int64_t r = -1;
  if(JS_IsObject(arg))
    r = js_date_gettime(ctx, arg);
  else if(!js_is_nullish(ctx, arg))
    JS_ToInt64(ctx, &r, arg);
  return r;
}

struct timespec
js_date_timespec(JSContext* ctx, JSValue arg) {
  struct timespec ts;
  int64_t r = js_date_time(ctx, arg);
  ts.tv_sec = r / 1000ull;
  ts.tv_nsec = (r - ts.tv_sec) * 1000000ull;
  return ts;
}
