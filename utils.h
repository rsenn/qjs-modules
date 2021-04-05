#ifndef UTILS_H
#define UTILS_H

#define _GNU_SOURCE

#include "quickjs.h"
#include "cutils.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define JS_CGETSET_ENUMERABLE_DEF(prop_name, fgetter, fsetter, magic_num)                                              \
  {                                                                                                                    \
    .name = prop_name, .prop_flags = JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE, .def_type = JS_DEF_CGETSET_MAGIC,      \
    .magic = magic_num, .u = {                                                                                         \
      .getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}}                                   \
    }                                                                                                                  \
  }

#if defined(_WIN32) || defined(__MINGW32__)
#define VISIBLE __declspec(dllexport)
#define HIDDEN
#else
#define VISIBLE __attribute__((visibility("default")))
#define HIDDEN __attribute__((visibility("hidden")))
#endif

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

static inline int
str_endb(const char* a, const char* x, size_t n) {
  size_t alen = strlen(a);
  a += alen - n;
  return alen >= n && !memcmp(a, x, n);
}

/* str_end returns 1 if the b is a suffix of a, 0 otherwise */
static inline int
str_end(const char* a, const char* b) {
  return str_endb(a, b, strlen(b));
}

#define str_contains(s, needle) (!!strchr((s), (needle)))

#define COLOR_RED "\x1b[31m"
#define COLOR_GREEN "\x1b[32m"
#define COLOR_YELLOW "\x1b[33m"
#define COLOR_MARINE "\x1b[36m"
#define COLOR_GRAY "\x1b[1;30m"
#define COLOR_NONE "\x1b[m"

size_t ansi_skip(const char* str, size_t len);
size_t ansi_length(const char* str, size_t len);
size_t ansi_truncate(const char* str, size_t len, size_t limit);

/*static inline char*
str_ndup(const char* s, size_t n) {
  char* r = malloc(n + 1);
  if(r == NULL)
    return NULL;
  memcpy(r, s, n);
  r[n] = '\0';
  return r;
}*/

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

size_t token_length(const char* str, size_t len, char delim);
int64_t array_search(void* a, size_t m, size_t elsz, void* needle);
#define array_contains(a, m, elsz, needle) (array_search((a), (m), (elsz), (needle)) != -1)
#define dbuf_append(d, x, n) dbuf_put((d), (const uint8_t*)(x), (n))
void dbuf_put_escaped_pred(DynBuf* db, const char* str, size_t len, int (*pred)(int));

static inline void
js_dbuf_init_rt(JSRuntime* rt, DynBuf* s) {
  dbuf_init2(s, rt, (DynBufReallocFunc*)js_realloc_rt);
}

static inline void
js_dbuf_init(JSContext* ctx, DynBuf* s) {
  js_dbuf_init_rt(JS_GetRuntime(ctx), s);
}

static inline void
dbuf_put_escaped(DynBuf* db, const char* str, size_t len) {
  return dbuf_put_escaped_pred(db, str, len, is_escape_char);
}

void dbuf_put_value(DynBuf* db, JSContext* ctx, JSValueConst value);
size_t dbuf_token_push(DynBuf* db, const char* str, size_t len, char delim);
size_t dbuf_token_pop(DynBuf* db, char delim);

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

char* dbuf_at_n(const DynBuf* db, size_t i, size_t* n, char sep);
const char* dbuf_last_line(DynBuf* db, size_t* len);
int32_t dbuf_get_column(DynBuf* db);
void dbuf_put_colorstr(DynBuf* db, const char* str, const char* color, int with_color);
int dbuf_reserve_start(DynBuf* s, size_t len);
int dbuf_prepend(DynBuf* s, const uint8_t* data, size_t len);
JSValue dbuf_tostring_free(DynBuf* s, JSContext* ctx);

static inline void
dbuf_bitflags(DynBuf* db, uint32_t bits, const char* const names[]) {
  size_t i, n = 0;
  for(i = 0; i < sizeof(bits) * 8; i++) {
    if(bits & (1 << i)) {
      size_t len = strlen(names[i]);
      if(n) {
        n++;
        dbuf_putstr(db, "|");
      }
      dbuf_put(db, names[i], len);
      n += len;
    }
  }
  return n;
}

