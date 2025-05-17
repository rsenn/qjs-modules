#include "defines.h"
#include <cutils.h>
#include "property-enumeration.h"
#include <quickjs.h>
#include "utils.h"
#include "pointer.h"
#include "virtual-properties.h"
#include "quickjs-predicate.h"
#include "quickjs-pointer.h"
#include "debug.h"

#include <stdint.h>

/**
 * \defgroup quickjs-deep quickjs-deep: Deep object
 * @{
 */
VISIBLE JSClassID js_deep_iterator_class_id = 0;
static JSValue deep_functions, deep_iterator_proto, deep_iterator_ctor;

typedef enum {
  YIELD_MASK = 1,
  YIELD_AND_RECURSE = 1,
  YIELD_NO_RECURSE = 3,
  RECURSE = 0,
  NO_RECURSE = 2,
} DeepIteratorStatus;

typedef enum {
  RETURN_VALUE_PATH = 0b00 << 24,
  RETURN_PATH = 0b01 << 24,
  RETURN_VALUE = 0b10 << 24,
  RETURN_PATH_VALUE = 0b11 << 24,
  RETURN_MASK = 0b11 << 24,
  PATH_AS_STRING = 0b100 << 24,
  PATH_AS_POINTER = 0b1000 << 24,
  NO_THROW = 0b10000 << 24,
  MAXDEPTH_MASK = 0x00ffffff,
} DeepIteratorFlags;

typedef struct DeepIterator {
  Vector frames;
  Pointer ptr;
  JSValue root, pred;
  DeepIteratorFlags flags;
  DeepIteratorStatus status;
  ValueType mask;
  uint32_t seq;
} DeepIterator;

static const uint32_t js_deep_defaultflags = 0;

JSValue
property_recursion_pointer_value(const Vector* vec, JSContext* ctx) {
  Pointer* ptr;

  if(!(ptr = property_recursion_pointer(vec, ctx)))
    return JS_EXCEPTION;

  return js_pointer_wrap(ctx, ptr);
}

static inline PropEnumPathValueFunc*
js_deep_pathfunc(int flags) {
  return (flags & PATH_AS_STRING)    ? property_recursion_pathstr_value
         : (flags & PATH_AS_POINTER) ? property_recursion_pointer_value
                                     : property_recursion_path;
}

static int
js_deep_predicate(JSContext* ctx, JSValueConst fn, JSValueConst value, const Vector* frames) {
  Predicate* pred;
  JSValue ret = JS_UNDEFINED;
  JSValueConst args[] = {
      JS_IsUninitialized(value) ? property_recursion_value(frames, ctx) : JS_DupValue(ctx, value),
      property_enumeration_key(property_recursion_top(frames), ctx),
  };

  if((pred = js_predicate_data(fn))) {
    JSArguments a = js_arguments_new(countof(args), args);
    ret = predicate_eval(pred, ctx, &a);
  } else if(JS_IsFunction(ctx, fn)) {
    ret = JS_Call(ctx, fn, JS_UNDEFINED, countof(args), args);
  }

  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, args[0]);

  if(JS_IsException(ret)) {
    JS_GetException(ctx);
    ret = JS_FALSE;
  }

  int32_t result = JS_IsNumber(ret) ? js_value_toint32_free(ctx, ret) : js_value_tobool_free(ctx, ret);

  /*if(result != 0)
    printf("js_deep_predicate ret=%s result=%" PRId32 "\n", JS_ToCString(ctx, ret), result);*/

  return result;
}

static JSValue
js_deep_return(JSContext* ctx, Vector* frames, int32_t flags) {
  JSValue ret;
  PropertyEnumeration* it = property_recursion_top(frames);
  PropEnumPathValueFunc* path_fn = js_deep_pathfunc(!!(flags & PATH_AS_STRING));
  int32_t return_flags = flags & RETURN_MASK;

  flags &= ~RETURN_MASK;

  switch(return_flags) {
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
      int idx = return_flags == RETURN_PATH_VALUE;
      ret = JS_NewArray(ctx);
      JS_SetPropertyUint32(ctx, ret, idx, property_enumeration_value(it, ctx));
      JS_SetPropertyUint32(ctx, ret, !idx, path_fn(frames, ctx));
      break;
    }
  }

  return ret;
}

