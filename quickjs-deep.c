#define _GNU_SOURCE

#include "cutils.h"
#include "property-enumeration.h"
#include "quickjs.h"
#include "utils.h"
#include "pointer.h"
#include "virtual-properties.h"

#include <stdint.h>

JSClassID js_deep_iterator_class_id = 0;
JSValue deep_iterator_proto, deep_iterator_constructor, deep_iterator_ctor;

typedef struct DeepIterator {
  vector frames;
  JSValue pred;
} DeepIterator;

static JSValue
js_deep_find(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;
  JSValueConst this_arg = argc > 2 ? argv[2] : JS_UNDEFINED;
  PropertyEnumeration* it;
  vector frames;

  if(!JS_IsFunction(ctx, argv[1]))
    return JS_ThrowTypeError(ctx, "argument 1 (predicate) is not a function");
  vector_init(&frames);

  it = property_enumeration_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);
  while((it = property_enumeration_recurse(&frames, ctx))) {
    BOOL result = property_enumeration_predicate(it, ctx, argv[1], this_arg);
    if(result) {
      ret = property_enumeration_value(it, ctx);
      break;
    }
  }
  property_enumeration_free(&frames, JS_GetRuntime(ctx));
  return ret;
}

static JSValue
js_deep_get(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Pointer* ptr;
  JSValue ret;
  if(!(ptr = pointer_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  pointer_from(ptr, ctx, argv[1], 0);
  ret = pointer_deref(ptr, ctx, argv[0]);
  pointer_free(ptr, ctx);
  return ret;
}

static JSValue
js_deep_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Pointer* ptr;
  JSValue obj;
  JSAtom prop;
  if(!(ptr = pointer_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  pointer_from(ptr, ctx, argv[1], 0);
  prop = pointer_pop(ptr);
  obj = pointer_acquire(ptr, ctx, argv[0]);

  if(!JS_IsException(obj))
    JS_SetProperty(ctx, obj, prop, argv[2]);
  JS_FreeAtom(ctx, prop);
  pointer_free(ptr, ctx);
  // return JS_UNDEFINED;
  return JS_DupValue(ctx, obj);
}

static JSValue
js_deep_unset(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Pointer* ptr;
  JSValue obj;
  JSAtom prop;
  if(!(ptr = pointer_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  pointer_from(ptr, ctx, argv[1], 0);
  prop = pointer_pop(ptr);
  obj = pointer_deref(ptr, ctx, argv[0]);

  if(!JS_IsException(obj))
    JS_DeleteProperty(ctx, obj, prop, 0);

  JS_FreeAtom(ctx, prop);
  pointer_free(ptr, ctx);
  return JS_DupValue(ctx, obj);
}

static JSValue
js_deep_flatten(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue value, path, ret = JS_UNDEFINED;
  JSValueConst this_arg, dest;
  PropertyEnumeration* it;
  vector frames, offsets;
  BOOL result;
  // DynBuf dbuf;
  int32_t level, prev;
  uint32_t mask = 0;
  VirtualProperties vmap;
  this_arg = argc > 2 ? argv[2] : JS_UNDEFINED;
  dest = argc > 1 ? argv[1] : JS_NewObject(ctx);

  if(js_object_is_map(ctx, dest))
    vmap = virtual_properties_map(ctx, dest);
  else
    vmap = virtual_properties_object(ctx, dest);

  /// dbuf_init(&dbuf);
  vector_init(&frames);
  vector_init(&offsets);
  property_enumeration_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);
  prev = 0;

  if(argc > 2)
    JS_ToUint32(ctx, &mask, argv[2]);

  while((it = property_enumeration_recurse(&frames, ctx))) {
    // const char* key;
    // size_t len;
    int dir, idx;
    // key = property_enumeration_keystr(it, ctx);
    path = property_enumeration_pathstr(&frames, ctx);
    // len = strlen(key);
    level = property_enumeration_level(it, &frames);
    dir = level - prev;
    //   if(dir <= 0) {for(idx = dir == 0 ? 0 : dir + 1; idx <= 0; idx++) dbuf_token_pop(&dbuf, '.'); }
    // if(dir >= 0) {dbuf_token_push(&dbuf, key, len, '.'); }
    ///  dbuf_0(&dbuf);
    // printf("level = %-2u  prev = %-2u  dir = %-2" PRId32 "  key = %-8s  dbuf = %s\n", level, prev, dir,
    // key, dbuf.buf);
    // JS_FreeCString(ctx, key);
    prev = level;
    if(mask) {
      JSValue value = property_enumeration_value(it, ctx);
      int32_t type = js_value_type(value);
      JS_FreeValue(ctx, value);
      if((mask & (1 << type)) == 0)
        continue;
    }
    value = property_enumeration_value(it, ctx);
    // dbuf_0(&dbuf);
    virtual_properties_set(&vmap, ctx, path, value);
    JS_FreeValue(ctx, value);
    JS_FreeValue(ctx, path);
  }
  property_enumeration_free(&frames, JS_GetRuntime(ctx));
  // dbuf_free(&dbuf);
  ret = vmap.this_obj;
  virtual_properties_free(&vmap, ctx);
  return ret;
}

static JSValue
js_deep_pathof(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;
  PropertyEnumeration* it;
  vector frames;

  vector_init(&frames);

  it = property_enumeration_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);
  while((it = property_enumeration_recurse(&frames, ctx))) {
    JSValue value = property_enumeration_value(it, ctx);
    BOOL result = js_value_equals(ctx, argv[1], value);
    JS_FreeValue(ctx, value);

    if(result) {
      ret = property_enumeration_path(&frames, ctx);
      break;
    }
  }
  property_enumeration_free(&frames, JS_GetRuntime(ctx));
  return ret;
}

static JSValue
js_deep_equals(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_TRUE;
  PropertyEnumeration *aenum, *benum;
  vector aframes, bframes;

  vector_init(&aframes);
  vector_init(&bframes);

  aenum = property_enumeration_push(&aframes,
                                    ctx,
                                    JS_DupValue(ctx, argv[0]),
                                    PROPENUM_DEFAULT_FLAGS | PROPENUM_SORT_ATOMS);
  benum = property_enumeration_push(&bframes,
                                    ctx,
                                    JS_DupValue(ctx, argv[1]),
                                    PROPENUM_DEFAULT_FLAGS | PROPENUM_SORT_ATOMS);
  while(((aenum = property_enumeration_recurse(&aframes, ctx)),
         (benum = property_enumeration_recurse(&bframes, ctx)))) {
    JSValue aval, bval;
    JSAtom akey, bkey;
    const char *astr, *bstr;
    BOOL result = TRUE;

    akey = aenum->tab_atom[aenum->idx].atom;
    bkey = benum->tab_atom[benum->idx].atom;

    printf("akey = %d, bkey = %d\n", akey, bkey);

    aval = property_enumeration_value(aenum, ctx);
    bval = property_enumeration_value(benum, ctx);

    astr = JS_ToCString(ctx, aval);
    bstr = JS_ToCString(ctx, bval);
    printf("astr = %s, bstr = %s\n", astr, bstr);

    if(JS_IsObject(aval) && JS_IsObject(bval))
      result = TRUE;
    else
      result = js_value_equals(ctx, aval, bval);
    JS_FreeValue(ctx, aval);
    JS_FreeValue(ctx, bval);

    if(!result) {
      ret = JS_FALSE;
      break;
    }
  }
  property_enumeration_free(&aframes, JS_GetRuntime(ctx));
  property_enumeration_free(&bframes, JS_GetRuntime(ctx));
  return ret;
}

static JSValue
js_deep_iterator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  DeepIterator* it;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  if(!(it = js_mallocz(ctx, sizeof(vector))))
    return JS_EXCEPTION;

  vector_init(&it->frames);
  it->pred = JS_UNDEFINED;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_deep_iterator_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, it);

  if(argc >= 1) {
    property_enumeration_push(&it->frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);
  }

  if(argc >= 2 && JS_IsFunction(ctx, argv[0])) {
    it->pred = JS_DupValue(ctx, argv[0]);
  }

  return obj;
fail:
  js_free(ctx, it);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_deep_iterator_next(
    JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, BOOL* pdone, int magic) {
  DeepIterator* it;
  PropertyEnumeration* penum;
  if(!(it = JS_GetOpaque2(ctx, this_val, js_deep_iterator_class_id)))
    return JS_EXCEPTION;

  if(!(penum = property_enumeration_recurse(&it->frames, ctx))) {
    *pdone = TRUE;
    return JS_UNDEFINED;
  }

  *pdone = FALSE;
  return property_enumeration_value(penum, ctx);
}

void
js_deep_iterator_finalizer(JSRuntime* rt, JSValue val) {
  DeepIterator* it = JS_GetOpaque(val, js_deep_iterator_class_id);
  if(it) {
    PropertyEnumeration *s, *e;

    for(s = vector_begin(&it->frames), e = vector_end(&it->frames); s != e; s++) {
      property_enumeration_reset(s, rt);
    }
    vector_free(&it->frames);
    js_free_rt(rt, it);
  }
  // JS_FreeValueRT(rt, val);
}

JSClassDef js_deep_iterator_class = {
    .class_name = "DeepIterator",
    .finalizer = js_deep_iterator_finalizer,
};

static const JSCFunctionListEntry js_deep_funcs[] = {
    JS_CFUNC_DEF("find", 2, js_deep_find),
    JS_CFUNC_DEF("get", 2, js_deep_get),
    JS_CFUNC_DEF("set", 3, js_deep_set),
    JS_CFUNC_DEF("unset", 2, js_deep_unset),
    JS_CFUNC_DEF("flatten", 1, js_deep_flatten),
    JS_CFUNC_DEF("pathOf", 2, js_deep_pathof),
    JS_CFUNC_DEF("equals", 2, js_deep_equals),
    JS_PROP_INT32_DEF("MASK_UNDEFINED", MASK_UNDEFINED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_NULL", MASK_NULL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_BOOL", MASK_BOOL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_INT", MASK_INT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_OBJECT", MASK_OBJECT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_STRING", MASK_STRING, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_SYMBOL", MASK_SYMBOL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_BIG_FLOAT", MASK_BIG_FLOAT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_BIG_INT", MASK_BIG_INT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_BIG_DECIMAL", MASK_BIG_DECIMAL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_FLOAT64", MASK_FLOAT64, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_NUMBER", MASK_NUMBER, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_ALL", MASK_ALL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_PRIMITIVE", MASK_PRIMITIVE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_ARRAY", MASK_ARRAY, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_FUNCTION", MASK_FUNCTION, JS_PROP_ENUMERABLE)};

static const JSCFunctionListEntry js_deep_iterator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_deep_iterator_next, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Deep Iterator", JS_PROP_CONFIGURABLE),
};

static int
js_deep_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_deep_iterator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_deep_iterator_class_id, &js_deep_iterator_class);

  deep_iterator_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx,
                             deep_iterator_proto,
                             js_deep_iterator_proto_funcs,
                             countof(js_deep_iterator_proto_funcs));
  JS_SetClassProto(ctx, js_deep_iterator_class_id, deep_iterator_proto);

  deep_iterator_ctor =
      JS_NewCFunction2(ctx, js_deep_iterator_constructor, "DeepIterator", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, deep_iterator_ctor, deep_iterator_proto);

  if(m) {
    JS_SetModuleExportList(ctx, m, js_deep_funcs, countof(js_deep_funcs));
    JS_SetModuleExport(ctx, m, "DeepIterator", deep_iterator_ctor);
  }
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_deep
#endif

JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_deep_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_deep_funcs, countof(js_deep_funcs));
  JS_AddModuleExport(ctx, m, "default");
  return m;
}
