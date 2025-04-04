#include "defines.h"
#include <cutils.h>
#include "property-enumeration.h"
#include <quickjs.h>
#include "utils.h"
#include "pointer.h"
#include "virtual-properties.h"
#include "quickjs-predicate.h"
#include "debug.h"

#include <stdint.h>

/**
 * \defgroup quickjs-deep quickjs-deep: Deep object
 * @{
 */
VISIBLE JSClassID js_deep_iterator_class_id = 0;
VISIBLE JSValue deep_functions = {{0}, JS_TAG_UNDEFINED}, deep_iterator_proto = {{0}, JS_TAG_UNDEFINED}, deep_iterator_ctor = {{0}, JS_TAG_UNDEFINED};

typedef struct DeepIterator {
  Vector frames;
  JSValue root, pred;
  uint32_t flags, seq;
} DeepIterator;

enum deep_iterator_return {
  RETURN_VALUE_PATH = 0,
  RETURN_PATH = 1,
  RETURN_VALUE = 2 ,
  RETURN_PATH_VALUE = 3,
  RETURN_MASK = 3,
  PATH_AS_STRING = 1 << 2,
  NO_THROW = 1 << 3,
  MAXDEPTH_MASK = 0xffffff,
};

static const uint32_t js_deep_defaultflags = 0;

static inline PropEnumPathValueFunc*
js_deep_pathfunc(BOOL as_string) {
  return as_string ? property_recursion_pathstr_value : property_recursion_path;
}

/*static uint32_t
js_deep_parseflags(JSContext* ctx, int argc, JSValueConst argv[]) {
  uint32_t flags = 0;

  for(int i = 0; i < argc; i++) {
    uint32_t num = 0;

    if(JS_IsNumber(argv[i])) {
      JS_ToUint32(ctx, &num, argv[i]);

      if(num & (RETURN_MASK | MAXDEPTH_MASK))
        flags |= num;
    }
  }

  return flags;
}

static uint32_t
js_deep_thisflags(JSContext* ctx, JSValueConst this_val) {
  uint32_t ret = 0;

  if(JS_IsObject(this_val)) {
    JSValue flags = JS_GetPropertyStr(ctx, this_val, "flags");

    if(JS_IsNumber(flags))
      JS_ToUint32(ctx, &ret, flags);

    JS_FreeValue(ctx, flags);
  }

  return ret;
}

static uint32_t
js_deep_getflags(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {

  uint32_t flags = 0;

  if((flags |= js_deep_parseflags(ctx, argc, argv)) == 0)
    flags |= js_deep_thisflags(ctx, this_val);

  return flags;
}*/

static BOOL
js_deep_predicate(JSContext* ctx, JSValueConst value, const Vector* frames) {
  BOOL result = TRUE;
  Predicate* pred;
  JSValue ret = JS_UNDEFINED;
  PropertyEnumeration* back = property_recursion_top(frames);
  JSValueConst args[3] = {
      back->tab_atom_len == 0 ? JS_UNDEFINED : property_enumeration_value(back, ctx),
      property_recursion_path(frames, ctx),
      ((PropertyEnumeration*)vector_front(frames, sizeof(PropertyEnumeration)))->obj,
  };

  if((pred = js_predicate_data(value))) {
    JSArguments a = js_arguments_new(countof(args), args);
    ret = predicate_eval(pred, ctx, &a);
  } else if(JS_IsFunction(ctx, value)) {
    ret = JS_Call(ctx, value, JS_UNDEFINED, 3, args);
  }

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);

  if(JS_IsException(ret)) {
    JS_GetException(ctx);
    ret = JS_FALSE;
  }

  result = js_value_tobool_free(ctx, ret);
  return result;
}

