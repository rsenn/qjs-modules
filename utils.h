#ifndef UTILS_H
#define UTILS_H

#include <quickjs.h>
#include "quickjs-internal.h"
#include <cutils.h>
#include <string.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#ifdef HAVE_THREADS_H
#include <threads.h>
#endif

/**
 * \defgroup utils Utilities
 * @{
 */
char* basename(const char*);

#ifndef offsetof
#define offsetof(type, field) ((size_t) & ((type*)0)->field)
#endif

#ifndef inrange
#define inrange(value, min, max) ((value) >= (min) && (value) <= (max))
#endif

#define trim_dotslash(str) (!strncmp((str), "./", 2) ? (str) + 2 : (str))

#ifndef thread_local
#ifdef _Thread_local
#define thread_local _Thread_local
#elif defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__SUNPRO_CC) || defined(__IBMCPP__)
#define thread_local __thread
#elif defined(_WIN32)
#define thread_local __declspec(thread)
#else
#error No TLS implementation found.
#endif
#endif

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

#define JS_CGETSET_ENUMERABLE_DEF(prop_name, fgetter, fsetter, magic_num) \
  { \
    .name = prop_name, .prop_flags = JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE, .def_type = JS_DEF_CGETSET_MAGIC, .magic = magic_num, \
    .u = { \
      .getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}} \
    } \
  }

#define JS_CGETSET_MAGIC_FLAGS_DEF(prop_name, fgetter, fsetter, magic_num, flags) \
  { \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CGETSET_MAGIC, .magic = magic_num, .u = { \
      .getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}} \
    } \
  }

#define JS_CFUNC_DEF_FLAGS(prop_name, length, func1, flags) \
  { \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CFUNC, .magic = 0, .u = { \
      .func = {length, JS_CFUNC_generic, {.generic = func1}} \
    } \
  }

#define JS_CONSTANT(name) JS_PROP_INT32_DEF(#name, name, JS_PROP_CONFIGURABLE)

#if defined(_WIN32) || defined(__MINGW32__)
#define VISIBLE __declspec(dllexport)
#define HIDDEN
#else
#define VISIBLE __attribute__((visibility("default")))
#define HIDDEN __attribute__((visibility("hidden")))
#endif

#ifndef MAX_NUM
#define MAX_NUM(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN_NUM
#define MIN_NUM(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef ABS_NUM
#define ABS_NUM(n) ((n) < 0 ? -(n) : (n))
#endif
#ifndef SIGN_NUM
#define SIGN_NUM(n) ((n) < 0)
#endif

#define JS_IsModule(value) (JS_VALUE_GET_TAG((value)) == JS_TAG_MODULE)

typedef struct {
  BOOL done;
  JSValue value;
} IteratorValue;

typedef struct {
  int c;
  const char** v;
} Arguments;

static inline Arguments
arguments_new(int argc, const char* argv[]) {
  Arguments args;
  args.c = argc;
  args.v = argv;
  return args;
}

