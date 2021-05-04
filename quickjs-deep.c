#define _GNU_SOURCE

#include "cutils.h"
#include "property-enumeration.h"
#include "quickjs.h"
#include "utils.h"
#include "pointer.h"
#include "virtual-properties.h"

#include <stdint.h>

VISIBLE JSClassID js_deep_iterator_class_id = 0;
static JSValue deep_iterator_proto, deep_iterator_ctor;

typedef struct DeepIterator {
  JSValue root;
  Vector frames;
  JSValue pred;
  uint32_t flags;
  uint32_t type_mask;
} DeepIterator;

enum deep_iterator_return {
  RETURN_VALUE = 0,
  RETURN_PATH = 1 << 24,
  RETURN_VALUE_PATH = 2 << 24,
  RETURN_PATH_VALUE = 3 << 24,
  RETURN_MASK = 7 << 24,
  PATH_AS_STRING = 1 << 28
};

static JSValue
js_deep_return(JSContext* ctx, Vector* frames, int32_t return_flag) {
  JSValue ret;
  PropertyEnumeration* penum = vector_back(frames, sizeof(PropertyEnumeration));
  JSValue (*path_fn)(Vector*, JSContext*);

  path_fn = return_flag & PATH_AS_STRING ? property_enumeration_pathstr_value : property_enumeration_path;

  switch(return_flag & RETURN_MASK) {
    case RETURN_VALUE: {
      ret = property_enumeration_value(penum, ctx);
      break;
    }

    case RETURN_PATH: {
      ret = path_fn(frames, ctx);
      break;
    }

    case RETURN_VALUE_PATH:
    case RETURN_PATH_VALUE: {
      JSValue path, value;
      ret = JS_NewArray(ctx);

      value = property_enumeration_value(penum, ctx);
      path = path_fn(frames, ctx);

      JS_SetPropertyUint32(ctx, ret, return_flag == RETURN_VALUE_PATH ? 0 : 1, value);
      JS_SetPropertyUint32(ctx, ret, return_flag == RETURN_VALUE_PATH ? 1 : 0, path);
      /*      JS_DefinePropertyValueStr(ctx, ret, "value", JS_DupValue(ctx, value), JS_PROP_CONFIGURABLE);
            JS_DefinePropertyValueStr(ctx, ret, "path", JS_DupValue(ctx, path), JS_PROP_CONFIGURABLE);*/

      break;
    }
  }
  return ret;
}

static JSValue
js_deep_iterator_new(JSContext* ctx, JSValueConst proto, JSValueConst root, JSValueConst pred, uint32_t flags) {
  DeepIterator* it;
  JSValue obj = JS_UNDEFINED;

  if(!(it = js_mallocz(ctx, sizeof(DeepIterator))))
    return JS_EXCEPTION;

  vector_init(&it->frames, ctx);

  it->pred = JS_UNDEFINED;
  it->type_mask = TYPE_ALL;

  obj = JS_NewObjectProtoClass(ctx, proto, js_deep_iterator_class_id);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, it);

  if(!JS_IsUndefined(root))
    it->root = JS_DupValue(ctx, root);

  if(JS_IsFunction(ctx, pred))
    it->pred = JS_DupValue(ctx, pred);
  else if(JS_IsNumber(pred))
    JS_ToUint32(ctx, &it->type_mask, pred);

  it->flags = flags;

  return obj;
fail:
  js_free(ctx, it);
  js_value_free(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_deep_iterator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  int32_t flags = RETURN_VALUE_PATH;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;

  if(argc >= 3)
    JS_ToInt32(ctx, &flags, argv[2]);

  obj = js_deep_iterator_new(ctx, proto, argc >= 1 ? argv[0] : JS_UNDEFINED, argc >= 2 ? argv[1] : JS_UNDEFINED, flags);
  // js_value_free(ctx, proto);

  return obj;
}

static JSValue
js_deep_iterator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, BOOL* pdone, int magic) {
  DeepIterator* it;
  PropertyEnumeration* penum;
  JSValue ret = JS_UNDEFINED;
  if(!(it = JS_GetOpaque2(ctx, this_val, js_deep_iterator_class_id)))
    return JS_EXCEPTION;

  for(;;) {

    if(!(penum = vector_empty(&it->frames)
                     ? property_enumeration_push(&it->frames, ctx, JS_DupValue(ctx, it->root), PROPENUM_DEFAULT_FLAGS)
                     : property_enumeration_recurse(&it->frames, ctx))) {

      *pdone = TRUE;
      return ret;
    }

    if(property_enumeration_length(penum) == 0)
      continue;

    if(JS_IsFunction(ctx, it->pred)) {
      JSValueConst args[] = {
          property_enumeration_value(penum, ctx),
          property_enumeration_path(&it->frames, ctx),
      };
      JSValue ret;
      ret = JS_Call(ctx, it->pred, this_val, 2, args);

      js_value_free(ctx, args[0]);
      js_value_free(ctx, args[1]);

      if(!JS_ToBool(ctx, ret))
        continue;
    } else if(it->type_mask != TYPE_ALL) {
      JSValue value = property_enumeration_value(penum, ctx);
      uint32_t type = js_value_type(ctx, value);
      js_value_free(ctx, value);

      if((type & it->type_mask) == 0)
        continue;
    }

    /*if(!JS_IsUndefined(ret))
      js_value_free(ctx, ret);*/

    ret = js_deep_return(ctx, &it->frames, it->flags);
    *pdone = FALSE;
    break;
  }

  return ret;
}