static JSValue
js_deep_return(JSContext* ctx, Vector* frames, int32_t return_flag) {
  JSValue ret;
  PropertyEnumeration* it = property_recursion_top(frames);
  PropEnumPathValueFunc* path_fn = js_deep_pathfunc(!!(return_flag & PATH_AS_STRING));

  switch(return_flag & RETURN_MASK) {
    case RETURN_VALUE: {
      ret = property_enumeration_value(it, ctx);
      break;
    }

    case RETURN_PATH: {
      ret = path_fn(frames, ctx);
      break;
    }

    case RETURN_VALUE_PATH:
    case RETURN_PATH_VALUE: {
      BOOL value_first = (return_flag & RETURN_MASK) == RETURN_VALUE_PATH;

      ret = JS_NewArray(ctx);

      JS_SetPropertyUint32(ctx, ret, value_first ? 0 : 1, property_enumeration_value(it, ctx));
      JS_SetPropertyUint32(ctx, ret, value_first ? 1 : 0, path_fn(frames, ctx));
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

  obj = JS_NewObjectProtoClass(ctx, proto, js_deep_iterator_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, it);

  if(!JS_IsUndefined(root))
    it->root = JS_DupValue(ctx, root);

  if(!JS_IsUndefined(pred))
    it->pred = JS_DupValue(ctx, pred);

  it->flags = flags;
  return obj;

fail:
  js_free(ctx, it);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_deep_iterator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED, root = JS_UNDEFINED, pred = JS_UNDEFINED;
  uint32_t flags = js_deep_defaultflags;
  int i = 0;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;

  if(i < argc)
    root = argv[i++];

  if(i < argc && JS_IsFunction(ctx, argv[i]))
    pred = argv[i++];

  if(i < argc)
    JS_ToUint32(ctx, &flags, argv[i]);

  obj = js_deep_iterator_new(ctx, proto, root, pred, flags);
  JS_FreeValue(ctx, proto);

  return obj;
}

static JSValue
js_deep_iterator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  DeepIterator* it;
  PropertyEnumeration* penum = 0;
  JSValue ret = JS_UNDEFINED;
  uint32_t depth, max_depth;

  if(!(it = JS_GetOpaque2(ctx, this_val, js_deep_iterator_class_id)))
    return JS_EXCEPTION;

  if((max_depth = (uint32_t)it->flags & 0xffffff) == 0)
    max_depth = INT32_MAX;

  if(!JS_IsObject(it->root)) {
    *pdone = TRUE;
    return JS_UNDEFINED;
  }

  for(;;) {
    depth = property_recursion_depth(&it->frames);
    it->seq == 0           ? (property_recursion_push(&it->frames, ctx, JS_DupValue(ctx, it->root), PROPENUM_DEFAULT_FLAGS), 1)
    : (depth >= max_depth) ? property_recursion_skip(&it->frames, ctx)
                           : /*depth > 0          ?*/ property_recursion_next(&it->frames, ctx);

    ++it->seq;

    if(!(penum = property_recursion_top(&it->frames))) {
      *pdone = TRUE;
      return ret;
    }

    if(property_enumeration_length(penum) == 0)
      continue;

    if(!js_deep_predicate(ctx, it->pred, &it->frames))
      continue;

    ret = js_deep_return(ctx, &it->frames, it->flags & ~MAXDEPTH_MASK);
    *pdone = FALSE;
    break;
  }

  return ret;
}

static void
js_deep_iterator_finalizer(JSRuntime* rt, JSValue val) {
  DeepIterator* it;

  if((it = JS_GetOpaque(val, js_deep_iterator_class_id))) {
    property_recursion_free(&it->frames, rt);
    JS_FreeValueRT(rt, it->root);
    JS_FreeValueRT(rt, it->pred);
  }
}

static JSValue
js_deep_iterator_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

static JSValue
js_deep_find(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  JSValueConst this_arg = argc > 3 ? argv[3] : JS_UNDEFINED;
  uint32_t flags = js_deep_defaultflags, max_depth;
  PropertyEnumeration* it;
  Vector frames;

  if(argc > 2)
    // flags = js_deep_getflags(ctx, this_val, argc - 2, argv + 2);
    JS_ToUint32(ctx, &flags, argv[2]);

  if((max_depth = (flags & MAXDEPTH_MASK)) == 0)
    max_depth = INT32_MAX;

  if(!predicate_callable(ctx, argv[1]))
    return JS_ThrowTypeError(ctx, "argument 2 (predicate) is not a function");

  if(!JS_IsObject(argv[0]))
    return JS_ThrowTypeError(ctx, "argument 1 (root) is not an object");

  vector_init(&frames, ctx);

  it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);
  // it = property_recursion_top(&frames);

  if(it)
    do {
      if(property_enumeration_predicate(it, ctx, argv[1], this_arg)) {
        ret = js_deep_return(ctx, &frames, flags & ~MAXDEPTH_MASK);
        break;
      }

      vector_size(&frames, sizeof(PropertyEnumeration)) >= max_depth ? property_recursion_skip(&frames, ctx) : property_recursion_next(&frames, ctx);

    } while((it = property_recursion_top(&frames)));

  /*t = time_us() - t; printf("js_deep_find took %" PRIu64 "s %" PRIu64 "us\n",
   * t / 1000000, t % 1000000);*/

  property_recursion_free(&frames, JS_GetRuntime(ctx));
  return ret;
}

