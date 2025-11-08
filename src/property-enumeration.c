#include "property-enumeration.h"
#include "defines.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include "buffer-utils.h"
#include "pointer.h"

/**
 * \addtogroup property-enumeration
 * @{
 */
int
property_enumeration_init(PropertyEnumeration* it, JSContext* ctx, JSValueConst obj, int flags) {
  *it = (PropertyEnumeration)PROPENUM_INIT();

  if(!(it->tab_atom = js_object_properties(ctx, &it->tab_atom_len, obj, flags & ~(PROPENUM_SORT_ATOMS)))) {
    it->tab_atom_len = 0;
    return -1;
  }

  if(flags & PROPENUM_SORT_ATOMS)
    qsort(it->tab_atom, it->tab_atom_len, sizeof(JSAtom), (int (*)(const void*, const void*)) & compare_jsatom);

  it->idx = 0;
  it->obj = obj;

  return 0;
}

void
property_enumeration_dump(PropertyEnumeration* it, JSContext* ctx, DynBuf* out) {
  size_t i;

  dbuf_putstr(out, "{ obj: 0x");
  dbuf_printf(out, "%p", JS_VALUE_GET_TAG(it->obj) == JS_TAG_OBJECT ? JS_VALUE_GET_PTR(it->obj) : NULL);
  dbuf_putstr(out, ", idx: ");
  dbuf_printf(out, "%u", it->idx);
  dbuf_putstr(out, ", len: ");
  dbuf_printf(out, "%u", it->tab_atom_len);
  dbuf_putstr(out, ", tab: [ ");

  for(i = 0; i < it->tab_atom_len; i++) {
    if(i)
      dbuf_putstr(out, ", ");

    const char* s = JS_AtomToCString(ctx, it->tab_atom[i]);

    dbuf_putstr(out, i == it->idx ? COLOR_LIGHTRED : COLOR_GRAY);
    dbuf_putstr(out, s);
    dbuf_putstr(out, COLOR_NONE);

    JS_FreeCString(ctx, s);
  }

  dbuf_putstr(out, " ] }");
}

JSValue
property_enumeration_path_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue separator = JS_NewString(ctx, ".");
  JSValue ret = js_invoke(ctx, this_val, "join", 1, &separator);

  JS_FreeValue(ctx, separator);

  return ret;
}

/*int32_t
property_enumeration_deepest(JSContext* ctx, JSValueConst object, int32_t max) {
  Vector vec = VECTOR(ctx);
  int32_t depth, max_depth = 0;
  JSValue root = JS_DupValue(ctx, object);

  if(JS_IsObject(root)) {
    PropertyEnumeration* it = property_recursion_push(&vec, ctx, root, PROPENUM_DEFAULT_FLAGS);

    do {
      depth = vector_size(&vec, sizeof(PropertyEnumeration));

      if(max_depth < depth)
        max_depth = depth;

      if(depth >= max) {
        while(!(it = property_enumeration_next(it))) {
          if((it = property_recursion_pop(&vec, ctx)) == 0)
            return max_depth;
        }
      } else {
        property_recursion_next(&vec, ctx);
      }
    } while((it = property_recursion_top(&vec)));
  }
  property_recursion_free(&vec, JS_GetRuntime(ctx));

  return max_depth;
}*/
void
property_enumeration_reset(PropertyEnumeration* it, JSRuntime* rt) {
  if(it->tab_atom) {
    uint32_t i;

    for(i = 0; i < it->tab_atom_len; i++)
      JS_FreeAtomRT(rt, it->tab_atom[i]);

    orig_js_free_rt(rt, it->tab_atom);

    it->tab_atom = 0;
    it->tab_atom_len = 0;
  }

  JS_FreeValueRT(rt, it->obj);
  it->obj = JS_UNDEFINED;
}

JSValue
property_enumeration_key(const PropertyEnumeration* it, JSContext* ctx) {
  JSValue key;

  assert(it->idx < it->tab_atom_len);

  key = JS_AtomToValue(ctx, it->tab_atom[it->idx]);

  if(JS_IsArray(ctx, it->obj)) {
    int64_t idx;

    if(!JS_ToInt64(ctx, &idx, key)) {
      JS_FreeValue(ctx, key);
      key = JS_NewInt64(ctx, idx);
    }
  }

  return key;
}

int
property_enumeration_predicate(PropertyEnumeration* it, JSContext* ctx, JSValueConst fn, JSValueConst this_arg) {
  JSValue ret;
  JSValueConst argv[3] = {
      JS_GetProperty(ctx, it->obj, it->tab_atom[it->idx]),
      JS_AtomToValue(ctx, it->tab_atom[it->idx]),
      this_arg,
  };

  ret = JS_Call(ctx, fn, JS_UNDEFINED, 3, argv);

  if(JS_IsException(ret)) {
    JS_GetException(ctx);
    ret = JS_FALSE;
  }

  int32_t result = JS_IsNumber(ret) ? js_value_toint32_free(ctx, ret) : js_value_tobool_free(ctx, ret);

  JS_FreeValue(ctx, argv[0]);
  JS_FreeValue(ctx, argv[1]);

  return result;
}

