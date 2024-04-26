#ifndef UTILS_H
#define UTILS_H

#include <quickjs.h>
#include <list.h>
#include <cutils.h>
#include <string.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
/*#ifdef HAVE_THREADS_H
#include <threads.h>
#endif*/
#include "defines.h"

#if defined(__EMSCRIPTEN__) && defined(__GNUC__)
#define atomic_add_int __sync_add_and_fetch
#else
#include <stdatomic.h>

static inline int
atomic_add_int(int* ptr, int v) {
  return atomic_fetch_add((_Atomic(uint32_t)*)ptr, v) + v;
}
#endif

/**
 * \defgroup utils utils: Utilities
 * @{
 */

#define JS_IsModule(value) (JS_VALUE_GET_TAG((value)) == JS_TAG_MODULE)

#define MAX_SAFE_INTEGER 9007199254740991

char* basename(const char*);

typedef enum endian { LIL = 0, BIG = 1 } Endian;

typedef enum precedence {
  PRECEDENCE_COMMA_SEQUENCE = 1,
  PRECEDENCE_YIELD,
  PRECEDENCE_ASSIGNMENT,
  PRECEDENCE_TERNARY,
  PRECEDENCE_NULLISH_COALESCING,
  PRECEDENCE_LOGICAL_OR,
  PRECEDENCE_LOGICAL_AND,
  PRECEDENCE_BITWISE_OR,
  PRECEDENCE_BITWISE_XOR,
  PRECEDENCE_BITWISE_AND,
  PRECEDENCE_EQUALITY,
  PRECEDENCE_LESS_GREATER_IN,
  PRECEDENCE_BITWISE_SHIFT,
  PRECEDENCE_ADDITIVE,
  PRECEDENCE_MULTIPLICATIVE,
  PRECEDENCE_EXPONENTIATION,
  PRECEDENCE_UNARY,
  PRECEDENCE_POSTFIX,
  PRECEDENCE_NEW,
  PRECEDENCE_MEMBER_ACCESS,
  PRECEDENCE_GROUPING,
} JSPrecedence;

typedef struct {
  BOOL done;
  JSValue value;
} IteratorValue;

typedef struct {
  uint16_t p, c, a;
  const char** v;
} Arguments;

typedef void* realloc_func(void*, void*, size_t);
typedef BOOL JSValueCompareFunc(JSContext*, JSValueConst, JSValueConst);

void* utils_js_realloc(JSContext* ctx, void* ptr, size_t size);
void* utils_js_realloc_rt(JSRuntime* rt, void* ptr, size_t size);

size_t list_size(struct list_head* list);
struct list_head* list_front(const struct list_head* list);
struct list_head* list_back(const struct list_head* list);

static inline void
list_splice(const struct list_head* list, struct list_head* head) {
  if(list->next != list) {
    struct list_head* a = list->next;
    struct list_head* b = list->prev;
    struct list_head* c = head->next;
    head->next = a;
    a->prev = head;
    b->next = c;
    c->prev = b;
  }
}

static inline Arguments
arguments_new(int argc, const char* argv[]) {
  Arguments args;
  args.p = 0;
  args.c = argc;
  args.a = 0;
  args.v = argv;
  return args;
}

static inline const char*
arguments_shift(Arguments* args) {
  const char* ret = 0;
  if(args->p < args->c) {
    ret = args->v[args->p];
    args->p++;
  }
  return ret;
}

static inline const char*
arguments_at(Arguments* args, int i) {
  return i >= 0 && i < args->c ? args->v[i] : 0;
}

static inline uint32_t
arguments_shiftn(Arguments* args, uint32_t n) {
  uint32_t i = 0;

  while(n > 0) {
    if(!arguments_shift(args))
      break;
    i++;
    n--;
  }
  return i;
}

BOOL arguments_alloc(Arguments* args, JSContext* ctx, int n);
const char* arguments_push(Arguments*, JSContext*, const char*);

void arguments_dump(Arguments const*, DynBuf*);

typedef struct {
  uint16_t p, c, a;
  JSValueConst* v;
} JSArguments;

static inline JSArguments
js_arguments_new(int argc, JSValueConst* argv) {
  JSArguments args;
  args.p = 0;
  args.c = argc;
  args.a = 0;
  args.v = argv;
  return args;
}

BOOL js_arguments_alloc(JSArguments* args, JSContext* ctx, int n);

static inline JSValueConst
js_arguments_shift(JSArguments* args) {
  JSValue ret = JS_EXCEPTION;
  if(args->p < args->c) {
    ret = args->v[args->p];
    args->p++;
  }
  return ret;
}

static inline int
js_arguments_count(const JSArguments* args) {
  return args->c - args->p;
}

static inline JSValueConst
js_arguments_at(JSArguments* args, int i) {
  i += args->p;
  return i >= 0 && i < args->c ? args->v[i] : JS_UNDEFINED;
}

static inline uint32_t
js_arguments_shiftn(JSArguments* args, uint32_t n) {
  uint32_t i = 0;

  while(n > 0) {
    if(JS_IsException(js_arguments_shift(args)))
      break;
    i++;
    n--;
  }
  return i;
}

void js_arguments_dump(JSArguments const*, JSContext*, DynBuf*);

static inline size_t
min_size(size_t a, size_t b) {
  if(a < b)
    return a;
  else
    return b;
}

static inline uint64_t
int64_abs(int64_t a) {
  return a < 0 ? -a : a;
}

static inline uint32_t
int32_abs(int32_t i) {
  return i < 0 ? -i : i;
}

