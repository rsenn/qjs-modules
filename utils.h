#ifndef QJS_MODULES_UTILS_H
#define QJS_MODULES_UTILS_H

#include <string.h>
#include <math.h>
#include "quickjs.h"

#define max_num(a, b) ((a) > (b) ? (a) : (b))

#define is_control_char(c)                                                                                             \
  ((c) == '\a' || (c) == '\b' || (c) == '\t' || (c) == '\n' || (c) == '\v' || (c) == '\f' || (c) == '\r')
#define is_alphanumeric_char(c) ((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z')
#define is_digit_char(c) ((c) >= '0' && (c) <= '9')
#define is_newline_char(c) ((c) == '\n')
#define is_identifier_char(c) (is_alphanumeric_char(c) || is_digit_char(c) || (c) == '$' || (c) == '_')
#define is_whitespace_char(c) ((c) == ' ' || (c) == '\t' || (c) == '\v' || (c) == '\n' || (c) == '\r')

typedef struct {
  BOOL done;
  JSValue value;
} IteratorValue;

static inline int
is_escape_char(char c) {
  return is_control_char(c) || c == '\\' || c == '\'' || c == 0x1b || c == 0;
}

static inline int
is_identifier(const char* str) {
  if(!((*str >= 'A' && *str <= 'Z') || (*str >= 'a' && *str <= 'z') || *str == '$'))
    return 0;
  while(*++str) {
    if(!is_identifier_char(*str))
      return 0;
  }
  return 1;
}

static inline int
is_integer(const char* str) {
  if(!(*str >= '1' && *str <= '9') && !(*str == '0' && str[1] == '\0'))
    return 0;
  while(*++str) {
    if(!is_digit_char(*str))
      return 0;
  }
  return 1;
}

static inline size_t
min_size(size_t a, size_t b) {
  if(a < b)
    return a;
  else
    return b;
}

static inline uint64_t
abs_int64(int64_t a) {
  return a < 0 ? -a : a;
}

static inline uint32_t
abs_int32(int32_t i) {
  return i < 0 ? -i : i;
}

static inline int32_t
sign_int32(uint32_t i) {
  return (i & 0x80000000) ? -1 : 1;
}

static inline size_t
byte_count(const void* s, size_t n, char c) {
  const unsigned char* t;
  unsigned char ch = (unsigned char)c;
  size_t count;
  for(t = (unsigned char*)s, count = 0; n; ++t, --n) {
    if(*t == ch)
      ++count;
  }
  return count;
}

static inline size_t
byte_chr(const char* str, size_t len, char c) {
  const char *s = str, *t = s + len;
  for(; s < t; ++s)
    if(*s == c)
      break;
  return s - str;
}

static inline size_t
byte_chrs(const char* str, size_t len, char needle[], size_t nl) {
  const char* s = str;
  const char* t = str + len;

  for(;;) {
    if(s == t)
      break;
    if(byte_chr(needle, nl, *s) < nl)
      break;
    ++s;
  }
  return s - (const char*)str;
}

static inline size_t
str_chr(const char* in, char needle) {
  const char* t = in;
  const char c = needle;
  for(;;) {
    if(!*t || *t == c) {
      break;
    };
    ++t;
  }
  return (size_t)(t - in);
}

static inline size_t
str_rchr(const char* s, char needle) {
  const char *in = s, *found = 0;
  for(;;) {
    if(!*in)
      break;
    if(*in == needle)
      found = in;
    ++in;
  }
  return (size_t)((found ? found : in) - s);
}

static inline size_t
str_rchrs(const char* in, const char needles[], size_t nn) {
  const char *s = in, *found = 0;
  size_t i;
  for(;;) {
    if(!*s)
      break;
    for(i = 0; i < nn; ++i) {
      if(*s == needles[i])
        found = s;
    }
    ++s;
  }
  return (size_t)((found ? found : s) - in);
}

#define COLOR_RED "\x1b[31m"
#define COLOR_GREEN "\x1b[32m"
#define COLOR_YELLOW "\x1b[33m"
#define COLOR_MARINE "\x1b[36m"
#define COLOR_GRAY "\x1b[1;30m"
#define COLOR_NONE "\x1b[m"

