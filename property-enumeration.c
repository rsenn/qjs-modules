#define _GNU_SOURCE

#include "property-enumeration.h"

int
property_enumeration_init(PropertyEnumeration* it, JSContext* ctx, JSValueConst object, int flags) {
  it->obj = object;
  it->idx = 0;
  it->is_array = JS_IsArray(ctx, object);

  if(JS_GetOwnPropertyNames(ctx, &it->tab_atom, &it->tab_atom_len, object, flags & 0x3f)) {
    it->tab_atom_len = 0;
    it->tab_atom = 0;
    return -1;
  }

  if(flags & PROPENUM_SORT_ATOMS)
    qsort(it->tab_atom,
          it->tab_atom_len,
          sizeof(JSPropertyEnum),
          (int (*)(const void*, const void*)) & compare_jspropertyenum);

  return 0;
}

void
property_enumeration_reset(PropertyEnumeration* it, JSRuntime* rt) {
  uint32_t i;
  if(it->tab_atom) {
    for(i = 0; i < it->tab_atom_len; i++) JS_FreeAtomRT(rt, it->tab_atom[i].atom);
    js_free_rt(rt, it->tab_atom);
    it->tab_atom = 0;
    it->tab_atom_len = 0;
  }
  js_value_free_rt(rt, it->obj);
  it->obj = JS_UNDEFINED;
}

void
property_enumeration_dump(PropertyEnumeration* it, JSContext* ctx, DynBuf* out) {
  size_t i;
  const char* s;
  dbuf_putstr(out, "{ obj: 0x");
  dbuf_printf(out, "%ld", (int64_t)(JS_VALUE_GET_TAG(it->obj) == JS_TAG_OBJECT ? JS_VALUE_GET_OBJ(it->obj) : 0));
  dbuf_putstr(out, ", idx: ");
  dbuf_printf(out, "%u", it->idx);
  dbuf_putstr(out, ", len: ");
  dbuf_printf(out, "%u", it->tab_atom_len);
  dbuf_putstr(out, ", tab: [ ");
  for(i = 0; i < it->tab_atom_len; i++) {
    if(i)
      dbuf_putstr(out, ", ");

    s = JS_AtomToCString(ctx, it->tab_atom[i].atom);
    dbuf_putstr(out, i == it->idx ? "\x1b[1;31m" : "\x1b[1;30m");
    dbuf_putstr(out, s);
    dbuf_putstr(out, "\x1b[m");
    js_cstring_free(ctx, s);
  }
  dbuf_putstr(out, " ] }");
}

void
property_enumeration_dumpall(Vector* vec, JSContext* ctx, DynBuf* out) {
  size_t i, n = vector_size(vec, sizeof(PropertyEnumeration));
  dbuf_printf(out, "(%zu) [", n);
  for(i = 0; i < n; i++) {
    dbuf_putstr(out, i ? ",\n    " : "\n    ");
    property_enumeration_dump(vector_at(vec, sizeof(PropertyEnumeration), i), ctx, out);
  }
  dbuf_putstr(out, i ? "\n  ]" : "]");
}

JSValue
property_enumeration_path_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret, separator;
  JSAtom join;
  separator = JS_NewString(ctx, ".");
  join = JS_NewAtom(ctx, "join");
  ret = JS_Invoke(ctx, this_val, join, 1, &separator);
  JS_FreeAtom(ctx, join);
  JS_FreeValue(ctx, separator);
  return ret;
}

PropertyEnumeration*
property_enumeration_recurse(Vector* vec, JSContext* ctx) {
  PropertyEnumeration* it;
  JSValue value = JS_UNDEFINED;
  int32_t type;
  if(vector_empty(vec))
    return 0;

  for(it = vector_back(vec, sizeof(PropertyEnumeration)); it;) {
    if(it->tab_atom_len > 0) {
      value = property_enumeration_value(it, ctx);
      type = JS_VALUE_GET_TAG(value);
      js_value_free(ctx, value);
      if(type == JS_TAG_OBJECT) {
        if((it = property_enumeration_enter(vec, ctx, PROPENUM_DEFAULT_FLAGS)) && property_enumeration_setpos(it, 0))
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

int32_t
property_enumeration_depth(JSContext* ctx, JSValueConst object) {
  Vector vec = VECTOR(ctx);
  int32_t depth, max_depth = 0;
  PropertyEnumeration* it;
  JSValue root = JS_DupValue(ctx, object);
  if(JS_IsObject(root)) {
    for(it = property_enumeration_push(&vec, ctx, root, PROPENUM_DEFAULT_FLAGS); it;
        (it = property_enumeration_recurse(&vec, ctx))) {
      depth = vector_size(&vec, sizeof(PropertyEnumeration));
      if(max_depth < depth)
        max_depth = depth;
    }
  }
  property_enumeration_free(&vec, JS_GetRuntime(ctx));
  /*
   if(!vector_empty(&vec)) {
   for(it = vector_begin(&vec), end = vector_end(&vec); it != end; it++) {
   property_enumeration_reset(it, JS_GetRuntime(ctx));
   }
   }
   vector_free(&vec);*/
  return max_depth;
}