/* clang-format off */
static inline void     uint16_put_be (void* x, uint16_t u) { uint8_t* y = x; y[0] = u >> 8; y[1] = u; }
static inline uint16_t uint16_get_be (const void* x) { const uint8_t* y = x; return (y[0] << 8) | y[1]; }
static inline void     uint16_read_be(const void* x, uint16_t* y) { *y = uint16_get_be(x); }
static inline void     uint16_put_le (void* x, uint16_t u) { uint8_t* y = x; y[0] = u; y[1] = u >> 8; }
static inline uint16_t uint16_get_le (const void* x) { const uint8_t* y = x; return (y[1] << 8) | y[0]; }
static inline void     uint16_read_le(const void* x, uint16_t* y) { *y = uint16_get_le(x); }
static inline void     uint16_put_endian (void* x, uint16_t u, Endian endian) { (endian == BIG ? uint16_put_be : uint16_put_le)(x, u); }
static inline uint16_t uint16_get_endian (const void* x, Endian endian) { return (endian == BIG ? uint16_get_be : uint16_get_le)(x); }
static inline void     uint16_read_endian(const void* x, uint16_t* y, Endian endian) { (endian == BIG ? uint16_read_be : uint16_read_le)(x,y); }
static inline void     uint32_put_be (void* x, uint32_t u) { uint8_t* y = x; y[0] = u >> 24; y[1] = u >> 16; y[2] = u >> 8; y[3] = u; }
static inline uint32_t uint32_get_be (const void* x) {const uint16_t* y = x; return (uint16_get_be(y) << 16) | uint16_get_be(y+1); }
static inline void     uint32_read_be(const void* x, uint32_t* y) { *y = uint32_get_be(x); }
static inline void     uint32_put_le (void* x, uint32_t u) { uint8_t* y = x;  y[3] = u >> 24; y[2] = u >> 16; y[1] = u >> 8; y[0] = u; }
static inline uint32_t uint32_get_le (const void* x) {const uint16_t* y = x; return (uint16_get_le(y+1) << 16) | uint16_get_le(y); }
static inline void     uint32_read_le(const void* x, uint32_t* y) { *y = uint32_get_le(x); }
static inline void     uint32_put_endian (void* x, uint32_t u, Endian endian) { (endian == BIG ? uint32_put_be : uint32_put_le)(x, u); }
static inline uint32_t uint32_get_endian (const void* x, Endian endian) { return (endian == BIG ? uint32_get_be : uint32_get_le)(x); }
static inline void     uint32_read_endian(const void* x, uint32_t* y, Endian endian) { (endian == BIG ? uint32_read_be : uint32_read_le)(x,y); }
/* clang-format on */

static inline int32_t
int32_sign(uint32_t i) {
  return (i & 0x80000000) ? -1 : 1;
}

static inline int32_t
int32_mod(int32_t a, int32_t b) {
  int32_t c = a % b;
  return (c < 0) ? c + b : c;
}

static inline uint32_t
uint32_mod(uint32_t a, uint32_t b) {
  return a % b;
}

static inline int64_t
int64_sign(uint64_t i) {
  return (i & 0x80000000) ? -1 : 1;
}

static inline int64_t
int64_mod(int64_t a, int64_t b) {
  int64_t c = a % b;
  return (c < 0) ? c + b : c;
}

static inline uint64_t
uint64_mod(uint64_t a, uint64_t b) {
  return a % b;
}
static inline size_t
size_mod(size_t a, size_t b) {
  return a % b;
}

static inline ssize_t
ssize_mod(ssize_t a, ssize_t b) {
  ssize_t c = a % b;
  return (c < 0) ? c + b : c;
}

uint64_t time_us(void);

typedef struct {
  char* source;
  size_t len;
  int flags;
} RegExp;

int regexp_flags_tostring(int, char*);
int regexp_flags_fromstring(const char*);
RegExp regexp_from_argv(int argc, JSValueConst argv[], JSContext* ctx);
RegExp regexp_from_string(char* str, int flags);
RegExp regexp_from_dbuf(DynBuf* dbuf, int flags);
uint8_t* regexp_compile(RegExp re, JSContext* ctx);
JSValue regexp_to_value(RegExp re, JSContext* ctx);
void regexp_free_rt(RegExp re, JSRuntime* rt);
BOOL regexp_match(const uint8_t* bc, const void* cbuf, size_t clen, JSContext* ctx);

static inline void
regexp_free(RegExp re, JSContext* ctx) {
  regexp_free_rt(re, JS_GetRuntime(ctx));
}

JSValue js_global_get_str(JSContext* ctx, const char* prop);
JSValue js_global_get_str_n(JSContext* ctx, const char* prop, size_t len);
JSValue js_global_get_atom(JSContext* ctx, JSAtom prop);

static inline JSValue
js_global_new(JSContext* ctx, const char* class_name, int argc, JSValueConst argv[]) {
  JSValue ctor = js_global_get_str(ctx, class_name);
  JSValue obj = JS_CallConstructor(ctx, ctor, argc, argv);
  JS_FreeValue(ctx, ctor);
  return obj;
}

static inline JSValue
js_global_call(JSContext* ctx, const char* ctor_name, int argc, JSValueConst argv[]) {
  JSValue ret, fn = js_global_get_str(ctx, ctor_name);
  ret = JS_Call(ctx, fn, JS_UNDEFINED, argc, argv);
  JS_FreeValue(ctx, fn);
  return ret;
}

JSValue js_global_prototype(JSContext* ctx, const char* class_name);
JSValue js_global_prototype_func(JSContext* ctx, const char* class_name, const char* func_name);
JSValue js_global_static_func(JSContext* ctx, const char* class_name, const char* func_name);
BOOL js_global_instanceof(JSContext* ctx, JSValueConst obj, const char* prop);

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
  FLAG_NAN,         // 11
  FLAG_FUNCTION,    // 12
  FLAG_ARRAY,       // 13
  FLAG_MODULE,
  FLAG_FUNCTION_BYTECODE,
  FLAG_UNINITIALIZED,
  FLAG_CATCH_OFFSET,
  FLAG_EXCEPTION

};