static inline size_t
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

static inline size_t
ansi_length(const char* str, size_t len) {
  size_t i, n = 0, p;
  for(i = 0; i < len;) {
    if((p = ansi_skip(&str[i], len - i)) > 0) {
      i += p;
      continue;
    }
    n++;
    i++;
  }
  return n;
}

static inline size_t
ansi_truncate(const char* str, size_t len, size_t limit) {
  size_t i, n = 0, p;
  for(i = 0; i < len;) {
    if((p = ansi_skip(&str[i], len - i)) > 0) {
      i += p;
      continue;
    }
    n += is_escape_char(str[i]) ? 2 : 1;
    if(n > limit)
      break;
    i++;
  }
  return i;
}

static inline char*
str_ndup(const char* s, size_t n) {
  char* r = malloc(n + 1);
  if(r == NULL)
    return NULL;
  memcpy(r, s, n);
  r[n] = '\0';
  return r;
}

static inline size_t
predicate_find(const char* str, size_t len, int (*pred)(char)) {
  size_t pos;
  for(pos = 0; pos < len; pos++)
    if(pred(str[pos]))
      break;
  return pos;
}

static inline char
escape_char_letter(char c) {
  switch(c) {
    case '\0': return '0';
    case '\a': return 'a';
    case '\b': return 'b';
    case '\t': return 't';
    case '\n': return 'n';
    case '\v': return 'v';
    case '\f': return 'f';
    case '\r': return 'r';
    case '\\': return '\\';
    case '\'': return '\'';
  }
  return 0;
}

static inline void
dbuf_put_escaped(DynBuf* db, const char* str, size_t len) {
  size_t i = 0, j;
  while(i < len) {
    if((j = predicate_find(&str[i], len - i, is_escape_char))) {
      dbuf_put(db, (const uint8_t*)&str[i], j);
      i += j;
    }
    if(i == len)
      break;
    dbuf_putc(db, '\\');

    if(str[i] == 0x1b)
      dbuf_put(db, (const uint8_t*)"x1b", 3);
    else
      dbuf_putc(db, escape_char_letter(str[i]));
    i++;
  }
}

static inline size_t
dbuf_count(DynBuf* db, int ch) {
  return byte_count(db->buf, db->size, ch);
}

static inline void
dbuf_0(DynBuf* db) {
  dbuf_putc(db, '\0');
  db->size--;
}

static inline void
dbuf_zero(DynBuf* db) {
  dbuf_realloc(db, 0);
}

static inline char*
dbuf_at_n(const DynBuf* db, size_t i, size_t* n, char sep) {
  size_t p, l = 0;
  for(p = 0; p < db->size; ++p) {
    if(l == i) {
      *n = byte_chr(&db->buf[p], db->size - p, sep);
      return &db->buf[p];
    }
    if(db->buf[p] == sep)
      ++l;
  }
  *n = 0;
  return 0;
}

static inline const char*
dbuf_last_line(DynBuf* db, size_t* len) {
  size_t i;
  for(i = db->size; i > 0; i--)
    if(db->buf[i - 1] == '\n')
      break;
  if(len)
    *len = db->size - i;
  return (const char*)&db->buf[i];
}

static inline int32_t
dbuf_get_column(DynBuf* db) {
  size_t len;
  const char* str;
  if(db->size) {
    str = dbuf_last_line(db, &len);
    return ansi_length(str, len);
  }
  return 0;
}

static inline void
dbuf_put_colorstr(DynBuf* db, const char* str, const char* color, int with_color) {
  if(with_color)
    dbuf_putstr(db, color);
  dbuf_putstr(db, str);
  if(with_color)
    dbuf_putstr(db, COLOR_NONE);
}

#define js_object_tmpmark_set(value)                                                                                   \
  do { ((uint8_t*)JS_VALUE_GET_OBJ((value)))[5] |= 0x40; } while(0);