JSValue
property_recursion_path(const Vector* vec, JSContext* ctx) {
  PropertyEnumeration* it;
  JSValue ret = JS_NewArray(ctx);
  size_t i = 0;

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
property_recursion_pathstr(const Vector* vec, JSContext* ctx, DynBuf* buf) {
  PropertyEnumeration* it;
  size_t i = 0;
  const char* key;

  vector_foreach_t(vec, it) {
    if(i++ > 0)
      dbuf_putc(buf, '.');

    if((key = property_enumeration_keystr(it, ctx))) {
      dbuf_putstr(buf, key);

      JS_FreeCString(ctx, key);
    }
  }

  dbuf_0(buf);
}

JSValue
property_recursion_pathstr_value(const Vector* vec, JSContext* ctx) {
  JSValue ret;
  DynBuf dbuf;

  js_dbuf_init(ctx, &dbuf);

  property_recursion_pathstr(vec, ctx, &dbuf);

  dbuf_0(&dbuf);
  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);

  return ret;
}

JSValue js_pointer_wrap(JSContext*, Pointer*);

int
property_recursion_pointer(const Vector* vec, Pointer* ptr, JSContext* ctx) {
  if(pointer_allocate(ptr, vector_size(vec, sizeof(PropertyEnumeration)), ctx)) {
    int i = 0;
    PropertyEnumeration* it;
    vector_foreach_t(vec, it) {
      JSAtom atom = property_enumeration_atom(it);
      if(ptr->atoms[i] != atom) {
        JS_FreeAtom(ctx, ptr->atoms[i]);
        ptr->atoms[i] = JS_DupAtom(ctx, atom);
      }
      ++i;
    }
    return i;
  }
  return -1;
}

void
property_recursion_dumpall(Vector* vec, JSContext* ctx, DynBuf* out) {
  size_t i, n = vector_size(vec, sizeof(PropertyEnumeration));

  dbuf_printf(out, "(%lu) [", (unsigned long)n);

  for(i = 0; i < n; i++) {
    dbuf_putstr(out, i ? ",\n    " : "\n    ");
    property_enumeration_dump(vector_at(vec, sizeof(PropertyEnumeration), i), ctx, out);
  }

  dbuf_putstr(out, i ? "\n  ]" : "]");
}

int
property_recursion_skip(Vector* vec, JSContext* ctx) {
  int i = 0;
  PropertyEnumeration* it;

  if((it = property_recursion_top(vec))) {
    // if(it->tab_atom_len > 0)
    if(property_enumeration_next(it))
      return 0;

    while((it = property_recursion_pop(vec, ctx))) {
      --i;

      if(property_enumeration_next(it))
        break;
    }
  }

  return i;
}

PropertyEnumeration*
property_recursion_leave(Vector* vec, JSContext* ctx) {
  PropertyEnumeration* it;

  if((it = property_recursion_top(vec)))
    while((it = property_recursion_pop(vec, ctx)))
      if(property_enumeration_next(it))
        break;

  return it;
}

int
property_recursion_insideof(Vector* vec, JSValueConst val) {
  PropertyEnumeration* it;
  void* obj = JS_VALUE_GET_PTR(val);

  vector_foreach_t(vec, it) {
    void* obj2 = JS_VALUE_GET_PTR(it->obj);

    if(obj == obj2)
      return 1;
  }

  return 0;
}

void
property_recursion_free(Vector* vec, JSRuntime* rt) {
  PropertyEnumeration* it;

  vector_foreach_t(vec, it) { property_enumeration_reset(it, rt); }
  vector_free(vec);
}

BOOL
property_recursion_circular(Vector* vec, JSValueConst object) {
  PropertyEnumeration* it;

  vector_foreach_t(vec, it) {
    if(js_object_same(it->obj, object))
      return TRUE;
  }

  return FALSE;
}

/*IndexTuple
property_recursion_check(Vector* vec) {
  PropertyEnumeration *i, *j;

  vector_foreach_t(vec, i) vector_foreach_t(vec, j) {
    if(i == j)
      return (IndexTuple){
          vector_indexof(vec, sizeof(PropertyEnumeration), i),
          vector_indexof(vec, sizeof(PropertyEnumeration), j),
      };
  }

  return (IndexTuple){-1, -1};
}*/

PropertyEnumeration*
property_recursion_push(Vector* vec, JSContext* ctx, JSValue object, int flags) {
  PropertyEnumeration* it;

  if(!(it = vector_readyplus(vec, sizeof(PropertyEnumeration))))
    return 0;

  assert(JS_IsObject(object));

  if(!property_enumeration_init(it, ctx, object, flags)) {
    if(it->tab_atom_len >= 0)
      return vector_growplus(vec, sizeof(PropertyEnumeration), 1);

    property_enumeration_reset(it, JS_GetRuntime(ctx));
  }

  return 0;
}

PropertyEnumeration*
property_recursion_enter(Vector* vec, JSContext* ctx, int32_t idx, int flags) {
  PropertyEnumeration* it;

  if((it = property_recursion_top(vec))) {
    JSValue value = property_enumeration_value(it, ctx);

    assert(JS_IsObject(value));

    if((it = property_recursion_push(vec, ctx, value, flags)))
      if(!property_enumeration_setpos(it, idx))
        return 0;
  }

  return it;
}