static void
js_deep_iterator_finalizer(JSRuntime* rt, JSValue val) {
  DeepIterator* it = JS_GetOpaque(val, js_deep_iterator_class_id);
  if(it) {
    // property_enumeration_free(&it->frames, rt);
  }
}

static JSValue
js_deep_iterator_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  return JS_DupValue(ctx, this_val);
}

static JSValue
js_deep_iterator_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  DeepIterator* it;
  if(!(it = JS_GetOpaque2(ctx, this_val, js_deep_iterator_class_id)))
    return JS_EXCEPTION;

  // if(vector_empty(&it->frames))
  return JS_NewString(ctx, "");

  //  return property_enumeration_pathstr_value(&it->frames, ctx);
}

static JSValue
js_deep_find(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;
  JSValueConst this_arg = argc > 3 ? argv[3] : JS_UNDEFINED;
  int32_t return_flag = 0;
  PropertyEnumeration* it;
  Vector frames;

  if(argc > 2)
    JS_ToInt32(ctx, &return_flag, argv[2]);

  if(!JS_IsFunction(ctx, argv[1]))
    return JS_ThrowTypeError(ctx, "argument 2 (predicate) is not a function");
  if(!JS_IsObject(argv[0]))
    return JS_ThrowTypeError(ctx, "argument 1 (root) is not an object");
  vector_init(&frames, ctx);

  uint64_t t;

  t = time_us();

  property_enumeration_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);
  it = vector_back(&frames, sizeof(PropertyEnumeration));

  do {
    BOOL result = property_enumeration_predicate(it, ctx, argv[1], this_arg);
    if(result) {
      ret = js_deep_return(ctx, &frames, return_flag);
      break;
    }
  } while((it = property_enumeration_recurse(&frames, ctx)));

  t = time_us() - t;

  printf("js_deep_find took %" PRIu64 "s %" PRIu64 "us\n", t / 1000000, t % 1000000);

  property_enumeration_free(&frames, JS_GetRuntime(ctx));
  return ret;
}

static JSValue
js_deep_select(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret;
  JSValueConst this_arg = argc > 3 ? argv[3] : JS_UNDEFINED;
  int32_t i = 0, return_flag = RETURN_VALUE_PATH;
  PropertyEnumeration* it;
  Vector frames;

  if(argc > 2)
    JS_ToInt32(ctx, &return_flag, argv[2]);

  if(!JS_IsFunction(ctx, argv[1]))
    return JS_ThrowTypeError(ctx, "argument 1 (predicate) is not a function");
  vector_init(&frames, ctx);

  ret = JS_NewArray(ctx);
  property_enumeration_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);
  it = vector_back(&frames, sizeof(PropertyEnumeration));

  do {
    BOOL result = property_enumeration_predicate(it, ctx, argv[1], this_arg);
    if(result)
      JS_SetPropertyUint32(ctx, ret, i++, js_deep_return(ctx, &frames, return_flag));

  } while((it = property_enumeration_recurse(&frames, ctx)));
  property_enumeration_free(&frames, JS_GetRuntime(ctx));
  return ret;
}