JSValue js_global_get(JSContext* ctx, const char* prop);
JSValue js_global_prototype(JSContext* ctx, const char* class_name);

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
  FLAG_FLOAT64,     // 10
  FLAG_FUNCTION,    // 11
  FLAG_ARRAY        // 12
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
  TYPE_PRIMITIVE = (TYPE_UNDEFINED | TYPE_NULL | TYPE_BOOL | TYPE_INT | TYPE_STRING | TYPE_SYMBOL | TYPE_BIG_FLOAT |
                    TYPE_BIG_INT | TYPE_BIG_DECIMAL),
  TYPE_ALL = (TYPE_PRIMITIVE | TYPE_OBJECT),
  TYPE_FUNCTION = (1 << FLAG_FUNCTION),
  TYPE_ARRAY = (1 << FLAG_ARRAY),
};

int32_t js_value_type_flag(JSValueConst value);
static inline int32_t
js_value_type_get(JSContext* ctx, JSValueConst value) {
  if(JS_IsArray(ctx, value))
    return FLAG_ARRAY;
  if(JS_IsFunction(ctx, value))
    return FLAG_FUNCTION;
  return js_value_type_flag(value);
}

static inline int32_t
js_value_type2flag(uint32_t type) {
  int32_t flag;
  for(flag = 0; (type >>= 1); flag++) {}
  return flag;
}

static inline int32_t
js_value_type(JSContext* ctx, JSValueConst value) {
  int32_t flag, type = 0;
  if((flag = js_value_type_get(ctx, value)) != -1)
    type = 1 << flag;
  return type;
}

static inline const char* const*
js_value_types() {
  return (const char* const[]){"UNDEFINED",
                               "NULL",
                               "BOOL",
                               "INT",
                               "OBJECT",
                               "STRING",
                               "SYMBOL",
                               "BIG_FLOAT",
                               "BIG_INT",
                               "BIG_DECIMAL",
                               "FLOAT64",
                               "FUNCTION",
                               "ARRAY",
                               0};
}

static inline const char*
js_value_type_name(int32_t type) {
  return js_value_types()[js_value_type2flag(type)];
}

static inline const char*
js_value_typestr(JSContext* ctx, JSValueConst value) {
  int32_t type = js_value_type(ctx, value);
  return js_value_type_name(type);
}

BOOL js_value_equals(JSContext* ctx, JSValueConst a, JSValueConst b);
void js_value_dump(JSContext* ctx, JSValueConst value, DynBuf* db);
void js_value_print(JSContext* ctx, JSValueConst value, DynBuf* db);
JSValue js_value_clone(JSContext* ctx, JSValueConst valpe);
JSValue* js_values_dup(JSContext* ctx, int nvalues, JSValueConst* values);
// void js_values_free(JSContext* ctx, int nvalues, JSValueConst* values);
void js_values_free(JSRuntime* rt, int nvalues, JSValueConst* values);
JSValue js_values_toarray(JSContext* ctx, int nvalues, JSValueConst* values);

typedef struct InputBuffer {
  const uint8_t* data;
  size_t size;
  size_t pos;
  void (*free)(JSContext*, const char*);
} InputBuffer;

static inline void input_buffer_free_default(JSContext* ctx, const char* str){

};

void input_buffer_dump(const InputBuffer* in, DynBuf* db);
void input_buffer_free(InputBuffer* in, JSContext* ctx);
InputBuffer js_input_buffer(JSContext* ctx, JSValueConst value);
uint8_t* js_input_buffer_get(InputBuffer* in, size_t* lenp);
uint32_t js_input_buffer_getc(InputBuffer* in, size_t* lenp);
uint8_t* js_input_buffer_peek(InputBuffer* in, size_t* lenp);
uint32_t js_input_buffer_peekc(InputBuffer*, size_t* lenp);

static inline BOOL
js_input_buffer_eof(InputBuffer* in) {
  return in->pos == in->size;
}
static inline size_t
js_input_buffer_remain(InputBuffer* in) {
  return in->size - in->pos;
}

static inline const char*
js_tostring(JSContext* ctx, JSValueConst value) {
  size_t len;
  const char *cstr, *ret = 0;
  if((cstr = JS_ToCStringLen(ctx, &len, value))) {
    ret = js_strndup(ctx, cstr, len);
    JS_FreeCString(ctx, cstr);
  }
  return ret;
}

JSValue js_value_tostring(JSContext* ctx, const char* class_name, JSValueConst value);
int js_value_to_size(JSContext* ctx, size_t* sz, JSValueConst value);
JSValue js_value_from_char(JSContext* ctx, int c);

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

