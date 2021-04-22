#ifndef PROPERTY_ENUMERATION_H
#define PROPERTY_ENUMERATION_H

#define _GNU_SOURCE

#include "quickjs.h"
#include "vector.h"
#include "utils.h"

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

int property_enumeration_init(PropertyEnumeration*, JSContext* ctx, JSValue object, int flags);
void property_enumeration_reset(PropertyEnumeration*, JSRuntime* rt);
void property_enumeration_dump(PropertyEnumeration*, JSContext* ctx, DynBuf* out);
void property_enumeration_dumpall(Vector*, JSContext* ctx, DynBuf* out);
JSValue property_enumeration_path_tostring(JSContext*, JSValue this_val, int argc, JSValue argv[]);
PropertyEnumeration* property_enumeration_recurse(Vector*, JSContext* ctx);
int32_t property_enumeration_depth(JSContext*, JSValue object);
JSValue property_enumeration_path(Vector*, JSContext* ctx);
void property_enumeration_pathstr(Vector*, JSContext* ctx, DynBuf* buf);
JSValue property_enumeration_pathstr_value(Vector*, JSContext* ctx);
int property_enumeration_insideof(Vector*, JSValue val);
void property_enumeration_free(Vector*, JSRuntime* rt);
int property_enumeration_predicate(PropertyEnumeration*, JSContext* ctx, JSValue fn, JSValue this_arg);
JSValue property_enumeration_key(PropertyEnumeration*, JSContext* ctx);

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

static inline void
property_enumeration_sort(PropertyEnumeration* it, JSContext* ctx) {
  qsort_r(it->tab_atom, it->tab_atom_len, sizeof(JSPropertyEnum), &js_propenum_cmp, ctx);
}

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

static inline int32_t
property_enumeration_level(const PropertyEnumeration* it, const Vector* vec) {
  return it - (const PropertyEnumeration*)vec->data;
}

#endif /* defined(PROPERTY_ENUMERATION_H) */