#define js_object_tmpmark_clear(value)                                                                                 \
  do { ((uint8_t*)JS_VALUE_GET_OBJ((value)))[5] &= ~0x40; } while(0);
#define js_object_tmpmark_isset(value) (((uint8_t*)JS_VALUE_GET_OBJ((value)))[5] & 0x40)

static inline char*
js_class_name(JSContext* ctx, JSValueConst value) {
  JSValue proto, ctor;
  const char* str;
  char* name = 0;
  int namelen;
  proto = JS_GetPrototype(ctx, value);
  ctor = JS_GetPropertyStr(ctx, proto, "constructor");
  if((str = JS_ToCString(ctx, ctor))) {
    if(!strncmp(str, "function ", 9)) {
      namelen = byte_chr(str + 9, strlen(str) - 9, '(');
      name = js_strndup(ctx, str + 9, namelen);
    }
  }
  if(!name) {
    if(str)
      JS_FreeCString(ctx, str);
    if((str = JS_ToCString(ctx, JS_GetPropertyStr(ctx, ctor, "name"))))
      name = js_strdup(ctx, str);
  }
  if(str)
    JS_FreeCString(ctx, str);
  return name;
}

static inline void
js_propertyenums_free(JSContext* ctx, JSPropertyEnum* props, size_t len) {
  uint32_t i;
  for(i = 0; i < len; i++) JS_FreeAtom(ctx, props[i].atom);
  js_free(ctx, props);
}

static inline void
js_propertydescriptor_free(JSContext* ctx, JSPropertyDescriptor* desc) {
  JS_FreeValue(ctx, desc->value);
  JS_FreeValue(ctx, desc->getter);
  JS_FreeValue(ctx, desc->setter);
}

static inline void
js_value_dump(JSContext* ctx, JSValue value, DynBuf* db) {
  const char* str;
  size_t len;

  if(JS_IsArray(ctx, value)) {
    dbuf_putstr(db, "[object Array]");
  } else {
    str = JS_ToCStringLen(ctx, &len, value);
    dbuf_put(db, (const uint8_t*)str, len);
    JS_FreeCString(ctx, str);
  }
}

#define js_value_free(ctx, value)                                                                                      \
  do {                                                                                                                 \
    JS_FreeValue((ctx), (value));                                                                                      \
    (value) = JS_UNDEFINED;                                                                                            \
  } while(0);
#define js_value_free_rt(ctx, value)                                                                                   \
  do {                                                                                                                 \
    JS_FreeValueRT((ctx), (value));                                                                                    \
    (value) = JS_UNDEFINED;                                                                                            \
  } while(0);

static inline JSValue
js_global_get(JSContext* ctx, const char* prop) {
  JSValue global_obj, ret;
  global_obj = JS_GetGlobalObject(ctx);
  ret = JS_GetPropertyStr(ctx, global_obj, prop);
  JS_FreeValue(ctx, global_obj);
  return ret;
}

static inline JSValue
js_global_prototype(JSContext* ctx, const char* class_name) {
  JSValue ctor, ret;
  ctor = js_global_get(ctx, class_name);
  ret = JS_GetPropertyStr(ctx, ctor, "prototype");
  JS_FreeValue(ctx, ctor);
  return ret;
}

static inline JSValue
js_symbol_ctor(JSContext* ctx) {
  return js_global_get(ctx, "Symbol");
}

static inline JSValue
js_symbol_get_static(JSContext* ctx, const char* name) {
  JSValue symbol_ctor, ret;
  symbol_ctor = js_symbol_ctor(ctx);
  ret = JS_GetPropertyStr(ctx, symbol_ctor, name);

  JS_FreeValue(ctx, symbol_ctor);
  return ret;
}

static inline JSAtom
js_symbol_atom(JSContext* ctx, const char* name) {
  JSValue sym = js_symbol_get_static(ctx, name);
  JSAtom ret = JS_ValueToAtom(ctx, sym);
  JS_FreeValue(ctx, sym);
  return ret;
}

