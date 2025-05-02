#ifndef PROPERTY_ENUMERATION_H
#define PROPERTY_ENUMERATION_H

#include <quickjs.h>
#include "vector.h"
#include "utils.h"
#include "debug.h"

/**
 * \defgroup property-enumeration property-enumeration: Property enumeration utilities
 * @{
 */
typedef struct PropertyEnumeration {
  uint32_t idx;
  uint32_t tab_atom_len;
  JSAtom* tab_atom;
  JSValue obj;
} PropertyEnumeration;

typedef struct {
  int32_t a, b;
} IndexTuple;

#define PROPENUM_INIT() \
  { 0, 0, NULL, JS_UNDEFINED }
#define PROPENUM_SORT_ATOMS (1 << 6)

#define PROPENUM_DEFAULT_FLAGS (JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY)

#define property_enumeration_new(vec) vector_emplace((vec), sizeof(PropertyEnumeration))
#define property_enumeration_index(enum) ((enum)->idx)

typedef JSValue PropEnumPathValueFunc(const Vector*, JSContext*);

static inline int
compare_jspropertyenum(const JSPropertyEnum* a, JSPropertyEnum* b) {
  return a->atom < b->atom ? -1 : a->atom > b->atom ? 1 : 0;
}
static inline int
compare_jsatom(const JSAtom* a, JSAtom* b) {
  return a < b ? -1 : a > b ? 1 : 0;
}

int property_enumeration_init(PropertyEnumeration*, JSContext* ctx, JSValue object, int flags);
void property_enumeration_dump(PropertyEnumeration*, JSContext* ctx, DynBuf* out);
JSValue property_enumeration_path_tostring(JSContext*, JSValue this_val, int argc, JSValue argv[]);
int property_enumeration_predicate(PropertyEnumeration*, JSContext* ctx, JSValue fn, JSValue this_arg);
void property_enumeration_reset(PropertyEnumeration*, JSRuntime*);
JSValue property_enumeration_key(const PropertyEnumeration*, JSContext*);

static inline size_t
property_enumeration_length(const PropertyEnumeration* propenum) {
  return propenum->tab_atom_len;
}

static inline JSValue
property_enumeration_value(const PropertyEnumeration* it, JSContext* ctx) {
  return it->idx < it->tab_atom_len ? JS_GetProperty(ctx, it->obj, it->tab_atom[it->idx])
                                    : JS_ThrowRangeError(ctx, "PropertyEnumeration is at end");
}

static inline const char*
property_enumeration_valuestrlen(const PropertyEnumeration* it, size_t* len, JSContext* ctx) {
  JSValue value = property_enumeration_value(it, ctx);

  if(JS_IsException(value))
    return 0;

  const char* str = JS_ToCStringLen(ctx, len, value);

  JS_FreeValue(ctx, value);

  return str;
}

static inline JSAtom
property_enumeration_atom(const PropertyEnumeration* it) {
  assert(it->idx < it->tab_atom_len);

  return it->tab_atom[it->idx];
}

static inline const char*
property_enumeration_keystr(const PropertyEnumeration* it, JSContext* ctx) {
  assert(it->idx < it->tab_atom_len);

  return JS_AtomToCString(ctx, it->tab_atom[it->idx]);
}

static inline const char*
property_enumeration_keystrlen(const PropertyEnumeration* it, size_t* len, JSContext* ctx) {
  assert(it->idx < it->tab_atom_len);

  return js_atom_to_cstringlen(ctx, len, it->tab_atom[it->idx]);
}

static inline void
property_enumeration_sort(PropertyEnumeration* it, JSContext* ctx) {
  quicksort_r(it->tab_atom, it->tab_atom_len, sizeof(JSPropertyEnum), &js_propenum_cmp, ctx);
}

static inline int
property_enumeration_setpos(PropertyEnumeration* it, int32_t idx) {
  if(idx < 0)
    idx += it->tab_atom_len;

  if(idx > (int32_t)it->tab_atom_len)
    return 0;

  assert((uint32_t)idx <= it->tab_atom_len);
  it->idx = idx;

  return (uint32_t)idx < it->tab_atom_len;
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
property_enumeration_prototype(PropertyEnumeration* it, JSContext* ctx, int flags) {
  if(it->idx < it->tab_atom_len)
    return property_enumeration_next(it);

  for(;;) {
    JSValue proto = JS_GetPrototype(ctx, it->obj);

    property_enumeration_reset(it, JS_GetRuntime(ctx));

    if(!JS_IsObject(proto))
      break;

    if(property_enumeration_init(it, ctx, proto, flags) != -1)
      if(it->idx < it->tab_atom_len)
        return it;
  }

  return 0;
}

JSValue property_recursion_path(const Vector*, JSContext* ctx);
void property_recursion_pathstr(const Vector*, JSContext* ctx, DynBuf* buf);
JSValue property_recursion_pathstr_value(const Vector*, JSContext* ctx);
void property_recursion_dumpall(Vector*, JSContext* ctx, DynBuf* out);
int property_recursion_insideof(Vector*, JSValue val);
void property_recursion_free(Vector*, JSRuntime* rt);
BOOL property_recursion_circular(Vector*, JSValue object);
PropertyEnumeration* property_recursion_push(Vector*, JSContext*, JSValueConst, int);
PropertyEnumeration* property_recursion_pop(Vector*, JSContext*);
PropertyEnumeration* property_recursion_enter(Vector*, JSContext*, int32_t, int);
int property_recursion_skip(Vector*, JSContext*);

static inline JSValue
property_recursion_root(const Vector* vec) {
  return vector_empty(vec) ? JS_EXCEPTION : vector_begin_t(vec, PropertyEnumeration)->obj;
}

static inline JSValue
property_recursion_object(const Vector* vec) {
  return vector_empty(vec) ? JS_EXCEPTION : (vector_end_t(vec, PropertyEnumeration) - 1)->obj;
}

static inline JSValue
property_recursion_value(const Vector* vec, JSContext* ctx) {
  return vector_empty(vec) ? JS_ThrowRangeError(ctx, "Property recursion is empty")
                           : property_enumeration_value(vector_back(vec, sizeof(PropertyEnumeration)), ctx);
}

static inline int32_t
property_recursion_depth(const Vector* vec) {
  return vector_size(vec, sizeof(PropertyEnumeration));
}

static inline PropertyEnumeration*
property_recursion_bottom(const Vector* vec) {
  return vector_empty(vec) ? 0 : vector_begin_t(vec, PropertyEnumeration);
}
static inline PropertyEnumeration*
property_recursion_top(const Vector* vec) {
  return vector_empty(vec) ? 0 : vector_end_t(vec, PropertyEnumeration) - 1;
}

static inline int
property_recursion_next(Vector* vec, JSContext* ctx) {
  JSValue value = property_recursion_value(vec, ctx);
  BOOL recurse = JS_VALUE_GET_TAG(value) == JS_TAG_OBJECT && !property_recursion_circular(vec, value);
  int i = 0;

  JS_FreeValue(ctx, value);

  if(recurse)
    if(property_recursion_enter(vec, ctx, 0, PROPENUM_DEFAULT_FLAGS))
      return 1;


  PropertyEnumeration* it = property_recursion_top(vec);

  while(!(it = property_enumeration_next(it))) {
    --i;

    if(!(it = property_recursion_pop(vec, ctx)))
      break;
  }

  return i;
}

/**
 * @}
 */
#endif /* defined(PROPERTY_ENUMERATION_H) */