static JSValue
js_deep_iterator_new(
    JSContext* ctx, JSValueConst proto, JSValueConst root, JSValueConst pred, uint32_t flags, ValueType mask) {
  JSValue obj = JS_UNDEFINED;
  DeepIterator* it;

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
  it->mask = mask;
  return obj;

fail:
  js_free(ctx, it);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_deep_iterator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED, root = JS_UNDEFINED, pred = JS_UNDEFINED;
  uint32_t flags = js_deep_defaultflags, mask = TYPE_ALL;

  int i = 0;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;

  if(i < argc)
    root = argv[i++];

  if(i < argc && JS_IsFunction(ctx, argv[i]))
    pred = argv[i++];

  if(i < argc) {
    JS_ToUint32(ctx, &flags, argv[i++]);

    if(i < argc)
      JS_ToUint32(ctx, &mask, argv[i++]);
  }

  obj = js_deep_iterator_new(ctx, proto, root, pred, flags, mask);

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

  if((max_depth = (it->flags & MAXDEPTH_MASK)) == 0)
    max_depth = MAXDEPTH_MASK;

  if(!JS_IsObject(it->root)) {
    *pdone = TRUE;
    return JS_UNDEFINED;
  }

  for(;;) {
    depth = property_recursion_depth(&it->frames);

    if(it->seq == 0)
      property_recursion_push(&it->frames, ctx, JS_DupValue(ctx, it->root), PROPENUM_DEFAULT_FLAGS);
    else if((it->status & NO_RECURSE) || depth >= max_depth)
      property_recursion_skip(&it->frames, ctx);
    else
      property_recursion_next(&it->frames, ctx);

    ++it->seq;

    if(!(penum = property_recursion_top(&it->frames))) {
      *pdone = TRUE;
      return ret;
    }

    if(property_enumeration_length(penum) == 0)
      continue;

    JSValue value = property_recursion_value(&it->frames, ctx);
    ValueType type = js_value_type(ctx, value);

    if(type & it->mask)
      it->status = js_deep_predicate(ctx, it->pred, value, &it->frames);
    else
      it->status = 0;

    JS_FreeValue(ctx, value);

    if(!(it->status & YIELD_MASK))
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
  uint32_t flags = js_deep_defaultflags, max_depth = MAXDEPTH_MASK, mask = TYPE_ALL;
  PropertyEnumeration* it;
  Vector frames;

  if(argc > 2) {
    JS_ToUint32(ctx, &flags, argv[2]);
    if(flags & MAXDEPTH_MASK)
      max_depth = flags & MAXDEPTH_MASK;
  }

  if(argc > 3)
    JS_ToUint32(ctx, &mask, argv[3]);

  if(!predicate_callable(ctx, argv[1]))
    return JS_ThrowTypeError(ctx, "argument 2 (predicate) is not a function");

  if(!JS_IsObject(argv[0]))
    return JS_ThrowTypeError(ctx, "argument 1 (root) is not an object");

  vector_init(&frames, ctx);

  it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);

  if(it)
    do {
      int r;

      JSValue value = property_recursion_value(&frames, ctx);
      ValueType type = js_value_type(ctx, value);

      if(type & mask)
        r = js_deep_predicate(ctx, argv[1], value, &frames);
      else
        r = 0;
      JS_FreeValue(ctx, value);

      if((r & YIELD_MASK)) {
        ret = js_deep_return(ctx, &frames, flags & ~MAXDEPTH_MASK);
        break;
      }

      if(vector_size(&frames, sizeof(PropertyEnumeration)) >= max_depth)
        property_recursion_skip(&frames, ctx);
      else
        property_recursion_next(&frames, ctx);

    } while((it = property_recursion_top(&frames)));

  /*t = time_us() - t; printf("js_deep_find took %" PRIu64 "s %" PRIu64 "us\n", t / 1000000, t %
   * 1000000);*/

  property_recursion_free(&frames, JS_GetRuntime(ctx));
  return ret;
}