static JSValue
js_deep_select(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  uint32_t i = 0, flags = js_deep_defaultflags, max_depth;
  PropertyEnumeration* it;
  Vector frames;

  if(argc > 2)
    JS_ToUint32(ctx, &flags, argv[2]);

  if((max_depth = (flags & MAXDEPTH_MASK)) == 0)
    max_depth = INT32_MAX;

  if(!predicate_callable(ctx, argv[1]))
    return JS_ThrowTypeError(ctx, "argument 1 (predicate) is not a function");

  vector_init(&frames, ctx);

  ret = JS_NewArray(ctx);
  it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);
  // it = vector_empty(&frames) ? 0 : vector_back(&frames, sizeof(PropertyEnumeration));

  while(it) {
    if(js_deep_predicate(ctx, argv[1], &frames))
      JS_SetPropertyUint32(ctx, ret, i++, js_deep_return(ctx, &frames, flags & ~MAXDEPTH_MASK));

    if(vector_size(&frames, sizeof(PropertyEnumeration)) >= max_depth) {
      property_recursion_skip(&frames, ctx);
      it = property_recursion_top(&frames);
    }

    property_recursion_next(&frames, ctx);
    it = property_recursion_top(&frames);
  }

  property_recursion_free(&frames, JS_GetRuntime(ctx));

  return ret;
}

static JSValue js_deep_get(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]);

static JSValue
js_deep_get2(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue* func_data) {
  JSValueConst args[2] = {
      func_data[0],
      argv[0],
  };

  return js_deep_get(ctx, this_val, countof(args), args);
}

static JSValue
js_deep_get(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;

  if(argc > 1) {
    Pointer* ptr;
    uint32_t flags = 0; /**/

    if(!(ptr = pointer_new(ctx)))
      return JS_EXCEPTION;

    if(argc > 2)
      JS_ToUint32(ctx, &flags, argv[2]);

    pointer_from(ptr, argv[1], ctx);
    ret = pointer_deref(ptr, argv[0], ctx);

    if(JS_IsException(ret) && (flags & NO_THROW)) {
      JS_GetException(ctx);
      ret = JS_NULL;
    }

    pointer_free(ptr, JS_GetRuntime(ctx));
  } else {
    ret = JS_NewCFunctionData(ctx, js_deep_get2, 1, 0, 1, &argv[0]);
  }

  return ret;
}

static JSValue js_deep_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]);

static JSValue
js_deep_set2(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue* func_data) {
  JSValueConst args[3] = {
      func_data[0],
      argv[0],
      argv[1],
  };

  return js_deep_set(ctx, this_val, countof(args), args);
}

static JSValue
js_deep_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  if(argc > 1) {
    JSValue obj;
    JSAtom prop;
    Pointer* ptr;

    if(!(ptr = pointer_new(ctx)))
      return JS_EXCEPTION;

    pointer_from(ptr, argv[1], ctx);
    prop = pointer_popatom(ptr);
    obj = pointer_acquire(ptr, argv[0], ctx);

    if(!JS_IsException(obj))
      JS_SetProperty(ctx, obj, prop, argv[2]);

    JS_FreeAtom(ctx, prop);
    pointer_free(ptr, JS_GetRuntime(ctx));

    // return JS_UNDEFINED;
    return JS_DupValue(ctx, obj);
  }

  return JS_NewCFunctionData(ctx, js_deep_set2, 2, 0, 1, &argv[0]);
}