static inline const char*
arguments_shift(Arguments* args) {
  const char* ret = 0;
  if(args->c > 0) {
    ret = args->v[0];
    args->c--;
    args->v++;
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

typedef struct {
  int c;
  JSValueConst* v;
} JSArguments;

static inline JSArguments
js_arguments_new(int argc, JSValueConst* argv) {
  JSArguments args;
  args.c = argc;
  args.v = argv;
  return args;
}

static inline JSValueConst
js_arguments_shift(JSArguments* args) {
  JSValue ret = JS_EXCEPTION;
  if(args->c > 0) {
    ret = args->v[0];
    args->c--;
    args->v++;
  }
  return ret;
}

static inline JSValueConst
js_arguments_at(JSArguments* args, int i) {
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

#define COLOR_BLACK "\x1b[0;30m"
#define COLOR_RED "\x1b[0;31m"
#define COLOR_GREEN "\x1b[0;32m"
#define COLOR_BROWN "\x1b[0;33m"
#define COLOR_BLUE "\x1b[0;34m"
#define COLOR_PURPLE "\x1b[0;35m"
#define COLOR_MARINE "\x1b[0;36m"
#define COLOR_LIGHTGRAY "\x1b[0;37m"
#define COLOR_GRAY "\x1b[1;30m"
#define COLOR_NONE "\x1b[0m"

#define COLOR_LIGHTRED "\x1b[1;31m"

#define COLOR_LIGHTGREEN "\x1b[1;32m"
#define COLOR_YELLOW "\x1b[1;33m"
#define COLOR_LIGHTBLUE "\x1b[1;34m"
#define COLOR_MAGENTA "\x1b[1;35m"
#define COLOR_CYAN "\x1b[1;36m"
#define COLOR_WHITE "\x1b[1;37m"

#define BACKGROUND_RED "\x1b[48;5;124m"
#define BACKGROUND_BLUE "\x1b[48;5;20m"
#define BACKGROUND_YELLOW "\x1b[48;5;214m"
#define BACKGROUND_GREEN "\x1b[48;5;28m"
#define BACKGROUND_PINK "\x1b[48;5;165m"

uint64_t time_us(void);

static inline void
js_dbuf_init_rt(JSRuntime* rt, DynBuf* s) {
  dbuf_init2(s, rt, (DynBufReallocFunc*)js_realloc_rt);
}

static inline void
js_dbuf_init(JSContext* ctx, DynBuf* s) {
  dbuf_init2(s, ctx, (DynBufReallocFunc*)js_realloc);
}

typedef struct {
  char* source;
  size_t len;
  int flags;
} RegExp;

int regexp_flags_tostring(int, char*);
int regexp_flags_fromstring(const char*);
RegExp regexp_from_argv(int argc, JSValueConst argv[], JSContext* ctx);
RegExp regexp_from_dbuf(DynBuf* dbuf, int flags);
uint8_t* regexp_compile(RegExp re, JSContext* ctx);
JSValue regexp_to_value(RegExp re, JSContext* ctx);

static inline void
regexp_free_rt(RegExp re, JSRuntime* rt) {
  js_free_rt(rt, re.source);
}
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

JSValue js_global_prototype(JSContext* ctx, const char* class_name);
JSValue js_global_static_func(JSContext* ctx, const char* class_name, const char* func_name);

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
  TYPE_NAN = (1 << FLAG_NAN),
  TYPE_NUMBER = (TYPE_INT | TYPE_BIG_FLOAT | TYPE_BIG_INT | TYPE_BIG_DECIMAL | TYPE_FLOAT64),
  TYPE_PRIMITIVE = (TYPE_UNDEFINED | TYPE_NULL | TYPE_BOOL | TYPE_INT | TYPE_STRING | TYPE_SYMBOL | TYPE_BIG_FLOAT | TYPE_BIG_INT |
                    TYPE_BIG_DECIMAL | TYPE_NAN),
  TYPE_ALL = (TYPE_PRIMITIVE | TYPE_OBJECT),
  TYPE_FUNCTION = (1 << FLAG_FUNCTION),
  TYPE_ARRAY = (1 << FLAG_ARRAY),
};

int32_t js_value_type_flag(JSValueConst value);
int32_t js_value_type_get(JSContext* ctx, JSValueConst value);

static inline int32_t
js_value_type2flag(uint32_t type) {
  int32_t flag;
  for(flag = 0; (type >>= 1); flag++) {}
  return flag;
}

enum value_mask js_value_type(JSContext* ctx, JSValueConst value);

static inline const char* const*
js_value_types() {
  return (const char* const[]){
      "UNDEFINED",     "NULL",         "BOOL",      "INT", "OBJECT",   "STRING", "SYMBOL", "BIG_FLOAT",
      "BIG_INT",       "BIG_DECIMAL",  "FLOAT64",   "NAN", "FUNCTION", "ARRAY",  "MODULE", "FUNCTION_BYTECODE",
      "UNINITIALIZED", "CATCH_OFFSET", "EXCEPTION", 0,
  };
}

static inline const char*
js_value_typeof(JSValueConst value) {
  int32_t flag = js_value_type_flag(value);
  return ((const char* const[]){
      "undefined",     "object",       "boolean",   "number", "object",   "string", "symbol", "bigfloat",
      "bigint",        "bigdecimal",   "number",    "number", "function", "object", "module", "function_bytecode",
      "uninitialized", "catch_offset", "exception", 0,
  })[flag];
}

const char* js_value_type_name(int32_t type);

const char* js_value_typestr(JSContext* ctx, JSValueConst value);

/*VISIBLE void* js_value_ptr(JSValueConst v);
VISIBLE JSObject* js_value_obj(JSValueConst v);
VISIBLE int32_t js_value_tag(JSValueConst v);
*/

static inline void*
js_value_ptr(JSValueConst v) {
  return JS_VALUE_GET_PTR(v);
}

static inline JSObject*
js_value_obj(JSValueConst v) {
  return JS_IsObject(v) ? JS_VALUE_GET_OBJ(v) : 0;
}

static inline int
js_value_tag(JSValueConst v) {
  return JS_VALUE_GET_TAG(v);
}

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

//#include "buffer-utils.h"

char* js_cstring_dup(JSContext* ctx, const char* str);
char* js_cstring_ptr(JSValueConst v);
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

static inline void
js_cstring_free(JSContext* ctx, const char* ptr) {
  if(!ptr)
    return;

  JS_FreeValue(ctx, JS_MKPTR(JS_TAG_STRING, (void*)(ptr - offsetof(JSString, u))));
}

static inline int64_t
js_toint64(JSContext* ctx, JSValueConst value) {
  int64_t ret = 0;
  JS_ToInt64(ctx, &ret, value);
  return ret;
}

static inline char*
js_tostringlen(JSContext* ctx, size_t* lenp, JSValueConst value) {
  size_t len;
  const char* cstr;
  char* ret = 0;
  if((cstr = JS_ToCStringLen(ctx, &len, value))) {
    ret = js_strndup(ctx, cstr, len);
    if(lenp)
      *lenp = len;
    js_cstring_free(ctx, cstr);
  }
  return ret;
}

static inline char*
js_tostring(JSContext* ctx, JSValueConst value) {
  return js_tostringlen(ctx, 0, value);
}

static inline wchar_t*
js_towstringlen(JSContext* ctx, size_t* lenp, JSValueConst value) {
  size_t i, len;
  const char* cstr;
  wchar_t* ret = 0;
  if((cstr = JS_ToCStringLen(ctx, &len, value))) {
    ret = js_mallocz(ctx, sizeof(wchar_t) * (len + 1));
    const uint8_t *ptr = (const uint8_t*)cstr, *end = (const uint8_t*)cstr + len;

    for(i = 0; ptr < end;) { ret[i++] = unicode_from_utf8(ptr, end - ptr, &ptr); }

    if(lenp)
      *lenp = i;
  }
  return ret;
}

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

#define JS_VALUE_FREE(ctx, value) \
  do { \
    JS_FreeValue((ctx), (value)); \
    (value) = JS_UNDEFINED; \
  } while(0);
#define JS_VALUE_FREE_RT(ctx, value) \
  do { \
    JS_FreeValueRT((ctx), (value)); \
    (value) = JS_UNDEFINED; \
  } while(0);

#if 0
#define js_object_tmpmark_set(value) \
  do { ((uint8_t*)JS_VALUE_GET_OBJ((value)))[5] |= 0x40; } while(0);
#define js_object_tmpmark_clear(value) \
  do { ((uint8_t*)JS_VALUE_GET_OBJ((value)))[5] &= ~0x40; } while(0);
#define js_object_tmpmark_isset(value) (((uint8_t*)JS_VALUE_GET_OBJ((value)))[5] & 0x40)
#else
#define js_object_tmpmark_set(value) \
  do { JS_VALUE_GET_OBJ((value))->tmp_mark |= 0x40; } while(0);
#define js_object_tmpmark_clear(value) \
  do { JS_VALUE_GET_OBJ((value))->tmp_mark &= ~0x40; } while(0);
#define js_object_tmpmark_isset(value) (JS_VALUE_GET_OBJ((value))->tmp_mark & 0x40)
#endif

#define js_runtime_exception_set(rt, value) \
  do { *(JSValue*)((uint8_t*)(rt) + 216) = value; } while(0);
#define js_runtime_exception_get(rt) (*(JSValue*)((uint8_t*)(rt) + 216))
#define js_runtime_exception_clear(rt) \
  do { \
    if(!JS_IsNull(js_runtime_exception_get(rt))) \
      JS_FreeValueRT((rt), js_runtime_exception_get(rt)); \
    js_runtime_exception_set(rt, JS_NULL); \
  } while(0)

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

static inline JSValue
js_new_bool_or_number(JSContext* ctx, int32_t n) {
  if(n == 0)
    return JS_NewBool(ctx, FALSE);
  return js_number_new(ctx, n);
}

#define JS_ATOM_TAG_INT (1U << 31)
#define JS_ATOM_MAX_INT (JS_ATOM_TAG_INT - 1)

#define js_atom_isint(i) ((JSAtom)((i)&JS_ATOM_TAG_INT))
#define js_atom_fromint(i) ((JSAtom)((i)&JS_ATOM_MAX_INT) | JS_ATOM_TAG_INT)
#define js_atom_toint(i) (unsigned int)(((JSAtom)(i) & (~(JS_ATOM_TAG_INT))))

JSAtom js_atom_from(JSContext*, const char*);
int js_atom_toint64(JSContext* ctx, int64_t* i, JSAtom atom);
int32_t js_atom_toint32(JSContext* ctx, JSAtom atom);

static inline JSValue
js_atom_tovalue(JSContext* ctx, JSAtom atom) {
  if(js_atom_isint(atom))
    return JS_MKVAL(JS_TAG_INT, js_atom_toint(atom));

  return JS_AtomToValue(ctx, atom);
}

unsigned int js_atom_tobinary(JSAtom atom);
const char* js_atom_to_cstringlen(JSContext* ctx, size_t* len, JSAtom atom);
void js_atom_dump(JSContext* ctx, JSAtom atom, DynBuf* db, BOOL color);
BOOL js_atom_is_index(JSContext* ctx, int64_t* pval, JSAtom atom);
BOOL js_atom_is_length(JSContext* ctx, JSAtom atom);

const char* js_object_tostring(JSContext* ctx, JSValueConst value);
const char* js_function_name(JSContext* ctx, JSValueConst value);
const char* js_function_tostring(JSContext* ctx, JSValueConst value);
JSCFunction* js_function_cfunc(JSContext*, JSValue value);
BOOL js_function_isnative(JSContext* ctx, JSValueConst value);
int js_function_argc(JSContext* ctx, JSValueConst value);
JSValue js_function_bind(JSContext*, JSValue func, int argc, JSValue argv[]);
JSValue js_function_bind_this(JSContext*, JSValue func, JSValue this_val);
JSValue js_function_throw(JSContext*, JSValue err);
JSValue js_function_return_undefined(JSContext*);
JSValue js_function_return_value(JSContext*, JSValue value);

char* js_object_classname(JSContext* ctx, JSValueConst value);
int js_object_is(JSContext* ctx, JSValueConst value, const char* cmp);
JSValue js_object_construct(JSContext* ctx, JSValueConst ctor);
JSValue js_object_error(JSContext* ctx, const char* message);
JSValue js_object_new(JSContext* ctx, const char* class_name, int argc, JSValueConst argv[]);
JSValue js_object_function(JSContext* ctx, const char* func_name, JSValueConst obj);

static inline BOOL
js_object_same(JSValueConst a, JSValueConst b) {
  JSObject *aobj, *bobj;
  if(!JS_IsObject(a) || !JS_IsObject(b))
    return FALSE;

  aobj = JS_VALUE_GET_OBJ(a);
  bobj = JS_VALUE_GET_OBJ(b);
  return aobj == bobj;
}

static inline JSClassID
js_get_classid(JSValue v) {
  JSObject* p;
  /* if(JS_VALUE_GET_TAG(v) != JS_TAG_OBJECT)
     return 0;*/
  p = JS_VALUE_GET_OBJ(v);
  assert(p != 0);
  return p->class_id;
}

BOOL js_has_propertystr(JSContext* ctx, JSValueConst obj, const char* str);
BOOL js_get_propertystr_bool(JSContext* ctx, JSValueConst obj, const char* str);
void js_set_propertyint_string(JSContext* ctx, JSValueConst obj, uint32_t i, const char* str);
void js_set_propertyint_int(JSContext* ctx, JSValueConst obj, uint32_t i, int32_t value);
void js_set_propertystr_string(JSContext* ctx, JSValueConst obj, const char* prop, const char* str);
void js_set_propertystr_stringlen(JSContext* ctx, JSValueConst obj, const char* prop, const char* str, size_t len);
const char* js_get_propertyint_cstring(JSContext* ctx, JSValueConst obj, uint32_t i);
int32_t js_get_propertyint_int32(JSContext* ctx, JSValueConst obj, uint32_t i);
const char* js_get_propertystr_cstring(JSContext* ctx, JSValueConst obj, const char* prop);
const char* js_get_propertystr_cstringlen(JSContext* ctx, JSValueConst obj, const char* prop, size_t* lenp);
char* js_get_propertystr_string(JSContext* ctx, JSValueConst obj, const char* prop);
char* js_get_propertystr_stringlen(JSContext* ctx, JSValueConst obj, const char* prop, size_t* lenp);
int32_t js_get_propertystr_int32(JSContext* ctx, JSValueConst obj, const char* prop);
uint64_t js_get_propertystr_uint64(JSContext* ctx, JSValueConst obj, const char* prop);
int js_get_propertydescriptor(JSContext* ctx, JSPropertyDescriptor* desc, JSValueConst obj, JSAtom prop);

static inline void
js_set_inspect_method(JSContext* ctx, JSValueConst obj, JSCFunction* func) {
  JSAtom inspect_symbol = js_symbol_for_atom(ctx, "quickjs.inspect.custom");
  JS_DefinePropertyValue(ctx, obj, inspect_symbol, JS_NewCFunction(ctx, func, "inspect", 1), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
  JS_FreeAtom(ctx, inspect_symbol);
}

static inline void
js_set_tostringtag_value(JSContext* ctx, JSValueConst obj, JSValue value) {
  JSAtom tostring_tag = js_symbol_static_atom(ctx, "toStringTag");
  JS_DefinePropertyValue(ctx, obj, tostring_tag, value, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
  JS_FreeAtom(ctx, tostring_tag);
}

static inline void
js_set_tostringtag_str(JSContext* ctx, JSValueConst obj, const char* str) {
  js_set_tostringtag_value(ctx, obj, JS_NewString(ctx, str));
}

JSClassID js_class_id(JSContext* ctx, int id);
JSClassID js_class_newid(void);
JSClass* js_class_get(JSContext* ctx, JSClassID id);
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
BOOL js_is_map(JSContext*, JSValueConst);
BOOL js_is_set(JSContext*, JSValueConst);
BOOL js_is_generator(JSContext*, JSValueConst);
BOOL js_is_regexp(JSContext*, JSValueConst);
BOOL js_is_promise(JSContext*, JSValueConst);
BOOL js_is_dataview(JSContext*, JSValueConst);

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

static inline BOOL
js_is_basic_array(JSContext* ctx, JSValueConst value) {
  JSValue ctor = js_global_get_str(ctx, "Array");
  BOOL ret = JS_IsInstanceOf(ctx, value, ctor);
  JS_FreeValue(ctx, ctor);
  return ret;
}

static inline BOOL
js_is_typedarray(JSValueConst value) {
  if(JS_IsObject(value)) {
    JSClassID id = js_get_classid(value);
    return id >= JS_CLASS_UINT8C_ARRAY && id <= JS_CLASS_FLOAT64_ARRAY;
  }
  return FALSE;
}

static inline BOOL
js_is_array(JSContext* ctx, JSValueConst value) {
  return JS_IsArray(ctx, value) || js_is_typedarray(value);
}

BOOL js_is_input(JSContext* ctx, JSValueConst value);

int js_propenum_cmp(const void* a, const void* b, void* ptr);
BOOL js_object_equals(JSContext* ctx, JSValueConst a, JSValueConst b);
int64_t js_array_length(JSContext* ctx, JSValueConst array);
void js_array_clear(JSContext* ctx, JSValueConst array);

size_t js_strv_length(char** strv);

char** js_strv_dup(JSContext* ctx, char** strv);

void js_strv_free_n(JSContext*, int, char* argv[]);
void js_strv_free(JSContext* ctx, char** strv);
void js_strv_free_rt(JSRuntime* rt, char** strv);
JSValue js_strv_to_array(JSContext* ctx, char** strv);
JSValue js_intv_to_array(JSContext* ctx, int* intv, size_t len);
char** js_array_to_argv(JSContext* ctx, int* argcp, JSValueConst array);
int js_array_copys(JSContext*, JSValueConst, int n, char** stra);
int js_strv_copys(JSContext*, int, JSValueConst argv[], int n, char** stra);

JSValue js_invoke(JSContext* ctx, JSValueConst this_obj, const char* method, int argc, JSValueConst argv[]);

static inline size_t
js_arraybuffer_length(JSContext* ctx, JSValueConst buffer) {
  uint8_t* ptr;
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

JSValue js_map_new(JSContext*, JSValueConst);

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
JSValue module_func(JSContext*, JSModuleDef*);
JSValue module_ns(JSContext*, JSModuleDef*);
JSValue module_exports_find(JSContext*, JSModuleDef*, JSAtom);
void module_exports_get(JSContext*, JSModuleDef*, BOOL rename_default, JSValueConst exports);
JSValue module_exports(JSContext*, JSModuleDef*);
struct list_head* js_modules_list(JSContext*);
JSValue js_modules_array(JSContext*, JSValueConst, int magic);
JSValue js_modules_entries(JSContext*, JSValueConst, int magic);
JSValue js_modules_map(JSContext*, JSValueConst, int magic);
JSValue js_modules_object(JSContext*, JSValueConst, int magic);
JSValue module_value(JSContext*, JSModuleDef*);
JSValue module_entry(JSContext*, JSModuleDef*);
JSValue module_object(JSContext*, JSModuleDef*);
char* js_module_search(JSContext*, const char* path, const char*);
char* js_module_search_ext(JSContext*, const char* path, const char*, const char* ext);
char* js_module_normalize(JSContext*, const char*, const char* name, void* opaque);
JSModuleDef* js_module_def(JSContext*, JSValueConst);
JSModuleDef* js_module_find(JSContext*, const char*);
int js_module_indexof(JSContext*, JSModuleDef* def);
JSModuleDef* js_module_at(JSContext*, int index);
JSValue js_import_load(JSContext*, ImportDirective);
JSValue js_import_eval(JSContext*, ImportDirective);
JSModuleDef* js_module_import_default(JSContext*, const char*, const char* var);
JSModuleDef* js_module_import_namespace(JSContext*, const char*, const char* ns);
JSValue js_module_import(JSContext*, const char*, const char* ns, const char* var, const char* prop);
JSModuleDef* js_module_loader_so(JSContext*, const char*);

JSValue js_eval_module(JSContext*, JSValueConst, BOOL load_only);
JSValue js_eval_binary(JSContext*, const uint8_t*, size_t buf_len, BOOL load_only);
JSValue js_eval_buf(JSContext*, const void*, int buf_len, const char* filename, int eval_flags);
int js_eval_str(JSContext*, const char*, const char* file, int flags);

int64_t js_time_ms(void);
int js_interrupt_handler(JSRuntime*, void*);

void js_timer_unlink(JSRuntime*, JSOSTimer*);
void js_timer_free(JSRuntime*, JSOSTimer*);

void js_call_handler(JSContext*, JSValueConst);

void* js_sab_alloc(void*, size_t);
void js_sab_free(void*, void*);
void js_sab_dup(void*, void*);

JSWorkerMessagePipe* js_new_message_pipe(void);
JSWorkerMessagePipe* js_dup_message_pipe(JSWorkerMessagePipe*);

void js_free_message(JSWorkerMessage*);
void js_free_message_pipe(JSWorkerMessagePipe*);

void js_error_dump(JSContext*, JSValueConst, DynBuf* db);
char* js_error_tostring(JSContext*, JSValueConst);
void js_error_print(JSContext*, JSValueConst);
JSValue js_error_stack(JSContext* ctx);

JSValue js_promise_resolve(JSContext* ctx, JSValueConst promise);
JSValue js_promise_then(JSContext* ctx, JSValueConst promise, JSValueConst func);

static inline JSValue
js_promise_resolve_then(JSContext* ctx, JSValueConst promise, JSValueConst func) {
  JSValue tmp, ret;
  tmp = js_promise_resolve(ctx, promise);
  ret = js_promise_then(ctx, tmp, func);
  JS_FreeValue(ctx, tmp);
  return ret;
}

JSValue js_promise_wrap(JSContext* ctx, JSValueConst value);
JSValue js_promise_adopt(JSContext* ctx, JSValueConst value);

/**
 * @}
 */

#endif /* defined(UTILS_H) */