static JSValue js_deep_get(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

static JSValue
js_deep_get2(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
  JSValueConst args[] = {func_data[0], argv[0]};
  return js_deep_get(ctx, this_val, 2, args);
}

static JSValue
js_deep_get(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret;

  if(argc > 1) {
    Pointer* ptr;
    if(!(ptr = pointer_new(ctx)))
      return JS_ThrowOutOfMemory(ctx);

    pointer_from(ptr, ctx, argv[1], 0);
    ret = pointer_deref(ptr, ctx, argv[0]);
    pointer_free(ptr, ctx);
  } else {
    ret = JS_NewCFunctionData(ctx, js_deep_get2, 1, 0, 1, &argv[0]);
  }
  return ret;
}

static JSValue js_deep_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

static JSValue
js_deep_set2(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
  JSValueConst args[] = {func_data[0], argv[0], argv[1]};
  return js_deep_set(ctx, this_val, 3, args);
}

static JSValue
js_deep_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  if(argc > 1) {
    JSValue obj;
    JSAtom prop;
    Pointer* ptr;

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

  return JS_NewCFunctionData(ctx, js_deep_set2, 2, 0, 1, &argv[0]);
}

static JSValue js_deep_unset(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

static JSValue
js_deep_unset2(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
  JSValueConst args[] = {func_data[0], argv[0]};
  return js_deep_unset(ctx, this_val, 2, args);
}

static JSValue
js_deep_unset(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  if(argc > 1) {
    JSValue obj;
    JSAtom prop;
    Pointer* ptr;
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

  return JS_NewCFunctionData(ctx, js_deep_unset2, 1, 0, 1, &argv[0]);
}

static JSValue
js_deep_flatten(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue value, path, ret = JS_UNDEFINED;
  JSValueConst this_arg, dest;
  PropertyEnumeration* it;
  Vector frames, offsets;
  DynBuf dbuf;
  int32_t level, prev;
  uint32_t mask = 0;
  VirtualProperties vmap;
  js_dbuf_init(ctx, &dbuf);
  this_arg = argc > 2 ? argv[2] : JS_UNDEFINED;
  dest = argc > 1 ? argv[1] : JS_NewObject(ctx);
  if(js_is_map(ctx, dest))
    vmap = virtual_properties_map(ctx, dest);
  else
    vmap = virtual_properties_object(ctx, dest);
  vector_init(&frames, ctx);
  ;
  vector_init(&offsets, ctx);
  ;
  it = property_enumeration_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);
  prev = 0;
  if(argc > 2)
    JS_ToUint32(ctx, &mask, argv[2]);
  do {
    int dir;
    path = property_enumeration_pathstr_value(&frames, ctx);
    level = property_enumeration_level(it, &frames);
    dir = level - prev;
    prev = level;
    if(mask) {
      JSValue value = property_enumeration_value(it, ctx);
      int32_t type = js_value_type(ctx, value);
      js_value_free(ctx, value);
      if((mask & (1 << type)) == 0)
        continue;
    }
    value = property_enumeration_value(it, ctx);
    virtual_properties_set(&vmap, ctx, path, value);
    js_value_free(ctx, value);
    js_value_free(ctx, path);
  } while((it = property_enumeration_recurse(&frames, ctx)));
  property_enumeration_free(&frames, JS_GetRuntime(ctx));
  ret = vmap.this_obj;
  virtual_properties_free(&vmap, ctx);
  return ret;
}

static JSValue
js_deep_pathof(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;
  PropertyEnumeration* it;
  Vector frames;

  vector_init(&frames, ctx);

  it = property_enumeration_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);
  do {
    JSValue value = property_enumeration_value(it, ctx);
    BOOL result = js_value_equals(ctx, argv[1], value);
    js_value_free(ctx, value);

    if(result) {
      ret = property_enumeration_path(&frames, ctx);
      break;
    }
  } while((it = property_enumeration_recurse(&frames, ctx)));

  property_enumeration_free(&frames, JS_GetRuntime(ctx));
  return ret;
}

static JSValue
js_deep_foreach(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  PropertyEnumeration* it;
  JSValueConst fn, this_arg;
  Vector frames;
  uint32_t type_mask = TYPE_ALL;

  vector_init(&frames, ctx);

  fn = argv[1];
  this_arg = argc > 2 ? argv[2] : JS_UNDEFINED;

  if(argc > 3)
    JS_ToUint32(ctx, &type_mask, argv[3]);

  it = property_enumeration_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);
  do {
    if(property_enumeration_length(it)) {
      JSValueConst args[3] = {property_enumeration_value(it, ctx), JS_UNDEFINED, argv[0]};
      uint32_t type = js_value_type(ctx, args[0]);

      if((type & type_mask) == 0) {
        js_value_free(ctx, args[0]);
        continue;
      }

      args[1] = property_enumeration_path(&frames, ctx);

      JS_Call(ctx, fn, this_arg, 3, args);

      js_value_free(ctx, args[0]);
      js_value_free(ctx, args[1]);
    }

  } while((it = property_enumeration_recurse(&frames, ctx)));

  property_enumeration_free(&frames, JS_GetRuntime(ctx));
  return JS_UNDEFINED;
}

