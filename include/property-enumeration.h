#ifndef PROPERTY_ENUMERATION_H
#define PROPERTY_ENUMERATION_H

#include <quickjs.h>
#include "vector.h"
#include "utils.h"
#include "debug.h"

/**
 * \defgroup property-enumeration Property enumeration utilities
 * @{
 */
typedef struct PropertyEnumeration {
  uint32_t idx;
  uint32_t tab_atom_len;
  JSPropertyEnum* tab_atom;
  JSValue obj, finalizer;
} PropertyEnumeration;

typedef struct {
  int32_t a, b;
} IndexTuple;

#define PROPENUM_SORT_ATOMS (1 << 6)

#define PROPENUM_DEFAULT_FLAGS (JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY)

#define property_enumeration_new(vec) vector_emplace((vec), sizeof(PropertyEnumeration))
#define property_enumeration_index(enum) ((enum)->idx)

typedef JSValue PropEnumPathValueFunc(const Vector*, JSContext*);

static inline int
compare_jspropertyenum(const JSPropertyEnum* a, JSPropertyEnum* b) {
  return a->atom < b->atom ? -1 : a->atom > b->atom ? 1 : 0;
}

int property_enumeration_init(PropertyEnumeration*, JSContext*, JSValue object, int flags);
void property_enumeration_dump(PropertyEnumeration*, JSContext*, DynBuf* out);
void property_enumeration_dumpall(Vector*, JSContext*, DynBuf* out);
JSValue property_enumeration_path_tostring(JSContext*, JSValue, int argc, JSValue argv[]);

PropertyEnumeration* property_enumeration_skip(Vector*, JSContext*);
int32_t property_enumeration_deepest(JSContext*, JSValue, int32_t maxdepth);
JSValue property_enumeration_path(const Vector*, JSContext*);
void property_enumeration_pathstr(const Vector*, JSContext*, DynBuf* buf);
JSValue property_enumeration_pathstr_value(const Vector*, JSContext*);
int property_enumeration_insideof(Vector*, JSValue);
void property_enumeration_free(Vector*, JSRuntime*);
int property_enumeration_predicate(PropertyEnumeration*, JSContext*, JSValue fn, JSValue this_arg);
BOOL property_enumeration_circular(Vector*, JSValue);
IndexTuple property_enumeration_check(Vector*);
IndexTuple property_enumeration_check(Vector* vec);

static inline size_t
property_enumeration_length(const PropertyEnumeration* propenum) {
  return propenum->tab_atom_len;
}