typedef enum {
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
  TYPE_NAN = (1 << FLAG_NAN),
  TYPE_NUMBER = (TYPE_INT | TYPE_BIG_FLOAT | TYPE_BIG_INT | TYPE_BIG_DECIMAL | TYPE_FLOAT64),
  TYPE_PRIMITIVE = (TYPE_UNDEFINED | TYPE_NULL | TYPE_BOOL | TYPE_INT | TYPE_STRING | TYPE_SYMBOL | TYPE_BIG_FLOAT | TYPE_BIG_INT | TYPE_BIG_DECIMAL | TYPE_NAN),
  TYPE_ALL = (TYPE_PRIMITIVE | TYPE_OBJECT),
  TYPE_FUNCTION = (1 << FLAG_FUNCTION),
  TYPE_ARRAY = (1 << FLAG_ARRAY),
} ValueTypeMask;

static inline int32_t
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

static inline int32_t
js_value_type_get(JSContext* ctx, JSValueConst value) {
  if(JS_IsArray(ctx, value))
    return FLAG_ARRAY;

  if(JS_IsFunction(ctx, value))
    return FLAG_FUNCTION;

  if(JS_VALUE_IS_NAN(value))
    return FLAG_NAN;

  return js_value_type_flag(value);
}

static inline int32_t
js_value_type2flag(uint32_t type) {
  int32_t flag;
  for(flag = 0; (type >>= 1); flag++) {
  }
  return flag;
}

ValueTypeMask js_value_type(JSContext* ctx, JSValueConst value);

const char* const* js_value_types(void);
const char* js_value_typeof(JSValueConst);
const char* js_value_type_name(int32_t type);
const char* js_value_tag_name(int tag);
int32_t js_value_tag_type(int tag);
const char* js_value_typestr(JSContext* ctx, JSValueConst value);

/* clang-format off */ 
int       js_value_tag(JSValueConst v);
void*     js_value_ptr(JSValueConst v);
int       js_value_int(JSValueConst v);
BOOL      js_value_bool(JSValueConst v);
double    js_value_float64(JSValueConst v);
JSValue   js_value_mkptr(int tag, void* ptr);
JSValue   js_value_mkval(int tag, intptr_t val);
JSObject* js_value_obj(JSValueConst v);
/* clang-format on */

BOOL js_value_has_ref_count(JSValueConst v);

void js_value_free(JSContext* ctx, JSValue v);
void js_value_free_rt(JSRuntime* rt, JSValue v);

BOOL js_value_equals(JSContext* ctx, JSValueConst a, JSValueConst b);
void js_value_print(JSContext* ctx, JSValueConst value);
JSValue js_value_clone(JSContext* ctx, JSValueConst valpe);
JSValue* js_values_dup(JSContext* ctx, int nvalues, JSValueConst* values);
void js_values_free(JSRuntime* rt, int nvalues, JSValueConst* values);
JSValue js_values_toarray(JSContext* ctx, int nvalues, JSValueConst* values);
JSValue* js_values_fromarray(JSContext* ctx, size_t* nvalues_p, JSValueConst arr);
void js_value_fwrite(JSContext*, JSValueConst, FILE* f);
void js_value_dump(JSContext*, JSValueConst, DynBuf* db);
JSValue js_value_coerce(JSContext* ctx, const char* func_name, JSValueConst);
JSValue js_global_new(JSContext* ctx, const char* ctor_name, int argc, JSValueConst argv[]);

//#include "buffer-utils.h"

char* js_cstring_dup(JSContext* ctx, const char* str);
char* js_cstring_ptr(JSValueConst v);
size_t js_cstring_len(JSValueConst v);
JSValueConst js_cstring_value(const char* ptr);
void js_cstring_dump(JSContext* ctx, JSValueConst value, DynBuf* db);

static inline const char*
js_cstring_new(JSContext* ctx, const char* str) {
  JSValue v = JS_NewString(ctx, str);
  const char* s = JS_ToCString(ctx, v);
  JS_FreeValue(ctx, v);
  return s;
}

static inline const char*
js_cstring_newlen(JSContext* ctx, const char* str, size_t len) {
  JSValue v = JS_NewStringLen(ctx, str, len);
  const char* s = JS_ToCString(ctx, v);
  JS_FreeValue(ctx, v);
  return s;
}

/*static inline void
JS_FreeCString(JSContext* ctx, const char* ptr) {
  if(!ptr)
    return;

  JS_FreeValue(ctx, JS_MKPTR(JS_TAG_STRING, (void*)(ptr - offsetof(JSString, u))));
}*/

#define js_cstring_destroy(ctx, cstr) \
  do { \
    if((cstr)) \
      JS_FreeCString((ctx), (cstr)); \
    (cstr) = 0; \
  } while(0)

static inline int64_t
js_toint64(JSContext* ctx, JSValueConst value) {
  int64_t ret = 0;
  JS_ToInt64(ctx, &ret, value);
  return ret;
}

char* js_tostringlen(JSContext* ctx, size_t* lenp, JSValueConst value);
char* js_tostring(JSContext* ctx, JSValueConst value);
wchar_t* js_towstringlen(JSContext* ctx, size_t* lenp, JSValueConst value);

static inline wchar_t*
js_towstring(JSContext* ctx, JSValueConst value) {
  return js_towstringlen(ctx, 0, value);
}

static inline JSValue
js_value_tostring(JSContext* ctx, const char* class_name, JSValueConst value) {
  JSAtom atom;
  JSValue proto, tostring, str;
  proto = js_global_prototype(ctx, class_name);
  atom = JS_NewAtom(ctx, "toString");
  tostring = JS_GetProperty(ctx, proto, atom);
  JS_FreeValue(ctx, proto);
  JS_FreeAtom(ctx, atom);
  str = JS_Call(ctx, tostring, value, 0, 0);
  JS_FreeValue(ctx, tostring);
  return str;
}

