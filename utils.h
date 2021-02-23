#ifndef QJS_MODULES_UTILS_H
#define QJS_MODULES_UTILS_H

#include <string.h>
#include <math.h>

#define is_control_char(c) ((c) == 8 || (c) == '\f' || (c) == '\n' || (c) == '\r' || (c) == '\t' || (c) == 11)
#define is_alphanumeric_char(c) ((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z')
#define is_digit_char(c) ((c) >= '0' && (c) <= '9')
#define is_newline_char(c) ((c) == '\n')
#define is_identifier_char(c) (is_alphanumeric_char(c) || is_digit_char(c) || (c) == '$' || (c) == '_')

static inline size_t
byte_chr(const char* str, size_t len, char c) {
  const char *s = str, *t = s + len;
  for(; s < t; ++s)
    if(*s == c)
      break;
  return s - str;
}

static inline int
is_escape_char(char c) {
  return is_control_char(c) || c == 0x5c || c == 0x27;
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

char*
strndup(const char* s, size_t n) {
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
    case '\t': return 't';
    case '\r': return 'r';
    case '\n': return 'n';
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
    dbuf_putc(db, escape_char_letter(str[i]));
    i++;
  }
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

typedef struct {
  BOOL done;
  JSValue value;
} IteratorValue;

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
js_property_names_free(JSContext* ctx, JSPropertyEnum* props, size_t len) {
  uint32_t i;
  for(i = 0; i < len; i++) JS_FreeAtom(ctx, props[i].atom);
  js_free(ctx, props);
}

static inline void
js_property_descriptor_free(JSContext* ctx, JSPropertyDescriptor* desc) {
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
    dbuf_put(db, str, len);
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
js_symbol_get_static(JSContext* ctx, const char* name) {
  JSValue global_obj, symbol_ctor, ret;
  global_obj = JS_GetGlobalObject(ctx);
  symbol_ctor = JS_GetPropertyStr(ctx, global_obj, "Symbol");
  ret = JS_GetPropertyStr(ctx, symbol_ctor, name);

  JS_FreeValue(ctx, symbol_ctor);
  JS_FreeValue(ctx, global_obj);
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



#endif