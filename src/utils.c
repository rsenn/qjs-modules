#undef _ISOC99_SOURCE
#define _ISOC99_SOURCE 1
#include "utils.h"
#include "defines.h"
#include <list.h>
#include <cutils.h>
#include "vector.h"
#include <libregexp.h>
#include "quickjs-internal.h"
#include "buffer-utils.h"
#include <time.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <quickjs-libc.h>
#include "debug.h"

#if defined(__EMSCRIPTEN__) && defined(__GNUC__)
#define atomic_add_int __sync_add_and_fetch
#endif

/**
 * \addtogroup utils
 * @{
 */
void quicksort_r(void*, size_t, size_t, int (*)(const void*, const void*, void*), void*);
int strverscmp(const char*, const char*);

#ifndef INFINITY
#define INFINITY __builtin_inf()
#endif

#ifdef USE_WORKER
#include <pthread.h>
#include <stdatomic.h>

static int
atomic_add_int(int* ptr, int v) {
  return atomic_fetch_add((_Atomic(uint32_t)*)ptr, v) + v;
}
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
  gettimeofday(&tv, 0);
  return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}
#endif

js_realloc_helper(utils_js_realloc);
js_realloc_rt_helper(utils_js_realloc_rt);

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
regexp_from_string(char* str, int flags) {
  RegExp re = {str, strlen(str), flags};
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

BOOL
regexp_match(const uint8_t* bc, const void* cbuf, size_t clen, JSContext* ctx) {
  uint8_t* capture[512];
  BOOL ret = FALSE;
  switch(lre_exec(capture, bc, cbuf, 0, clen, 0, ctx)) {
    case 1: ret = TRUE; break;
    case -1: fprintf(stderr, "regexp_match ERROR\n"); break;
    case 0: break;
  }
  return ret;
}

JSValue
regexp_to_value(RegExp re, JSContext* ctx) {
  char flagstr[32] = {0};
  size_t flaglen = regexp_flags_tostring(re.flags, flagstr);
  JSValueConst args[2] = {JS_NewStringLen(ctx, re.source, re.len), JS_NewStringLen(ctx, flagstr, flaglen)};
  JSValue regex, ctor = js_global_get_str(ctx, "RegExp");
  regex = JS_CallConstructor(ctx, ctor, 2, args);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  return regex;
}

void
regexp_free_rt(RegExp re, JSRuntime* rt) {
  js_free_rt(rt, re.source);
}

int64_t
js_array_length(JSContext* ctx, JSValueConst array) {
  int64_t len = -1;
  /*if(js_is_array(ctx, array) || js_is_typedarray(array)|| js_is_array_like(ctx, array))*/ {
    JSValue length = JS_GetPropertyStr(ctx, array, "length");
    if(JS_IsNumber(length))
      JS_ToInt64(ctx, &len, length);
    JS_FreeValue(ctx, length);
  }
  return len;
}

void
js_array_clear(JSContext* ctx, JSValueConst array) {
  int64_t len = js_array_length(ctx, array);
  JSValue ret;
  JSAtom splice = JS_NewAtom(ctx, "splice");
  JSValueConst args[2] = {JS_NewInt64(ctx, 0), JS_NewInt64(ctx, len)};

  ret = JS_Invoke(ctx, array, splice, 2, args);
  JS_FreeAtom(ctx, splice);
  JS_FreeValue(ctx, ret);

  assert(js_array_length(ctx, array) == 0);
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

int
js_array_copys(JSContext* ctx, JSValueConst array, int n, char** stra) {
  int i, len = MIN_NUM(n, js_array_length(ctx, array));
  for(i = 0; i < len; i++) {
    JSValue item = JS_GetPropertyUint32(ctx, array, i);
    if(stra[i])
      js_free(ctx, stra[i]);
    stra[i] = js_tostring(ctx, item);
    JS_FreeValue(ctx, item);
  }
  return i;
}

int
js_strv_copys(JSContext* ctx, int argc, JSValueConst argv[], int n, char** stra) {
  int i, len = MIN_NUM(n, argc);
  for(i = 0; i < len; i++) {
    if(!JS_IsNull(argv[i]) && !JS_IsUndefined(argv[i]))
      stra[i] = js_tostring(ctx, argv[i]);
    else
      stra[i] = 0;
  }
  for(; i < n; i++) stra[i] = 0;

  return i;
}

JSAtom
js_atom_from(JSContext* ctx, const char* str) {
  if(str[0] == '[') {
    JSValue obj, val = JS_UNDEFINED;
    JSAtom prop, ret;
    size_t objlen, proplen;
    objlen = str_chr(&str[1], '.');
    obj = js_global_get_str_n(ctx, &str[1], objlen);
    proplen = str_chr(&str[1 + objlen + 1], ']');
    prop = JS_NewAtomLen(ctx, &str[1 + objlen + 1], proplen);
    val = JS_GetProperty(ctx, obj, prop);
    JS_FreeAtom(ctx, prop);
    ret = JS_ValueToAtom(ctx, val);
    JS_FreeValue(ctx, val);
    return ret;
  }

  return JS_NewAtom(ctx, str);
}

void
js_atom_dump(JSContext* ctx, JSAtom atom, DynBuf* db, BOOL color) {
  const char* str;
  BOOL is_int;
  str = JS_AtomToCString(ctx, atom);
  is_int = js_atom_isint(atom) || is_integer(str);
  if(color)
    dbuf_putstr(db, is_int ? COLOR_BROWN : COLOR_GRAY);

  dbuf_putstr(db, str);
  if(color)
    dbuf_putstr(db, COLOR_CYAN);

  if(!is_int)
    dbuf_printf(db, "(0x%x)", js_atom_tobinary(atom));

  if(color)
    dbuf_putstr(db, COLOR_NONE);
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
js_atom_is_string(JSContext* ctx, JSAtom atom, const char* other) {
  const char* str = JS_AtomToCString(ctx, atom);
  BOOL ret = !strcmp(str, other);
  JS_FreeCString(ctx, str);
  return ret;
}

BOOL
js_atom_is_length(JSContext* ctx, JSAtom atom) {
  return js_atom_is_string(ctx, atom, "length");
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

int
js_function_argc(JSContext* ctx, JSValueConst value) {
  return js_get_propertystr_int32(ctx, value, "length");
}

JSCFunction*
js_function_cfunc(JSContext* ctx, JSValueConst value) {
  if(js_value_isclass(ctx, value, JS_CLASS_C_FUNCTION)) {
    JSObject* obj = JS_VALUE_GET_OBJ(value);
    return obj->u.cfunc.c_function.generic;
  }
  return 0;
}

static JSValue
js_function_bound(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue* func_data) {
  int i = 0, j = 0, k = ABS_NUM(magic), l = SIGN_NUM(magic);
  JSValue args[argc + k];

  for(i = 0; i < magic; i++) args[i] = func_data[i + 1];
  for(j = 0; j < argc; j++) args[i++] = argv[j];

  return JS_Call(ctx, func_data[0], l ? args[0] : this_val, i, args + l);
}

JSValue
js_function_bind(JSContext* ctx, JSValueConst func, int argc, JSValueConst argv[]) {
  JSValue data[argc + 1];
  int i;
  data[0] = JS_DupValue(ctx, func);
  for(i = 0; i < argc; i++) data[i + 1] = JS_DupValue(ctx, argv[i]);
  return JS_NewCFunctionData(ctx, js_function_bound, 0, argc, argc + 1, data);
}

static JSValue
js_function_bound_this(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue func_data[]) {
  return JS_Call(ctx, func_data[0], func_data[1], argc, argv);
}

JSValue
js_function_bind_this(JSContext* ctx, JSValueConst func, JSValueConst this_val) {
  JSValue data[2];
  data[0] = JS_DupValue(ctx, func);
  data[1] = JS_DupValue(ctx, this_val);
  return JS_NewCFunctionData(ctx, js_function_bound_this, js_function_argc(ctx, func), 0, 2, data);
}

static JSValue
js_function_throw_fn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValueConst data[]) {
  if(!JS_IsUndefined(data[0]))
    return JS_Throw(ctx, data[0]);

  return JS_DupValue(ctx, argc >= 1 ? argv[0] : JS_UNDEFINED);
}

JSValue
js_function_throw(JSContext* ctx, JSValueConst err) {
  JSValueConst data[1];
  data[0] = JS_DupValue(ctx, err);
  return JS_NewCFunctionData(ctx, js_function_throw_fn, 0, 0, 1, data);
}

static JSValue
js_function_return_value_fn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValueConst data[]) {
  return data[0];
}

JSValue
js_function_return_undefined(JSContext* ctx) {
  JSValue data[1];
  data[0] = JS_UNDEFINED;
  return JS_NewCFunctionData(ctx, js_function_return_value_fn, 0, 0, 1, data);
}

JSValue
js_function_return_value(JSContext* ctx, JSValueConst value) {
  JSValue data[1];
  data[0] = JS_DupValue(ctx, value);
  return JS_NewCFunctionData(ctx, js_function_return_value_fn, 0, 0, 1, data);
}

JSValue
js_global_get_str(JSContext* ctx, const char* prop) {
  JSValue global_obj, ret;
  global_obj = JS_GetGlobalObject(ctx);
  ret = JS_GetPropertyStr(ctx, global_obj, prop);
  JS_FreeValue(ctx, global_obj);
  return ret;
}

JSValue
js_global_get_str_n(JSContext* ctx, const char* prop, size_t len) {
  JSValue ret;
  JSAtom atom = JS_NewAtomLen(ctx, prop, len);
  ret = js_global_get_atom(ctx, atom);
  JS_FreeAtom(ctx, atom);
  return ret;
}

JSValue
js_global_get_atom(JSContext* ctx, JSAtom prop) {
  JSValue global_obj, ret;
  global_obj = JS_GetGlobalObject(ctx);
  ret = JS_GetProperty(ctx, global_obj, prop);
  JS_FreeValue(ctx, global_obj);
  return ret;
}

JSValue
js_global_prototype(JSContext* ctx, const char* class_name) {
  JSValue ctor, ret;
  ctor = js_global_get_str(ctx, class_name);
  ret = JS_GetPropertyStr(ctx, ctor, "prototype");
  JS_FreeValue(ctx, ctor);
  return ret;
}

JSValue
js_global_static_func(JSContext* ctx, const char* class_name, const char* func_name) {
  JSValue ctor, func;
  ctor = js_global_get_str(ctx, class_name);
  func = JS_GetPropertyStr(ctx, ctor, func_name);
  JS_FreeValue(ctx, ctor);
  return func;
}

JSValue
js_global_prototype_func(JSContext* ctx, const char* class_name, const char* func_name) {
  JSValue proto, func;
  proto = js_global_prototype(ctx, class_name);
  func = JS_GetPropertyStr(ctx, proto, func_name);
  JS_FreeValue(ctx, proto);
  return func;
}

JSValue
js_iterator_method(JSContext* ctx, JSValueConst obj) {
  JSAtom atom;
  JSValue ret = JS_UNDEFINED;
  atom = js_symbol_static_atom(ctx, "asyncIterator");
  if(JS_HasProperty(ctx, obj, atom))
    ret = JS_GetProperty(ctx, obj, atom);

  JS_FreeAtom(ctx, atom);
  if(!JS_IsFunction(ctx, ret)) {
    atom = js_symbol_static_atom(ctx, "iterator");
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
js_iterator_result(JSContext* ctx, JSValueConst value, BOOL done) {
  JSValue ret = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, ret, "done", JS_NewBool(ctx, done));
  JS_SetPropertyStr(ctx, ret, "value", JS_DupValue(ctx, value));

  return ret;
}

static JSValue
js_iterator_then_fn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValueConst data[]) {
  JSValue ret = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, ret, "done", JS_DupValue(ctx, data[0]));
  if(argc >= 1)
    JS_SetPropertyStr(ctx, ret, "value", JS_DupValue(ctx, argv[0]));

  return ret;
}

JSValue
js_iterator_then(JSContext* ctx, BOOL done) {
  JSValue ret;
  JSValueConst data[1] = {JS_NewBool(ctx, done)};

  return JS_NewCFunctionData(ctx, js_iterator_then_fn, 1, 0, 1, data);
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

  quicksort_r(&atoms_a, natoms_a, sizeof(JSPropertyEnum), &js_propenum_cmp, ctx);
  quicksort_r(&atoms_b, natoms_b, sizeof(JSPropertyEnum), &js_propenum_cmp, ctx);
  for(i = 0; i < natoms_a; i++)
    if(atoms_a[i].atom != atoms_b[i].atom)
      return FALSE;
  return TRUE;
}

int
js_object_is(JSContext* ctx, JSValueConst value, const char* cmp) {
  BOOL ret = FALSE;
  const char* str;
  if((str = js_object_tostring(ctx, value))) {
    ret = strcmp(str, cmp) == 0;
    js_cstring_free(ctx, str);
  }
  return ret;
}

JSValue
js_object_construct(JSContext* ctx, JSValueConst ctor) {
  JSValueConst args[] = {JS_UNDEFINED};
  return JS_CallConstructor(ctx, ctor, 0, args);
}

JSValue
js_object_error(JSContext* ctx, const char* message) {
  JSValueConst args[] = {JS_NewString(ctx, message)};
  JSValue ret = js_object_new(ctx, "Error", 1, args);
  JS_FreeValue(ctx, args[0]);
  return ret;
}

JSValue
js_object_new(JSContext* ctx, const char* class_name, int argc, JSValueConst argv[]) {
  JSValue ctor = js_global_get_str(ctx, class_name);
  JSValue obj = JS_CallConstructor(ctx, ctor, argc, argv);
  JS_FreeValue(ctx, ctor);
  return obj;
}

JSValue
js_object_function(JSContext* ctx, const char* func_name, JSValueConst obj) {
  JSValue ret, ctor = js_global_get_str(ctx, "Object");
  ret = js_invoke(ctx, ctor, func_name, 1, &obj);
  JS_FreeValue(ctx, ctor);
  return ret;
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

JSValue
js_get_propertyatom_value(JSContext* ctx, JSValueConst obj, JSAtom prop) {
  return JS_GetPropertyInternal(ctx, obj, prop, JS_NULL, FALSE);
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

int32_t
js_get_propertyint_int32(JSContext* ctx, JSValueConst obj, uint32_t prop) {
  int32_t ret;
  JSValue value = JS_GetPropertyUint32(ctx, obj, prop);
  JS_ToInt32(ctx, &ret, value);
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

JSAtom
js_get_propertystr_atom(JSContext* ctx, JSValueConst obj, const char* prop) {
  JSValue value;
  JSAtom ret;
  value = JS_GetPropertyStr(ctx, obj, prop);
  if(JS_IsUndefined(value) || JS_IsException(value))
    return 0;
  ret = JS_ValueToAtom(ctx, value);
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

JSClassID
js_class_find(JSContext* ctx, const char* name) {
  JSAtom atom = JS_NewAtom(ctx, name);
  JSRuntime* rt = ctx->rt;
  int i, n = rt->class_count;
  for(i = 0; i < n; i++)
    if(rt->class_array[i].class_name == atom)
      return i;

  return -1;
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
  static thread_local JSValue method;

  if(JS_VALUE_GET_TAG(method) == 0)
    method = js_global_prototype_func(ctx, "Object", "toString");

  return js_object_tostring2(ctx, method, value);
}

const char*
js_object_tostring2(JSContext* ctx, JSValueConst method, JSValueConst value) {
  JSValue str = JS_Call(ctx, method, value, 0, 0);
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
js_strv_free_n(JSContext* ctx, int n, char* argv[]) {
  int i;
  for(i = 0; i < n; i++) {
    if(argv[i]) {
      js_free(ctx, argv[i]);
      argv[i] = 0;
    }
  }
}

void
js_strv_free(JSContext* ctx, char** strv) {
  size_t i;
  if(strv == 0)
    return;

  for(i = 0; strv[i]; i++) { js_free(ctx, strv[i]); }
  js_free(ctx, strv);
}

void
js_strv_free_rt(JSRuntime* rt, char** strv) {
  size_t i;
  if(strv == 0)
    return;

  for(i = 0; strv[i]; i++) { js_free_rt(rt, strv[i]); }
  js_free_rt(rt, strv);
}

JSValue
js_strv_to_array(JSContext* ctx, char** strv) {
  JSValue ret = JS_NewArray(ctx);
  if(strv) {
    size_t i;
    for(i = 0; strv[i]; i++) JS_SetPropertyUint32(ctx, ret, i, JS_NewString(ctx, strv[i]));
  }
  return ret;
}

size_t
js_strv_length(char** strv) {
  size_t i;
  for(i = 0; strv[i]; i++) {}
  return i;
}

char**
js_strv_dup(JSContext* ctx, char** strv) {
  char** ret;
  size_t i, len = js_strv_length(strv);
  ret = js_malloc(ctx, (len + 1) * sizeof(char*));
  for(i = 0; i < len; i++) { ret[i] = js_strdup(ctx, strv[i]); }
  ret[i] = 0;
  return ret;
}

JSValue
js_intv_to_array(JSContext* ctx, int* intv, size_t len) {
  JSValue ret = JS_NewArray(ctx);
  if(intv) {
    size_t i;
    for(i = 0; i < len; i++) JS_SetPropertyUint32(ctx, ret, i, JS_NewInt32(ctx, intv[i]));
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
  return js_global_get_str(ctx, "Symbol");
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

JSValue*
js_values_fromarray(JSContext* ctx, size_t* nvalues_p, JSValueConst arr) {
  size_t i, len = js_array_length(ctx, arr);
  JSValue* ret = js_mallocz(ctx, sizeof(JSValueConst) * len);

  if(nvalues_p)
    *nvalues_p = len;

  for(i = 0; i < len; i++) { ret[i] = JS_GetPropertyUint32(ctx, arr, i); }
  return ret;
}

const char*
js_value_type_name(int32_t type) {
  int32_t flag = js_value_type2flag(type);
  const char* const types[] = {
      "undefined",     "null",         "bool",      "int", "object",   "string", "symbol", "big_float",
      "big_int",       "big_decimal",  "float64",   "nan", "function", "array",  "module", "function_bytecode",
      "uninitialized", "catch_offset", "exception",
  };
  if(flag >= 0 && flag < countof(types))
    return types[flag];
  return 0;
}

const char*
js_value_typestr(JSContext* ctx, JSValueConst value) {
  int32_t type = js_value_type(ctx, value);
  return js_value_type_name(type);
}

BOOL
js_value_has_ref_count(JSValue v) {
  return ((unsigned)js_value_tag(v) >= (unsigned)JS_TAG_FIRST);
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

  if(JS_IsFunction(ctx, value) && JS_GetClassID(value) <= JS_CLASS_ASYNC_GENERATOR)
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
    JSRefCountHeader* p = (JSRefCountHeader*)js_value_ptr(v);
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
      if(!JS_GetOwnPropertyNames(ctx, &tab_atom, &tab_atom_len, value, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY)) {
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
js_value_fwrite(JSContext* ctx, JSValueConst val, FILE* f) {
  DynBuf dbuf = {0};
  size_t n;
  dbuf_init2(&dbuf, ctx, (realloc_func*)&utils_js_realloc);
  js_value_dump(ctx, val, &dbuf);
  dbuf_putc(&dbuf, '\n');
  n = dbuf.size;
  dbuf_0(&dbuf);
  fwrite(dbuf.buf, 1, n, f);
  fflush(f);
  dbuf_free(&dbuf);
}

void
js_value_dump(JSContext* ctx, JSValueConst value, DynBuf* db) {
  const char* str;
  size_t len;
  dbuf_putstr(db, js_value_typestr(ctx, value));
  dbuf_putstr(db, " ");
  if(JS_IsException(value)) {
    dbuf_putstr(db, "[exception]");
  } else if(JS_IsFunction(ctx, value)) {
    JSValue src = js_invoke(ctx, value, "toSource", 0, 0);
    js_value_dump(ctx, src, db);
    JS_FreeValue(ctx, src);
  } else if(JS_IsObject(value)) {
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
  dbuf_init2(&dbuf, ctx, (realloc_func*)&utils_js_realloc);
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
    JSRefCountHeader* p = (JSRefCountHeader*)js_value_ptr(v);
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

size_t
js_cstring_len(JSValueConst v) {
  JSString* p;

  if(JS_IsString(v)) {
    p = JS_VALUE_GET_PTR(v);
    return p->len;
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

JSValue
js_map_new(JSContext* ctx, JSValueConst entries) {
  JSValue map, ctor = js_global_get_str(ctx, "Map");
  map = JS_CallConstructor(ctx, ctor, 1, &entries);
  JS_FreeValue(ctx, ctor);
  return map;
}

JSValue
module_name(JSContext* ctx, JSModuleDef* m) {
  if(m->module_name < ctx->rt->atom_count)
    return JS_AtomToValue(ctx, m->module_name);

  return JS_UNDEFINED;
}

char*
module_namestr(JSContext* ctx, JSModuleDef* m) {
  const char* name = JS_AtomToCString(ctx, m->module_name);
  char* str = js_strdup(ctx, name);
  JS_FreeCString(ctx, name);
  return str;
}

static JSValue
call_module_func(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) {
  union {
    JSModuleInitFunc* init_func;
    int32_t i[2];
  } u;

  u.i[0] = JS_VALUE_GET_INT(data[0]);
  u.i[1] = JS_VALUE_GET_INT(data[1]);

  if(argc >= 1 && JS_IsModule(argv[0]))
    return JS_NewInt32(ctx, u.init_func(ctx, JS_VALUE_GET_PTR(argv[0])));

  return JS_ThrowTypeError(ctx, "argument 1 module expected");
}

JSValue
module_func(JSContext* ctx, JSModuleDef* m) {
  JSValue func = JS_UNDEFINED;
  if(JS_IsFunction(ctx, m->func_obj)) {
    func = JS_DupValue(ctx, m->func_obj);
  } else if(m->init_func) {
    union {
      JSModuleInitFunc* init_func;
      int32_t i[2];
    } u = {m->init_func};
    JSValueConst data[2] = {
        JS_MKVAL(JS_TAG_INT, u.i[0]),
        JS_MKVAL(JS_TAG_INT, u.i[1]),
    };
    func = JS_NewCFunctionData(ctx, call_module_func, 1, 0, 2, data);
  }
  return func;
}

JSValue
module_ns(JSContext* ctx, JSModuleDef* m) {
  return JS_DupValue(ctx, m->module_ns);
}

JSValue
module_exports_find(JSContext* ctx, JSModuleDef* m, JSAtom atom) {
  size_t i;
  for(i = 0; i < m->export_entries_count; i++) {
    JSExportEntry* entry = &m->export_entries[i];

    if(entry->export_name == atom) {
      JSVarRef* ref = entry->u.local.var_ref;
      JSValue export = ref ? JS_DupValue(ctx, ref->pvalue ? *ref->pvalue : ref->value) : JS_UNDEFINED;
      return export;
    }
  }
  return JS_UNDEFINED;
}

void
module_exports_get(JSContext* ctx, JSModuleDef* m, BOOL rename_default, JSValueConst exports) {
  JSAtom def = JS_NewAtom(ctx, "default");

  size_t i;
  for(i = 0; i < m->export_entries_count; i++) {
    JSExportEntry* entry = &m->export_entries[i];
    JSVarRef* ref = entry->u.local.var_ref;
    JSValue val = JS_UNDEFINED;
    JSAtom name = entry->export_name;

    if(ref) {
      val = JS_DupValue(ctx, ref->pvalue ? *ref->pvalue : ref->value);
      if(rename_default && name == def)
        name = m->module_name;
    }
    JS_SetProperty(ctx, exports, name, val);
  }
  JS_FreeAtom(ctx, def);
}

JSValue
module_default_export(JSContext* ctx, JSModuleDef* m) {
  JSAtom def = JS_NewAtom(ctx, "default");
  JSValue ret = JS_UNDEFINED;
  size_t i;

  for(i = 0; i < m->export_entries_count; i++) {
    JSExportEntry* entry = &m->export_entries[i];
    JSVarRef* ref = entry->u.local.var_ref;
    JSAtom name = entry->export_name;

    if(ref) {
      if(name == def) {
        ret = JS_DupValue(ctx, ref->pvalue ? *ref->pvalue : ref->value);
        break;
      }
    }
  }
  JS_FreeAtom(ctx, def);
  return ret;
}

JSValue
module_exports(JSContext* ctx, JSModuleDef* m) {
  JSValue exports;
  exports = JS_NewObject /*Proto*/ (ctx /*, JS_NULL*/);
  module_exports_get(ctx, m, FALSE, exports);
  return exports;
}

struct list_head*
js_modules_list(JSContext* ctx) {
  return &ctx->loaded_modules;
}

JSValue
js_modules_array(JSContext* ctx, JSValueConst this_val, int magic) {
  struct list_head* el;
  JSValue ret = JS_NewArray(ctx);
  uint32_t i = 0;
  list_for_each(el, &ctx->loaded_modules) {
    JSModuleDef* m = list_entry(el, JSModuleDef, link);
    char* str = module_namestr(ctx, m);
    JSValue entry = magic ? module_entry(ctx, m) : module_value(ctx, m);
    if(1 /*str[0] != '<'*/)
      JS_SetPropertyUint32(ctx, ret, i++, entry);
    else
      JS_FreeValue(ctx, entry);
    js_free(ctx, str);
  }
  return ret;
}

JSValue
js_modules_entries(JSContext* ctx, JSValueConst this_val, int magic) {
  struct list_head* el;
  JSValue ret = JS_NewArray(ctx);
  uint32_t i = 0;
  list_for_each(el, &ctx->loaded_modules) {
    JSModuleDef* m = list_entry(el, JSModuleDef, link);
    //  char* name = module_namestr(ctx, m);
    JSValue entry = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, entry, 0, JS_AtomToValue(ctx, m->module_name));
    JS_SetPropertyUint32(ctx, entry, 1, magic ? module_entry(ctx, m) : module_value(ctx, m));
    if(1 /*str[0] != '<'*/)
      JS_SetPropertyUint32(ctx, ret, i++, entry);
    else
      JS_FreeValue(ctx, entry);
    //  js_free(ctx, name);
  }
  return ret;
}

JSValue
js_modules_map(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue map, entries = js_modules_entries(ctx, this_val, magic);
  map = js_map_new(ctx, entries);
  JS_FreeValue(ctx, entries);
  return map;
}

JSValue
js_modules_object(JSContext* ctx, JSValueConst this_val, int magic) {
  struct list_head* it;
  JSValue obj = JS_NewObject(ctx);
  list_for_each(it, &ctx->loaded_modules) {
    JSModuleDef* m = list_entry(it, JSModuleDef, link);
    char* name = module_namestr(ctx, m);
    JSValue entry = magic ? module_entry(ctx, m) : module_value(ctx, m);
    if(1 /*str[0] != '<'*/)
      JS_SetPropertyStr(ctx, obj, basename(name), entry);
    else
      JS_FreeValue(ctx, entry);
    js_free(ctx, name);
  }
  return obj;
}

JSValue
module_value(JSContext* ctx, JSModuleDef* m) {
  return JS_DupValue(ctx, JS_MKPTR(JS_TAG_MODULE, m));
}

JSValue
module_entry(JSContext* ctx, JSModuleDef* m) {
  JSValue entry = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, entry, 0, module_ns(ctx, m));
  JS_SetPropertyUint32(ctx, entry, 1, module_exports(ctx, m));
  JS_SetPropertyUint32(ctx, entry, 2, module_func(ctx, m));
  return entry;
}

JSValue
module_object(JSContext* ctx, JSModuleDef* m) {
  JSValue ns, exports, func, obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, obj, "name", module_name(ctx, m));
  JS_SetPropertyStr(ctx, obj, "resolved", JS_NewBool(ctx, m->resolved));
  JS_SetPropertyStr(ctx, obj, "func_created", JS_NewBool(ctx, m->func_created));
  JS_SetPropertyStr(ctx, obj, "instantiated", JS_NewBool(ctx, m->instantiated));
  JS_SetPropertyStr(ctx, obj, "evaluated", JS_NewBool(ctx, m->evaluated));

  ns = module_ns(ctx, m);
  if(!JS_IsUndefined(ns))
    JS_SetPropertyStr(ctx, obj, "ns", ns);
  exports = module_exports(ctx, m);
  if(!JS_IsUndefined(exports))
    JS_SetPropertyStr(ctx, obj, "exports", module_exports(ctx, m));
  func = module_func(ctx, m);
  if(!JS_IsUndefined(func))
    JS_SetPropertyStr(ctx, obj, "func", func);
  return obj;
}

JSModuleDef*
js_module_def(JSContext* ctx, JSValueConst value) {
  JSModuleDef* m = 0;
  if(JS_IsString(value)) {
    const char* name = JS_ToCString(ctx, value);
    m = js_module_find(ctx, name);
    JS_FreeCString(ctx, name);
  } else if(JS_VALUE_GET_TAG(value) == JS_TAG_MODULE) {
    m = JS_VALUE_GET_PTR(value);
  }
  return m;
}

JSModuleDef*
js_module_find_fwd(JSContext* ctx, const char* name) {
  struct list_head* el;
  size_t namelen = strlen(name);
  list_for_each(el, &ctx->loaded_modules) {
    JSModuleDef* m = list_entry(el, JSModuleDef, link);
    char *n, *str = module_namestr(ctx, m);
    size_t len;
    n = basename(str);
    len = str_rchr(n, '.');
    if(!strcmp(str, name) || !strcmp(n, name) || (len == namelen && !strncmp(n, name, len)))
      return m;
    js_free(ctx, str);
  }
  return 0;
}

JSModuleDef*
js_module_find_rev(JSContext* ctx, const char* name) {
  struct list_head* el;
  size_t namelen = strlen(name);
  list_for_each_prev(el, &ctx->loaded_modules) {
    JSModuleDef* m = list_entry(el, JSModuleDef, link);
    char *n, *str = module_namestr(ctx, m);
    size_t len;
    n = basename(str);
    len = str_rchr(n, '.');
    if(!strcmp(str, name) || !strcmp(n, name) || (len == namelen && !strncmp(n, name, len)))
      return m;
    js_free(ctx, str);
  }
  return 0;
}

int
js_module_indexof(JSContext* ctx, JSModuleDef* def) {
  struct list_head* el;
  int i = 0;
  list_for_each(el, &ctx->loaded_modules) {
    JSModuleDef* m = list_entry(el, JSModuleDef, link);
    if(m == def)
      return i;
    ++i;
  }
  return -1;
}
/*
Vector
js_module_vector(JSContext* ctx) {
  Vector ret = VECTOR(ctx);
  struct list_head* el;
  list_for_each(el, &ctx->loaded_modules) {
    JSModuleDef* m = list_entry(el, JSModuleDef, link);
    vector_push(&ret, m);
  }
  return ret;
}
*/
JSModuleDef*
js_module_at(JSContext* ctx, int index) {
  struct list_head* el;
  int i = 0;
  if(index >= 0) {
    list_for_each(el, &ctx->loaded_modules) {
      JSModuleDef* m = list_entry(el, JSModuleDef, link);
      if(index == i)
        return m;
      ++i;
    }
  } else {
    index = -(index + 1);
    list_for_each_prev(el, &ctx->loaded_modules) {
      JSModuleDef* m = list_entry(el, JSModuleDef, link);
      if(index == i)
        return m;
      ++i;
    }
  }
  return 0;
}

BOOL
js_is_arraybuffer(JSContext* ctx, JSValueConst value) {
  BOOL ret = FALSE;
  if(!JS_IsObject(value))
    return ret;
  if(!ret)
    ret |= js_value_isclass(ctx, value, JS_CLASS_ARRAY_BUFFER);
  /* if(!ret)
    ret |= js_object_is(ctx, value, "[object ArrayBuffer]"); */
  /*  if(!ret) {
      JSObject* obj;
      if((obj = js_value_obj(value)) && obj->class_id) {
        JSValue ctor = js_global_get_str(ctx, "ArrayBuffer");
        ret = JS_IsInstanceOf(ctx, value, ctor);
        JS_FreeValue(ctx, ctor);
      }
    }*/
  return ret;
}

BOOL
js_is_sharedarraybuffer(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) && (js_value_isclass(ctx, value, JS_CLASS_SHARED_ARRAY_BUFFER));
}

BOOL
js_is_date(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) && (js_value_isclass(ctx, value, JS_CLASS_DATE));
}

BOOL
js_is_map(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) && (js_value_isclass(ctx, value, JS_CLASS_MAP));
}

BOOL
js_is_set(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) && (js_value_isclass(ctx, value, JS_CLASS_SET));
}

BOOL
js_is_generator(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) && (js_value_isclass(ctx, value, JS_CLASS_GENERATOR));
}

BOOL
js_is_regexp(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) && (js_value_isclass(ctx, value, JS_CLASS_REGEXP));
}

BOOL
js_is_promise(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) && (js_value_isclass(ctx, value, JS_CLASS_PROMISE) || js_object_is(ctx, value, "[object Promise]"));
}

BOOL
js_is_dataview(JSContext* ctx, JSValueConst value) {
  return JS_IsObject(value) && (js_value_isclass(ctx, value, JS_CLASS_DATAVIEW));
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
js_typedarray_new(JSContext* ctx, int bits, BOOL floating, BOOL sign, JSValueConst buffer) {
  char class_name[64] = {0};

  snprintf(class_name, sizeof(class_name), "%s%s%dArray", (!floating && bits >= 64) ? "Big" : "", floating ? "Float" : sign ? "Int" : "Uint", bits);

  JSValue ret, typedarray_ctor = js_global_get_str(ctx, class_name);
  ret = JS_CallConstructor(ctx, typedarray_ctor, 1, &buffer);

  JS_FreeValue(ctx, typedarray_ctor);
  return ret;
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
  JSValue operators = js_global_get_str(ctx, "Operators");
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
  JSValue ctor = js_global_get_str(ctx, "Date");
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

void
js_arraybuffer_freevalue(JSRuntime* rt, void* opaque, void* ptr) {
  JSValue* valptr = opaque;
  JS_FreeValueRT(rt, *valptr);
  js_free_rt(rt, opaque);
}

JSValue
js_arraybuffer_fromvalue(JSContext* ctx, void* x, size_t n, JSValueConst val) {
  JSValue* valptr;
  if(!(valptr = js_malloc(ctx, sizeof(JSValue))))
    return JS_ThrowOutOfMemory(ctx);
  *valptr = JS_DupValue(ctx, val);
  return JS_NewArrayBuffer(ctx, x, n, js_arraybuffer_freevalue, valptr, FALSE);
}

int64_t
js_arraybuffer_bytelength(JSContext* ctx, JSValueConst value) {
  int64_t len = -1;
  if(js_is_arraybuffer(ctx, value)) {
    JSValue length = JS_GetPropertyStr(ctx, value, "byteLength");
    JS_ToInt64(ctx, &len, length);
    JS_FreeValue(ctx, length);
  }
  return len;
}

JSValue
js_eval_module(JSContext* ctx, JSValueConst obj, BOOL load_only) {
  JSValue ret = JS_UNDEFINED;
  int tag = JS_VALUE_GET_TAG(obj);

  if(tag == JS_TAG_MODULE) {
    if(!load_only && JS_ResolveModule(ctx, obj) < 0) {
      JS_FreeValue(ctx, obj);
      return JS_ThrowInternalError(ctx, "Failed resolving module");
    }
    js_module_set_import_meta(ctx, obj, FALSE, !load_only);
    return load_only ? JS_DupValue(ctx, obj) : JS_EvalFunction(ctx, obj);
  }

  return JS_ThrowInternalError(ctx, "invalid tag %i", tag);
}

JSValue
js_eval_binary(JSContext* ctx, const uint8_t* buf, size_t buf_len, BOOL load_only) {
  JSValue obj = JS_ReadObject(ctx, buf, buf_len, JS_READ_OBJ_BYTECODE);
  if(JS_IsException(obj))
    return obj;
  // printf("js_eval_binary obj=%s\n", js_value_typestr(ctx, obj));
  if(!load_only) {
    JSValue tmp = js_eval_module(ctx, obj, load_only);
    int tag = JS_VALUE_GET_TAG(tmp);
    // printf("js_eval_binary tmp=%s\n", js_value_typestr(ctx, tmp));
    if(!JS_IsException(tmp) && !JS_IsUndefined(tmp))
      if(tag >= JS_TAG_FIRST && tag <= JS_TAG_FLOAT64)
        return tmp;
  }
  return obj;
}

JSValue
js_eval_buf(JSContext* ctx, const void* buf, int buf_len, const char* filename, int eval_flags) {
  JSValue val;
  int ret;

  if((eval_flags & JS_EVAL_TYPE_MASK) == JS_EVAL_TYPE_MODULE) {
    /* for the modules, we compile then run to be able to set import.meta */
    val = JS_Eval(ctx, buf, buf_len, filename, eval_flags | JS_EVAL_FLAG_COMPILE_ONLY);
    if(!JS_IsException(val)) {
      js_module_set_import_meta(ctx, val, TRUE, TRUE);
      val = JS_EvalFunction(ctx, val);
    }
  } else {
    val = JS_Eval(ctx, buf, buf_len, filename, eval_flags);
  }

  return val;
}

int
js_eval_str(JSContext* ctx, const char* str, const char* file, int flags) {
  int32_t ret = 0;
  JSValue val = js_eval_buf(ctx, str, strlen(str), file, flags);
  if(JS_IsException(val))
    ret = -1;
  else if(JS_IsNumber(val))
    JS_ToInt32(ctx, &ret, val);
  return ret;
}

int __attribute__((format(printf, 3, 4))) js_eval_fmt(JSContext* ctx, int flags, const char* fmt, ...) {
  int ret;
  va_list ap;
  DynBuf buf;
  dbuf_init2(&buf, ctx, (realloc_func*)&utils_js_realloc);
  va_start(ap, fmt);
  dbuf_vprintf(&buf, fmt, ap);
  va_end(ap);
  dbuf_0(&buf);
  ret = js_eval_str(ctx, (const char*)buf.buf, "<input>", flags);
  dbuf_free(&buf);
  return ret;
}

thread_local uint64_t js_pending_signals = 0;

int64_t
js_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

int
js_interrupt_handler(JSRuntime* rt, void* opaque) {
  return (js_pending_signals >> SIGINT) & 1;
}

void
js_timer_unlink(JSRuntime* rt, JSOSTimer* th) {
  if(th->link.prev) {
    list_del(&th->link);
    th->link.prev = th->link.next = 0;
  }
}

void
js_timer_free(JSRuntime* rt, JSOSTimer* th) {
  JS_FreeValueRT(rt, th->func);
  js_free_rt(rt, th);
}

void
js_call_handler(JSContext* ctx, JSValueConst func) {
  JSValue ret, func1;
  func1 = JS_DupValue(ctx, func);
  ret = JS_Call(ctx, func1, JS_UNDEFINED, 0, 0);
  JS_FreeValue(ctx, func1);
  if(JS_IsException(ret))
    js_std_dump_error(ctx);
  JS_FreeValue(ctx, ret);
}
void*
js_sab_alloc(void* opaque, size_t size) {
  JSSABHeader* sab;
  sab = malloc(sizeof(JSSABHeader) + size);
  if(!sab)
    return 0;
  sab->ref_count = 1;
  return sab->buf;
}
void
js_sab_free(void* opaque, void* ptr) {
  JSSABHeader* sab;
  int ref_count;
  sab = (JSSABHeader*)((uint8_t*)ptr - sizeof(JSSABHeader));
  ref_count = atomic_add_int(&sab->ref_count, -1);
  assert(ref_count >= 0);
  if(ref_count == 0) {
    free(sab);
  }
}

void
js_sab_dup(void* opaque, void* ptr) {
  JSSABHeader* sab;
  sab = (JSSABHeader*)((uint8_t*)ptr - sizeof(JSSABHeader));
  atomic_add_int(&sab->ref_count, 1);
}

void
js_error_dump(JSContext* ctx, JSValueConst error, DynBuf* db) {
  const char *str, *stack = 0;
  if(JS_IsObject(error)) {
    JSValue st = JS_GetPropertyStr(ctx, error, "stack");
    if(!JS_IsUndefined(st))
      stack = JS_ToCString(ctx, st);
    JS_FreeValue(ctx, st);
  }
  if((str = JS_ToCString(ctx, error))) {
    const char* type = JS_IsObject(error) ? js_object_classname(ctx, error) : js_value_typestr(ctx, error);

    if(!str_start(str, type)) {
      dbuf_putstr(db, type);
      dbuf_putstr(db, ": ");
    }
    dbuf_putstr(db, str);
    dbuf_putc(db, '\n');
    if(stack) {
      dbuf_putstr(db, "STACK\n");
      dbuf_putstr(db, stack);
      dbuf_putc(db, '\n');
    }
    dbuf_0(db);
  }
  if(stack)
    JS_FreeCString(ctx, stack);
  JS_FreeCString(ctx, str);
}

char*
js_error_tostring(JSContext* ctx, JSValueConst error) {
  DynBuf db;
  dbuf_init2(&db, ctx, (realloc_func*)&utils_js_realloc);
  js_error_dump(ctx, error, &db);
  return (char*)db.buf;
}

void
js_error_print(JSContext* ctx, JSValueConst error) {
  const char *str = 0, *stack = 0;

  if(JS_IsObject(error)) {
    JSValue st = JS_GetPropertyStr(ctx, error, "stack");

    if(!JS_IsUndefined(st))
      stack = JS_ToCString(ctx, st);

    JS_FreeValue(ctx, st);
  }

  fputs("Toplevel error:\n", stderr);

  if(!JS_IsNull(error) && (str = JS_ToCString(ctx, error))) {
    const char* type = JS_IsObject(error) ? js_object_classname(ctx, error) : js_value_typestr(ctx, error);
    const char* exception = str;
    size_t typelen = strlen(type);

    if(!strncmp(exception, type, typelen) && exception[typelen] == ':') {
      exception += typelen + 2;
    }
    fprintf(stderr, "%s: %s\n", type, exception);
  }
  if(stack)
    fprintf(stderr, "Stack:\n%s\n", stack);
  fflush(stderr);
  if(stack)
    JS_FreeCString(ctx, stack);
  if(str)
    JS_FreeCString(ctx, str);
}

JSValue
js_error_stack(JSContext* ctx) {
  JSValue error = js_object_error(ctx, "");
  JSValue stack = JS_GetPropertyStr(ctx, error, "stack");
  JS_FreeValue(ctx, error);
  return stack;
}

JSValue
js_io_readhandler_fn(JSContext* ctx, BOOL write) {
  JSModuleDef* os;
  const char* handlers[2] = {"setReadHandler", "setWriteHandler"};
  JSAtom func_name;
  JSValue set_handler;

  if(!(os = js_module_find(ctx, "os")))
    return JS_ThrowReferenceError(ctx, "'os' module required");

  func_name = JS_NewAtom(ctx, handlers[write]);
  set_handler = module_exports_find(ctx, os, func_name);
  JS_FreeAtom(ctx, func_name);

  if(!JS_IsFunction(ctx, set_handler)) {
    JS_FreeValue(ctx, set_handler);
    return JS_ThrowReferenceError(ctx, "no os.%s function", handlers[write]);
  }

  return set_handler;
}

static thread_local JSCFunction* readhandler_cfunc;

JSCFunction*
js_io_readhandler_cfunc(JSContext* ctx, BOOL write) {
  if(!readhandler_cfunc) {
    JSValue set_handler;
    JSObject* obj;
    set_handler = js_io_readhandler_fn(ctx, write);
    if(JS_IsException(set_handler))
      return 0;
    readhandler_cfunc = js_function_cfunc(ctx, set_handler);
  }
  return readhandler_cfunc;
}

JSValue
js_promise_resolve(JSContext* ctx, JSValueConst promise) {
  JSValue ret, ctor = js_global_get_str(ctx, "Promise");
  ret = js_invoke(ctx, ctor, "resolve", 1, &promise);
  JS_FreeValue(ctx, ctor);
  return ret;
}

JSValue
js_promise_then(JSContext* ctx, JSValueConst promise, JSValueConst func) {
  return js_invoke(ctx, promise, "then", 1, &func);
}

JSValue
js_promise_catch(JSContext* ctx, JSValueConst promise, JSValueConst func) {
  return js_invoke(ctx, promise, "catch", 1, &func);
}

JSValue
js_promise_wrap(JSContext* ctx, JSValueConst value) {
  JSValue ret, promise, resolving_funcs[2];
  promise = JS_NewPromiseCapability(ctx, resolving_funcs);
  ret = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &value);
  JS_FreeValue(ctx, ret);

  JS_FreeValue(ctx, resolving_funcs[0]);
  JS_FreeValue(ctx, resolving_funcs[1]);
  return promise;
}

JSValue
js_promise_adopt(JSContext* ctx, JSValueConst value) {
  if(js_is_promise(ctx, value))
    return JS_DupValue(ctx, value);
  return js_promise_wrap(ctx, value);
}

JSValue
js_to_string(JSContext* ctx, JSValueConst this_obj) {
  JSValue ret = JS_UNDEFINED;
  JSAtom key = JS_NewAtom(ctx, "toString");
  if(JS_HasProperty(ctx, this_obj, key))
    ret = JS_Invoke(ctx, this_obj, key, 0, 0);
  else
    ret = JS_ThrowTypeError(ctx, "value has no .toString() method");
  JS_FreeAtom(ctx, key);
  return ret;
}
JSValue
js_to_source(JSContext* ctx, JSValueConst this_obj) {
  JSValue ret = JS_UNDEFINED;
  JSAtom key = JS_NewAtom(ctx, "toSource");
  if(JS_HasProperty(ctx, this_obj, key))
    ret = JS_Invoke(ctx, this_obj, key, 0, 0);
  else
    ret = JS_ThrowTypeError(ctx, "value has no .toSource() method");
  JS_FreeAtom(ctx, key);
  return ret;
}

void
arguments_dump(Arguments const* args, /*JSContext* ctx,*/ DynBuf* dbuf) {
  int n = args->c, i;

  if(n > 1)
    dbuf_putstr(dbuf, "(");
  for(i = 0; i < n; i++) {
    const char* arg = args->v[i];
    if(i > 0)
      dbuf_putstr(dbuf, ", ");
    dbuf_putstr(dbuf, arg ? arg : "NULL");
  }
  if(n > 1)
    dbuf_putstr(dbuf, ")");
}

BOOL
arguments_alloc(Arguments* args, JSContext* ctx, int n) {
  int i, j, c;
  if(args->a) {
    if(!(args->v = js_realloc(ctx, args->v, sizeof(char*) * (n + 1))))
      return FALSE;
    for(i = args->c; i < args->a; i++) args->v[i] = 0;
    args->a = n;
  } else {
    char** v;
    if(!(v = js_mallocz(ctx, sizeof(char*) * (n + 1))))
      return FALSE;
    c = MIN_NUM(args->c, n);
    for(i = 0; i < c; i++) v[i] = js_strdup(ctx, args->v[i]);
    for(j = c; j < args->c; j++) js_free(ctx, (void*)args->v[j]);
    for(j = i; j <= n; j++) v[j] = 0;
    args->v = (const char**)v;
    args->c = c;
    args->a = n;
  }
  return TRUE;
}

const char*
arguments_push(Arguments* args, JSContext* ctx, const char* arg) {
  int r;
  if(args->c + 1 >= args->a)
    if(!arguments_alloc(args, ctx, args->a + 1))
      return 0;
  r = args->c;
  args->v[r] = js_strdup(ctx, arg);
  args->v[r + 1] = 0;
  args->c++;
  return args->v[r];
}

BOOL
js_arguments_alloc(JSArguments* args, JSContext* ctx, int n) {
  int i;
  if(args->a) {
    if(!(args->v = js_realloc(ctx, args->v, sizeof(JSValueConst) * (n + 1))))
      return FALSE;

    for(i = args->c; i < args->a; i++) args->v[i] = JS_UNDEFINED;
  } else {
    JSValueConst* v;

    if(!(v = js_mallocz(ctx, sizeof(JSValueConst) * (n + 1))))
      return FALSE;
    memcpy(v, args->v, sizeof(JSValueConst) * (args->c + 1));
    args->v = v;
    args->a = n;
  }
  return TRUE;
}

void
js_arguments_dump(JSArguments const* args, JSContext* ctx, DynBuf* dbuf) {
  int n = args->c, i;

  if(n > 1)
    dbuf_putstr(dbuf, "(");

  for(i = 0; i < n; i++) {
    JSValue arg = args->v[i];
    if(JS_IsException(arg))
      break;

    js_value_dump(ctx, arg, dbuf);
    if(i > 0)
      dbuf_putstr(dbuf, ", ");
  }

  if(n > 1)
    dbuf_putstr(dbuf, ")");
}

char*
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

char*
js_tostring(JSContext* ctx, JSValueConst value) {
  return js_tostringlen(ctx, 0, value);
}

char*
js_tosource(JSContext* ctx, JSValueConst value) {
  JSValue src = js_to_source(ctx, value);
  const char* str = JS_ToCString(ctx, src);
  JS_FreeValue(ctx, src);
  char* ret = js_strdup(ctx, str);
  JS_FreeCString(ctx, str);
  return ret;
}

wchar_t*
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

char*
js_json_stringify(JSContext* ctx, JSValueConst value) {
  JSValue strv;
  char* str = 0;
  strv = JS_JSONStringify(ctx, value, JS_NULL, JS_MKVAL(JS_TAG_INT, 2));
  str = js_tostring(ctx, strv);
  JS_FreeValue(ctx, strv);
  return str;
}

BOOL
js_is_identifier_len(JSContext* ctx, const char* str, size_t len) {
  /* clang-format off */ 
  RegExp re = regexp_from_string((char*)"([$_a-zA-Z]|[\\xaa\\xb5\\xba\\xc0-\\xd6\\xd8-\\xf6\\xf8-\\u02c1\\u02c6-\\u02d1\\u02e0-\\u02e4\\u02ec\\u02ee\\u0370-\\u0374\\u0376\\u0377\\u037a-\\u037d\\u0386\\u0388-\\u038a\\u038c\\u038e-\\u03a1\\u03a3-\\u03f5\\u03f7-\\u0481\\u048a-\\u0527\\u0531-\\u0556\\u0559\\u0561-\\u0587\\u05d0-\\u05ea\\u05f0-\\u05f2\\u0620-\\u064a\\u066e\\u066f\\u0671-\\u06d3\\u06d5\\u06e5\\u06e6\\u06ee\\u06ef\\u06fa-\\u06fc\\u06ff\\u0710\\u0712-\\u072f\\u074d-\\u07a5\\u07b1\\u07ca-\\u07ea\\u07f4\\u07f5\\u07fa\\u0800-\\u0815\\u081a\\u0824\\u0828\\u0840-\\u0858\\u08a0\\u08a2-\\u08ac\\u0904-\\u0939\\u093d\\u0950\\u0958-\\u0961\\u0971-\\u0977\\u0979-\\u097f\\u0985-\\u098c\\u098f\\u0990\\u0993-\\u09a8\\u09aa-\\u09b0\\u09b2\\u09b6-\\u09b9\\u09bd\\u09ce\\u09dc\\u09dd\\u09df-\\u09e1\\u09f0\\u09f1\\u0a05-\\u0a0a\\u0a0f\\u0a10\\u0a13-\\u0a28\\u0a2a-\\u0a30\\u0a32\\u0a33\\u0a35\\u0a36\\u0a38\\u0a39\\u0a59-\\u0a5c\\u0a5e\\u0a72-\\u0a74\\u0a85-\\u0a8d\\u0a8f-\\u0a91\\u0a93-\\u0aa8\\u0aaa-\\u0ab0\\u0ab2\\u0ab3\\u0ab5-\\u0ab9\\u0abd\\u0ad0\\u0ae0\\u0ae1\\u0b05-\\u0b0c\\u0b0f\\u0b10\\u0b13-\\u0b28\\u0b2a-\\u0b30\\u0b32\\u0b33\\u0b35-\\u0b39\\u0b3d\\u0b5c\\u0b5d\\u0b5f-\\u0b61\\u0b71\\u0b83\\u0b85-\\u0b8a\\u0b8e-\\u0b90\\u0b92-\\u0b95\\u0b99\\u0b9a\\u0b9c\\u0b9e\\u0b9f\\u0ba3\\u0ba4\\u0ba8-\\u0baa\\u0bae-\\u0bb9\\u0bd0\\u0c05-\\u0c0c\\u0c0e-\\u0c10\\u0c12-\\u0c28\\u0c2a-\\u0c33\\u0c35-\\u0c39\\u0c3d\\u0c58\\u0c59\\u0c60\\u0c61\\u0c85-\\u0c8c\\u0c8e-\\u0c90\\u0c92-\\u0ca8\\u0caa-\\u0cb3\\u0cb5-\\u0cb9\\u0cbd\\u0cde\\u0ce0\\u0ce1\\u0cf1\\u0cf2\\u0d05-\\u0d0c\\u0d0e-\\u0d10\\u0d12-\\u0d3a\\u0d3d\\u0d4e\\u0d60\\u0d61\\u0d7a-\\u0d7f\\u0d85-\\u0d96\\u0d9a-\\u0db1\\u0db3-\\u0dbb\\u0dbd\\u0dc0-\\u0dc6\\u0e01-\\u0e30\\u0e32\\u0e33\\u0e40-\\u0e46\\u0e81\\u0e82\\u0e84\\u0e87\\u0e88\\u0e8a\\u0e8d\\u0e94-\\u0e97\\u0e99-\\u0e9f\\u0ea1-\\u0ea3\\u0ea5\\u0ea7\\u0eaa\\u0eab\\u0ead-\\u0eb0\\u0eb2\\u0eb3\\u0ebd\\u0ec0-\\u0ec4\\u0ec6\\u0edc-\\u0edf\\u0f00\\u0f40-\\u0f47\\u0f49-\\u0f6c\\u0f88-\\u0f8c\\u1000-\\u102a\\u103f\\u1050-\\u1055\\u105a-\\u105d\\u1061\\u1065\\u1066\\u106e-\\u1070\\u1075-\\u1081\\u108e\\u10a0-\\u10c5\\u10c7\\u10cd\\u10d0-\\u10fa\\u10fc-\\u1248\\u124a-\\u124d\\u1250-\\u1256\\u1258\\u125a-\\u125d\\u1260-\\u1288\\u128a-\\u128d\\u1290-\\u12b0\\u12b2-\\u12b5\\u12b8-\\u12be\\u12c0\\u12c2-\\u12c5\\u12c8-\\u12d6\\u12d8-\\u1310\\u1312-\\u1315\\u1318-\\u135a\\u1380-\\u138f\\u13a0-\\u13f4\\u1401-\\u166c\\u166f-\\u167f\\u1681-\\u169a\\u16a0-\\u16ea\\u16ee-\\u16f0\\u1700-\\u170c\\u170e-\\u1711\\u1720-\\u1731\\u1740-\\u1751\\u1760-\\u176c\\u176e-\\u1770\\u1780-\\u17b3\\u17d7\\u17dc\\u1820-\\u1877\\u1880-\\u18a8\\u18aa\\u18b0-\\u18f5\\u1900-\\u191c\\u1950-\\u196d\\u1970-\\u1974\\u1980-\\u19ab\\u19c1-\\u19c7\\u1a00-\\u1a16\\u1a20-\\u1a54\\u1aa7\\u1b05-\\u1b33\\u1b45-\\u1b4b\\u1b83-\\u1ba0\\u1bae\\u1baf\\u1bba-\\u1be5\\u1c00-\\u1c23\\u1c4d-\\u1c4f\\u1c5a-\\u1c7d\\u1ce9-\\u1cec\\u1cee-\\u1cf1\\u1cf5\\u1cf6\\u1d00-\\u1dbf\\u1e00-\\u1f15\\u1f18-\\u1f1d\\u1f20-\\u1f45\\u1f48-\\u1f4d\\u1f50-\\u1f57\\u1f59\\u1f5b\\u1f5d\\u1f5f-\\u1f7d\\u1f80-\\u1fb4\\u1fb6-\\u1fbc\\u1fbe\\u1fc2-\\u1fc4\\u1fc6-\\u1fcc\\u1fd0-\\u1fd3\\u1fd6-\\u1fdb\\u1fe0-\\u1fec\\u1ff2-\\u1ff4\\u1ff6-\\u1ffc\\u2071\\u207f\\u2090-\\u209c\\u2102\\u2107\\u210a-\\u2113\\u2115\\u2119-\\u211d\\u2124\\u2126\\u2128\\u212a-\\u212d\\u212f-\\u2139\\u213c-\\u213f\\u2145-\\u2149\\u214e\\u2160-\\u2188\\u2c00-\\u2c2e\\u2c30-\\u2c5e\\u2c60-\\u2ce4\\u2ceb-\\u2cee\\u2cf2\\u2cf3\\u2d00-\\u2d25\\u2d27\\u2d2d\\u2d30-\\u2d67\\u2d6f\\u2d80-\\u2d96\\u2da0-\\u2da6\\u2da8-\\u2dae\\u2db0-\\u2db6\\u2db8-\\u2dbe\\u2dc0-\\u2dc6\\u2dc8-\\u2dce\\u2dd0-\\u2dd6\\u2dd8-\\u2dde\\u2e2f\\u3005-\\u3007\\u3021-\\u3029\\u3031-\\u3035\\u3038-\\u303c\\u3041-\\u3096\\u309d-\\u309f\\u30a1-\\u30fa\\u30fc-\\u30ff\\u3105-\\u312d\\u3131-\\u318e\\u31a0-\\u31ba\\u31f0-\\u31ff\\u3400-\\u4db5\\u4e00-\\u9fcc\\ua000-\\ua48c\\ua4d0-\\ua4fd\\ua500-\\ua60c\\ua610-\\ua61f\\ua62a\\ua62b\\ua640-\\ua66e\\ua67f-\\ua697\\ua6a0-\\ua6ef\\ua717-\\ua71f\\ua722-\\ua788\\ua78b-\\ua78e\\ua790-\\ua793\\ua7a0-\\ua7aa\\ua7f8-\\ua801\\ua803-\\ua805\\ua807-\\ua80a\\ua80c-\\ua822\\ua840-\\ua873\\ua882-\\ua8b3\\ua8f2-\\ua8f7\\ua8fb\\ua90a-\\ua925\\ua930-\\ua946\\ua960-\\ua97c\\ua984-\\ua9b2\\ua9cf\\uaa00-\\uaa28\\uaa40-\\uaa42\\uaa44-\\uaa4b\\uaa60-\\uaa76\\uaa7a\\uaa80-\\uaaaf\\uaab1\\uaab5\\uaab6\\uaab9-\\uaabd\\uaac0\\uaac2\\uaadb-\\uaadd\\uaae0-\\uaaea\\uaaf2-\\uaaf4\\uab01-\\uab06\\uab09-\\uab0e\\uab11-\\uab16\\uab20-\\uab26\\uab28-\\uab2e\\uabc0-\\uabe2\\uac00-\\ud7a3\\ud7b0-\\ud7c6\\ud7cb-\\ud7fb\\uf900-\\ufa6d\\ufa70-\\ufad9\\ufb00-\\ufb06\\ufb13-\\ufb17\\ufb1d\\ufb1f-\\ufb28\\ufb2a-\\ufb36\\ufb38-\\ufb3c\\ufb3e\\ufb40\\ufb41\\ufb43\\ufb44\\ufb46-\\ufbb1\\ufbd3-\\ufd3d\\ufd50-\\ufd8f\\ufd92-\\ufdc7\\ufdf0-\\ufdfb\\ufe70-\\ufe74\\ufe76-\\ufefc\\uff21-\\uff3a\\uff41-\\uff5a\\uff66-\\uffbe\\uffc2-\\uffc7\\uffca-\\uffcf\\uffd2-\\uffd7\\uffda-\\uffdc]|\\\\[u][0-9a-fA-F]{4})([$_a-zA-Z]|[\\xaa\\xb5\\xba\\xc0-\\xd6\\xd8-\\xf6\\xf8-\\u02c1\\u02c6-\\u02d1\\u02e0-\\u02e4\\u02ec\\u02ee\\u0370-\\u0374\\u0376\\u0377\\u037a-\\u037d\\u0386\\u0388-\\u038a\\u038c\\u038e-\\u03a1\\u03a3-\\u03f5\\u03f7-\\u0481\\u048a-\\u0527\\u0531-\\u0556\\u0559\\u0561-\\u0587\\u05d0-\\u05ea\\u05f0-\\u05f2\\u0620-\\u064a\\u066e\\u066f\\u0671-\\u06d3\\u06d5\\u06e5\\u06e6\\u06ee\\u06ef\\u06fa-\\u06fc\\u06ff\\u0710\\u0712-\\u072f\\u074d-\\u07a5\\u07b1\\u07ca-\\u07ea\\u07f4\\u07f5\\u07fa\\u0800-\\u0815\\u081a\\u0824\\u0828\\u0840-\\u0858\\u08a0\\u08a2-\\u08ac\\u0904-\\u0939\\u093d\\u0950\\u0958-\\u0961\\u0971-\\u0977\\u0979-\\u097f\\u0985-\\u098c\\u098f\\u0990\\u0993-\\u09a8\\u09aa-\\u09b0\\u09b2\\u09b6-\\u09b9\\u09bd\\u09ce\\u09dc\\u09dd\\u09df-\\u09e1\\u09f0\\u09f1\\u0a05-\\u0a0a\\u0a0f\\u0a10\\u0a13-\\u0a28\\u0a2a-\\u0a30\\u0a32\\u0a33\\u0a35\\u0a36\\u0a38\\u0a39\\u0a59-\\u0a5c\\u0a5e\\u0a72-\\u0a74\\u0a85-\\u0a8d\\u0a8f-\\u0a91\\u0a93-\\u0aa8\\u0aaa-\\u0ab0\\u0ab2\\u0ab3\\u0ab5-\\u0ab9\\u0abd\\u0ad0\\u0ae0\\u0ae1\\u0b05-\\u0b0c\\u0b0f\\u0b10\\u0b13-\\u0b28\\u0b2a-\\u0b30\\u0b32\\u0b33\\u0b35-\\u0b39\\u0b3d\\u0b5c\\u0b5d\\u0b5f-\\u0b61\\u0b71\\u0b83\\u0b85-\\u0b8a\\u0b8e-\\u0b90\\u0b92-\\u0b95\\u0b99\\u0b9a\\u0b9c\\u0b9e\\u0b9f\\u0ba3\\u0ba4\\u0ba8-\\u0baa\\u0bae-\\u0bb9\\u0bd0\\u0c05-\\u0c0c\\u0c0e-\\u0c10\\u0c12-\\u0c28\\u0c2a-\\u0c33\\u0c35-\\u0c39\\u0c3d\\u0c58\\u0c59\\u0c60\\u0c61\\u0c85-\\u0c8c\\u0c8e-\\u0c90\\u0c92-\\u0ca8\\u0caa-\\u0cb3\\u0cb5-\\u0cb9\\u0cbd\\u0cde\\u0ce0\\u0ce1\\u0cf1\\u0cf2\\u0d05-\\u0d0c\\u0d0e-\\u0d10\\u0d12-\\u0d3a\\u0d3d\\u0d4e\\u0d60\\u0d61\\u0d7a-\\u0d7f\\u0d85-\\u0d96\\u0d9a-\\u0db1\\u0db3-\\u0dbb\\u0dbd\\u0dc0-\\u0dc6\\u0e01-\\u0e30\\u0e32\\u0e33\\u0e40-\\u0e46\\u0e81\\u0e82\\u0e84\\u0e87\\u0e88\\u0e8a\\u0e8d\\u0e94-\\u0e97\\u0e99-\\u0e9f\\u0ea1-\\u0ea3\\u0ea5\\u0ea7\\u0eaa\\u0eab\\u0ead-\\u0eb0\\u0eb2\\u0eb3\\u0ebd\\u0ec0-\\u0ec4\\u0ec6\\u0edc-\\u0edf\\u0f00\\u0f40-\\u0f47\\u0f49-\\u0f6c\\u0f88-\\u0f8c\\u1000-\\u102a\\u103f\\u1050-\\u1055\\u105a-\\u105d\\u1061\\u1065\\u1066\\u106e-\\u1070\\u1075-\\u1081\\u108e\\u10a0-\\u10c5\\u10c7\\u10cd\\u10d0-\\u10fa\\u10fc-\\u1248\\u124a-\\u124d\\u1250-\\u1256\\u1258\\u125a-\\u125d\\u1260-\\u1288\\u128a-\\u128d\\u1290-\\u12b0\\u12b2-\\u12b5\\u12b8-\\u12be\\u12c0\\u12c2-\\u12c5\\u12c8-\\u12d6\\u12d8-\\u1310\\u1312-\\u1315\\u1318-\\u135a\\u1380-\\u138f\\u13a0-\\u13f4\\u1401-\\u166c\\u166f-\\u167f\\u1681-\\u169a\\u16a0-\\u16ea\\u16ee-\\u16f0\\u1700-\\u170c\\u170e-\\u1711\\u1720-\\u1731\\u1740-\\u1751\\u1760-\\u176c\\u176e-\\u1770\\u1780-\\u17b3\\u17d7\\u17dc\\u1820-\\u1877\\u1880-\\u18a8\\u18aa\\u18b0-\\u18f5\\u1900-\\u191c\\u1950-\\u196d\\u1970-\\u1974\\u1980-\\u19ab\\u19c1-\\u19c7\\u1a00-\\u1a16\\u1a20-\\u1a54\\u1aa7\\u1b05-\\u1b33\\u1b45-\\u1b4b\\u1b83-\\u1ba0\\u1bae\\u1baf\\u1bba-\\u1be5\\u1c00-\\u1c23\\u1c4d-\\u1c4f\\u1c5a-\\u1c7d\\u1ce9-\\u1cec\\u1cee-\\u1cf1\\u1cf5\\u1cf6\\u1d00-\\u1dbf\\u1e00-\\u1f15\\u1f18-\\u1f1d\\u1f20-\\u1f45\\u1f48-\\u1f4d\\u1f50-\\u1f57\\u1f59\\u1f5b\\u1f5d\\u1f5f-\\u1f7d\\u1f80-\\u1fb4\\u1fb6-\\u1fbc\\u1fbe\\u1fc2-\\u1fc4\\u1fc6-\\u1fcc\\u1fd0-\\u1fd3\\u1fd6-\\u1fdb\\u1fe0-\\u1fec\\u1ff2-\\u1ff4\\u1ff6-\\u1ffc\\u2071\\u207f\\u2090-\\u209c\\u2102\\u2107\\u210a-\\u2113\\u2115\\u2119-\\u211d\\u2124\\u2126\\u2128\\u212a-\\u212d\\u212f-\\u2139\\u213c-\\u213f\\u2145-\\u2149\\u214e\\u2160-\\u2188\\u2c00-\\u2c2e\\u2c30-\\u2c5e\\u2c60-\\u2ce4\\u2ceb-\\u2cee\\u2cf2\\u2cf3\\u2d00-\\u2d25\\u2d27\\u2d2d\\u2d30-\\u2d67\\u2d6f\\u2d80-\\u2d96\\u2da0-\\u2da6\\u2da8-\\u2dae\\u2db0-\\u2db6\\u2db8-\\u2dbe\\u2dc0-\\u2dc6\\u2dc8-\\u2dce\\u2dd0-\\u2dd6\\u2dd8-\\u2dde\\u2e2f\\u3005-\\u3007\\u3021-\\u3029\\u3031-\\u3035\\u3038-\\u303c\\u3041-\\u3096\\u309d-\\u309f\\u30a1-\\u30fa\\u30fc-\\u30ff\\u3105-\\u312d\\u3131-\\u318e\\u31a0-\\u31ba\\u31f0-\\u31ff\\u3400-\\u4db5\\u4e00-\\u9fcc\\ua000-\\ua48c\\ua4d0-\\ua4fd\\ua500-\\ua60c\\ua610-\\ua61f\\ua62a\\ua62b\\ua640-\\ua66e\\ua67f-\\ua697\\ua6a0-\\ua6ef\\ua717-\\ua71f\\ua722-\\ua788\\ua78b-\\ua78e\\ua790-\\ua793\\ua7a0-\\ua7aa\\ua7f8-\\ua801\\ua803-\\ua805\\ua807-\\ua80a\\ua80c-\\ua822\\ua840-\\ua873\\ua882-\\ua8b3\\ua8f2-\\ua8f7\\ua8fb\\ua90a-\\ua925\\ua930-\\ua946\\ua960-\\ua97c\\ua984-\\ua9b2\\ua9cf\\uaa00-\\uaa28\\uaa40-\\uaa42\\uaa44-\\uaa4b\\uaa60-\\uaa76\\uaa7a\\uaa80-\\uaaaf\\uaab1\\uaab5\\uaab6\\uaab9-\\uaabd\\uaac0\\uaac2\\uaadb-\\uaadd\\uaae0-\\uaaea\\uaaf2-\\uaaf4\\uab01-\\uab06\\uab09-\\uab0e\\uab11-\\uab16\\uab20-\\uab26\\uab28-\\uab2e\\uabc0-\\uabe2\\uac00-\\ud7a3\\ud7b0-\\ud7c6\\ud7cb-\\ud7fb\\uf900-\\ufa6d\\ufa70-\\ufad9\\ufb00-\\ufb06\\ufb13-\\ufb17\\ufb1d\\ufb1f-\\ufb28\\ufb2a-\\ufb36\\ufb38-\\ufb3c\\ufb3e\\ufb40\\ufb41\\ufb43\\ufb44\\ufb46-\\ufbb1\\ufbd3-\\ufd3d\\ufd50-\\ufd8f\\ufd92-\\ufdc7\\ufdf0-\\ufdfb\\ufe70-\\ufe74\\ufe76-\\ufefc\\uff21-\\uff3a\\uff41-\\uff5a\\uff66-\\uffbe\\uffc2-\\uffc7\\uffca-\\uffcf\\uffd2-\\uffd7\\uffda-\\uffdc]|\\\\[u][0-9a-fA-F]{4}|[\\xaa\\xb5\\xba\\xc0-\\xd6\\xd8-\\xf6\\xf8-\\u02c1\\u02c6-\\u02d1\\u02e0-\\u02e4\\u02ec\\u02ee\\u0370-\\u0374\\u0376\\u0377\\u037a-\\u037d\\u0386\\u0388-\\u038a\\u038c\\u038e-\\u03a1\\u03a3-\\u03f5\\u03f7-\\u0481\\u048a-\\u0527\\u0531-\\u0556\\u0559\\u0561-\\u0587\\u05d0-\\u05ea\\u05f0-\\u05f2\\u0620-\\u064a\\u066e\\u066f\\u0671-\\u06d3\\u06d5\\u06e5\\u06e6\\u06ee\\u06ef\\u06fa-\\u06fc\\u06ff\\u0710\\u0712-\\u072f\\u074d-\\u07a5\\u07b1\\u07ca-\\u07ea\\u07f4\\u07f5\\u07fa\\u0800-\\u0815\\u081a\\u0824\\u0828\\u0840-\\u0858\\u08a0\\u08a2-\\u08ac\\u0904-\\u0939\\u093d\\u0950\\u0958-\\u0961\\u0971-\\u0977\\u0979-\\u097f\\u0985-\\u098c\\u098f\\u0990\\u0993-\\u09a8\\u09aa-\\u09b0\\u09b2\\u09b6-\\u09b9\\u09bd\\u09ce\\u09dc\\u09dd\\u09df-\\u09e1\\u09f0\\u09f1\\u0a05-\\u0a0a\\u0a0f\\u0a10\\u0a13-\\u0a28\\u0a2a-\\u0a30\\u0a32\\u0a33\\u0a35\\u0a36\\u0a38\\u0a39\\u0a59-\\u0a5c\\u0a5e\\u0a72-\\u0a74\\u0a85-\\u0a8d\\u0a8f-\\u0a91\\u0a93-\\u0aa8\\u0aaa-\\u0ab0\\u0ab2\\u0ab3\\u0ab5-\\u0ab9\\u0abd\\u0ad0\\u0ae0\\u0ae1\\u0b05-\\u0b0c\\u0b0f\\u0b10\\u0b13-\\u0b28\\u0b2a-\\u0b30\\u0b32\\u0b33\\u0b35-\\u0b39\\u0b3d\\u0b5c\\u0b5d\\u0b5f-\\u0b61\\u0b71\\u0b83\\u0b85-\\u0b8a\\u0b8e-\\u0b90\\u0b92-\\u0b95\\u0b99\\u0b9a\\u0b9c\\u0b9e\\u0b9f\\u0ba3\\u0ba4\\u0ba8-\\u0baa\\u0bae-\\u0bb9\\u0bd0\\u0c05-\\u0c0c\\u0c0e-\\u0c10\\u0c12-\\u0c28\\u0c2a-\\u0c33\\u0c35-\\u0c39\\u0c3d\\u0c58\\u0c59\\u0c60\\u0c61\\u0c85-\\u0c8c\\u0c8e-\\u0c90\\u0c92-\\u0ca8\\u0caa-\\u0cb3\\u0cb5-\\u0cb9\\u0cbd\\u0cde\\u0ce0\\u0ce1\\u0cf1\\u0cf2\\u0d05-\\u0d0c\\u0d0e-\\u0d10\\u0d12-\\u0d3a\\u0d3d\\u0d4e\\u0d60\\u0d61\\u0d7a-\\u0d7f\\u0d85-\\u0d96\\u0d9a-\\u0db1\\u0db3-\\u0dbb\\u0dbd\\u0dc0-\\u0dc6\\u0e01-\\u0e30\\u0e32\\u0e33\\u0e40-\\u0e46\\u0e81\\u0e82\\u0e84\\u0e87\\u0e88\\u0e8a\\u0e8d\\u0e94-\\u0e97\\u0e99-\\u0e9f\\u0ea1-\\u0ea3\\u0ea5\\u0ea7\\u0eaa\\u0eab\\u0ead-\\u0eb0\\u0eb2\\u0eb3\\u0ebd\\u0ec0-\\u0ec4\\u0ec6\\u0edc-\\u0edf\\u0f00\\u0f40-\\u0f47\\u0f49-\\u0f6c\\u0f88-\\u0f8c\\u1000-\\u102a\\u103f\\u1050-\\u1055\\u105a-\\u105d\\u1061\\u1065\\u1066\\u106e-\\u1070\\u1075-\\u1081\\u108e\\u10a0-\\u10c5\\u10c7\\u10cd\\u10d0-\\u10fa\\u10fc-\\u1248\\u124a-\\u124d\\u1250-\\u1256\\u1258\\u125a-\\u125d\\u1260-\\u1288\\u128a-\\u128d\\u1290-\\u12b0\\u12b2-\\u12b5\\u12b8-\\u12be\\u12c0\\u12c2-\\u12c5\\u12c8-\\u12d6\\u12d8-\\u1310\\u1312-\\u1315\\u1318-\\u135a\\u1380-\\u138f\\u13a0-\\u13f4\\u1401-\\u166c\\u166f-\\u167f\\u1681-\\u169a\\u16a0-\\u16ea\\u16ee-\\u16f0\\u1700-\\u170c\\u170e-\\u1711\\u1720-\\u1731\\u1740-\\u1751\\u1760-\\u176c\\u176e-\\u1770\\u1780-\\u17b3\\u17d7\\u17dc\\u1820-\\u1877\\u1880-\\u18a8\\u18aa\\u18b0-\\u18f5\\u1900-\\u191c\\u1950-\\u196d\\u1970-\\u1974\\u1980-\\u19ab\\u19c1-\\u19c7\\u1a00-\\u1a16\\u1a20-\\u1a54\\u1aa7\\u1b05-\\u1b33\\u1b45-\\u1b4b\\u1b83-\\u1ba0\\u1bae\\u1baf\\u1bba-\\u1be5\\u1c00-\\u1c23\\u1c4d-\\u1c4f\\u1c5a-\\u1c7d\\u1ce9-\\u1cec\\u1cee-\\u1cf1\\u1cf5\\u1cf6\\u1d00-\\u1dbf\\u1e00-\\u1f15\\u1f18-\\u1f1d\\u1f20-\\u1f45\\u1f48-\\u1f4d\\u1f50-\\u1f57\\u1f59\\u1f5b\\u1f5d\\u1f5f-\\u1f7d\\u1f80-\\u1fb4\\u1fb6-\\u1fbc\\u1fbe\\u1fc2-\\u1fc4\\u1fc6-\\u1fcc\\u1fd0-\\u1fd3\\u1fd6-\\u1fdb\\u1fe0-\\u1fec\\u1ff2-\\u1ff4\\u1ff6-\\u1ffc\\u2071\\u207f\\u2090-\\u209c\\u2102\\u2107\\u210a-\\u2113\\u2115\\u2119-\\u211d\\u2124\\u2126\\u2128\\u212a-\\u212d\\u212f-\\u2139\\u213c-\\u213f\\u2145-\\u2149\\u214e\\u2160-\\u2188\\u2c00-\\u2c2e\\u2c30-\\u2c5e\\u2c60-\\u2ce4\\u2ceb-\\u2cee\\u2cf2\\u2cf3\\u2d00-\\u2d25\\u2d27\\u2d2d\\u2d30-\\u2d67\\u2d6f\\u2d80-\\u2d96\\u2da0-\\u2da6\\u2da8-\\u2dae\\u2db0-\\u2db6\\u2db8-\\u2dbe\\u2dc0-\\u2dc6\\u2dc8-\\u2dce\\u2dd0-\\u2dd6\\u2dd8-\\u2dde\\u2e2f\\u3005-\\u3007\\u3021-\\u3029\\u3031-\\u3035\\u3038-\\u303c\\u3041-\\u3096\\u309d-\\u309f\\u30a1-\\u30fa\\u30fc-\\u30ff\\u3105-\\u312d\\u3131-\\u318e\\u31a0-\\u31ba\\u31f0-\\u31ff\\u3400-\\u4db5\\u4e00-\\u9fcc\\ua000-\\ua48c\\ua4d0-\\ua4fd\\ua500-\\ua60c\\ua610-\\ua61f\\ua62a\\ua62b\\ua640-\\ua66e\\ua67f-\\ua697\\ua6a0-\\ua6ef\\ua717-\\ua71f\\ua722-\\ua788\\ua78b-\\ua78e\\ua790-\\ua793\\ua7a0-\\ua7aa\\ua7f8-\\ua801\\ua803-\\ua805\\ua807-\\ua80a\\ua80c-\\ua822\\ua840-\\ua873\\ua882-\\ua8b3\\ua8f2-\\ua8f7\\ua8fb\\ua90a-\\ua925\\ua930-\\ua946\\ua960-\\ua97c\\ua984-\\ua9b2\\ua9cf\\uaa00-\\uaa28\\uaa40-\\uaa42\\uaa44-\\uaa4b\\uaa60-\\uaa76\\uaa7a\\uaa80-\\uaaaf\\uaab1\\uaab5\\uaab6\\uaab9-\\uaabd\\uaac0\\uaac2\\uaadb-\\uaadd\\uaae0-\\uaaea\\uaaf2-\\uaaf4\\uab01-\\uab06\\uab09-\\uab0e\\uab11-\\uab16\\uab20-\\uab26\\uab28-\\uab2e\\uabc0-\\uabe2\\uac00-\\ud7a3\\ud7b0-\\ud7c6\\ud7cb-\\ud7fb\\uf900-\\ufa6d\\ufa70-\\ufad9\\ufb00-\\ufb06\\ufb13-\\ufb17\\ufb1d\\ufb1f-\\ufb28\\ufb2a-\\ufb36\\ufb38-\\ufb3c\\ufb3e\\ufb40\\ufb41\\ufb43\\ufb44\\ufb46-\\ufbb1\\ufbd3-\\ufd3d\\ufd50-\\ufd8f\\ufd92-\\ufdc7\\ufdf0-\\ufdfb\\ufe70-\\ufe74\\ufe76-\\ufefc\\uff21-\\uff3a\\uff41-\\uff5a\\uff66-\\uffbe\\uffc2-\\uffc7\\uffca-\\uffcf\\uffd2-\\uffd7\\uffda-\\uffdc0-9\\u0300-\\u036f\\u0483-\\u0487\\u0591-\\u05bd\\u05bf\\u05c1\\u05c2\\u05c4\\u05c5\\u05c7\\u0610-\\u061a\\u064b-\\u0669\\u0670\\u06d6-\\u06dc\\u06df-\\u06e4\\u06e7\\u06e8\\u06ea-\\u06ed\\u06f0-\\u06f9\\u0711\\u0730-\\u074a\\u07a6-\\u07b0\\u07c0-\\u07c9\\u07eb-\\u07f3\\u0816-\\u0819\\u081b-\\u0823\\u0825-\\u0827\\u0829-\\u082d\\u0859-\\u085b\\u08e4-\\u08fe\\u0900-\\u0903\\u093a-\\u093c\\u093e-\\u094f\\u0951-\\u0957\\u0962\\u0963\\u0966-\\u096f\\u0981-\\u0983\\u09bc\\u09be-\\u09c4\\u09c7\\u09c8\\u09cb-\\u09cd\\u09d7\\u09e2\\u09e3\\u09e6-\\u09ef\\u0a01-\\u0a03\\u0a3c\\u0a3e-\\u0a42\\u0a47\\u0a48\\u0a4b-\\u0a4d\\u0a51\\u0a66-\\u0a71\\u0a75\\u0a81-\\u0a83\\u0abc\\u0abe-\\u0ac5\\u0ac7-\\u0ac9\\u0acb-\\u0acd\\u0ae2\\u0ae3\\u0ae6-\\u0aef\\u0b01-\\u0b03\\u0b3c\\u0b3e-\\u0b44\\u0b47\\u0b48\\u0b4b-\\u0b4d\\u0b56\\u0b57\\u0b62\\u0b63\\u0b66-\\u0b6f\\u0b82\\u0bbe-\\u0bc2\\u0bc6-\\u0bc8\\u0bca-\\u0bcd\\u0bd7\\u0be6-\\u0bef\\u0c01-\\u0c03\\u0c3e-\\u0c44\\u0c46-\\u0c48\\u0c4a-\\u0c4d\\u0c55\\u0c56\\u0c62\\u0c63\\u0c66-\\u0c6f\\u0c82\\u0c83\\u0cbc\\u0cbe-\\u0cc4\\u0cc6-\\u0cc8\\u0cca-\\u0ccd\\u0cd5\\u0cd6\\u0ce2\\u0ce3\\u0ce6-\\u0cef\\u0d02\\u0d03\\u0d3e-\\u0d44\\u0d46-\\u0d48\\u0d4a-\\u0d4d\\u0d57\\u0d62\\u0d63\\u0d66-\\u0d6f\\u0d82\\u0d83\\u0dca\\u0dcf-\\u0dd4\\u0dd6\\u0dd8-\\u0ddf\\u0df2\\u0df3\\u0e31\\u0e34-\\u0e3a\\u0e47-\\u0e4e\\u0e50-\\u0e59\\u0eb1\\u0eb4-\\u0eb9\\u0ebb\\u0ebc\\u0ec8-\\u0ecd\\u0ed0-\\u0ed9\\u0f18\\u0f19\\u0f20-\\u0f29\\u0f35\\u0f37\\u0f39\\u0f3e\\u0f3f\\u0f71-\\u0f84\\u0f86\\u0f87\\u0f8d-\\u0f97\\u0f99-\\u0fbc\\u0fc6\\u102b-\\u103e\\u1040-\\u1049\\u1056-\\u1059\\u105e-\\u1060\\u1062-\\u1064\\u1067-\\u106d\\u1071-\\u1074\\u1082-\\u108d\\u108f-\\u109d\\u135d-\\u135f\\u1712-\\u1714\\u1732-\\u1734\\u1752\\u1753\\u1772\\u1773\\u17b4-\\u17d3\\u17dd\\u17e0-\\u17e9\\u180b-\\u180d\\u1810-\\u1819\\u18a9\\u1920-\\u192b\\u1930-\\u193b\\u1946-\\u194f\\u19b0-\\u19c0\\u19c8\\u19c9\\u19d0-\\u19d9\\u1a17-\\u1a1b\\u1a55-\\u1a5e\\u1a60-\\u1a7c\\u1a7f-\\u1a89\\u1a90-\\u1a99\\u1b00-\\u1b04\\u1b34-\\u1b44\\u1b50-\\u1b59\\u1b6b-\\u1b73\\u1b80-\\u1b82\\u1ba1-\\u1bad\\u1bb0-\\u1bb9\\u1be6-\\u1bf3\\u1c24-\\u1c37\\u1c40-\\u1c49\\u1c50-\\u1c59\\u1cd0-\\u1cd2\\u1cd4-\\u1ce8\\u1ced\\u1cf2-\\u1cf4\\u1dc0-\\u1de6\\u1dfc-\\u1dff\\u200c\\u200d\\u203f\\u2040\\u2054\\u20d0-\\u20dc\\u20e1\\u20e5-\\u20f0\\u2cef-\\u2cf1\\u2d7f\\u2de0-\\u2dff\\u302a-\\u302f\\u3099\\u309a\\ua620-\\ua629\\ua66f\\ua674-\\ua67d\\ua69f\\ua6f0\\ua6f1\\ua802\\ua806\\ua80b\\ua823-\\ua827\\ua880\\ua881\\ua8b4-\\ua8c4\\ua8d0-\\ua8d9\\ua8e0-\\ua8f1\\ua900-\\ua909\\ua926-\\ua92d\\ua947-\\ua953\\ua980-\\ua983\\ua9b3-\\ua9c0\\ua9d0-\\ua9d9\\uaa29-\\uaa36\\uaa43\\uaa4c\\uaa4d\\uaa50-\\uaa59\\uaa7b\\uaab0\\uaab2-\\uaab4\\uaab7\\uaab8\\uaabe\\uaabf\\uaac1\\uaaeb-\\uaaef\\uaaf5\\uaaf6\\uabe3-\\uabea\\uabec\\uabed\\uabf0-\\uabf9\\ufb1e\\ufe00-\\ufe0f\\ufe20-\\ufe26\\ufe33\\ufe34\\ufe4d-\\ufe4f\\uff10-\\uff19\\uff3f]|[0-9])*", LRE_FLAG_STICKY);
  /* clang-format on */
  uint8_t* bc;
  BOOL ret = FALSE;
  if((bc = regexp_compile(re, ctx))) {

    ret = regexp_match(bc, str, len, ctx);
    js_free(ctx, bc);
  }

  return ret;
}

BOOL
js_is_identifier_atom(JSContext* ctx, JSAtom atom) {
  const char* str;
  size_t len;
  BOOL ret;
  str = js_atom_to_cstringlen(ctx, &len, atom);
  ret = js_is_identifier_len(ctx, str, len);
  JS_FreeCString(ctx, str);
  return ret;
}

/**
 * @}
 */
