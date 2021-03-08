#ifndef UTILS_H
#define UTILS_H

#define _GNU_SOURCE

#include "quickjs.h"
#include "cutils.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define max_num(a, b) ((a) > (b) ? (a) : (b))

#define is_control_char(c)                                                                                 \
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
is_escape_char(int c) {
  return is_control_char(c) || c == '\\' || c == '\'' || c == 0x1b || c == 0;
}

static inline int
is_dot_char(int c) {
  return c == '.';
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

static inline int32_t
mod_int32(int32_t a, int32_t b) {
  int32_t c = a % b;
  return (c < 0) ? c + b : c;
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
  const char *s, *t;
  for(s = str, t = s + len; s < t; ++s)
    if(*s == c)
      break;
  return s - str;
}

static inline size_t
byte_rchr(const void* str, size_t len, char c) {
  const char *s, *t;
  for(s = (const char*)str, t = s + len; --t >= s;)
    if(*t == c)
      return (size_t)(t - s);
  return len;
}

static inline size_t
byte_chrs(const char* str, size_t len, char needle[], size_t nl) {
  const char *s, *t;
  for(s = str, t = str + len; s != t; s++)
    if(byte_chr(needle, nl, *s) < nl)
      break;
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
predicate_find(const char* str, size_t len, int (*pred)(int32_t)) {
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

static inline size_t
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

static inline int64_t
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

#define array_contains(a, m, elsz, needle) (array_search((a), (m), (elsz), (needle)) != -1)

#define dbuf_append(d, x, n) dbuf_put((d), (const uint8_t*)(x), (n))

static inline void
dbuf_put_escaped_pred(DynBuf* db, const char* str, size_t len, int (*pred)(int)) {
  size_t i = 0, j;
  while(i < len) {
    if((j = predicate_find(&str[i], len - i, pred))) {
      dbuf_append(db, (const uint8_t*)&str[i], j);
      i += j;
    }
    if(i == len)
      break;
    dbuf_putc(db, '\\');

    if(str[i] == 0x1b)
      dbuf_append(db, (const uint8_t*)"x1b", 3);
    else
      dbuf_putc(db, escape_char_letter(str[i]));
    i++;
  }
}

static inline void
dbuf_put_escaped(DynBuf* db, const char* str, size_t len) {
  return dbuf_put_escaped_pred(db, str, len, is_escape_char);
}

static inline size_t
dbuf_token_push(DynBuf* db, const char* str, size_t len, char delim) {
  size_t pos;
  if(db->size)
    dbuf_putc(db, delim);
  pos = db->size;
  dbuf_put_escaped_pred(db, str, len, is_dot_char);
  return db->size - pos;
}

static inline size_t
dbuf_token_pop(DynBuf* db, char delim) {
  const char* x;
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
      *n = byte_chr((const char*)&db->buf[p], db->size - p, sep);
      return (char*)&db->buf[p];
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

static int
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

static inline int
dbuf_prepend(DynBuf* s, const uint8_t* data, size_t len) {
  int ret;
  if(!(ret = dbuf_reserve_start(s, len)))
    memcpy(s->buf, data, len);
  return 0;
}

enum value_types {
  FLAG_UNDEFINED = 0,
  FLAG_NULL,        // 1
  FLAG_BOOL,        // 2
  FLAG_INT,         // 3
  FLAG_OBJECT,      // 4
  FLAG_STRING,      // 5
  FLAG_SYMBOL,      // 6
  FLAG_BIG_FLOAT,   // 7
  FLAG_BIG_INT,     // 8
  FLAG_BIG_DECIMAL, // 9
  FLAG_FLOAT64,
  FLAG_FUNCTION = 16,
  FLAG_ARRAY = 17
};

enum value_mask {
  TYPE_UNDEFINED = (1 << FLAG_UNDEFINED),
  TYPE_NULL = (1 << FLAG_NULL),
  TYPE_BOOL = (1 << FLAG_BOOL),
  TYPE_INT = (1 << FLAG_INT),
  TYPE_OBJECT = (1 << FLAG_OBJECT),
  TYPE_STRING = (1 << FLAG_STRING),
  TYPE_SYMBOL = (1 << FLAG_SYMBOL),
  TYPE_BIG_FLOAT = (1 << FLAG_BIG_FLOAT),
  TYPE_BIG_INT = (1 << FLAG_BIG_INT),
  TYPE_BIG_DECIMAL = (1 << FLAG_BIG_DECIMAL),
  TYPE_FLOAT64 = (1 << FLAG_FLOAT64),
  TYPE_NUMBER = (TYPE_INT | TYPE_BIG_FLOAT | TYPE_BIG_INT | TYPE_BIG_DECIMAL | TYPE_FLOAT64),
  TYPE_PRIMITIVE = (TYPE_UNDEFINED | TYPE_NULL | TYPE_BOOL | TYPE_INT | TYPE_STRING | TYPE_SYMBOL |
                    TYPE_BIG_FLOAT | TYPE_BIG_INT | TYPE_BIG_DECIMAL),
  TYPE_ALL = (TYPE_PRIMITIVE | TYPE_OBJECT),
  TYPE_FUNCTION = (1 << FLAG_FUNCTION),
  TYPE_ARRAY = (1 << FLAG_ARRAY),
};

static inline int32_t
js_value_type_flag(JSValueConst value) {
  switch(JS_VALUE_GET_TAG(value)) {
    case JS_TAG_UNDEFINED: return FLAG_UNDEFINED;
    case JS_TAG_NULL: return FLAG_NULL;
    case JS_TAG_BOOL: return FLAG_BOOL;
    case JS_TAG_INT: return FLAG_INT;
    case JS_TAG_OBJECT: return FLAG_OBJECT;
    case JS_TAG_STRING: return FLAG_STRING;
    case JS_TAG_SYMBOL: return FLAG_SYMBOL;
    case JS_TAG_BIG_FLOAT: return FLAG_BIG_FLOAT;
    case JS_TAG_BIG_INT: return FLAG_BIG_INT;
    case JS_TAG_BIG_DECIMAL: return FLAG_BIG_DECIMAL;
    case JS_TAG_FLOAT64: return FLAG_FLOAT64;
  }
  return -1;
}

static inline int32_t
js_value_type(JSValueConst value) {
  int32_t flag, type = 0;

  if((flag = js_value_type_flag(value)) != -1)
    type = 1 << flag;

  return type;
}

static inline BOOL
js_value_equals(JSContext* ctx, JSValueConst a, JSValueConst b) {
  int32_t ta, tb;
  BOOL ret = FALSE;

  ta = js_value_type(a);
  tb = js_value_type(a);

  if(ta != tb)
    return FALSE;

  if(ta & TYPE_INT) {
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
    void *obja, *objb;

    obja = JS_VALUE_GET_OBJ(a);
    objb = JS_VALUE_GET_OBJ(b);

    ret = obja == objb;
  } else if(ta & TYPE_STRING) {
    const char *stra, *strb;

    stra = JS_ToCString(ctx, a);
    strb = JS_ToCString(ctx, b);

    ret = !strcmp(stra, strb);

    JS_FreeCString(ctx, stra);
    JS_FreeCString(ctx, strb);
  }

  return ret;
}

static inline void
js_value_dump(JSContext* ctx, JSValue value, DynBuf* db) {
  const char* str;
  size_t len;

  if(JS_IsArray(ctx, value)) {
    dbuf_putstr(db, "[object Array]");
  } else {
    str = JS_ToCStringLen(ctx, &len, value);
    dbuf_append(db, (const uint8_t*)str, len);
    JS_FreeCString(ctx, str);
  }
}

static inline JSValue
js_value_clone(JSContext* ctx, JSValueConst value) {

  int32_t type = js_value_type(value);
  JSValue ret = JS_UNDEFINED;
  switch(type) {

      /* case TYPE_NULL: {
         ret = JS_NULL;
         break;
       }
       case TYPE_UNDEFINED: {
         ret = JS_UNDEFINED;
         break;
       }
       case TYPE_STRING: {
         size_t len;
         const char* str;
         str = JS_ToCStringLen(ctx, &len, value);
         ret = JS_NewStringLen(ctx, str, len);
         JS_FreeCString(ctx, str);
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
    case TYPE_OBJECT: {
      JSPropertyEnum* tab_atom;
      uint32_t tab_atom_len;
      ret = JS_IsArray(ctx, value) ? JS_NewArray(ctx) : JS_NewObject(ctx);
      if(!JS_GetOwnPropertyNames(ctx,
                                 &tab_atom,
                                 &tab_atom_len,
                                 value,
                                 JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY)) {
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
    case TYPE_SYMBOL:
    case TYPE_STRING:
    case TYPE_BIG_DECIMAL:
    case TYPE_BIG_INT:
    case TYPE_BIG_FLOAT: {
      ret = JS_DupValue(ctx, value);
      break;
    }
    default: {
      ret = JS_ThrowTypeError(ctx, "No such type: %08x\n", type);
      break;
    }
  }
  return ret;
}

typedef struct {
  const uint8_t* x;
  size_t n;
  size_t p;
  void (*free)(JSContext*, const char*);
} InputValue;

static void input_value_free_default(JSContext* ctx, const char* str){

};

static inline InputValue
js_value_to_bytes(JSContext* ctx, JSValueConst value) {
  InputValue ret = {0, 0, 0, &input_value_free_default};

  if(JS_IsString(value)) {
    ret.x = (const uint8_t*)JS_ToCStringLen(ctx, &ret.n, value);
    ret.free = JS_FreeCString;
  } else {
    ret.x = JS_GetArrayBuffer(ctx, &ret.n, value);
  }
  return ret;
}

#define js_value_free(ctx, value)                                                                          \
  do {                                                                                                     \
    JS_FreeValue((ctx), (value));                                                                          \
    (value) = JS_UNDEFINED;                                                                                \
  } while(0);
#define js_value_free_rt(ctx, value)                                                                       \
  do {                                                                                                     \
    JS_FreeValueRT((ctx), (value));                                                                        \
    (value) = JS_UNDEFINED;                                                                                \
  } while(0);

#define js_object_tmpmark_set(value)                                                                       \
  do { ((uint8_t*)JS_VALUE_GET_OBJ((value)))[5] |= 0x40; } while(0);
#define js_object_tmpmark_clear(value)                                                                     \
  do { ((uint8_t*)JS_VALUE_GET_OBJ((value)))[5] &= ~0x40; } while(0);
#define js_object_tmpmark_isset(value) (((uint8_t*)JS_VALUE_GET_OBJ((value)))[5] & 0x40)

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
/*
#define js_atom_dup(ctx, atom) (js_atom_isint(atom) ? (atom) : JS_DupAtom((ctx), (atom)))
#define js_atom_free(ctx, atom)                                                                            \
  do {                                                                                                     \
    if((atom) > 0)                                                                                         \
      JS_FreeAtom((ctx), (atom));                                                                          \
  } while(0)
*/
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

/*static inline JSAtom
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

  return JS_NewAtomUInt32(i);
}*/

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

static inline const char*
js_atom_tocstringlen(JSContext* ctx, size_t* len, JSAtom atom) {
  JSValue v;
  const char* s;
  v = JS_AtomToValue(ctx, atom);
  s = JS_ToCStringLen(ctx, len, v);
  JS_FreeValue(ctx, v);
  return s;
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
  JS_FreeAtom(ctx, atom);
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
js_object_propertystr_setstr(
    JSContext* ctx, JSValueConst obj, const char* prop, const char* str, size_t len) {
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

static inline char*
js_object_classname(JSContext* ctx, JSValueConst value) {
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

static int
js_object_is(JSContext* ctx, JSValueConst value, const char* cmp) {
  int ret;
  const char* str;
  str = js_object_tostring(ctx, value);
  ret = strcmp(str, cmp) == 0;
  JS_FreeCString(ctx, str);
  return ret;
}

static int
js_object_is_map(JSContext* ctx, JSValueConst value) {
  return js_object_is(ctx, value, "[object Map]");
}

static int
js_object_is_set(JSContext* ctx, JSValueConst value) {
  return js_object_is(ctx, value, "[object Set]");
}

static int
js_object_is_generator(JSContext* ctx, JSValueConst value) {
  return js_object_is(ctx, value, "[object Generator]");
}

static int
js_object_is_arraybuffer(JSContext* ctx, JSValueConst value) {
  int ret = 0;
  int n, m;
  void* obj = JS_VALUE_GET_OBJ(value);
  char* name = 0;
  if((name = js_object_classname(ctx, value))) {
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
js_object_is_typedarray(JSContext* ctx, JSValueConst value) {
  int ret;
  JSValue buf;
  size_t byte_offset, byte_length, bytes_per_element;

  buf = JS_GetTypedArrayBuffer(ctx, value, &byte_offset, &byte_length, &bytes_per_element);
  ret = js_object_is_arraybuffer(ctx, buf);
  JS_FreeValue(ctx, buf);
  return ret;
}

static inline int
js_propenum_cmp(const void* a, const void* b, void* ptr) {
  JSContext* ctx = ptr;
  const char *stra, *strb;
  int ret;
  stra = JS_AtomToCString(ctx, ((const JSPropertyEnum*)a)->atom);
  strb = JS_AtomToCString(ctx, ((const JSPropertyEnum*)b)->atom);
  ret = strverscmp(stra, strb);
  JS_FreeCString(ctx, stra);
  JS_FreeCString(ctx, strb);
  return ret;
}

static BOOL
js_object_equals(JSContext* ctx, JSValueConst a, JSValueConst b) {
  BOOL ret = FALSE;

  JSPropertyEnum *atoms_a, *atoms_b;
  uint32_t i, natoms_a, natoms_b;
  int32_t ta, tb;

  ta = js_value_type(a);
  tb = js_value_type(b);
  assert(ta == TYPE_OBJECT);
  assert(tb == TYPE_OBJECT);

  if(JS_GetOwnPropertyNames(
         ctx, &atoms_a, &natoms_a, a, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY))
    return FALSE;
  if(JS_GetOwnPropertyNames(
         ctx, &atoms_b, &natoms_b, b, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY))
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

static inline int64_t
js_array_length(JSContext* ctx, JSValueConst array) {
  int64_t len = -1;
  if(JS_IsArray(ctx, array) || js_object_is_typedarray(ctx, array)) {
    JSValue length = JS_GetPropertyStr(ctx, array, "length");
    JS_ToInt64(ctx, &len, length);
    JS_FreeValue(ctx, length);
  }
  return len;
}

#endif /* defined(UTILS_H) */