static JSValue
js_deep_equals(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_TRUE;
  PropertyEnumeration *aenum, *benum;
  Vector aframes, bframes;

  vector_init(&aframes, ctx);
  vector_init(&bframes, ctx);

  aenum =
      property_enumeration_push(&aframes, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS | PROPENUM_SORT_ATOMS);
  benum =
      property_enumeration_push(&bframes, ctx, JS_DupValue(ctx, argv[1]), PROPENUM_DEFAULT_FLAGS | PROPENUM_SORT_ATOMS);
  do {
    JSValue aval, bval;
    JSAtom akey, bkey;
    const char *astr, *bstr, *avstr, *bvstr;
    BOOL result = TRUE;

    if(!aenum || !benum) {
      ret = JS_FALSE;
      break;
    }

    akey = aenum->tab_atom[aenum->idx].atom;
    bkey = benum->tab_atom[benum->idx].atom;

    aval = property_enumeration_value(aenum, ctx);
    bval = property_enumeration_value(benum, ctx);

    astr = JS_AtomToCString(ctx, akey);
    bstr = JS_AtomToCString(ctx, bkey);
    avstr = JS_ToCString(ctx, aval);
    bvstr = JS_ToCString(ctx, bval);

    if(JS_IsObject(aval) && JS_IsObject(bval))
      result = TRUE;
    else
      result = js_value_equals(ctx, aval, bval);

    js_value_free(ctx, aval);
    js_value_free(ctx, bval);

    // printf("a %s: %s b %s: %s result: %d\n", astr, avstr, bstr, bvstr, result);

    JS_FreeCString(ctx, astr);
    JS_FreeCString(ctx, bstr);
    JS_FreeCString(ctx, avstr);
    JS_FreeCString(ctx, bvstr);

    if(!result) {
      ret = JS_FALSE;
      break;
    }
  } while(
      ((aenum = property_enumeration_recurse(&aframes, ctx)), (benum = property_enumeration_recurse(&bframes, ctx))));

  property_enumeration_free(&aframes, JS_GetRuntime(ctx));
  property_enumeration_free(&bframes, JS_GetRuntime(ctx));
  return ret;
}

static JSValue
js_deep_iterate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  return js_deep_iterator_constructor(ctx, deep_iterator_ctor, argc, argv);
}

static JSValue
js_deep_clone(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {

  return js_value_clone(ctx, argv[0]);
}

static JSClassDef js_deep_iterator_class = {
    .class_name = "DeepIterator",
    .finalizer = js_deep_iterator_finalizer,
};

static const JSCFunctionListEntry js_deep_funcs[] = {
    JS_CFUNC_DEF("find", 2, js_deep_find),
    JS_CFUNC_DEF("select", 2, js_deep_select),
    JS_CFUNC_DEF("get", 2, js_deep_get),
    JS_CFUNC_DEF("set", 3, js_deep_set),
    JS_CFUNC_DEF("unset", 2, js_deep_unset),
    JS_CFUNC_DEF("flatten", 1, js_deep_flatten),
    JS_CFUNC_DEF("pathOf", 2, js_deep_pathof),
    JS_CFUNC_DEF("equals", 2, js_deep_equals),
    JS_CFUNC_DEF("iterate", 1, js_deep_iterate),
    JS_CFUNC_DEF("forEach", 2, js_deep_foreach),
    JS_CFUNC_DEF("clone", 1, js_deep_clone),
    JS_PROP_INT32_DEF("TYPE_UNDEFINED", TYPE_UNDEFINED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_NULL", TYPE_NULL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_BOOL", TYPE_BOOL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_INT", TYPE_INT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_OBJECT", TYPE_OBJECT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_STRING", TYPE_STRING, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_SYMBOL", TYPE_SYMBOL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_BIG_FLOAT", TYPE_BIG_FLOAT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_BIG_INT", TYPE_BIG_INT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_BIG_DECIMAL", TYPE_BIG_DECIMAL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_FLOAT64", TYPE_FLOAT64, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_NUMBER", TYPE_NUMBER, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_NAN", TYPE_NAN, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ALL", TYPE_ALL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_PRIMITIVE", TYPE_PRIMITIVE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ARRAY", TYPE_ARRAY, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_FUNCTION", TYPE_FUNCTION, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("RETURN_VALUE", RETURN_VALUE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("RETURN_PATH", RETURN_PATH, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("RETURN_VALUE_PATH", RETURN_VALUE_PATH, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("RETURN_PATH_VALUE", RETURN_PATH_VALUE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("PATH_AS_STRING", PATH_AS_STRING, JS_PROP_ENUMERABLE),
};

static const JSCFunctionListEntry js_deep_iterator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_deep_iterator_next, 0),
    JS_CFUNC_DEF("toString", 0, js_deep_iterator_tostring),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_deep_iterator_iterator),
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

  deep_iterator_ctor = JS_NewCFunction2(ctx, js_deep_iterator_constructor, "DeepIterator", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, deep_iterator_ctor, deep_iterator_proto);

  if(m) {
    JS_SetModuleExportList(ctx, m, js_deep_funcs, countof(js_deep_funcs));
    // JS_SetModuleExport(ctx, m, "DeepIterator", deep_iterator_ctor);
  }
  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_deep
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_deep_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_deep_funcs, countof(js_deep_funcs));
  JS_AddModuleExport(ctx, m, "default");
  return m;
}
