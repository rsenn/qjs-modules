#include "property-enumeration.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include "buffer-utils.h"

/**
 * \addtogroup property-enumeration
 * @{
 */
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
    qsort(it->tab_atom, it->tab_atom_len, sizeof(JSPropertyEnum), (int (*)(const void*, const void*)) & compare_jspropertyenum);

  return 0;
}

void
property_enumeration_dump(PropertyEnumeration* it, JSContext* ctx, DynBuf* out) {
  size_t i;
  const char* s;
  dbuf_putstr(out, "{ obj: 0x");
  dbuf_printf(out, "%ld", (long)(JS_VALUE_GET_TAG(it->obj) == JS_TAG_OBJECT ? JS_VALUE_GET_OBJ(it->obj) : 0));
  dbuf_putstr(out, ", idx: ");
  dbuf_printf(out, "%u", it->idx);
  dbuf_putstr(out, ", len: ");
  dbuf_printf(out, "%u", it->tab_atom_len);
  dbuf_putstr(out, ", tab: [ ");
  for(i = 0; i < it->tab_atom_len; i++) {
    if(i)
      dbuf_putstr(out, ", ");

    s = JS_AtomToCString(ctx, it->tab_atom[i].atom);
    dbuf_putstr(out, i == it->idx ? COLOR_LIGHTRED : COLOR_GRAY);
    dbuf_putstr(out, s);
    dbuf_putstr(out, COLOR_NONE);
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
property_enumeration_skip(Vector* vec, JSContext* ctx) {
  PropertyEnumeration* it;
  JSValue value = JS_UNDEFINED;
  if(vector_empty(vec))
    return 0;

  for(it = vector_back(vec, sizeof(PropertyEnumeration)); it;) {
    if(it->tab_atom_len > 0) {
      if(property_enumeration_setpos(it, it->idx + 1))
        break;
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
property_enumeration_deepest(JSContext* ctx, JSValueConst object, int32_t max) {
  Vector vec = VECTOR(ctx);
  int32_t depth, max_depth = 0;
  JSValue root = JS_DupValue(ctx, object);

  if(JS_IsObject(root)) {
    PropertyEnumeration* it = property_enumeration_push(&vec, ctx, root, PROPENUM_DEFAULT_FLAGS);

    while(it) {
      depth = vector_size(&vec, sizeof(PropertyEnumeration));
      // printf("depth = %" PRIu32 ", atom = %x\n", depth, it->tab_atom[it->idx].atom);
      if(max_depth < depth)
        max_depth = depth;

      if(depth >= max) {
        if(!(it = property_enumeration_next(it)))
          it = property_enumeration_pop(&vec, ctx);
      } else {
        it = property_enumeration_recurse(&vec, ctx);
      }
    }
  }
  property_enumeration_free(&vec, JS_GetRuntime(ctx));

  return max_depth;
}

JSValue
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

void
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

JSValue
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
 property_enumeration_pointer(Vector* vec, JSContext* ctx, struct Pointer* ptr)
 { pointer_fromarray(ptr, ctx, property_enumeration_path(vec,ctx));
 }*/
int
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

void
property_enumeration_free(Vector* vec, JSRuntime* rt) {
  PropertyEnumeration *it, *end;
  it = vector_begin(vec);
  end = vector_end(vec);
  while(it++ != end) property_enumeration_reset(it, rt);
  // vector_free(vec);
}

int
property_enumeration_predicate(PropertyEnumeration* it, JSContext* ctx, JSValueConst fn, JSValueConst this_arg) {
  BOOL result;
  JSValue ret;
  JSValueConst argv[3];
  argv[0] = property_enumeration_value(it, ctx);
  argv[1] = property_enumeration_key(it, ctx);
  argv[2] = this_arg;
  ret = JS_Call(ctx, fn, JS_UNDEFINED, 3, argv);

  if(JS_IsException(ret)) {
    JS_GetException(ctx);
    ret = JS_FALSE;
  }

  result = JS_ToBool(ctx, ret);
  JS_FreeValue(ctx, argv[0]);
  JS_FreeValue(ctx, argv[1]);
  JS_FreeValue(ctx, ret);
  return result;
}

BOOL
property_enumeration_circular(Vector* vec, JSValueConst object) {
  PropertyEnumeration* it;

  vector_foreach_t(vec, it) {
    if(js_object_same(it->obj, object)) {
      // printf("circular reference %p\n", ptr);
      return TRUE;
    }
  }
  return FALSE;
}

IndexTuple
property_enumeration_check(Vector* vec) {
  PropertyEnumeration *i, *j;

  vector_foreach_t(vec, i) {
    vector_foreach_t(vec, j) {
      if(i == j)
        continue;

      if(js_object_same(i->obj, j->obj)) {
        return (IndexTuple){vector_indexof(vec, sizeof(PropertyEnumeration), i), vector_indexof(vec, sizeof(PropertyEnumeration), j)};
      }
    }
  }
  return (IndexTuple){-1, -1};
}

/**
 * @}
 */
