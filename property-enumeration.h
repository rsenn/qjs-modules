#ifndef PROPERTY_ENUMERATION_H
#define PROPERTY_ENUMERATION_H


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

typedef struct {
  int32_t a, b;
} IndexTuple;

#define PROPENUM_SORT_ATOMS (1 << 6)

#define PROPENUM_DEFAULT_FLAGS (JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY)

#define property_enumeration_new(vec) vector_emplace((vec), sizeof(PropertyEnumeration))
//#define property_enumeration_length(enum) ((enum)->tab_atom_len)
#define property_enumeration_index(enum) ((enum)->idx)

static inline int
compare_jspropertyenum(JSPropertyEnum* a, JSPropertyEnum* b) {
  return a->atom < b->atom ? -1 : a->atom > b->atom ? 1 : 0;
}

static inline size_t
property_enumeration_length(PropertyEnumeration* propenum) {
  return propenum->tab_atom_len;
}

int property_enumeration_init(PropertyEnumeration*, JSContext*, JSValue object, int flags);
void property_enumeration_reset(PropertyEnumeration*, JSRuntime*);
void property_enumeration_dump(PropertyEnumeration*, JSContext*, DynBuf* out);
void property_enumeration_dumpall(Vector*, JSContext*, DynBuf* out);
JSValue property_enumeration_path_tostring(JSContext*, JSValue, int argc, JSValue argv[]);
PropertyEnumeration* property_enumeration_recurse(Vector*, JSContext*);
PropertyEnumeration* property_enumeration_skip(Vector*, JSContext*);
static inline int32_t
property_enumeration_depth(Vector* vec) {
  return vector_size(vec, sizeof(PropertyEnumeration));
}
int32_t property_enumeration_deepest(JSContext*, JSValue);
JSValue property_enumeration_path(Vector*, JSContext*);
void property_enumeration_pathstr(Vector*, JSContext*, DynBuf* buf);
JSValue property_enumeration_pathstr_value(Vector*, JSContext*);
int property_enumeration_insideof(Vector*, JSValue);
void property_enumeration_free(Vector*, JSRuntime*);
int property_enumeration_predicate(PropertyEnumeration*, JSContext*, JSValue fn, JSValue this_arg);
JSValue property_enumeration_key(PropertyEnumeration*, JSContext*);
BOOL property_enumeration_circular(Vector*, JSValue);
IndexTuple property_enumeration_check(Vector*);
int property_enumeration_setpos(PropertyEnumeration*, int32_t);
PropertyEnumeration* property_enumeration_push(Vector*, JSContext*, JSValue object, int flags);
PropertyEnumeration* property_enumeration_pop(Vector*, JSContext*);
PropertyEnumeration* property_enumeration_enter(Vector*, JSContext*, int32_t idx, int flags);

static inline JSValue
property_enumeration_value(PropertyEnumeration* it, JSContext* ctx) {
  assert(it->idx < it->tab_atom_len);
  return JS_GetProperty(ctx, it->obj, it->tab_atom[it->idx].atom);
}

static inline const char*
property_enumeration_valuestr(PropertyEnumeration* it, JSContext* ctx) {
  JSValue value = property_enumeration_value(it, ctx);
  const char* str = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);
  return str;
}

static inline const char*
property_enumeration_valuestrlen(PropertyEnumeration* it, size_t* len, JSContext* ctx) {
  JSValue value = property_enumeration_value(it, ctx);
  const char* str = JS_ToCStringLen(ctx, len, value);
  JS_FreeValue(ctx, value);
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

int property_enumeration_setpos(PropertyEnumeration* it, int32_t idx);

static inline void
property_enumeration_sort(PropertyEnumeration* it, JSContext* ctx) {
  qsort_r(it->tab_atom, it->tab_atom_len, sizeof(JSPropertyEnum), &js_propenum_cmp, ctx);
}

static inline PropertyEnumeration*
property_enumeration_next(PropertyEnumeration* it) {
  return property_enumeration_setpos(it, it->idx + 1) ? it : 0;
}

PropertyEnumeration* property_enumeration_push(Vector* vec, JSContext* ctx, JSValue object, int flags);

PropertyEnumeration* property_enumeration_pop(Vector* vec, JSContext* ctx);

PropertyEnumeration* property_enumeration_enter(Vector* vec, JSContext* ctx, int32_t idx, int flags);

static inline int32_t
property_enumeration_level(const PropertyEnumeration* it, const Vector* vec) {
  return it - (const PropertyEnumeration*)vec->data;
}

IndexTuple property_enumeration_check(Vector* vec);

#endif /* defined(PROPERTY_ENUMERATION_H) */