int js_value_tosize(JSContext* ctx, size_t* sz, JSValueConst value);

static inline double
js_value_todouble_free(JSContext* ctx, JSValueConst value) {
  double ret = 0;
  JS_ToFloat64(ctx, &ret, value);
  JS_FreeValue(ctx, value);
  return ret;
}

static inline int64_t
js_value_toint64_free(JSContext* ctx, JSValueConst value) {
  int64_t ret = 0;
  JS_ToInt64(ctx, &ret, value);
  JS_FreeValue(ctx, value);
  return ret;
}

static inline BOOL
js_value_tobool_free(JSContext* ctx, JSValueConst value) {
  BOOL ret = JS_ToBool(ctx, value);
  JS_FreeValue(ctx, value);
  return ret;
}

static inline JSAtom
js_value_toatom_free(JSContext* ctx, JSValueConst value) {
  JSAtom atom = JS_ValueToAtom(ctx, value);
  JS_FreeValue(ctx, value);
  return atom;
}

JSValue js_value_from_char(JSContext* ctx, int c);
static inline int
js_value_cmpstring(JSContext* ctx, JSValueConst value, const char* other) {
  const char* str = JS_ToCString(ctx, value);
  int ret = strcmp(str, other);
  JS_FreeCString(ctx, str);
  return ret;
}

void js_propertyenums_free(JSContext* ctx, JSPropertyEnum* props, size_t len);

static inline void
js_propertydescriptor_free(JSContext* ctx, JSPropertyDescriptor* desc) {
  JS_FreeValue(ctx, desc->value);
  JS_FreeValue(ctx, desc->getter);
  JS_FreeValue(ctx, desc->setter);
}

JSValue js_symbol_ctor(JSContext* ctx);

JSValue js_symbol_invoke_static(JSContext* ctx, const char* name, JSValueConst arg);

JSValue js_symbol_to_string(JSContext* ctx, JSValueConst sym);

const char* js_symbol_to_cstring(JSContext* ctx, JSValueConst sym);

JSValue js_symbol_static_value(JSContext* ctx, const char* name);
JSAtom js_symbol_static_atom(JSContext* ctx, const char* name);
BOOL js_is_iterable(JSContext* ctx, JSValueConst obj);
BOOL js_is_iterator(JSContext* ctx, JSValueConst obj);
JSValue js_iterator_method(JSContext* ctx, JSValueConst obj);
JSValue js_iterator_new(JSContext* ctx, JSValueConst obj);
JSValue js_iterator_next(JSContext* ctx, JSValueConst obj, BOOL* done_p);
JSValue js_iterator_result(JSContext*, JSValue value, BOOL done);
JSValue js_iterator_then(JSContext*, BOOL done);
JSValue js_symbol_for(JSContext* ctx, const char* sym_for);
JSAtom js_symbol_for_atom(JSContext* ctx, const char* sym_for);

JSValue js_symbol_operatorset_value(JSContext* ctx);

JSAtom js_symbol_operatorset_atom(JSContext* ctx);

JSValue js_operators_create(JSContext* ctx, JSValue* this_obj);

static inline int64_t
js_int64_default(JSContext* ctx, JSValueConst value, int64_t i) {
  if(JS_IsNumber(value))
    JS_ToInt64(ctx, &i, value);
  return i;
}

JSValue js_number_new(JSContext* ctx, int32_t n);
BOOL js_number_integral(JSValueConst value);

static inline JSValue
js_new_bool_or_number(JSContext* ctx, int32_t n) {
  if(n == 0)
    return JS_NewBool(ctx, FALSE);
  return js_number_new(ctx, n);
}

JSAtom js_atom_from(JSContext*, const char*);
int js_atom_toint64(JSContext* ctx, int64_t* i, JSAtom atom);
int32_t js_atom_toint32(JSContext* ctx, JSAtom atom);

static inline JSValue
js_atom_tovalue(JSContext* ctx, JSAtom atom) {
  if(JS_ATOM_ISINT(atom))
    return JS_MKVAL(JS_TAG_INT, JS_ATOM_TOINT(atom));

  return JS_AtomToValue(ctx, atom);
}

unsigned int js_atom_tobinary(JSAtom atom);
const char* js_atom_to_cstringlen(JSContext* ctx, size_t* len, JSAtom atom);
void js_atom_dump(JSContext* ctx, JSAtom atom, DynBuf* db, BOOL color);
BOOL js_atom_is_index(JSContext* ctx, int64_t* pval, JSAtom atom);
BOOL js_atom_is_string(JSContext* ctx, JSAtom atom);
BOOL js_atom_is_symbol(JSContext* ctx, JSAtom atom);

static inline BOOL
js_atom_is_integer(JSAtom atom) {
  return JS_ATOM_ISINT(atom);
}

static inline uint32_t
js_atom_get_integer(JSAtom atom) {
  return JS_ATOM_TOINT(atom);
}

static inline JSAtom
js_atom_from_integer(JSContext* ctx, int32_t i) {
  if(i >= 0)
    return JS_ATOM_FROMINT(i);

  JSValue val = JS_NewInt32(ctx, i);
  JSAtom ret = JS_ValueToAtom(ctx, val);
  JS_FreeValue(ctx, val);
  return ret;
}

static inline BOOL
js_atom_is_number(JSContext* ctx, JSAtom atom) {
  if(!js_atom_is_integer(atom)) {
    BOOL ret = FALSE;
    int64_t index;
    JSValue value = JS_AtomToValue(ctx, atom);

    if(!JS_ToInt64(ctx, &index, value))
      ret = TRUE;

    JS_FreeValue(ctx, value);
    return ret;
  }

  return TRUE;
}

