#ifndef PROPERTY_ENUMERATION_H
#define PROPERTY_ENUMERATION_H

#define _GNU_SOURCE

#include "quickjs.h"
#include "vector.h"
#include "utils.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct PropertyEnumeration {
  JSValue obj;
  uint32_t idx;
  uint32_t tab_atom_len;
  JSPropertyEnum* tab_atom;
  BOOL is_array;
} PropertyEnumeration;

#define PROPENUM_SORT_ATOMS (1 << 6)

#define PROPENUM_DEFAULT_FLAGS (JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY)

#define property_enumeration_new(vec) vector_emplace((vec), sizeof(PropertyEnumeration))
#define property_enumeration_length(enum) ((enum)->tab_atom_len)
#define property_enumeration_index(enum) ((enum)->idx)

static inline int
compare_jspropertyenum(JSPropertyEnum* a, JSPropertyEnum* b) {
  return a->atom < b->atom ? -1 : a->atom > b->atom ? 1 : 0;
}

int property_enumeration_init(PropertyEnumeration* it, JSContext* ctx, JSValueConst object, int flags);

void property_enumeration_reset(PropertyEnumeration* it, JSRuntime* rt);

static inline JSValue
property_enumeration_value(PropertyEnumeration* it, JSContext* ctx) {
  assert(it->idx < it->tab_atom_len);
  return JS_GetProperty(ctx, it->obj, it->tab_atom[it->idx].atom);
}

static inline const char*
property_enumeration_valuestr(PropertyEnumeration* it, JSContext* ctx) {
  JSValue value = property_enumeration_value(it, ctx);
  const char* str = JS_ToCString(ctx, value);
  js_value_free(ctx, value);
  return str;
}

static inline const char*
property_enumeration_valuestrlen(PropertyEnumeration* it, size_t* len, JSContext* ctx) {
  JSValue value = property_enumeration_value(it, ctx);
  const char* str = JS_ToCStringLen(ctx, len, value);
  js_value_free(ctx, value);
  return str;
}

static inline enum value_mask
property_enumeration_type(PropertyEnumeration* it, JSContext* ctx) {
  JSValue value = property_enumeration_value(it, ctx);
  enum value_mask ret = js_value_type(ctx, value);
  JS_FreeValue(ctx, value);
  return ret;
}

static inline JSAtom
property_enumeration_atom(PropertyEnumeration* it) {
  assert(it->idx < it->tab_atom_len);
  return it->tab_atom[it->idx].atom;
}

static inline JSValue
property_enumeration_key(PropertyEnumeration* it, JSContext* ctx) {
  JSValue key;
  assert(it->idx < it->tab_atom_len);
  key = JS_AtomToValue(ctx, it->tab_atom[it->idx].atom);
  if(it->is_array) {
    int64_t idx;
    JS_ToInt64(ctx, &idx, key);
    js_value_free(ctx, key);
    key = JS_NewInt64(ctx, idx);
  }
  return key;
}

static inline const char*
property_enumeration_keystr(PropertyEnumeration* it, JSContext* ctx) {
  assert(it->idx < it->tab_atom_len);
  return JS_AtomToCString(ctx, it->tab_atom[it->idx].atom);
}

static inline const char*
property_enumeration_keystrlen(PropertyEnumeration* it, size_t* len, JSContext* ctx) {
  assert(it->idx < it->tab_atom_len);
  return js_atom_to_cstringlen(ctx, len, it->tab_atom[it->idx].atom);
}

static inline int
property_enumeration_setpos(PropertyEnumeration* it, int32_t idx) {
  if(idx < 0)
    idx += it->tab_atom_len;
  if(idx >= (int32_t)it->tab_atom_len)
    return 0;
  assert((uint32_t)idx < it->tab_atom_len);
  it->idx = idx;
  return 1;
}

static inline int
property_enumeration_predicate(PropertyEnumeration* it, JSContext* ctx, JSValueConst fn, JSValueConst this_arg) {
  BOOL result;
  JSValue ret;
  JSValueConst argv[3];
  argv[0] = property_enumeration_value(it, ctx);
  argv[1] = property_enumeration_key(it, ctx);
  argv[2] = this_arg;
  ret = JS_Call(ctx, fn, JS_UNDEFINED, 3, argv);
  result = JS_ToBool(ctx, ret);
  js_value_free(ctx, argv[0]);
  js_value_free(ctx, argv[1]);
  js_value_free(ctx, ret);
  return result;
}

static inline void
property_enumeration_sort(PropertyEnumeration* it, JSContext* ctx) {
  qsort_r(it->tab_atom, it->tab_atom_len, sizeof(JSPropertyEnum), &js_propenum_cmp, ctx);
}

