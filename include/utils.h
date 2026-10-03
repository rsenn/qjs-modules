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

#ifndef PUBLIC_JSOBJECT_DEF
typedef struct JSObject JSObject;
#endif

#define JS_IsModule(value) (JS_VALUE_GET_TAG((value)) == JS_TAG_MODULE)

#define MAX_SAFE_INTEGER 9007199254740991

#define JS_EVAL_IS_MAIN (1 << 2)

#define JS_EVAL_FLAG_MASK (JS_EVAL_FLAG_STRICT | JS_EVAL_FLAG_COMPILE_ONLY | JS_EVAL_FLAG_BACKTRACE_BARRIER | JS_EVAL_FLAG_ASYNC)

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
typedef int JSValueCompareFunc(JSContext*, JSValueConst, JSValueConst, BOOL);

void* utils_js_realloc(JSContext*, void* ptr, size_t size);
void* utils_js_realloc_rt(JSRuntime*, void* ptr, size_t size);

size_t list_size(struct list_head*);
struct list_head* list_front(const struct list_head*);
struct list_head* list_back(const struct list_head*);
struct list_head* list_unlink_before(struct list_head*);
struct list_head list_unlink(struct list_head*, struct list_head*);
void list_link_next(struct list_head*, struct list_head*);
void list_link_prev(struct list_head*, struct list_head*);
void __list_splice(struct list_head*, struct list_head*);
void list_splice(struct list_head*, struct list_head*);
void __list_sort(struct list_head*, int (*cmp)(struct list_head*, struct list_head*, void*), void*);
void __list_reverse(struct list_head*);

#define list_first(list, type, member) list_entry(list_front((list)), type, member)
#define list_first_entry(ptr, type, member) list_entry((ptr)->next, type, member)

#define list_last(list, type, member) list_entry(list_back((list)), type, member)
#define list_last_entry(ptr, type, member) list_entry((ptr)->prev, type, member)


static inline const char*
arguments_shift(Arguments* args) {
  const char* ret = 0;

  if(args->p < args->c) {
    ret = args->v[args->p];
    args->p++;
  }

  return ret;
}



BOOL arguments_alloc(Arguments* args, JSContext* ctx, int n);
const char* arguments_push(Arguments*, JSContext*, const char*);

void arguments_dump(Arguments const*, DynBuf*);

typedef struct {
  uint16_t p, c, a;
  JSValueConst* v;
} JSArguments;