static JSValue
js_deep_select(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  uint32_t i = 0, flags = js_deep_defaultflags, max_depth = MAXDEPTH_MASK, mask = TYPE_ALL;
  PropertyEnumeration* it;
  Vector frames;

  if(argc > 2) {
    JS_ToUint32(ctx, &flags, argv[2]);

    if(flags & MAXDEPTH_MASK)
      max_depth = flags & MAXDEPTH_MASK;
  }
  if(argc > 3)
    JS_ToUint32(ctx, &mask, argv[3]);

  if(!predicate_callable(ctx, argv[1]))
    return JS_ThrowTypeError(ctx, "argument 1 (predicate) is not a function");

  vector_init(&frames, ctx);

  ret = JS_NewArray(ctx);
  it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);

  int r = 0;

  while(it) {
    JSValue value = property_recursion_value(&frames, ctx);
    ValueType type = js_value_type(ctx, value);

    if(type & mask)
      r = js_deep_predicate(ctx, argv[1], value, &frames);
    else
      r = 0;

    JS_FreeValue(ctx, value);

    if((r & YIELD_MASK))
      JS_SetPropertyUint32(ctx, ret, i++, js_deep_return(ctx, &frames, flags & ~MAXDEPTH_MASK));

    if(vector_size(&frames, sizeof(PropertyEnumeration)) >= max_depth) {
      property_recursion_skip(&frames, ctx);
      it = property_recursion_top(&frames);
    }

    if(r & NO_RECURSE)
      property_recursion_skip(&frames, ctx);
    else {
      property_recursion_next(&frames, ctx);
      it = property_recursion_top(&frames);
    }
  }

  property_recursion_free(&frames, JS_GetRuntime(ctx));
  return ret;
}

static JSValue js_deep_get(JSContext*, JSValueConst, int, JSValueConst[]);

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
    uint32_t flags = 0;

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

static JSValue js_deep_set(JSContext*, JSValueConst, int, JSValueConst[]);

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

    return JS_DupValue(ctx, obj);
  }

  return JS_NewCFunctionData(ctx, js_deep_set2, 2, 0, 1, &argv[0]);
}

static JSValue js_deep_unset(JSContext*, JSValueConst, int, JSValueConst[]);

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
  ValueType type = js_value_type(ctx, argv[1]);
  JSValueCompareFunc* cmp_fn = (type & TYPE_OBJECT) ? js_object_same2 : js_value_equals;
  Vector frames;
  PropertyEnumeration* it;
  uint32_t flags = js_deep_defaultflags;

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

  a.it =
      property_recursion_push(&a.frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS | PROPENUM_SORT_ATOMS);
  b.it =
      property_recursion_push(&b.frames, ctx, JS_DupValue(ctx, argv[1]), PROPENUM_DEFAULT_FLAGS | PROPENUM_SORT_ATOMS);

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
    JS_CONSTANT(YIELD_MASK),
    JS_CONSTANT(YIELD_AND_RECURSE),
    JS_CONSTANT(YIELD_NO_RECURSE),
    JS_CONSTANT(RECURSE),
    JS_CONSTANT(NO_RECURSE),
    JS_CONSTANT(RETURN_VALUE_PATH),
    JS_CONSTANT(RETURN_PATH),
    JS_CONSTANT(RETURN_VALUE),
    JS_CONSTANT(RETURN_PATH_VALUE),
    JS_CONSTANT(RETURN_MASK),
    JS_CONSTANT(PATH_AS_POINTER),
    JS_CONSTANT(PATH_AS_STRING),
    JS_CONSTANT(NO_THROW),
    JS_CONSTANT(TYPE_UNDEFINED),
    JS_CONSTANT(TYPE_NULL),
    JS_CONSTANT(TYPE_BOOL),
    JS_CONSTANT(TYPE_INT),
    JS_CONSTANT(TYPE_OBJECT),
    JS_CONSTANT(TYPE_STRING),
    JS_CONSTANT(TYPE_SYMBOL),
    JS_CONSTANT(TYPE_BIG_FLOAT),
    JS_CONSTANT(TYPE_BIG_INT),
    JS_CONSTANT(TYPE_BIG_DECIMAL),
    JS_CONSTANT(TYPE_FLOAT64),
    JS_CONSTANT(TYPE_NAN),
    JS_CONSTANT(TYPE_NUMBER),
    JS_CONSTANT(TYPE_PRIMITIVE),
    JS_CONSTANT(TYPE_ALL),
    JS_CONSTANT(TYPE_FUNCTION),
    JS_CONSTANT(TYPE_ARRAY),
    JS_CONSTANT(TYPE_MODULE),
    JS_CONSTANT(TYPE_FUNCTION_BYTECODE),
    JS_CONSTANT(TYPE_UNINITIALIZED),
    JS_CONSTANT(TYPE_CATCH_OFFSET),
    JS_CONSTANT(TYPE_EXCEPTION),
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
  JS_SetPropertyFunctionList(ctx,
                             deep_iterator_proto,
                             js_deep_iterator_proto_funcs,
                             countof(js_deep_iterator_proto_funcs));
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