static JSValue js_deep_unset(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]);

static JSValue
js_deep_unset2(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue* func_data) {
  JSValueConst args[2] = {
      func_data[0],
      argv[0],
  };
  return js_deep_unset(ctx, this_val, countof(args), args);
}

static JSValue
js_deep_unset(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  if(argc > 1) {
    JSValue obj;
    JSAtom prop;
    Pointer* ptr;

    if(!(ptr = pointer_new(ctx)))
      return JS_EXCEPTION;

    pointer_from(ptr, argv[1], ctx);
    prop = pointer_popatom(ptr);
    obj = pointer_deref(ptr, argv[0], ctx);

    if(!JS_IsException(obj))
      JS_DeleteProperty(ctx, obj, prop, 0);

    JS_FreeAtom(ctx, prop);
    pointer_free(ptr, JS_GetRuntime(ctx));

    return JS_DupValue(ctx, obj);
  }

  return JS_NewCFunctionData(ctx, js_deep_unset2, 1, 0, 1, &argv[0]);
}

static JSValue
js_deep_flatten(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  DynBuf dbuf;
  JSValueConst dest;
  VirtualProperties vmap;
  Vector frames, offsets;
  PropertyEnumeration* it;
  uint32_t mask = 0;

  js_dbuf_init(ctx, &dbuf);

  dest = argc > 1 ? argv[1] : JS_NewObject(ctx);
  vmap = virtual_properties(ctx, dest);

  vector_init(&frames, ctx);
  vector_init(&offsets, ctx);

  it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);

  if(argc > 2)
    JS_ToUint32(ctx, &mask, argv[2]);

  do {
    JSValue value, path;

    if(mask) {
      JSValue tmp = property_enumeration_value(it, ctx);
      int32_t type = js_value_type(ctx, tmp);
      JS_FreeValue(ctx, tmp);
      if((mask & (1 << type)) == 0)
        continue;
    }

    value = property_enumeration_value(it, ctx);
    path = property_recursion_pathstr_value(&frames, ctx);

    virtual_properties_set(&vmap, ctx, path, value);

    JS_FreeValue(ctx, value);
    JS_FreeValue(ctx, path);

  } while((property_recursion_next(&frames, ctx), it = property_recursion_top(&frames)));

  property_recursion_free(&frames, JS_GetRuntime(ctx));

  ret = vmap.this_obj;
  virtual_properties_free(&vmap, ctx);

  return ret;
}

static JSValue
js_deep_pathof(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  ValueTypeMask type = js_value_type(ctx, argv[1]);
  JSValueCompareFunc* cmp_fn = (type & TYPE_OBJECT) ? js_object_same2 : js_value_equals;
  Vector frames;
  PropertyEnumeration* it;
  uint32_t flags=js_deep_defaultflags;

  if(argc > 2)
    JS_ToUint32(ctx, &flags, argv[2]);

  vector_init(&frames, ctx);

  it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);

  do {
    JSValue value = property_enumeration_value(it, ctx);
    BOOL result = js_value_type(ctx, value) != type ? FALSE : cmp_fn(ctx, argv[1], value);
    JS_FreeValue(ctx, value);

    if(result) {

      ret = ((flags & PATH_AS_STRING) ? property_recursion_pathstr_value : property_recursion_path)(&frames, ctx);
      break;
    }

    property_recursion_next(&frames, ctx);
  } while((it = property_recursion_top(&frames)));

  property_recursion_free(&frames, JS_GetRuntime(ctx));
  return ret;
}