int js_atom_cmp_string(JSContext* ctx, JSAtom atom, const char* other);
BOOL js_atom_is_length(JSContext* ctx, JSAtom atom);
char* js_atom_tostring(JSContext* ctx, JSAtom atom);

/*static inline BOOL
js_atom_equal_string(JSContext* ctx, JSAtom atom, const char* other) {
  return js_atom_cmp_string(ctx, atom, other) == 0;
}*/

const char* js_object_tostring(JSContext* ctx, JSValueConst value);
const char* js_object_tostring2(JSContext* ctx, JSValueConst method, JSValueConst value);
const char* js_function_name(JSContext* ctx, JSValueConst value);
BOOL js_function_set_name(JSContext* ctx, JSValueConst func, const char* name);
const char* js_function_tostring(JSContext* ctx, JSValueConst value);
BOOL js_function_isnative(JSContext* ctx, JSValueConst value);
int js_function_argc(JSContext* ctx, JSValueConst value);
JSValue js_function_bind(JSContext*, JSValue func, int argc, JSValue argv[]);
JSValue js_function_bind_this(JSContext*, JSValue func, JSValue this_val);
JSValue js_function_bind_this_args(JSContext* ctx, JSValueConst func, JSValueConst this_val, int argc, JSValueConst argv[]);
JSValue js_function_throw(JSContext*, JSValue err);
JSValue js_function_return_undefined(JSContext*);
JSValue js_function_return_value(JSContext*, JSValue value);
JSValue js_function_prototype(JSContext* ctx);

typedef JSValue CClosureFunc(JSContext*, JSValueConst, int, JSValueConst[], int, void*);
JSValue js_function_cclosure(JSContext*, CClosureFunc*, int length, int magic, void*, void (*)(void*));

JSValue js_object_constructor(JSContext*, JSValueConst value);
JSValue js_object_species(JSContext*, JSValueConst value);
char* js_object_classname(JSContext*, JSValueConst value);
BOOL js_object_equals(JSContext*, JSValueConst a, JSValueConst b);
int js_object_is(JSContext*, JSValueConst value, const char* cmp);
JSValue js_object_construct(JSContext*, JSValueConst ctor);
JSValue js_object_error(JSContext*, const char* message);
JSValue js_object_new(JSContext*, const char* class_name, int argc, JSValueConst argv[]);
JSValue js_object_function(JSContext*, const char* func_name, JSValueConst obj);
BOOL js_object_same2(JSContext*, JSValueConst, JSValueConst);
JSAtom* js_object_properties(JSContext*, uint32_t* lenptr, JSValueConst obj, int flags);
int js_object_copy(JSContext* ctx, JSValueConst dst, JSValueConst src);

#define JS_GPN_RECURSIVE (1 << 7)

static inline BOOL
js_object_same(JSValueConst a, JSValueConst b) {
  JSObject *aobj, *bobj;

  if(!JS_IsObject(a) || !JS_IsObject(b))
    return FALSE;

  aobj = JS_VALUE_GET_OBJ(a);
  bobj = JS_VALUE_GET_OBJ(b);

  return aobj == bobj;
}

/*static inline JSClassID
js_get_classid(JSValue v) {
  JSObject* p = JS_VALUE_GET_OBJ(v);
  assert(p != 0);
  return p->class_id;
}*/

BOOL js_has_propertystr(JSContext* ctx, JSValueConst obj, const char* str);
BOOL js_get_propertystr_bool(JSContext* ctx, JSValueConst obj, const char* str);

static inline BOOL
js_has_propertyvalue(JSContext* ctx, JSValueConst obj, JSValueConst prop) {
  JSAtom atom = JS_ValueToAtom(ctx, prop);
  BOOL ret = JS_HasProperty(ctx, obj, atom);
  JS_FreeAtom(ctx, atom);
  return ret;
}

static inline JSValue
js_get_propertyvalue(JSContext* ctx, JSValueConst obj, JSValueConst prop) {
  JSAtom atom = JS_ValueToAtom(ctx, prop);
  JSValue ret = JS_GetProperty(ctx, obj, atom);
  JS_FreeAtom(ctx, atom);
  return ret;
}

static inline int
js_set_propertyvalue(JSContext* ctx, JSValueConst obj, JSValueConst prop, JSValueConst value) {
  JSAtom atom = JS_ValueToAtom(ctx, prop);
  int ret = JS_SetProperty(ctx, obj, atom, value);
  JS_FreeAtom(ctx, atom);
  return ret;
}

static inline int
js_delete_propertystr(JSContext* ctx, JSValueConst obj, const char* prop) {
  JSAtom atom = JS_NewAtom(ctx, prop);
  int ret = JS_DeleteProperty(ctx, obj, atom, 0);
  JS_FreeAtom(ctx, atom);
  return ret;
}

static inline int
js_delete_propertyvalue(JSContext* ctx, JSValueConst obj, JSValueConst prop) {
  JSAtom atom = JS_ValueToAtom(ctx, prop);
  int ret = JS_DeleteProperty(ctx, obj, atom, 0);
  JS_FreeAtom(ctx, atom);
  return ret;
}

