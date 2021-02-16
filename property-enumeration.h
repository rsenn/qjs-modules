#ifndef PROPERTY_ENUMERATION_H
#define PROPERTY_ENUMERATION_H

#include "quickjs.h"
#include "vector.h"

typedef struct {
  JSValue obj;
  uint32_t idx;
  uint32_t tab_atom_len;
  JSPropertyEnum* tab_atom;
  BOOL is_array;
} PropertyEnumeration;

static void
property_enumeration_free(PropertyEnumeration* it, JSRuntime* rt) {
  uint32_t i;
  if(it->tab_atom) {
    for(i = 0; i < it->tab_atom_len; i++) JS_FreeAtomRT(rt, it->tab_atom[i].atom);
    js_free_rt(rt, it->tab_atom);
  }
  JS_FreeValueRT(rt, it->obj);
}

static int
property_enumeration_init(PropertyEnumeration* it, JSContext* ctx, JSValueConst object, int flags) {
  it->obj = object;
  it->idx = 0;
  it->is_array = JS_IsArray(ctx, object);

  if(JS_GetOwnPropertyNames(ctx, &it->tab_atom, &it->tab_atom_len, object, flags)) {
    it->tab_atom_len = 0;
    it->tab_atom = 0;
    return -1;
  }
  return 0;
}

static int
property_enumeration_setpos(PropertyEnumeration* it, int32_t idx) {
  if((idx < 0 ? -idx : idx) >= it->tab_atom_len)
    return 0;

  if(idx < 0)
    idx += it->tab_atom_len;
  assert(idx >= 0);
  assert(idx < it->tab_atom_len);
  it->idx = idx;
  return 1;
}

static JSValue
property_enumeration_value(PropertyEnumeration* it, JSContext* ctx) {
  assert(it->idx >= 0);
  assert(it->idx < it->tab_atom_len);
  return JS_GetProperty(ctx, it->obj, it->tab_atom[it->idx].atom);
}

static JSAtom
property_enumeration_atom(PropertyEnumeration* it) {
  assert(it->idx >= 0);
  assert(it->idx < it->tab_atom_len);
  return it->tab_atom[it->idx].atom;
}

static JSValue
property_enumeration_key(PropertyEnumeration* it, JSContext* ctx) {
  assert(it->idx >= 0);
  assert(it->idx < it->tab_atom_len);
  return JS_AtomToValue(ctx, it->tab_atom[it->idx].atom);
}

static void
property_enumeration_dump(PropertyEnumeration* it, JSContext* ctx, vector* vec) {
  size_t i;
  const char* s;
  vector_cats(vec, "{ obj: 0x");
  vector_catlong(vec, (long)(JS_VALUE_GET_TAG(it->obj) == JS_TAG_OBJECT ? JS_VALUE_GET_OBJ(it->obj) : 0), 16);
  //  js_value_dump(ctx, it->obj, vec);
  vector_cats(vec, ", idx: ");
  vector_catlong(vec, it->idx, 10);
  vector_cats(vec, ", tab_atom_len: ");
  vector_catlong(vec, it->tab_atom_len, 10);
  vector_cats(vec, ", tab_atom: [ ");
  for(i = 0; i < it->tab_atom_len; i++) {
    if(i)
      vector_catb(vec, ", ", 2);

    s = JS_AtomToCString(ctx, it->tab_atom[i].atom);
    if(i == it->idx)
      vector_cats(vec, "\x1b[32m");
    vector_cats(vec, s);
    if(i == it->idx)
      vector_cats(vec, "\x1b[m");
    JS_FreeCString(ctx, s);
  }

  vector_cats(vec, " ] }");
}
#endif // PROPERTY_ENUMERATION_H