static inline int32_t
property_enumeration_depth(Vector* vec) {
  return vector_size(vec, sizeof(PropertyEnumeration));
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
property_enumeration_reset(PropertyEnumeration* it, JSRuntime* rt) {
  uint32_t i;

  if(it->tab_atom) {
    for(i = 0; i < it->tab_atom_len; i++) JS_FreeAtomRT(rt, it->tab_atom[i].atom);
    orig_js_free_rt(rt, it->tab_atom);
    it->tab_atom = 0;
    it->tab_atom_len = 0;
  }

  JS_FreeValueRT(rt, it->obj);
  it->obj = JS_UNDEFINED;
}

static inline JSValue
property_enumeration_key(const PropertyEnumeration* it, JSContext* ctx) {
  JSValue key;

  assert(it->idx < it->tab_atom_len);

  key = JS_AtomToValue(ctx, it->tab_atom[it->idx].atom);

  if(JS_IsArray(ctx, it->obj)) {
    int64_t idx;
    JS_ToInt64(ctx, &idx, key);
    JS_FreeValue(ctx, key);
    key = JS_NewInt64(ctx, idx);
  }

  return key;
}

static inline PropertyEnumeration*
property_enumeration_push(Vector* vec, JSContext* ctx, JSValue object, int flags) {
  PropertyEnumeration* it;

  assert(JS_IsObject(object));

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

static inline JSValue
property_enumeration_value(const PropertyEnumeration* it, JSContext* ctx) {
  assert(it->idx < it->tab_atom_len);

  return JS_GetProperty(ctx, it->obj, it->tab_atom[it->idx].atom);
}

static inline const char*
property_enumeration_valuestr(const PropertyEnumeration* it, JSContext* ctx) {
  JSValue value = property_enumeration_value(it, ctx);
  const char* str = JS_ToCString(ctx, value);

  JS_FreeValue(ctx, value);

  return str;
}

static inline const char*
property_enumeration_valuestrlen(const PropertyEnumeration* it, size_t* len, JSContext* ctx) {
  JSValue value = property_enumeration_value(it, ctx);
  const char* str = JS_ToCStringLen(ctx, len, value);

  JS_FreeValue(ctx, value);

  return str;
}

static inline ValueTypeMask
property_enumeration_type(const PropertyEnumeration* it, JSContext* ctx) {
  JSValue value = property_enumeration_value(it, ctx);
  ValueTypeMask ret = js_value_type(ctx, value);

  JS_FreeValue(ctx, value);

  return ret;
}

static inline JSAtom
property_enumeration_atom(const PropertyEnumeration* it) {
  assert(it->idx < it->tab_atom_len);

  return it->tab_atom[it->idx].atom;
}

static inline const char*
property_enumeration_keystr(const PropertyEnumeration* it, JSContext* ctx) {
  assert(it->idx < it->tab_atom_len);

  return JS_AtomToCString(ctx, it->tab_atom[it->idx].atom);
}

static inline const char*
property_enumeration_keystrlen(const PropertyEnumeration* it, size_t* len, JSContext* ctx) {
  assert(it->idx < it->tab_atom_len);

  return js_atom_to_cstringlen(ctx, len, it->tab_atom[it->idx].atom);
}

static inline void
property_enumeration_sort(PropertyEnumeration* it, JSContext* ctx) {
  quicksort_r(it->tab_atom, it->tab_atom_len, sizeof(JSPropertyEnum), &js_propenum_cmp, ctx);
}

static inline PropertyEnumeration*
property_enumeration_next(PropertyEnumeration* it) {
  return property_enumeration_setpos(it, it->idx + 1) ? it : 0;
}

static inline int32_t
property_enumeration_level(const PropertyEnumeration* it, const Vector* vec) {
  return it - (const PropertyEnumeration*)vec->data;
}

static inline PropertyEnumeration*
property_enumeration_enter(Vector* vec, JSContext* ctx, int32_t idx, int flags) {
  PropertyEnumeration* it = 0;
  JSValue value;

  assert(!vector_empty(vec));

  it = vector_back(vec, sizeof(PropertyEnumeration));
  value = property_enumeration_value(it, ctx);

  if(!JS_IsObject(value))
    return 0;

  if((it = property_enumeration_push(vec, ctx, value, flags)))
    if(!property_enumeration_setpos(it, idx))
      return 0;

  return it;
}

static inline PropertyEnumeration*
property_enumeration_recurse(Vector* vec, JSContext* ctx) {
  PropertyEnumeration* it;

  if(vector_empty(vec))
    return 0;

  for(it = vector_back(vec, sizeof(PropertyEnumeration)); it;) {
    if(it->tab_atom_len > 0) {
      JSValue value = property_enumeration_value(it, ctx);
      int32_t type = JS_VALUE_GET_TAG(value);
      BOOL circular = type == JS_TAG_OBJECT && property_enumeration_circular(vec, value);
      JS_FreeValue(ctx, value);

      if(type == JS_TAG_OBJECT && !circular) {
        if((it = property_enumeration_enter(vec, ctx, 0, PROPENUM_DEFAULT_FLAGS)))
          break;
      } else {
        if(property_enumeration_setpos(it, it->idx + 1))
          break;
      }
    }
    for(;;) {
      if((it = property_enumeration_pop(vec, ctx)) == 0)
        return it;
      if(property_enumeration_setpos(it, it->idx + 1))
        break;
    }
    break;
  }

  return it;
}

static inline PropEnumPathValueFunc*
property_enumeration_pathfunc(BOOL as_string) {
  return as_string ? property_enumeration_pathstr_value : property_enumeration_path;
}

/**
 * @}
 */
#endif /* defined(PROPERTY_ENUMERATION_H) */