void property_enumeration_dump(PropertyEnumeration* it, JSContext* ctx, DynBuf* out);

static inline PropertyEnumeration*
property_enumeration_next(PropertyEnumeration* it) {
  return property_enumeration_setpos(it, it->idx + 1) ? it : 0;
}

static inline PropertyEnumeration*
property_enumeration_push(Vector* vec, JSContext* ctx, JSValue object, int flags) {
  PropertyEnumeration* it;

  if(!JS_IsObject(object)) {
    JS_ThrowTypeError(ctx, "not an object");
    return 0;
  }
  if((it = vector_emplace(vec, sizeof(PropertyEnumeration)))) {
    property_enumeration_init(it, ctx, object, flags);
    return vector_back(vec, sizeof(PropertyEnumeration));
  }

  return it;
}

static inline PropertyEnumeration*
property_enumeration_pop(Vector* vec, JSContext* ctx) {
  PropertyEnumeration* it;
  assert(!vector_empty(vec));
  it = vector_back(vec, sizeof(PropertyEnumeration));
  property_enumeration_reset(it, JS_GetRuntime(ctx));
  vector_pop(vec, sizeof(PropertyEnumeration));
  return vector_empty(vec) ? 0 : it - 1;
}

static inline PropertyEnumeration*
property_enumeration_enter(Vector* vec, JSContext* ctx, int flags) {
  PropertyEnumeration* it;
  JSValue value;

  assert(!vector_empty(vec));
  it = vector_back(vec, sizeof(PropertyEnumeration));
  value = property_enumeration_value(it, ctx);

  return property_enumeration_push(vec, ctx, value, flags);
}

void property_enumeration_dumpall(Vector* vec, JSContext* ctx, DynBuf* out);

JSValue property_enumeration_path_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]);

static inline JSValue
property_enumeration_path(Vector* vec, JSContext* ctx) {
  JSValue ret;
  PropertyEnumeration* it;
  size_t i = 0;
  ret = JS_NewArray(ctx);
  vector_foreach_t(vec, it) {
    JSValue key = property_enumeration_key(it, ctx);
    JS_SetPropertyUint32(ctx, ret, i++, key);
  }
  JS_DefinePropertyValueStr(ctx,
                            ret,
                            "toString",
                            JS_NewCFunction(ctx, property_enumeration_path_tostring, "toString", 0),
                            JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
  return ret;
}

static inline void
property_enumeration_pathstr(Vector* vec, JSContext* ctx, DynBuf* buf) {
  PropertyEnumeration* it;
  size_t i = 0;

  vector_foreach_t(vec, it) {
    const char* key;
    if(i++ > 0)
      dbuf_putc(buf, '.');

    key = property_enumeration_keystr(it, ctx);
    dbuf_putstr(buf, key);
    js_cstring_free(ctx, key);
  }
  dbuf_0(buf);
}

static inline JSValue
property_enumeration_pathstr_value(Vector* vec, JSContext* ctx) {
  DynBuf dbuf;
  JSValue ret;
  js_dbuf_init(ctx, &dbuf);
  property_enumeration_pathstr(vec, ctx, &dbuf);
  dbuf_0(&dbuf);
  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
  return ret;
}

/*static void
property_enumeration_pointer(Vector* vec, JSContext* ctx, struct Pointer* ptr) {
  pointer_fromarray(ptr, ctx, property_enumeration_path(vec,ctx));
}*/

static inline int
property_enumeration_insideof(Vector* vec, JSValueConst val) {
  PropertyEnumeration* it;
  void* obj = JS_VALUE_GET_OBJ(val);
  vector_foreach_t(vec, it) {
    void* obj2 = JS_VALUE_GET_OBJ(it->obj);
    if(obj == obj2)
      return 1;
  }
  return 0;
}

PropertyEnumeration* property_enumeration_recurse(Vector* vec, JSContext* ctx);

static inline int32_t
property_enumeration_level(const PropertyEnumeration* it, const Vector* vec) {
  return it - (const PropertyEnumeration*)vec->data;
}

static inline void
property_enumeration_free(Vector* vec, JSRuntime* rt) {
  PropertyEnumeration *it, *end;

  it = vector_begin(vec);
  end = vector_end(vec);

  while(it++ != end) property_enumeration_reset(it, rt);
  // vector_free(vec);
}

int32_t property_enumeration_depth(JSContext* ctx, JSValueConst object);
#endif /* defined(PROPERTY_ENUMERATION_H) */
