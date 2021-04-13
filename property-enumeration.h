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

static inline int
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

static inline void
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
  if(idx >= (int32_t)it->tab_atom_len)
    return 0;
  if(idx < 0)
    idx += it->tab_atom_len;
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

static inline void
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

static inline void
property_enumeration_dumpall(Vector* vec, JSContext* ctx, DynBuf* out) {
  size_t i, n = vector_size(vec, sizeof(PropertyEnumeration));
  dbuf_printf(out, "(%zu) [", n);
  for(i = 0; i < n; i++) {
    dbuf_putstr(out, i ? ",\n    " : "\n    ");
    property_enumeration_dump(vector_at(vec, sizeof(PropertyEnumeration), i), ctx, out);
  }
  dbuf_putstr(out, i ? "\n  ]" : "]");
}

static inline JSValue
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

static inline PropertyEnumeration*
property_enumeration_recurse(Vector* vec, JSContext* ctx) {
  PropertyEnumeration* it;
  JSValue value = JS_UNDEFINED;
  int32_t type;

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

static inline int32_t
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
#endif /* defined(PROPERTY_ENUMERATION_H) */