static JSValue
js_deep_foreach(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  PropertyEnumeration* it;
  JSValueConst fn, this_arg;
  Vector frames;
  uint32_t type_mask = TYPE_ALL;

  vector_init(&frames, ctx);

  fn = argv[1];
  this_arg = argc > 2 ? argv[2] : JS_UNDEFINED;

  if(argc > 3)
    JS_ToUint32(ctx, &type_mask, argv[3]);

  it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);

  while(it) {
    if(property_enumeration_length(it)) {
      JSValueConst args[3] = {
          property_enumeration_value(it, ctx),
          JS_UNDEFINED,
          argv[0],
      };
      uint32_t type = js_value_type(ctx, args[0]);

      if((type & type_mask) == 0) {
        JS_FreeValue(ctx, args[0]);
        continue;
      }

      args[1] = property_recursion_path(&frames, ctx);

      JS_Call(ctx, fn, this_arg, countof(args), args);

      JS_FreeValue(ctx, args[0]);
      JS_FreeValue(ctx, args[1]);
    }

    property_recursion_next(&frames, ctx);
    it = property_recursion_top(&frames);
  }

  property_recursion_free(&frames, JS_GetRuntime(ctx));

  return JS_UNDEFINED;
}

static JSValue
js_deep_equals(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_TRUE;
  struct {
    Vector frames;
    PropertyEnumeration* it;
    JSValue val;
  } a = {VECTOR(ctx), 0, JS_UNDEFINED}, b = {VECTOR(ctx), 0, JS_UNDEFINED};

  if(!JS_IsObject(argv[0]) || !JS_IsObject(argv[1]))
    return JS_NewBool(ctx, js_value_equals(ctx, argv[0], argv[1]));

  a.it = property_recursion_push(&a.frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS | PROPENUM_SORT_ATOMS);
  b.it = property_recursion_push(&b.frames, ctx, JS_DupValue(ctx, argv[1]), PROPENUM_DEFAULT_FLAGS | PROPENUM_SORT_ATOMS);

  for(;;) {
    BOOL result = TRUE;

    if(!a.it || !b.it) {
      ret = (!a.it && !b.it) ? JS_TRUE : JS_FALSE;
      break;
    }

    a.val = property_enumeration_value(a.it, ctx);
    b.val = property_enumeration_value(b.it, ctx);

    result = (JS_IsObject(a.val) && JS_IsObject(b.val)) ? TRUE : js_value_equals(ctx, a.val, b.val);

    JS_FreeValue(ctx, a.val);
    JS_FreeValue(ctx, b.val);

    if(!result) {
      ret = JS_FALSE;
      break;
    }

    property_recursion_next(&a.frames, ctx);
    a.it = property_recursion_top(&a.frames);
    property_recursion_next(&b.frames, ctx);
    b.it = property_recursion_top(&b.frames);
  }

  property_recursion_free(&a.frames, JS_GetRuntime(ctx));
  property_recursion_free(&b.frames, JS_GetRuntime(ctx));

  return ret;
}

static JSValue
js_deep_iterate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return js_deep_iterator_constructor(ctx, deep_iterator_ctor, argc, argv);
}

static JSValue
js_deep_clone(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
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
    JS_PROP_INT32_DEF("NO_THROW", NO_THROW, JS_PROP_ENUMERABLE),
};

static const JSCFunctionListEntry js_deep_iterator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_deep_iterator_next, 0),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_deep_iterator_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Deep Iterator", JS_PROP_CONFIGURABLE),
};

static int
js_deep_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_deep_iterator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_deep_iterator_class_id, &js_deep_iterator_class);

  deep_iterator_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, deep_iterator_proto, js_deep_iterator_proto_funcs, countof(js_deep_iterator_proto_funcs));
  JS_SetClassProto(ctx, js_deep_iterator_class_id, deep_iterator_proto);

  deep_iterator_ctor = JS_NewCFunction2(ctx, js_deep_iterator_constructor, "DeepIterator", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, deep_iterator_ctor, deep_iterator_proto);

  deep_functions = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, deep_functions, js_deep_funcs, countof(js_deep_funcs));

  if(m) {
    JS_SetModuleExportList(ctx, m, js_deep_funcs, countof(js_deep_funcs));
    JS_SetModuleExport(ctx, m, "DeepIterator", deep_iterator_ctor);
    JS_SetModuleExport(ctx, m, "default", deep_functions);
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

  if((m = JS_NewCModule(ctx, module_name, js_deep_init))) {
    JS_AddModuleExportList(ctx, m, js_deep_funcs, countof(js_deep_funcs));
    JS_AddModuleExport(ctx, m, "DeepIterator");
    JS_AddModuleExport(ctx, m, "default");
  }

  return m;
}

/**
 * @}
 */