static inline JSValue
js_iterator_method(JSContext* ctx, JSValueConst obj) {
  JSAtom atom;
  JSValue ret = JS_UNDEFINED;
  atom = js_symbol_atom(ctx, "iterator");
  if(JS_HasProperty(ctx, obj, atom))
    ret = JS_GetProperty(ctx, obj, atom);
  JS_FreeAtom(ctx, atom);
  if(!JS_IsFunction(ctx, ret)) {
    atom = js_symbol_atom(ctx, "asyncIterator");
    if(JS_HasProperty(ctx, obj, atom))
      ret = JS_GetProperty(ctx, obj, atom);
    JS_FreeAtom(ctx, atom);
  }
  return ret;
}

static inline JSValue
js_iterator_new(JSContext* ctx, JSValueConst obj) {
  JSValue fn, ret;
  fn = js_iterator_method(ctx, obj);

  ret = JS_Call(ctx, fn, obj, 0, 0);
  JS_FreeValue(ctx, fn);
  return ret;
}

static inline IteratorValue
js_iterator_next(JSContext* ctx, JSValueConst obj) {
  JSValue fn, result, done;
  IteratorValue ret;

  fn = JS_GetPropertyStr(ctx, obj, "next");

  result = JS_Call(ctx, fn, obj, 0, 0);
  JS_FreeValue(ctx, fn);

  done = JS_GetPropertyStr(ctx, result, "done");
  ret.value = JS_GetPropertyStr(ctx, result, "value");
  JS_FreeValue(ctx, result);

  ret.done = JS_ToBool(ctx, done);
  JS_FreeValue(ctx, done);

  return ret;
}

static inline int64_t
js_int64_default(JSContext* ctx, JSValueConst value, int64_t i) {
  if(!JS_IsUndefined(value))
    JS_ToInt64(ctx, &i, value);
  return i;
}

static inline JSValue
js_new_number(JSContext* ctx, int32_t n) {
  if(n == INT32_MAX)
    return JS_NewFloat64(ctx, INFINITY);
  return JS_NewInt32(ctx, n);
}

static inline JSValue
js_new_bool_or_number(JSContext* ctx, int32_t n) {
  if(n == 0)
    return JS_NewBool(ctx, FALSE);
  return js_new_number(ctx, n);
}

#define JS_ATOM_TAG_INT (1U << 31)
#define JS_ATOM_MAX_INT (JS_ATOM_TAG_INT - 1)

#define js_atom_isint(i) ((JSAtom)((i)&JS_ATOM_TAG_INT))
#define js_atom_fromint(i) ((JSAtom)((i)&JS_ATOM_MAX_INT) | JS_ATOM_TAG_INT)
#define js_atom_toint(i) (unsigned int)(((JSAtom)(i) & (~(JS_ATOM_TAG_INT))))

#define js_atom_dup(ctx, atom) (js_atom_isint(atom) ? (atom) : JS_DupAtom((ctx), (atom)))
#define js_atom_free(ctx, atom)                                                                                        \
  do {                                                                                                                 \
    if((atom) > 0)                                                                                                     \
      JS_FreeAtom((ctx), (atom));                                                                                      \
  } while(0)

static inline int
js_atom_toint64(JSContext* ctx, int64_t* i, JSAtom atom) {
  int ret;
  JSValue value;
  *i = INT64_MAX;
  value = JS_AtomToValue(ctx, atom);
  ret = !JS_ToInt64(ctx, i, value);
  JS_FreeValue(ctx, value);

  return ret;
}

static inline int32_t
js_atom_toint32(JSContext* ctx, JSAtom atom) {
  if(!js_atom_isint(atom)) {
    int64_t i = INT64_MIN;
    js_atom_toint64(ctx, &i, atom);
    return i;
  }

  return -atom;
}

static inline JSValue
js_atom_tovalue(JSContext* ctx, JSAtom atom) {
  if(js_atom_isint(atom))
    return JS_MKVAL(JS_TAG_INT, -atom);

  return JS_AtomToValue(ctx, atom);
}

