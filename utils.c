#undef _ISOC99_SOURCE
#define _ISOC99_SOURCE 1

#include "utils.h"
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
    re.flags =
        regexp_flags_fromstring((flagstr = js_get_propertystr_cstring(ctx, argv[0], "flags")));
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
  if(!(bytecode =
           lre_compile(&len, error_msg, sizeof(error_msg), re.source, re.len, re.flags, ctx)))
    JS_ThrowInternalError(
        ctx, "Error compiling regex /%.*s/: %s", (int)re.len, re.source, error_msg);

  return bytecode;
}

JSValue
regexp_to_value(RegExp re, JSContext* ctx) {
  char flagstr[32] = {0};
  size_t flaglen = regexp_flags_tostring(re.flags, flagstr);
  JSValueConst args[2] = {JS_NewStringLen(ctx, re.source, re.len),
                          JS_NewStringLen(ctx, flagstr, flaglen)};
  JSValue regex, ctor = js_global_get_str(ctx, "RegExp");
  regex = JS_CallConstructor(ctx, ctor, 2, args);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  return regex;
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
  if(JS_GetOwnPropertyNames(ctx,
                            &atoms_a,
                            &natoms_a,
                            a,
                            JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY))
    return FALSE;

  if(JS_GetOwnPropertyNames(ctx,
                            &atoms_b,
                            &natoms_b,
                            b,
                            JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY))
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
  JSValueConst args[] = {JS_NewString(ctx, message)};
  JSValue ret = js_object_new(ctx, "Error", 1, args);
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

JSValue
js_object_new(JSContext* ctx, const char* class_name, int argc, JSValueConst argv[]) {
  JSValue ctor = js_global_get_str(ctx, class_name);
  JSValue obj = JS_CallConstructor(ctx, ctor, argc, argv);
  JS_FreeValue(ctx, ctor);
  return obj;
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
js_get_propertystr_cstringlen(JSContext* ctx,
                              JSValueConst obj,
                              const char* prop,
                              size_t* lenp) {
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
js_set_propertystr_stringlen(
    JSContext* ctx, JSValueConst obj, const char* prop, const char* str, size_t len) {
  JSValue value;
  value = JS_NewStringLen(ctx, str, len);
  JS_SetPropertyStr(ctx, obj, prop, value);
}

int
js_get_propertydescriptor(JSContext* ctx,
                          JSPropertyDescriptor* desc,
                          JSValueConst value,
                          JSAtom prop) {
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

const char*
js_value_type_name(int32_t type) {
  int32_t flag = js_value_type2flag(type);
  const char* const types[] = {
      "UNDEFINED",     "0",
      "BOOL",          "INT",
      "OBJECT",        "STRING",
      "SYMBOL",        "BIG_FLOAT",
      "BIG_INT",       "BIG_DECIMAL",
      "FLOAT64",       "NAN",
      "FUNCTION",      "ARRAY",
      "MODULE",        "FUNCTION_BYTECODE",
      "UNINITIALIZED", "CATCH_OFFSET",
      "EXCEPTION",
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
    case TYPE_STRING:
    case TYPE_SYMBOL:
    case TYPE_BIG_DECIMAL:
    case TYPE_BIG_INT:
    case TYPE_BIG_FLOAT: {
      ret = JS_DupValue(ctx, value);
      break;
    }
    default: {
      ret =
          JS_ThrowTypeError(ctx, "No such type: %s (0x%08x)\n", js_value_type_name(type), type);
      break;
    }
  }
  return ret;
}

void
js_value_fwrite(JSContext* ctx, JSValueConst val, FILE* f) {
  DynBuf dbuf = {0};
  size_t n;
  js_dbuf_init(ctx, &dbuf);
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
call_module_func(JSContext* ctx,
                 JSValueConst this_val,
                 int argc,
                 JSValueConst* argv,
                 int magic,
                 JSValue* data) {
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
      JSValue export =
          ref ? JS_DupValue(ctx, ref->pvalue ? *ref->pvalue : ref->value) : JS_UNDEFINED;
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
    if(ref) {
      JSValue export = JS_DupValue(ctx, ref->pvalue ? *ref->pvalue : ref->value);
      JSAtom name = entry->export_name;
      if(rename_default && name == def)
        name = m->module_name;
      JS_SetProperty(ctx, exports, name, export);
    }
  }
  JS_FreeAtom(ctx, def);
}

JSValue
module_exports(JSContext* ctx, JSModuleDef* m) {
  JSValue exports;
  exports = JS_NewObjectProto(ctx, JS_NULL);
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
    if(str[0] != '<')
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
    char* name = module_namestr(ctx, m);
    JSValue entry = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, entry, 0, JS_NewString(ctx, /*basename*/ (name)));
    JS_SetPropertyUint32(ctx, entry, 1, magic ? module_entry(ctx, m) : module_value(ctx, m));
    if(name[0] != '<')
      JS_SetPropertyUint32(ctx, ret, i++, entry);
    else
      JS_FreeValue(ctx, entry);
    js_free(ctx, name);
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
    if(name[0] != '<')
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

char*
js_module_search(JSContext* ctx, const char* search_path, const char* module) {
  size_t len;
  char* path = 0;

  while(!strncmp(module, "./", 2)) module = trim_dotslash(module);
  len = strlen(module);

  if(!str_contains(module, '/') || str_ends(module, ".so"))
    path = js_module_search_ext(ctx, search_path, module, ".so");

  if(!path)
    path = js_module_search_ext(ctx, search_path, module, ".js");

  return path;
}

char*
js_module_search_ext(JSContext* ctx, const char* path, const char* name, const char* ext) {
  const char *p, *q;
  char* file = 0;
  size_t i, j;
  struct stat st;

  for(p = path; *p; p = q) {
    if((q = strchr(p, ':')) == 0)
      q = p + strlen(p);
    i = q - p;
    file = js_malloc(ctx, i + 1 + strlen(name) + 3 + 1);
    strncpy(file, p, i);
    file[i] = '/';
    strcpy(&file[i + 1], name);
    j = strlen(name);
    if(!(j >= 3 && !strcmp(&name[j - 3], ext)))
      strcpy(&file[i + 1 + j], ext);
    if(!stat(file, &st))
      return file;
    js_free(ctx, file);
    if(*q == ':')
      ++q;
  }
  return 0;
}

char*
js_module_normalize(JSContext* ctx, const char* path, const char* name, void* opaque) {
  size_t p;
  const char* r;
  DynBuf file = {0, 0, 0};
  size_t n;
  if(name[0] != '.')
    return js_strdup(ctx, name);
  js_dbuf_init(ctx, &file);
  n = path[(p = str_rchr(path, '/'))] ? p : 0;
  dbuf_put(&file, (const uint8_t*)path, n);
  dbuf_0(&file);
  for(r = name;;) {
    if(r[0] == '.' && r[1] == '/') {
      r += 2;
    } else if(r[0] == '.' && r[1] == '.' && r[2] == '/') {
      if(file.size == 0)
        break;
      if((p = byte_rchr(file.buf, file.size, '/')) < file.size)
        p++;
      else
        p = 0;
      if(!strcmp((const char*)&file.buf[p], ".") || !strcmp((const char*)&file.buf[p], ".."))
        break;
      if(p > 0)
        p--;
      file.size = p;
      r += 3;
    } else {
      break;
    }
  }
  if(file.size == 0)
    dbuf_putc(&file, '.');
  dbuf_putc(&file, '/');
  dbuf_putstr(&file, r);
  dbuf_0(&file);
  return (char*)file.buf;
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
js_module_find(JSContext* ctx, const char* name) {
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

static void
js_import_directive(JSContext* ctx, ImportDirective imp, DynBuf* db) {
  BOOL has_prop = imp.prop && imp.prop[0];
  BOOL is_ns = imp.spec && imp.spec[0] == '*';
  BOOL is_default = str_equal(imp.spec, "default");
  const char *var, *base = basename(imp.path);
  size_t blen = str_chr(base, '.');
  dbuf_putstr(db, "import ");
  if(imp.spec) {
    if(!is_default)
      dbuf_putstr(db, imp.spec);
    if(is_ns) {
      if(!imp.ns) {
        char* x;
        imp.ns = js_strndup(ctx, base, blen);
        for(x = (char*)imp.ns; *x; x++)
          if(!is_identifier_char(*x))
            *x = '_';
      }
      dbuf_putstr(db, " as ");
    }
  }
  if(imp.spec == 0 || str_equal(imp.spec, "default")) {
    if(!imp.ns)
      imp.ns = js_strndup(ctx, base, blen);
  }
  if(imp.ns)
    dbuf_putm(db, imp.ns, 0);
  if(imp.path)
    dbuf_putm(db, " from '", imp.path, "'", 0);
  if(!(var = imp.var))
    if(!(var = imp.ns))
      var = imp.spec;
  dbuf_putstr(db, ";\n");

  if((has_prop || is_ns || is_default) && var[0] != '*') {
    dbuf_putm(db,
              "globalThis.",
              var,
              " = ",
              imp.ns ? imp.ns : imp.spec,
              imp.prop && *imp.prop ? "." : 0,
              imp.prop,
              0);
  } else {
    dbuf_putm(db, "Object.assign(globalThis, ", imp.ns ? imp.ns : imp.spec, 0);
    dbuf_putc(db, ')');
  }
  dbuf_putm(db, ";", 0);
  dbuf_0(db);
}

JSValue
js_import_load(JSContext* ctx, ImportDirective imp) {
  DynBuf buf;
  char* code;
  js_dbuf_init(ctx, &buf);
  js_import_directive(ctx, imp, &buf);
  code = str_escape((const char*)buf.buf);
  printf("js_import_eval: '%s'\n", code);
  free(code);
  return JS_Eval(ctx,
                 (const char*)buf.buf,
                 buf.size,
                 imp.args[0],
                 JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
}

JSValue
js_import_eval(JSContext* ctx, ImportDirective imp) {
  DynBuf buf;
  char* code;
  js_dbuf_init(ctx, &buf);
  js_import_directive(ctx, imp, &buf);
  code = str_escape((const char*)buf.buf);
  printf("js_import_eval: '%s'\n", code);
  free(code);
  return JS_Eval(ctx, buf.buf, buf.size, imp.args[0], JS_EVAL_TYPE_MODULE);
}

JSModuleDef*
js_module_import_default(JSContext* ctx, const char* path, const char* var) {
  JSValue ret;

  ret = js_import_eval(ctx,
                       (ImportDirective){
                           .path = path,
                           .spec = "default",
                           .ns = 0,
                           .prop = 0,
                           .var = 0,
                       });

  /* if(JS_IsException(ret)) {
     fprintf(stderr, "EXCEPTION: ", JS_ToCString(ctx, ret));
     return 0;
   }

   if(JS_VALUE_GET_TAG(ret) == JS_TAG_MODULE)
     return JS_VALUE_GET_PTR(ret);*/

  return js_module_find(ctx, path);
}

JSModuleDef*
js_module_import_namespace(JSContext* ctx, const char* path, const char* ns) {
  js_import_eval(ctx,
                 (ImportDirective){
                     .path = path,
                     .spec = "*",
                     .ns = ns,
                     .prop = 0,
                     .var = 0,
                 });
  return js_module_find(ctx, path);
}

JSValue
js_module_import(
    JSContext* ctx, const char* path, const char* ns, const char* var, const char* prop) {
  DynBuf buf;
  const char* name;
  size_t len, nslen;
  name = basename(path);
  len = 0;
  while(name[len] && is_identifier_char(name[len])) ++len;
  nslen = ns ? strlen(ns) : len;
  ns = ns ? js_strdup(ctx, ns) : js_strndup(ctx, name, len);
  js_dbuf_init(ctx, &buf);
  dbuf_printf(&buf,
              "import %s%s from '%s'; globalThis.%s = %s",
              ns ? "* as " : "",
              ns,
              path,
              var ? var : ns,
              ns);

  if(prop && *prop) {
    dbuf_putc(&buf, '.');
    dbuf_putstr(&buf, prop);
  }
  dbuf_putc(&buf, ';');
  dbuf_0(&buf);
  // printf("js_module_import: '%s'\n", buf.buf);
  return js_eval_buf(ctx, buf.buf, buf.size, 0, JS_EVAL_TYPE_MODULE);
}

BOOL
js_is_arraybuffer(JSContext* ctx, JSValueConst value) {
  BOOL ret = FALSE;
  if(!JS_IsObject(value))
    return ret;
  if(!ret)
    ret |= js_value_isclass(ctx, value, JS_CLASS_ARRAY_BUFFER);
  if(!ret)
    ret |= js_object_is(ctx, value, "[object ArrayBuffer]");
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
  return JS_IsObject(value) && (js_value_isclass(ctx, value, JS_CLASS_PROMISE) ||
                                js_object_is(ctx, value, "[object Promise]"));
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
js_invoke(
    JSContext* ctx, JSValueConst this_obj, const char* method, int argc, JSValueConst argv[]) {
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
js_eval_buf(
    JSContext* ctx, const void* buf, int buf_len, const char* filename, int eval_flags) {
  JSValue val;

  if((eval_flags & JS_EVAL_TYPE_MASK) == JS_EVAL_TYPE_MODULE) {
    /* for the modules, we compile then run to be able to set import.meta */
    val = JS_Eval(ctx,
                  buf,
                  buf_len,
                  filename ? filename : "<input>",
                  eval_flags | JS_EVAL_FLAG_COMPILE_ONLY);
    if(!JS_IsException(val)) {
      js_module_set_import_meta(ctx, val, !!filename, TRUE);
      /*val = */ JS_EvalFunction(ctx, val);
    }
  } else {
    val = JS_Eval(ctx, buf, buf_len, filename, eval_flags);
  }
  if(JS_IsException(val))
    js_error_print(ctx, JS_GetException(ctx));
  return val;
}

int
js_eval_str(JSContext* ctx, const char* str, const char* file, int flags) {
  JSValue val = js_eval_buf(ctx, str, strlen(str), file, flags);
  int32_t ret = -1;
  if(JS_IsNumber(val))
    JS_ToInt32(ctx, &ret, val);
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
    const char* type =
        JS_IsObject(error) ? js_object_classname(ctx, error) : js_value_typestr(ctx, error);

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
  js_dbuf_init(ctx, &db);
  js_error_dump(ctx, error, &db);
  return (char*)db.buf;
}

void
js_error_print(JSContext* ctx, JSValueConst error) {
  const char *str, *stack = 0;

  if(JS_IsObject(error)) {
    JSValue st = JS_GetPropertyStr(ctx, error, "stack");

    if(!JS_IsUndefined(st))
      stack = JS_ToCString(ctx, st);

    JS_FreeValue(ctx, st);
  }

  if((str = JS_ToCString(ctx, error))) {
    const char* type =
        JS_IsObject(error) ? js_object_classname(ctx, error) : js_value_typestr(ctx, error);
    const char* exception = str;
    size_t typelen = strlen(type);

    if(!strncmp(exception, type, typelen) && exception[typelen] == ':') {
      exception += typelen + 2;
    }
    // printf("%s: %s\n", type, exception);
    //  if(stack) printf("STACK=\n%s\n", stack);
    fflush(stdout);
  }
  if(stack)
    JS_FreeCString(ctx, stack);
  JS_FreeCString(ctx, str);
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