static inline JSArguments
js_arguments_new(int argc, JSValueConst argv[]) {
  JSArguments args;
  args.p = 0;
  args.c = argc;
  args.a = 0;
  args.v = argv;
  return args;
}


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
js_arguments_at(const JSArguments* args, int i) {
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


static inline size_t
min_size(size_t a, size_t b) {
  if(a < b)
    return a;
  else
    return b;
}



/* clang-format off */
static inline void     uint16_put_be (void* x, uint16_t u) { uint8_t* y = x; y[0] = u >> 8; y[1] = u; }
static inline uint16_t uint16_get_be (const void* x) { const uint8_t* y = x; return (y[0] << 8) | y[1]; }
static inline void     uint16_put_le (void* x, uint16_t u) { uint8_t* y = x; y[0] = u; y[1] = u >> 8; }
static inline uint16_t uint16_get_le (const void* x) { const uint8_t* y = x; return (y[1] << 8) | y[0]; }
static inline void     uint16_put_endian (void* x, uint16_t u, Endian endian) { (endian == BIG ? uint16_put_be : uint16_put_le)(x, u); }
static inline void     uint32_put_be (void* x, uint32_t u) { uint8_t* y = x; y[0] = u >> 24; y[1] = u >> 16; y[2] = u >> 8; y[3] = u; }
static inline uint32_t uint32_get_be (const void* x) {const uint16_t* y = x; return (uint16_get_be(y) << 16) | uint16_get_be(y+1); }
static inline void     uint32_put_le (void* x, uint32_t u) { uint8_t* y = x;  y[3] = u >> 24; y[2] = u >> 16; y[1] = u >> 8; y[0] = u; }
static inline uint32_t uint32_get_le (const void* x) {const uint16_t* y = x; return (uint16_get_le(y+1) << 16) | uint16_get_le(y); }
static inline void     uint32_put_endian (void* x, uint32_t u, Endian endian) { (endian == BIG ? uint32_put_be : uint32_put_le)(x, u); }
static inline uint32_t uint32_get_endian (const void* x, Endian endian) { return (endian == BIG ? uint32_get_be : uint32_get_le)(x); }
/* clang-format on */








typedef struct {
  char* source;
  size_t len;
  int flags;
} RegExp;

int regexp_flags_tostring(int, char*);
int regexp_flags_fromstring(const char*);
int regexp_from_argv(RegExp*, int argc, JSValueConst[], JSContext* ctx);
RegExp regexp_from_dbuf(DynBuf* dbuf, int flags);
uint8_t* regexp_compile(RegExp re, JSContext* ctx);
JSValue regexp_to_value(RegExp re, JSContext* ctx);


JSValue js_global_get_str(JSContext*, const char* prop);
JSValue js_global_get_str_n(JSContext*, const char* prop, size_t len);
JSValue js_global_get_atom(JSContext*, JSAtom prop);

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

JSValue js_global_prototype(JSContext*, const char* class_name);
JSValue js_global_prototype_func(JSContext*, const char* class_name, const char* func_name);
JSValue js_global_static_func(JSContext*, const char* class_name, const char* func_name);
BOOL js_global_instanceof(JSContext*, JSValueConst, const char* prop);

typedef enum {
  FLAG_UNDEFINED = 0,
  FLAG_NULL,   // 1
  FLAG_BOOL,   // 2
  FLAG_INT,    // 3
  FLAG_OBJECT, // 4
  FLAG_STRING, // 5
  FLAG_SYMBOL, // 6
#ifdef QJS_BIGNUM_EXT
  FLAG_BIG_FLOAT, // 7
#endif
  FLAG_BIG_INT, // 8
#ifdef QJS_BIGNUM_EXT
  FLAG_BIG_DECIMAL, // 9
#endif
  FLAG_FLOAT64,           // 10
  FLAG_NAN,               // 11
  FLAG_FUNCTION,          // 12
  FLAG_ARRAY,             // 13
  FLAG_MODULE,            // 14
  FLAG_FUNCTION_BYTECODE, // 15
  FLAG_UNINITIALIZED,     // 16
  FLAG_CATCH_OFFSET,      // 17
  FLAG_EXCEPTION,         // 18
  FLAG_INVALID = -1,
} ValueTypeFlag;

typedef enum {
  TYPE_UNDEFINED = (1 << FLAG_UNDEFINED),
  TYPE_NULL = (1 << FLAG_NULL),
  TYPE_BOOL = (1 << FLAG_BOOL),
  TYPE_INT = (1 << FLAG_INT),
  TYPE_OBJECT = (1 << FLAG_OBJECT),
  TYPE_STRING = (1 << FLAG_STRING),
  TYPE_SYMBOL = (1 << FLAG_SYMBOL),
#ifdef QJS_BIGNUM_EXT
  TYPE_BIG_FLOAT = (1 << FLAG_BIG_FLOAT),
  TYPE_BIG_DECIMAL = (1 << FLAG_BIG_DECIMAL),
#endif
  TYPE_BIG_INT = (1 << FLAG_BIG_INT),
  TYPE_FLOAT64 = (1 << FLAG_FLOAT64),
  TYPE_NAN = (1 << FLAG_NAN),
#ifdef QJS_BIGNUM_EXT
  TYPE_BIGNUM = (TYPE_BIG_FLOAT | TYPE_BIG_DECIMAL | TYPE_BIG_INT),
#else
  TYPE_BIGNUM = (TYPE_BIG_INT),
#endif
  TYPE_NUMBER = (TYPE_INT | TYPE_BIGNUM | TYPE_FLOAT64 | TYPE_NAN),
  TYPE_PRIMITIVE = (TYPE_UNDEFINED | TYPE_NULL | TYPE_BOOL | TYPE_STRING | TYPE_SYMBOL | TYPE_NUMBER),
  TYPE_FUNCTION = (1 << FLAG_FUNCTION),
  TYPE_ARRAY = (1 << FLAG_ARRAY),
  TYPE_ALL = (TYPE_PRIMITIVE | TYPE_OBJECT | TYPE_FUNCTION | TYPE_ARRAY),
  TYPE_MODULE = (1 << FLAG_MODULE),
  TYPE_FUNCTION_BYTECODE = (1 << FLAG_FUNCTION_BYTECODE),
  TYPE_UNINITIALIZED = (1 << FLAG_UNINITIALIZED),
  TYPE_CATCH_OFFSET = (1 << FLAG_CATCH_OFFSET),
  TYPE_EXCEPTION = (1 << FLAG_EXCEPTION),
} ValueType;

static inline ValueTypeFlag
js_value_type_flag(JSValueConst value) {
  switch(JS_VALUE_GET_TAG(value)) {
#ifdef QJS_BIGNUM_EXT
    case JS_TAG_BIG_DECIMAL: return FLAG_BIG_DECIMAL;
    case JS_TAG_BIG_FLOAT: return FLAG_BIG_FLOAT;
#endif
    case JS_TAG_BIG_INT: return FLAG_BIG_INT;
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

static inline ValueTypeFlag
js_value_type_get(JSContext* ctx, JSValueConst value) {
  if(JS_IsArray(ctx, value))
    return FLAG_ARRAY;

  if(JS_IsFunction(ctx, value))
    return FLAG_FUNCTION;

  if(JS_VALUE_IS_NAN(value))
    return FLAG_NAN;

  return js_value_type_flag(value);
}

static inline ValueTypeFlag
js_value_type2flag(ValueType type) {
  ValueTypeFlag flag = 0;

  while((type >>= 1))
    ++flag;

  return flag;
}

ValueType js_value_type(JSContext*, JSValueConst);
const char* const* js_value_types(void);
const int js_value_types_length();
const char* js_value_typeof(JSValueConst);
const char* js_value_type_name(ValueType type);
const char* js_value_typestr(JSContext*, JSValueConst);

/* clang-format off */ 
void*        js_value_ptr(JSValueConst v);
JSValueConst js_value_mkptr(int tag, void* ptr);
JSValueConst js_value_mkobj(void*);
void*        js_value_obj(JSValueConst v);

static inline void*    js_value_obj2(JSContext* ctx, JSValueConst v) {
  return js_value_obj(JS_DupValue(ctx, v));
}

static inline JSValue  js_value_mkobj2(JSContext* ctx, void* obj)  {
  return obj? JS_DupValue(ctx, js_value_mkobj(obj)): JS_NULL;
}

static inline void  js_freeobj(JSContext*ctx, void*obj)  {
  if(obj) JS_FreeValue(ctx,  js_value_mkobj(obj));
}

static inline void  js_freeobj_rt(JSRuntime*rt, void*obj)  {
  if(obj) JS_FreeValueRT(rt,  js_value_mkobj(obj));
}
/* clang-format on */



int js_value_equals(JSContext*, JSValueConst a, JSValueConst b, BOOL deep);
JSValue js_value_clone(JSContext*, JSValueConst valpe);
JSValue* js_values_dup(JSContext*, int nvalues, JSValueConst* values);
void js_values_free(JSRuntime*, int nvalues, JSValueConst* values);
JSValue js_values_toarray(JSContext*, int nvalues, JSValueConst* values);
void js_value_dump(JSContext*, JSValueConst, DynBuf* db);
JSValue js_value_coerce(JSContext*, const char* func_name, JSValueConst);
JSValue js_global_new(JSContext*, const char* ctor_name, int argc, JSValueConst argv[]);

// #include "buffer-utils.h"

void js_cstring_dump(JSContext*, JSValueConst value, DynBuf* db);



#define js_cstring_destroy(ctx, cstr) \
  do { \
    if((cstr)) \
      JS_FreeCString((ctx), (cstr)); \
    (cstr) = 0; \
  } while(0)

static inline int32_t
js_toint32(JSContext* ctx, JSValueConst value) {
  int32_t ret = 0;
  JS_ToInt32(ctx, &ret, value);
  return ret;
}

static inline int32_t
js_toint32_free(JSContext* ctx, JSValue value) {
  int32_t ret = js_toint32(ctx, value);
  JS_FreeValue(ctx, value);
  return ret;
}

static inline BOOL
js_tobool_free(JSContext* ctx, JSValue value) {
  BOOL ret = JS_ToBool(ctx, value);
  JS_FreeValue(ctx, value);
  return ret;
}

static inline uint32_t
js_touint32(JSContext* ctx, JSValueConst value) {
  uint32_t ret = 0;
  JS_ToUint32(ctx, &ret, value);
  return ret;
}

static inline int64_t
js_toint64(JSContext* ctx, JSValueConst value) {
  int64_t ret = 0;
  JS_ToInt64Ext(ctx, &ret, value);
  return ret;
}


uint64_t js_touint64(JSContext*, JSValueConst);
int js_toint64clamp(JSContext*, int64_t*, JSValueConst, int64_t, int64_t, int64_t);
void* js_topointer(JSContext*, JSValueConst);
char* js_tostringlen(JSContext*, size_t* lenp, JSValueConst value);
char* js_tostring(JSContext*, JSValueConst value);

static inline char*
js_tostring_free(JSContext* ctx, JSValue value) {
  char* s = js_tostring(ctx, value);
  JS_FreeValue(ctx, value);
  return s;
}

wchar_t* js_towstringlen(JSContext*, size_t* lenp, JSValueConst value);
JSValue js_newpointer(JSContext*, void*);

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

static inline int32_t
js_value_toint32_free(JSContext* ctx, JSValueConst value) {
  int32_t ret = 0;
  JS_ToInt32(ctx, &ret, value);
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




void js_propertyenums_clear(JSContext* ctx, JSPropertyEnum* props, size_t len);

static inline void
js_propertyenums_free(JSContext* ctx, JSPropertyEnum* props, size_t len) {
  js_propertyenums_clear(ctx, props, len);
  js_free(ctx, props);
}


JSValue js_symbol_ctor(JSContext*);

JSValue js_symbol_invoke_static(JSContext*, const char* name, JSValueConst arg);

JSValue js_symbol_to_string(JSContext*, JSValueConst sym);

const char* js_symbol_to_cstring(JSContext*, JSValueConst sym);

JSValue js_symbol_static_value(JSContext*, const char* name);
JSAtom js_symbol_static_atom(JSContext*, const char* name);

BOOL js_is_primitive(JSValueConst obj);
BOOL js_is_iterable(JSContext*, JSValueConst obj);

JSValue js_iterator_method(JSContext*, JSValueConst obj);
JSValue js_iterator_new(JSContext*, JSValueConst obj);
JSValue js_iterator_next(JSContext*, JSValueConst obj, BOOL* done_p);
JSValue js_iterator_result(JSContext*, JSValue value, BOOL done);
JSValue js_iterator_then(JSContext*, BOOL done);
JSValue js_symbol_for(JSContext*, const char* sym_for);
JSValue js_symbol_keyfor(JSContext* ctx, JSValueConst sym);
JSAtom js_symbol_for_atom(JSContext*, const char* sym_for);

JSValue js_symbol_operatorset_value(JSContext*);

JSAtom js_symbol_operatorset_atom(JSContext*);

JSValue js_operators_create(JSContext*, JSValue* this_obj);

static inline int64_t
js_int64_default(JSContext* ctx, JSValueConst value, int64_t i) {
  if(JS_IsNumber(value))
    JS_ToInt64(ctx, &i, value);

  return i;
}

JSValue js_number_new(JSContext*, int32_t n);
BOOL js_number_integral(JSValueConst value);

static inline JSValue
js_new_bool_or_number(JSContext* ctx, int32_t n) {
  if(n == INT32_MAX)
    return JS_NewBool(ctx, FALSE);

  if(n == INT32_MIN)
    return JS_NewBool(ctx, TRUE);

  return js_number_new(ctx, n);
}

JSAtom js_atom_from(JSContext*, const char*);

static inline JSValue
js_atom_tovalue(JSContext* ctx, JSAtom atom) {
  if(JS_ATOM_ISINT(atom))
    return JS_MKVAL(JS_TAG_INT, JS_ATOM_TOINT(atom));

  return JS_AtomToValue(ctx, atom);
}

const char* js_atom_to_cstringlen(JSContext*, size_t* len, JSAtom atom);
BOOL js_atom_is_index(JSContext*, int64_t* pval, JSAtom atom);
BOOL js_atom_is_symbol(JSContext*, JSAtom atom);



static inline JSAtom
js_atom_from_integer(JSContext* ctx, int32_t i) {
  if(i >= 0)
    return JS_ATOM_FROMINT(i);

  JSValue val = JS_NewInt32(ctx, i);
  JSAtom ret = JS_ValueToAtom(ctx, val);
  JS_FreeValue(ctx, val);
  return ret;
}


char* js_atom_tostring(JSContext*, JSAtom atom);

/*static inline BOOL
js_atom_equal_string(JSContext* ctx, JSAtom atom, const char* other) {
  return js_atom_cmp_string(ctx, atom, other) == 0;
}*/

const char* js_object_tostring(JSContext*, JSValueConst value);
const char* js_object_tostring2(JSContext*, JSValueConst method, JSValueConst value);

typedef struct {
  JSContext* ctx;
  void* obj;
} JSTrampoline;

const char* js_function_name(JSContext*, JSValueConst value);


const char* js_function_tostring(JSContext*, JSValueConst value);
int js_function_argc(JSContext*, JSValueConst value);
JSValue js_function_bind_this(JSContext*, JSValue func, JSValue this_val);
JSValue js_function_throw(JSContext*, JSValue err);
JSValue js_function_return_undefined(JSContext*);
JSValue js_function_return_value(JSContext*, JSValue value);
JSValue js_function_prototype(JSContext*);



typedef JSValue CClosureFunc(JSContext*, JSValueConst, int, JSValueConst[], int, void*);
typedef void FinalizerFunc(JSRuntime*, void*);

JSValue js_function_cclosure(JSContext*, CClosureFunc*, int length, int magic, void*, FinalizerFunc* finalizer);

JSClassID js_object_classid(JSValueConst);
void* js_object_opaque(JSValueConst);
int js_object_refcount(JSValueConst);
JSValue js_object_constructor(JSContext*, JSValueConst value);
JSValue js_object_species(JSContext*, JSValueConst value);
char* js_object_classname(JSContext*, JSValueConst value);
BOOL js_object_equals(JSContext*, JSValueConst a, JSValueConst b, BOOL);
int js_object_is(JSContext*, JSValueConst value, const char* cmp);
JSValue js_object_new(JSContext*, const char* class_name, int argc, JSValueConst argv[]);
BOOL js_object_same2(JSContext*, JSValueConst, JSValueConst, BOOL);
JSAtom* js_object_properties(JSContext*, uint32_t* lenptr, JSValueConst obj, int flags);
int js_object_copy(JSContext*, JSValueConst dst, JSValueConst src);
int64_t js_object_keyof(JSContext*, JSValueConst obj, JSValueConst value);

enum {
  JS_GPN_RECURSIVE = (1 << 7),
};

static inline BOOL
js_object_same(JSValueConst a, JSValueConst b) {
  void *aobj, *bobj;

  if(!JS_IsObject(a) || !JS_IsObject(b))
    return FALSE;

  aobj = JS_VALUE_GET_PTR(a);
  bobj = JS_VALUE_GET_PTR(b);

  return aobj == bobj;
}

static inline BOOL
js_has_prototype(JSContext* ctx, JSValueConst obj, JSValueConst proto) {
  while(JS_IsObject(obj) && !JS_IsNull(obj) && JS_VALUE_GET_PTR(obj)) {

    if(((uintptr_t*)JS_VALUE_GET_PTR(obj))[3] == 0)
      break;

    JSValue val = JS_GetPrototype(ctx, obj);

    if(js_object_same(val, proto))
      return TRUE;

    obj = val;
  }

  return FALSE;
}

BOOL js_has_propertystr(JSContext*, JSValueConst obj, const char* str);
BOOL js_get_propertystr_bool(JSContext*, JSValueConst obj, const char* str);

static inline BOOL
js_has_property_value(JSContext* ctx, JSValueConst obj, JSValueConst prop) {
  JSAtom atom = JS_ValueToAtom(ctx, prop);
  BOOL ret = JS_HasProperty(ctx, obj, atom);
  JS_FreeAtom(ctx, atom);
  return ret;
}

static inline JSValue
js_get_property_value(JSContext* ctx, JSValueConst obj, JSValueConst prop) {
  JSAtom atom = JS_ValueToAtom(ctx, prop);
  JSValue ret = JS_GetProperty(ctx, obj, atom);
  JS_FreeAtom(ctx, atom);
  return ret;
}

static inline int
js_set_property_value(JSContext* ctx, JSValueConst obj, JSValueConst prop, JSValueConst value) {
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
js_delete_property_value(JSContext* ctx, JSValueConst obj, JSValueConst prop) {
  JSAtom atom = JS_ValueToAtom(ctx, prop);
  int ret = JS_DeleteProperty(ctx, obj, atom, 0);
  JS_FreeAtom(ctx, atom);
  return ret;
}

void js_set_propertyint_string(JSContext*, JSValueConst obj, uint32_t i, const char* str);
void js_set_propertystr_int(JSContext*, JSValueConst obj, const char* prop, int32_t value);
void js_set_propertystr_string(JSContext*, JSValueConst obj, const char* prop, const char* str);
void js_set_propertystr_stringlen(JSContext*, JSValueConst obj, const char* prop, const char* str, size_t len);
int32_t js_get_propertyint_int32(JSContext*, JSValueConst obj, uint32_t i);
int64_t js_get_propertyint_int64(JSContext*, JSValueConst obj, uint32_t i);
double js_get_propertyint_float64(JSContext*, JSValueConst obj, uint32_t i);
const char* js_get_propertystr_cstring(JSContext*, JSValueConst obj, const char* prop);
const char* js_get_propertystr_cstringlen(JSContext*, JSValueConst obj, const char* prop, size_t* lenp);
const char* js_get_property_cstring(JSContext*, JSValueConst obj, JSAtom prop);
char* js_get_property_string(JSContext*, JSValueConst obj, JSAtom prop);
char* js_get_propertystr_string(JSContext*, JSValueConst obj, const char* prop);
char* js_get_propertystr_stringlen(JSContext*, JSValueConst obj, const char* prop, size_t* lenp);
int32_t js_get_propertystr_int32(JSContext*, JSValueConst obj, const char* prop);
uint32_t js_get_propertystr_unt32(JSContext*, JSValueConst obj, const char* prop);
int64_t js_get_propertystr_int64(JSContext*, JSValueConst obj, const char* prop);
uint64_t js_get_propertystr_uint64(JSContext*, JSValueConst obj, const char* prop);
JSAtom js_get_propertystr_atom(JSContext*, JSValueConst obj, const char* prop);

static inline void
js_set_inspect_method(JSContext* ctx, JSValueConst obj, JSCFunction* func) {
  JSAtom inspect_symbol = js_symbol_for_atom(ctx, "quickjs.inspect.custom");
  JS_DefinePropertyValue(ctx, obj, inspect_symbol, JS_NewCFunction(ctx, func, "inspect", 1), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
  JS_FreeAtom(ctx, inspect_symbol);
}

JSValue js_get_tostringtag_value(JSContext*, JSValueConst obj);
void js_set_tostringtag_value(JSContext*, JSValueConst obj, JSValueConst value);
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

uint32_t js_class_count(JSRuntime*);
JSAtom js_class_atom(JSContext*, JSClassID id);
JSValue js_class_value(JSContext*, JSClassID id);
JSClassID js_class_id(JSContext*, JSClassID id);
JSClassID js_class_find(JSContext*, JSAtom);



static inline BOOL
js_is_object(JSContext* ctx, JSValueConst val) {
  return JS_IsObject(val) && !JS_IsNull(val);
}

BOOL js_is_arraybuffer(JSContext*, JSValueConst);
BOOL js_is_sharedarraybuffer(JSContext*, JSValueConst);
BOOL js_is_date(JSContext*, JSValueConst);
BOOL js_is_map(JSContext*, JSValueConst);
BOOL js_is_weakmap(JSContext*, JSValueConst);
BOOL js_is_set(JSContext*, JSValueConst);
BOOL js_is_generator(JSContext*, JSValueConst);
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
js_is_nullish(JSContext* ctx, JSValueConst value) {
  return JS_IsUndefined(value) || JS_IsNull(value);
}

JSValue js_typedarray_prototype(JSContext*);
JSValue js_typedarray_newv(JSContext*, int bits, BOOL floating, BOOL sign, int argc, JSValueConst argv[]);
JSValue js_typedarray_new(JSContext*, int bits, BOOL floating, BOOL sign, JSValue buffer);


static inline BOOL
js_is_typedarray(JSContext* ctx, JSValueConst value) {
  JSValue proto = js_typedarray_prototype(ctx);
  BOOL ret = js_has_prototype(ctx, value, proto);
  JS_FreeValue(ctx, proto);
  return ret;
}

int64_t js_array_length(JSContext*, JSValueConst array);

static inline BOOL
js_is_array(JSContext* ctx, JSValueConst value) {
  return JS_IsArray(ctx, value) || js_is_typedarray(ctx, value);
}


static inline BOOL
js_is_bignumber(JSContext* ctx, JSValueConst value) {
#ifdef QJS_BIGNUM_EXT
  if(JS_IsBigDecimal(value) || JS_IsBigFloat(value))
    return TRUE;
#endif
  return JS_IsBigInt(ctx, value);
}

static inline BOOL
js_is_numeric(JSContext* ctx, JSValueConst value) {
  return JS_IsNumber(value) || js_is_bignumber(ctx, value);
}

int js_propenum_cmp(const void* a, const void* b, void* ptr);
int64_t js_array_clear(JSContext*, JSValueConst array);

size_t js_strv_length(char** strv);

char** js_strv_dup(JSContext*, char** strv);

void js_strv_free(JSContext*, char** strv);
void js_strv_free_rt(JSRuntime*, char** strv);
JSValue js_strv_to_array(JSContext*, char** strv);

int32_t* js_argv_to_int32v(JSContext*, int argc, JSValueConst argv[]);
JSAtom* js_argv_to_atoms(JSContext*, int argc, JSValueConst argv[]);

JSValue js_intv_to_array(JSContext*, int const*, size_t);

char** js_array_to_argv(JSContext*, size_t*, JSValueConst);
int32_t* js_array_to_int32v(JSContext*, size_t*, JSValueConst);
int64_t* js_array_to_int64v(JSContext*, size_t*, JSValueConst);


int js_array_copys(JSContext*, JSValueConst, int n, char** stra);

JSValue js_invoke(JSContext*, JSValueConst this_obj, const char* method, int argc, JSValueConst argv[]);

JSValue js_to_source(JSContext*, JSValueConst this_obj);
char* js_tosource(JSContext*, JSValueConst value);


int64_t js_arraybuffer_bytelength(JSContext*, JSValueConst value);



JSValue js_date_new(JSContext*, JSValue arg);
JSValue js_date_from_ms(JSContext*, int64_t ms);
JSValue js_date_from_time_ns(JSContext*, time_t t, long ns);
int64_t js_date_gettime(JSContext*, JSValue arg);
int64_t js_date_time(JSContext*, JSValue arg);
struct timespec js_date_timespec(JSContext*, JSValue arg);

void js_arraybuffer_freevalue(JSRuntime*, void* opaque, void* ptr);
JSValue js_arraybuffer_fromvalue(JSContext*, void* x, size_t n, JSValueConst val);
void js_arraybuffer_freeptr(JSRuntime*, void* opaque, void* ptr);
JSValue js_arraybuffer_mmap(JSContext*, const char*, BOOL);




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

const char* module_namecstr(JSContext*, JSModuleDef* m);
JSValue module_value(JSContext*, JSModuleDef*);

JSModuleDef* js_module_def(JSContext*, JSValueConst value);

JSValue js_eval_module(JSContext*, JSValueConst, BOOL);
JSValue js_eval_binary(JSContext*, const uint8_t*, size_t, BOOL load_only);
JSValue js_eval_buf(JSContext*, const void*, size_t, const char* filename, int eval_flags);
JSValue js_eval_this_buf(JSContext*, JSValueConst, const void*, size_t, const char* filename, int eval_flags);
JSValue js_eval_file(JSContext*, const char*, int);
JSValue js_eval_this_file(JSContext*, JSValueConst, const char*, int);
int js_eval_str(JSContext*, const char*, const char*, int flags);
JSValue js_eval_fmt(JSContext*, int flags, const char* fmt, ...) FORMAT_STRING(3, 4);



/*JSWorkerMessagePipe* js_new_message_pipe(void);
JSWorkerMessagePipe* js_dup_message_pipe(JSWorkerMessagePipe*);*/

/*void js_free_message(JSWorkerMessage*);
void js_free_message_pipe(JSWorkerMessagePipe*);*/

void js_error_dump(JSContext*, JSValueConst, DynBuf* db);
char* js_error_tostring(JSContext*, JSValueConst);
void js_error_print(JSContext*, JSValueConst);
JSValue js_error_stack(JSContext*);
/*JSValue js_error_uncatchable(JSContext*);*/

JSValue js_iohandler_fn(JSContext*, BOOL write, const char* global_obj);
BOOL js_iohandler_set(JSContext*, JSValueConst set_handler, int fd, JSValue handler);

JSValue js_promise_immediate(JSContext*, BOOL reject, JSValueConst promise);
JSValue js_promise_resolve(JSContext*, JSValueConst promise);
JSValue js_promise_then(JSContext*, JSValueConst promise, JSValueConst func);

static inline JSValue
js_promise_resolve_then(JSContext* ctx, JSValueConst promise, JSValueConst func) {
  JSValue tmp, ret;
  tmp = js_promise_resolve(ctx, promise);
  ret = js_promise_then(ctx, tmp, func);
  JS_FreeValue(ctx, tmp);
  return ret;
}





JSValue js_generator_prototype(JSContext*);

static inline JSValue
js_generator_constructor(JSContext* ctx) {
  JSValue proto = js_generator_prototype(ctx);
  JSValue ret = js_object_constructor(ctx, proto);
  JS_FreeValue(ctx, proto);
  return ret;
}


JSValue js_std_file(JSContext*, FILE* f);

struct OffsetLength;
union IndexRange;

int js_offset_length(JSContext*, int64_t, int, JSValueConst[], int, struct OffsetLength*);
int js_index_range(JSContext*, int64_t, int, JSValueConst[], int, union IndexRange*);

/**
 * @}
 */

#endif /* defined(UTILS_H) */