static inline JSAtom
js_atom_fromvalue(JSContext* ctx, JSValueConst value) {
  if(JS_VALUE_GET_TAG(value) == JS_TAG_INT) {
    if(JS_VALUE_GET_INT(value) <= JS_ATOM_MAX_INT && JS_VALUE_GET_INT(value) >= 0)
      return (unsigned)JS_VALUE_GET_INT(value) | JS_ATOM_TAG_INT;
  }
  return JS_ValueToAtom(ctx, value);
}

static inline JSAtom
js_atom_fromuint32(JSContext* ctx, uint32_t i) {
  if(i > JS_ATOM_MAX_INT)
    return JS_NewAtomUInt32(ctx, i);

  return js_atom_fromint(i);
}

static inline unsigned int
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

static void
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

static inline const char*
js_object_tostring(JSContext* ctx, JSValueConst value) {
  JSAtom atom;
  JSValue proto, tostring, str;
  const char* s;
  proto = js_global_prototype(ctx, "Object");
  atom = JS_NewAtom(ctx, "toString");
  tostring = JS_GetProperty(ctx, proto, atom);
  JS_FreeValue(ctx, proto);
  js_atom_free(ctx, atom);
  str = JS_Call(ctx, tostring, value, 0, 0);
  JS_FreeValue(ctx, tostring);
  s = JS_ToCString(ctx, str);
  JS_FreeValue(ctx, str);
  return s;
}

static inline BOOL
js_object_propertystr_bool(JSContext* ctx, JSValueConst obj, const char* str) {
  BOOL ret = FALSE;
  JSValue value;
  value = JS_GetPropertyStr(ctx, obj, str);

  if(!JS_IsException(value))
    ret = JS_ToBool(ctx, value);
  JS_FreeValue(ctx, value);
  return ret;
}

static inline void
js_object_propertystr_setstr(JSContext* ctx, JSValueConst obj, const char* prop, const char* str, size_t len) {
  JSValue value;
  value = JS_NewStringLen(ctx, str, len);
  JS_SetPropertyStr(ctx, obj, prop, value);
}

static inline const char*
js_object_propertystr_getstr(JSContext* ctx, JSValueConst obj, const char* prop) {
  JSValue value;
  const char* ret;
  value = JS_GetPropertyStr(ctx, obj, prop);
  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;
  ret = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);
  return ret;
}

static int
js_is_arraybuffer(JSContext* ctx, JSValueConst value) {
  int ret = 0;
  int n, m;
  void* obj = JS_VALUE_GET_OBJ(value);
  char* name = 0;
  if((name = js_class_name(ctx, value))) {
    n = strlen(name);
    m = n >= 11 ? n - 11 : 0;
    if(!strcmp(name + m, "ArrayBuffer"))
      ret = 1;
  }
  if(!ret) {
    const char* str;
    JSValue ctor = js_global_get(ctx, "ArrayBuffer");
    if(JS_IsInstanceOf(ctx, value, ctor))
      ret = 1;
    else if(!JS_IsArray(ctx, value) && (str = js_object_tostring(ctx, value))) {
      ret = strstr(str, "ArrayBuffer]") != 0;
      JS_FreeCString(ctx, str);
    }
    JS_FreeValue(ctx, ctor);
  }
  if(name)
    js_free(ctx, (void*)name);
  return ret;
}

static int
js_is_typedarray(JSContext* ctx, JSValueConst value) {
  int ret;
  JSValue buf;
  size_t byte_offset, byte_length, bytes_per_element;

  buf = JS_GetTypedArrayBuffer(ctx, value, &byte_offset, &byte_length, &bytes_per_element);
  ret = js_is_arraybuffer(ctx, buf);
  JS_FreeValue(ctx, buf);
  return ret;
}

static inline int64_t
js_array_length(JSContext* ctx, JSValueConst array) {
  int64_t len = -1;
  if(JS_IsArray(ctx, array) || js_is_typedarray(ctx, array)) {
    JSValue length = JS_GetPropertyStr(ctx, array, "length");
    JS_ToInt64(ctx, &len, length);
    JS_FreeValue(ctx, length);
  }
  return len;
}

#endif /* defined(QJS_MODULES_UTILS_H) */