void js_set_propertyint_string(JSContext* ctx, JSValueConst obj, uint32_t i, const char* str);
void js_set_propertyint_int(JSContext* ctx, JSValueConst obj, uint32_t i, int32_t value);
void js_set_propertystr_int(JSContext* ctx, JSValueConst obj, const char* prop, int32_t value);
void js_set_propertystr_string(JSContext* ctx, JSValueConst obj, const char* prop, const char* str);
void js_set_propertystr_stringlen(JSContext* ctx, JSValueConst obj, const char* prop, const char* str, size_t len);
const char* js_get_propertyint_cstring(JSContext* ctx, JSValueConst obj, uint32_t i);
int32_t js_get_propertyint_int32(JSContext* ctx, JSValueConst obj, uint32_t i);
const char* js_get_propertystr_cstring(JSContext* ctx, JSValueConst obj, const char* prop);
const char* js_get_propertystr_cstringlen(JSContext* ctx, JSValueConst obj, const char* prop, size_t* lenp);
const char* js_get_property_cstring(JSContext* ctx, JSValueConst obj, JSAtom prop);
char* js_get_property_string(JSContext* ctx, JSValueConst obj, JSAtom prop);
char* js_get_propertystr_string(JSContext* ctx, JSValueConst obj, const char* prop);
char* js_get_propertystr_stringlen(JSContext* ctx, JSValueConst obj, const char* prop, size_t* lenp);
int32_t js_get_propertystr_int32(JSContext* ctx, JSValueConst obj, const char* prop);
uint64_t js_get_propertystr_uint64(JSContext* ctx, JSValueConst obj, const char* prop);
int js_get_propertydescriptor(JSContext* ctx, JSPropertyDescriptor* desc, JSValueConst obj, JSAtom prop);
JSAtom js_get_propertystr_atom(JSContext* ctx, JSValueConst obj, const char* prop);