#define js_object_tmpmark_set(value)                                                                                   \
  do { ((uint8_t*)JS_VALUE_GET_OBJ((value)))[5] |= 0x40; } while(0);
#define js_object_tmpmark_clear(value)                                                                                 \
  do { ((uint8_t*)JS_VALUE_GET_OBJ((value)))[5] &= ~0x40; } while(0);
#define js_object_tmpmark_isset(value) (((uint8_t*)JS_VALUE_GET_OBJ((value)))[5] & 0x40)

#define js_runtime_exception_set(rt, value)                                                                            \
  do { *(JSValue*)((uint8_t*)(rt) + 216) = value; } while(0);
#define js_runtime_exception_get(rt) (*(JSValue*)((uint8_t*)(rt) + 216))
#define js_runtime_exception_clear(rt)                                                                                 \
  do {                                                                                                                 \
    if(!JS_IsNull(js_runtime_exception_get(rt)))                                                                       \
      JS_FreeValueRT((rt), js_runtime_exception_get(rt));                                                              \
    js_runtime_exception_set(rt, JS_NULL);                                                                             \
  } while(0)

void js_propertyenums_free(JSContext* ctx, JSPropertyEnum* props, size_t len);
void js_propertydescriptor_free(JSContext* ctx, JSPropertyDescriptor* desc);

static inline JSValue
js_symbol_ctor(JSContext* ctx) {
  return js_global_get(ctx, "Symbol");
}

JSValue js_symbol_get_static(JSContext* ctx, const char* name);
JSAtom js_symbol_atom(JSContext* ctx, const char* name);
BOOL js_is_iterable(JSContext* ctx, JSValueConst obj);
JSValue js_iterator_method(JSContext* ctx, JSValueConst obj);
JSValue js_iterator_new(JSContext* ctx, JSValueConst obj);
IteratorValue js_iterator_next(JSContext* ctx, JSValueConst obj);

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

int js_atom_toint64(JSContext* ctx, int64_t* i, JSAtom atom);
int32_t js_atom_toint32(JSContext* ctx, JSAtom atom);
JSValue js_atom_tovalue(JSContext* ctx, JSAtom atom);

unsigned int js_atom_tobinary(JSAtom atom);
const char* js_atom_tocstringlen(JSContext* ctx, size_t* len, JSAtom atom);
void js_atom_dump(JSContext* ctx, JSAtom atom, DynBuf* db, BOOL color);
const char* js_object_tostring(JSContext* ctx, JSValueConst value);
const char* js_function_name(JSContext* ctx, JSValueConst value);
BOOL js_object_propertystr_bool(JSContext* ctx, JSValueConst obj, const char* str);
void js_object_propertystr_setstr(JSContext* ctx, JSValueConst obj, const char* prop, const char* str, size_t len);
const char* js_object_propertystr_getstr(JSContext* ctx, JSValueConst obj, const char* prop);
int32_t js_object_propertystr_getint32(JSContext* ctx, JSValueConst obj, const char* prop);
char* js_object_classname(JSContext* ctx, JSValueConst value);
int js_object_is(JSContext* ctx, JSValueConst value, const char* cmp);

static inline int
js_is_map(JSContext* ctx, JSValueConst value) {
  return js_object_is(ctx, value, "[object Map]");
}

static inline int
js_is_set(JSContext* ctx, JSValueConst value) {
  return js_object_is(ctx, value, "[object Set]");
}

static inline int
js_is_generator(JSContext* ctx, JSValueConst value) {
  return js_object_is(ctx, value, "[object Generator]");
}

static inline int
js_is_regexp(JSContext* ctx, JSValueConst value) {
  return js_object_is(ctx, value, "[object RegExp]");
}

int js_is_arraybuffer(JSContext* ctx, JSValueConst value);
int js_is_typedarray(JSContext* ctx, JSValueConst value);
int js_propenum_cmp(const void* a, const void* b, void* ptr);
BOOL js_object_equals(JSContext* ctx, JSValueConst a, JSValueConst b);
int64_t js_array_length(JSContext* ctx, JSValueConst array);
void js_strvec_free(JSContext* ctx, char** strv);
JSValue js_strvec_to_array(JSContext* ctx, char** strv);
char** js_array_to_strvec(JSContext* ctx, JSValueConst array);

#endif /* defined(UTILS_H) */