static inline void
js_set_inspect_method(JSContext* ctx, JSValueConst obj, JSCFunction* func) {
  JSAtom inspect_symbol = js_symbol_for_atom(ctx, "quickjs.inspect.custom");
  JS_DefinePropertyValue(ctx, obj, inspect_symbol, JS_NewCFunction(ctx, func, "inspect", 1), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
  JS_FreeAtom(ctx, inspect_symbol);
}

JSValue js_get_tostringtag_value(JSContext*, JSValueConst obj);
void js_set_tostringtag_value(JSContext*, JSValueConst obj, JSValueConst value);
void js_set_tostringtag_str(JSContext*, JSValueConst obj, const char* str);
const char* js_get_tostringtag_cstr(JSContext*, JSValueConst obj);

static inline char*
js_get_tostringtag_str(JSContext* ctx, JSValueConst obj) {
  char* ret = 0;
  const char* str;
  if((str = js_get_tostringtag_cstr(ctx, obj))) {
    ret = js_strdup(ctx, str);
    JS_FreeCString(ctx, str);
  }
  return ret;
}

JSClassID js_class_id(JSContext* ctx, int id);
JSClassID js_class_newid(void);
JSAtom js_class_atom(JSContext* ctx, JSClassID id);
const char* js_class_name(JSContext* ctx, JSClassID id);
JSClassID js_class_find(JSContext* ctx, const char* name);

static inline BOOL
js_object_isclass(JSValue obj, int32_t class_id) {
  return JS_GetOpaque(obj, class_id) != 0;
}

static inline BOOL
js_value_isclass(JSContext* ctx, JSValue obj, int id) {
  int32_t class_id = js_class_id(ctx, id);
  return js_object_isclass(obj, class_id);
}

BOOL js_is_arraybuffer(JSContext*, JSValueConst);
BOOL js_is_sharedarraybuffer(JSContext*, JSValueConst);
BOOL js_is_date(JSContext*, JSValueConst);
BOOL js_is_map(JSContext*, JSValueConst);
BOOL js_is_set(JSContext*, JSValueConst);
BOOL js_is_generator(JSContext*, JSValueConst);
BOOL js_is_asyncgenerator(JSContext*, JSValueConst);
BOOL js_is_regexp(JSContext*, JSValueConst);
BOOL js_is_promise(JSContext*, JSValueConst);
BOOL js_is_dataview(JSContext*, JSValueConst);
BOOL js_is_error(JSContext*, JSValueConst);
BOOL js_is_nan(JSValueConst obj);

static inline BOOL
js_is_null_or_undefined(JSValueConst value) {
  return JS_IsUndefined(value) || JS_IsNull(value);
}

static inline BOOL
js_is_falsish(JSValueConst value) {
  switch(JS_VALUE_GET_TAG(value)) {
    case JS_TAG_NULL: return TRUE;
    case JS_TAG_UNDEFINED: return TRUE;
    case JS_TAG_INT: return JS_VALUE_GET_INT(value) == 0;
    case JS_TAG_BOOL: return !JS_VALUE_GET_BOOL(value);
    case JS_TAG_FLOAT64: return JS_VALUE_GET_FLOAT64(value) == 0;
    default: return FALSE;
  }
}

static inline BOOL
js_is_truish(JSValueConst value) {
  return !js_is_falsish(value);
}

static inline BOOL
js_is_nullish(JSContext* ctx, JSValueConst value) {
  int64_t i = -1;

  if(JS_IsUndefined(value) || JS_IsNull(value))
    return TRUE;
  JS_ToInt64(ctx, &i, value);
  return i == 0;
}

JSValue js_typedarray_prototype(JSContext* ctx);
JSValue js_typedarray_constructor(JSContext* ctx);
JSValue js_typedarray_new(JSContext*, int bits, BOOL floating, BOOL sign, JSValue buffer);

static inline BOOL
js_is_basic_array(JSContext* ctx, JSValueConst value) {
  JSValue ctor = js_global_get_str(ctx, "Array");
  BOOL ret = JS_IsInstanceOf(ctx, value, ctor);
  JS_FreeValue(ctx, ctor);
  return ret;
}

static inline BOOL
js_is_typedarray(JSContext* ctx, JSValueConst value) {
  JSValue ctor = js_typedarray_constructor(ctx);
  BOOL ret = JS_IsInstanceOf(ctx, value, js_typedarray_constructor(ctx));
  JS_FreeValue(ctx, ctor);
  return ret;
}

int64_t js_array_length(JSContext* ctx, JSValueConst array);

static inline BOOL
js_is_array(JSContext* ctx, JSValueConst value) {
  return JS_IsArray(ctx, value) || js_is_typedarray(ctx, value);
}

static inline BOOL
js_is_array_like(JSContext* ctx, JSValueConst obj) {
  int64_t len = js_array_length(ctx, obj);
  return len >= 0;
}

BOOL js_is_input(JSContext* ctx, JSValueConst value);

static inline BOOL
js_is_bignumber(JSContext* ctx, JSValueConst value) {
  return JS_IsBigInt(ctx, value) || JS_IsBigDecimal(value) || JS_IsBigFloat(value);
}

static inline BOOL
js_is_numeric(JSContext* ctx, JSValueConst value) {
  return JS_IsNumber(value) || js_is_bignumber(ctx, value);
}

int js_propenum_cmp(const void* a, const void* b, void* ptr);
BOOL js_object_equals(JSContext* ctx, JSValueConst a, JSValueConst b);
void js_array_clear(JSContext* ctx, JSValueConst array);

size_t js_strv_length(char** strv);

char** js_strv_dup(JSContext* ctx, char** strv);

void js_strv_free_n(JSContext*, int, char* argv[]);
void js_strv_free(JSContext* ctx, char** strv);
void js_strv_free_rt(JSRuntime* rt, char** strv);
JSValue js_strv_to_array(JSContext* ctx, char** strv);

int32_t* js_argv_to_int32v(JSContext* ctx, int argc, JSValueConst argv[]);
JSAtom* js_argv_to_atoms(JSContext*, int argc, JSValueConst argv[]);

JSValue js_int32v_to_array(JSContext*, int32_t const*, size_t);
JSValue js_intv_to_array(JSContext*, int const*, size_t);

char** js_array_to_argv(JSContext*, size_t*, JSValueConst);
int32_t* js_array_to_int32v(JSContext*, size_t*, JSValueConst);
uint32_t* js_array_to_uint32v(JSContext*, size_t*, JSValueConst);
int64_t* js_array_to_int64v(JSContext*, size_t*, JSValueConst);

JSValue js_array_iterator_prototype(JSContext*);

int js_array_copys(JSContext*, JSValueConst, int n, char** stra);
int js_strv_copys(JSContext*, int, JSValueConst argv[], int n, char** stra);

JSValue js_invoke(JSContext* ctx, JSValueConst this_obj, const char* method, int argc, JSValueConst argv[]);

JSValue js_to_string(JSContext* ctx, JSValueConst this_obj);
JSValue js_to_source(JSContext* ctx, JSValueConst this_obj);
char* js_tosource(JSContext* ctx, JSValueConst value);

static inline size_t
js_arraybuffer_length(JSContext* ctx, JSValueConst buffer) {
  size_t len;

  if(JS_GetArrayBuffer(ctx, &len, buffer))
    return len;
  return 0;
}

int64_t js_arraybuffer_bytelength(JSContext* ctx, JSValueConst value);

static inline int
js_find_cfunction_entry(const JSCFunctionListEntry* entries, size_t n_entries, const char* name, int def_type) {
  size_t i;
  for(i = 0; i < n_entries; i++)
    if(entries[i].def_type == def_type && !strcmp(entries[i].name, name))
      return i;
  return -1;
}

static inline int
js_find_cfunction_atom(JSContext* ctx, const JSCFunctionListEntry* entries, size_t n_entries, JSAtom atom, int def_type) {
  const char* name = JS_AtomToCString(ctx, atom);
  int i;
  i = js_find_cfunction_entry(entries, n_entries, name, def_type);
  JS_FreeCString(ctx, name);
  return i;
}

JSValue js_date_new(JSContext*, JSValue arg);
JSValue js_date_from_ms(JSContext*, int64_t ms);
JSValue js_date_from_time_ns(JSContext*, time_t t, long ns);
JSValue js_date_from_timespec(JSContext*, const struct timespec ts);
int64_t js_date_gettime(JSContext*, JSValue arg);
int64_t js_date_time(JSContext*, JSValue arg);
struct timespec js_date_timespec(JSContext*, JSValue arg);

void js_arraybuffer_freevalue(JSRuntime*, void* opaque, void* ptr);
JSValue js_arraybuffer_fromvalue(JSContext*, void* x, size_t n, JSValue val);
JSValue js_arraybuffer_fromstring(JSContext* ctx, JSValueConst str);
static inline JSValue
js_arraybuffer_constructor(JSContext* ctx) {
  return js_global_get_str(ctx, "ArrayBuffer");
}
static inline JSValue
js_sharedarraybuffer_constructor(JSContext* ctx) {
  return js_global_get_str(ctx, "SharedArrayBuffer");
}

JSValue js_map_new(JSContext*, JSValueConst);
JSValue js_map_iterator_prototype(JSContext*);

typedef union import_directive {
  struct {
    const char* path; /**< Module path */
    const char* spec; /**< Import specifier(s) */
    const char* prop; /**< if != 0 && *prop, ns += "." + prop */
    const char* var;  /**< Global variable name */
    const char* ns;   /**< Namespace variable */
  };
  const char* args[5];
} ImportDirective;

JSValue module_name(JSContext*, JSModuleDef*);
char* module_namestr(JSContext*, JSModuleDef*);
const char* module_namecstr(JSContext* ctx, JSModuleDef* m);
JSValue module_func(JSContext*, JSModuleDef*);
JSValue module_ns(JSContext*, JSModuleDef*);
JSValue module_exception(JSContext*, JSModuleDef*);
JSValue module_meta_obj(JSContext*, JSModuleDef*);
JSValue module_exports_find(JSContext*, JSModuleDef*, JSAtom);
JSValue module_exports_find_str(JSContext* ctx, JSModuleDef* m, const char* name);
int module_exports_get(JSContext*, JSModuleDef*, BOOL, JSValue exports);
JSValue module_imports(JSContext* ctx, JSModuleDef* m);
JSValue module_reqmodules(JSContext* ctx, JSModuleDef* m);
JSValue module_default_export(JSContext*, JSModuleDef*);
JSValue module_exports(JSContext*, JSModuleDef*);
JSValue module_value(JSContext*, JSModuleDef*);
JSValue module_entry(JSContext*, JSModuleDef*);
void module_make_object(JSContext*, JSModuleDef*, JSValueConst obj);
JSValue module_object(JSContext*, JSModuleDef*);
JSModuleDef* module_next(JSContext* ctx, JSModuleDef* m);
JSModuleDef* module_prev(JSContext* ctx, JSModuleDef* m);
JSModuleDef* module_last(JSContext* ctx);
void module_rename(JSContext* ctx, JSModuleDef* m, JSAtom name);

struct list_head* js_modules_list(JSContext*);
JSModuleDef** js_modules_vector(JSContext* ctx);
JSValue js_modules_entries(JSContext*, JSValue this_val, int magic);
JSValue js_modules_map(JSContext*, JSValue this_val, int magic);
JSValue js_modules_object(JSContext*, JSValue this_val, int magic);

int js_module_index(JSContext* ctx, JSModuleDef* m);

JSModuleDef* js_module_def(JSContext*, JSValue value);
JSModuleDef* js_module_find(JSContext*, const char* name);
JSModuleDef* js_module_find_from(JSContext*, const char* name, int pos);
JSModuleDef* js_module_find_fwd(JSContext*, const char* name, JSModuleDef* start);
JSModuleDef* js_module_find_rev(JSContext*, const char* name, JSModuleDef* start);
int js_module_indexof(JSContext*, JSModuleDef* def);
JSModuleDef* js_module_at(JSContext*, int index);
JSModuleDef* js_module_load(JSContext* ctx, const char* name);

JSValue js_eval_module(JSContext*, JSValue, BOOL);
JSValue js_eval_binary(JSContext*, const uint8_t*, size_t, BOOL load_only);
JSValue js_eval_buf(JSContext*, const void*, int, const char* filename, int eval_flags);
JSValue js_eval_file(JSContext*, const char*, int);
int js_eval_str(JSContext*, const char*, const char*, int flags);
JSValue __attribute__((format(printf, 3, 4))) js_eval_fmt(JSContext* ctx, int flags, const char* fmt, ...);

int64_t js_time_ms(void);
int js_interrupt_handler(JSRuntime*, void*);

void js_call_handler(JSContext*, JSValueConst);

void* js_sab_alloc(void*, size_t);
void js_sab_free(void*, void*);
void js_sab_dup(void*, void*);

/*JSWorkerMessagePipe* js_new_message_pipe(void);
JSWorkerMessagePipe* js_dup_message_pipe(JSWorkerMessagePipe*);*/

/*void js_free_message(JSWorkerMessage*);
void js_free_message_pipe(JSWorkerMessagePipe*);*/

void js_error_dump(JSContext*, JSValueConst, DynBuf* db);
char* js_error_tostring(JSContext*, JSValueConst);
void js_error_print(JSContext*, JSValueConst);
JSValue js_error_stack(JSContext* ctx);
/*JSValue js_error_uncatchable(JSContext* ctx);*/

JSValue js_iohandler_fn(JSContext*, BOOL write);
BOOL js_iohandler_set(JSContext* ctx, JSValueConst set_handler, int fd, JSValue handler);

JSValue js_promise_new(JSContext* ctx, JSValue resolving_funcs[2]);
JSValue js_promise_immediate(JSContext* ctx, BOOL reject, JSValueConst promise);
JSValue js_promise_resolve(JSContext* ctx, JSValueConst promise);
JSValue js_promise_reject(JSContext* ctx, JSValueConst promise);
JSValue js_promise_then(JSContext* ctx, JSValueConst promise, JSValueConst func);

static inline JSValue
js_promise_resolve_then(JSContext* ctx, JSValueConst promise, JSValueConst func) {
  JSValue tmp, ret;
  tmp = js_promise_resolve(ctx, promise);
  ret = js_promise_then(ctx, tmp, func);
  JS_FreeValue(ctx, tmp);
  return ret;
}

JSValue js_promise_adopt(JSContext* ctx, JSValueConst value);

char* js_json_stringify(JSContext* ctx, JSValueConst value);

BOOL js_is_identifier_len(JSContext* ctx, const char* str, size_t len);
BOOL js_is_identifier_atom(JSContext* ctx, JSAtom atom);

static inline BOOL
js_is_identifier(JSContext* ctx, const char* str) {
  return js_is_identifier_len(ctx, str, strlen(str));
}

JSValue js_generator_prototype(JSContext*);

static inline JSValue
js_generator_constructor(JSContext* ctx) {
  JSValue proto = js_generator_prototype(ctx);
  JSValue ret = js_object_constructor(ctx, proto);
  JS_FreeValue(ctx, proto);
  return ret;
}

JSValue js_asyncgenerator_prototype(JSContext*);

static inline JSValue
js_asyncgenerator_constructor(JSContext* ctx) {
  JSValue proto = js_asyncgenerator_prototype(ctx);
  JSValue ret = js_object_constructor(ctx, proto);
  JS_FreeValue(ctx, proto);
  return ret;
}

JSValue js_set_iterator_prototype(JSContext*);
JSValue js_std_file(JSContext* ctx, FILE* f);

JSValue js_get_bytecode(JSContext* ctx, JSValueConst value);
JSValue js_opcode_list(JSContext* ctx, BOOL as_object);

void js_cstring_dump_free(JSContext*, JSValue, DynBuf*);
void js_stackframe_dump(JSContext*, JSValueConst, DynBuf*);
void js_stack_dump(JSContext*, JSValueConst, DynBuf*);
char* js_stack_tostring(JSContext*, JSValueConst);
JSValue js_stack_get(JSContext* ctx);
void js_stack_print(JSContext*, JSValueConst);

/**
 * @}
 */

#endif /* defined(UTILS_H) */
