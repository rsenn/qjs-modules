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
  YIELD = 1,
  YIELD_NO_RECURSE = 3,
  RECURSE = 0,
  NO_RECURSE = 2,
} DeepIteratorStatus;

#define STATUS_YIELD(status) ((status) & YIELD)
#define STATUS_RECURSE(status) (!((status) & NO_RECURSE))

typedef enum {
  RETURN_VALUE_PATH = 0 << 24,
  RETURN_PATH = 1 << 24,
  RETURN_VALUE = 2 << 24,
  RETURN_PATH_VALUE = 3 << 24,
  RETURN_MASK = 3 << 24,
  PATH_AS_ARRAY = 0 << 26,
  PATH_AS_STRING = 1 << 26,
  PATH_AS_POINTER = 2 << 26,
  PATH_AS_MASK = 3 << 26,
  NO_THROW = 1 << 28,
  FILTER_KEY_OF = 0 << 30,
  FILTER_HAS_KEY = 1 << 30,
  FILTER_NEGATE = 2 << 30,
  FILTER_MASK = 1 << 30,
  MAXDEPTH_MASK = (1 << 24) - 1,
} DeepIteratorFlags;

#define FLAGS_RETURN(flags) ((flags) & RETURN_MASK)
#define FLAGS_FILTER(flags) ((flags) & FILTER_MASK)
#define FLAGS_NEGATE_FILTER(flags) (!!((uint32_t)(flags) & (uint32_t)FILTER_NEGATE))
#define FLAGS_MAXDEPTH(flags) (((flags) & MAXDEPTH_MASK) ? (flags) & MAXDEPTH_MASK : MAXDEPTH_MASK)
#define FLAGS_PATH_AS(flags) ((flags) & PATH_AS_MASK)

typedef struct DeepIterator {
  Vector frames;
  Pointer* pointer;
  JSValue pointer_value;
  JSValue root, pred;
  DeepIteratorFlags flags;
  DeepIteratorStatus status;
  ValueType mask;
  Vector atoms;
  uint32_t seq;
} DeepIterator;

static const uint32_t js_deep_defaultflags = 0;

static BOOL
atom_skip(Vector const* vec, JSAtom atom) {
  return vector_find(vec, sizeof(JSAtom), &atom) == -1;
}

static BOOL
atoms_skip(Vector const* vec, JSAtom const atoms[], size_t len) {
  for(size_t i = 0; i < len; ++i)
    if(vector_find(vec, sizeof(JSAtom), &atoms[i]) != -1)
      return FALSE;

  return TRUE;
}

static uint32_t
atoms_from_iterable(JSContext* ctx, JSValueConst arg, Vector* atoms) {
  JSValue iter = js_iterator_new(ctx, arg);

  for(;;) {
    BOOL done = FALSE;
    JSValue item = js_iterator_next(ctx, iter, &done);

    if(done) {
      JS_FreeValue(ctx, item);
      break;
    }

    JSAtom atom = JS_ValueToAtom(ctx, item);
    JS_FreeValue(ctx, item);

    vector_push(atoms, atom);
  }

  JS_FreeValue(ctx, iter);
  return vector_size(atoms, sizeof(JSAtom));
}

static void
atoms_free(Vector* atoms, JSRuntime* rt) {
  JSAtom* ptr;

  vector_foreach_t(atoms, ptr) {
    JS_FreeAtomRT(rt, *ptr);
  }

  vector_free(atoms);
}

static BOOL
atoms_in_object(Vector* atoms, JSValueConst obj, JSContext* ctx) {
  JSAtom* ptr;

  vector_foreach_t(atoms, ptr) if(JS_HasProperty(ctx, obj, *ptr)) return TRUE;

  return FALSE;
}

JSValue
property_recursion_pointer_value(const Vector* vec, JSContext* ctx, void* opaque) {
  DeepIterator* it = opaque;
  Pointer* ptr;

  if(it) {
    if(!it->pointer) {
      it->pointer = pointer_new(ctx);
      it->pointer_value = js_pointer_wrap(ctx, it->pointer);
    }

    ptr = it->pointer;
  } else
    ptr = pointer_new(ctx);

  if(property_recursion_pointer(vec, ptr, ctx) < 0)
    return JS_EXCEPTION;

  return it ? JS_DupValue(ctx, it->pointer_value) : js_pointer_wrap(ctx, ptr);
}

typedef JSValue path_func_type(const Vector*, JSContext*, void*);

static inline path_func_type*
js_deep_pathfunc(int flags) {
  switch(FLAGS_PATH_AS(flags)) {
    case PATH_AS_STRING: {
      return (path_func_type*)(void*)&property_recursion_pathstr_value;
    }

    case PATH_AS_POINTER: {
      return property_recursion_pointer_value;
    }

    default: {
      return (path_func_type*)(void*)&property_recursion_path;
    }
  }
}

static int
js_deep_predicate(JSContext* ctx, JSValueConst fn, JSValueConst value, const Vector* frames, JSValueConst pointer) {
  Predicate* pred;
  JSValue ret = JS_UNDEFINED;
  JSValueConst args[] = {
      JS_IsUninitialized(value) ? property_recursion_value(frames, ctx) : JS_DupValue(ctx, value),
      JS_IsUndefined(pointer) ? property_enumeration_key(property_recursion_top(frames), ctx) : JS_DupValue(ctx, pointer),
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

  int32_t result = JS_IsBool(ret) ? (js_value_tobool_free(ctx, ret) ? YIELD : RECURSE) : js_value_toint32_free(ctx, ret);

  /*if(result != 0)
    printf("js_deep_predicate ret=%s result=%" PRId32 "\n", JS_ToCString(ctx, ret), result);*/

  return result;
}

static JSValue
js_deep_return(JSContext* ctx, Vector* frames, int32_t flags, void* opaque) {
  JSValue ret;
  PropertyEnumeration* it = property_recursion_top(frames);
  path_func_type* path_fn = js_deep_pathfunc(flags);

  switch(FLAGS_RETURN(flags)) {
    case RETURN_VALUE: {
      ret = property_enumeration_value(it, ctx);
      break;
    }

    case RETURN_PATH: {
      ret = path_fn(frames, ctx, opaque);
      break;
    }

    case RETURN_VALUE_PATH:
    case RETURN_PATH_VALUE: {
      int idx = FLAGS_RETURN(flags) == RETURN_PATH_VALUE;
      ret = JS_NewArray(ctx);
      JS_SetPropertyUint32(ctx, ret, idx, property_enumeration_value(it, ctx));
      JS_SetPropertyUint32(ctx, ret, !idx, path_fn(frames, ctx, opaque));
      break;
    }
  }

  return ret;
}

static JSValue
js_deep_iterator_new(JSContext* ctx, JSValueConst proto, JSValueConst root, JSValueConst pred, uint32_t flags, ValueType mask, JSValueConst props) {
  JSValue obj = JS_UNDEFINED;
  DeepIterator* it;

  if(!(it = js_mallocz(ctx, sizeof(DeepIterator))))
    return JS_EXCEPTION;

  vector_init(&it->frames, ctx);

  it->root = JS_UNDEFINED;
  it->pred = JS_UNDEFINED;
  it->atoms = VECTOR(ctx);
  it->pointer = 0;
  it->pointer_value = JS_UNDEFINED;

  obj = JS_NewObjectProtoClass(ctx, proto, js_deep_iterator_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, it);

  if(!js_is_null_or_undefined(root))
    it->root = JS_DupValue(ctx, root);

  if(!js_is_null_or_undefined(pred))
    it->pred = JS_DupValue(ctx, pred);

  if(!js_is_null_or_undefined(props))
    atoms_from_iterable(ctx, props, &it->atoms);

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
  JSValue proto, obj = JS_UNDEFINED, root = JS_UNDEFINED, pred = JS_UNDEFINED, props = JS_UNDEFINED;
  uint32_t flags = js_deep_defaultflags, mask = TYPE_ALL;

  int i = 0;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;

  if(i < argc) {
    root = argv[i++];

    if(i < argc) {
      if(!JS_IsNumber(argv[i])) {
        if(JS_IsFunction(ctx, argv[i]))
          pred = argv[i];
        ++i;
      }
    }

    if(i < argc) {
      if(!JS_ToUint32(ctx, &flags, argv[i]))
        ++i;

      if(i < argc) {
        if(!JS_ToUint32(ctx, &mask, argv[i]))
          ++i;

        if(i < argc) {
          props = argv[i++];
        }
      }
    }
  }

  obj = js_deep_iterator_new(ctx, proto, root, pred, flags, mask, props);

  JS_FreeValue(ctx, proto);
  return obj;
}

static JSValue
js_deep_iterator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  DeepIterator* iter;
  PropertyEnumeration* penum = 0;
  JSValue ret = JS_UNDEFINED;
  uint32_t max_depth;

  if(!(iter = JS_GetOpaque2(ctx, this_val, js_deep_iterator_class_id)))
    return JS_EXCEPTION;

  max_depth = FLAGS_MAXDEPTH(iter->flags);

  if(!JS_IsObject(iter->root)) {
    *pdone = TRUE;
    return JS_UNDEFINED;
  }

  for(;;) {
    uint32_t depth = property_recursion_depth(&iter->frames);

    if(iter->seq == 0)
      property_recursion_push(&iter->frames, ctx, JS_DupValue(ctx, iter->root), PROPENUM_DEFAULT_FLAGS);
    else if(!STATUS_RECURSE(iter->status) || depth >= max_depth)
      property_recursion_skip(&iter->frames, ctx);
    else
      property_recursion_next(&iter->frames, ctx);

    ++iter->seq;

    if(!(penum = property_recursion_top(&iter->frames))) {
      *pdone = TRUE;
      return ret;
    }

    if(property_enumeration_length(penum) == 0)
      continue;

    JSAtom atom = property_enumeration_atom(penum);
    ValueType obj_type = js_value_type(ctx, penum->obj);

    if(!(obj_type & TYPE_ARRAY) && FLAGS_FILTER(iter->flags) == FILTER_KEY_OF && (atom_skip(&iter->atoms, atom) ^ FLAGS_NEGATE_FILTER(iter->flags))) {
      iter->status = NO_RECURSE;
      continue;
    }

    JSValue value = property_recursion_value(&iter->frames, ctx);
    ValueType type = 1 << js_value_type_get(ctx, value);

    /*printf(
        "%s depth=%u seq=%u idx=%u/%u return=%x path-as=%x filter=%x max_depth=%06x status=%02x mask=%04x
       type=%04x\n",
        __func__,
        property_recursion_depth(&iter->frames),
        iter->seq,
        penum ? penum->idx : -1,
        penum ? penum->tab_atom_len : -1,
        FLAGS_RETURN(iter->flags) >> 24,
        FLAGS_PATH_AS(iter->flags) >> 26,
        FLAGS_FILTER(iter->flags) >> 30,
        max_depth,
        iter->status,
        iter->mask,
        type);*/

    if(type & (TYPE_OBJECT | TYPE_FUNCTION) && !(type & TYPE_ARRAY))
      if(FLAGS_FILTER(iter->flags) == FILTER_HAS_KEY && ((!atoms_in_object(&iter->atoms, value, ctx)) ^ FLAGS_NEGATE_FILTER(iter->flags))) {
        JS_FreeValue(ctx, value);
        continue;
      }

    if(iter->flags & PATH_AS_POINTER) {
      if(!iter->pointer) {
        iter->pointer = pointer_new(ctx);
        iter->pointer_value = js_pointer_wrap(ctx, iter->pointer);
      }

      property_recursion_pointer(&iter->frames, iter->pointer, ctx);
    }

    if((1 << js_value_type2flag(type)) & iter->mask) {
      if(js_is_null_or_undefined(iter->pred))
        iter->status = YIELD;
      else
        iter->status = js_deep_predicate(ctx, iter->pred, value, &iter->frames, iter->pointer_value);
    } else
      iter->status = RECURSE;

    JS_FreeValue(ctx, value);

    if(!STATUS_YIELD(iter->status))
      continue;

    ret = js_deep_return(ctx, &iter->frames, iter->flags & ~MAXDEPTH_MASK, iter);
    *pdone = FALSE;
    break;
  }

  return ret;
}

enum {
  METHOD_LEAVE,
  METHOD_SKIP,
};

static JSValue
js_deep_iterator_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  DeepIterator* iter;

  if(!(iter = JS_GetOpaque2(ctx, this_val, js_deep_iterator_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_LEAVE: {
      PropertyEnumeration* penum;

      if((penum = property_recursion_top(&iter->frames))) {

        property_recursion_pop(&iter->frames, ctx);

        iter->status |= NO_RECURSE;

        ret = JS_NewUint32(ctx, property_recursion_depth(&iter->frames));
      }

      break;
    }

    case METHOD_SKIP: {
      ret = JS_NewInt32(ctx, property_recursion_skip(&iter->frames, ctx));
      break;
    }
  }

  return ret;
}

enum {
  PROPERTY_PATH,
};

static JSValue
js_deep_iterator_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue ret = JS_UNDEFINED;
  DeepIterator* iter;

  if(!(iter = JS_GetOpaque2(ctx, this_val, js_deep_iterator_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROPERTY_PATH: {
      path_func_type* fn = js_deep_pathfunc(iter->flags);

      ret = fn(&iter->frames, ctx, 0);
      break;
    }
  }

  return ret;
}

static JSValue
js_deep_iterator_return(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  DeepIterator* iter;

  if(!(iter = JS_GetOpaque2(ctx, this_val, js_deep_iterator_class_id)))
    return JS_EXCEPTION;

  property_recursion_free(&iter->frames, JS_GetRuntime(ctx));
  JS_FreeValue(ctx, iter->root);
  iter->root = JS_UNDEFINED;
  JS_FreeValue(ctx, iter->pred);
  iter->pred = JS_UNDEFINED;
  // pointer_free(iter->pointer, JS_GetRuntime(ctx));
  JS_FreeValue(ctx, iter->pointer_value);
  atoms_free(&iter->atoms, JS_GetRuntime(ctx));

  *pdone = TRUE;
  return argc > 0 ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;
}

static void
js_deep_iterator_finalizer(JSRuntime* rt, JSValue val) {
  DeepIterator* iter;

  if((iter = JS_GetOpaque(val, js_deep_iterator_class_id))) {
    property_recursion_free(&iter->frames, rt);
    JS_FreeValueRT(rt, iter->root);
    JS_FreeValueRT(rt, iter->pred);

    // pointer_free(iter->pointer, rt);
    JS_FreeValueRT(rt, iter->pointer_value);

    atoms_free(&iter->atoms, rt);

    js_free_rt(rt, iter);
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
  DeepIteratorFlags flags = js_deep_defaultflags;
  ValueType mask = TYPE_ALL;
  uint32_t max_depth;
  PropertyEnumeration* it;
  Vector frames, atoms = VECTOR(ctx);
  JSValue pointer = JS_UNDEFINED;

  if(argc > 2)
    flags = js_touint32(ctx, argv[2]);

  max_depth = FLAGS_MAXDEPTH(flags);
  flags &= ~MAXDEPTH_MASK;

  if(argc > 3)
    JS_ToUint32(ctx, &mask, argv[3]);

  if(argc > 4)
    atoms_from_iterable(ctx, argv[4], &atoms);

  if(!JS_IsObject(argv[0]))
    return JS_ThrowTypeError(ctx, "argument 1 (root) is not an object");

  if(!predicate_callable(ctx, argv[1]))
    return JS_ThrowTypeError(ctx, "argument 2 (predicate) is not a function");

  vector_init(&frames, ctx);

  if(flags & PATH_AS_POINTER)
    pointer = js_pointer_new(ctx);

  if((it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS)))
    do {
      int r;

      JSAtom atom = property_enumeration_atom(it);
      ValueType obj_type = js_value_type(ctx, it->obj);

      BOOL filter = FLAGS_FILTER(flags) == FILTER_KEY_OF && /*!vector_empty(&atoms) &&*/ (atom_skip(&atoms, atom) ^ FLAGS_NEGATE_FILTER(flags));

      if((obj_type & TYPE_ARRAY) || !filter) {
        JSValue value = property_recursion_value(&frames, ctx);
        ValueType type = 1 << js_value_type_get(ctx, value);

        if(flags & PATH_AS_POINTER)
          property_recursion_pointer(&frames, js_pointer_data(pointer), ctx);

        if((type & (TYPE_OBJECT | TYPE_FUNCTION)) && !(type & TYPE_ARRAY) && FLAGS_FILTER(flags) == FILTER_HAS_KEY &&
           ((!atoms_in_object(&atoms, value, ctx)) ^ FLAGS_NEGATE_FILTER(flags)))
          r = 0;
        else if(type & mask)
          r = js_deep_predicate(ctx, argv[1], value, &frames, pointer);
        else
          r = 0;

        JS_FreeValue(ctx, value);

        if((r & YIELD)) {
          ret = js_deep_return(ctx, &frames, flags & ~MAXDEPTH_MASK, 0);
          break;
        }
      }

      if(filter || property_recursion_depth(&frames) >= max_depth)
        property_recursion_skip(&frames, ctx);
      else
        property_recursion_next(&frames, ctx);

    } while((it = property_recursion_top(&frames)));

  property_recursion_free(&frames, JS_GetRuntime(ctx));

  JS_FreeValue(ctx, pointer);

  atoms_free(&atoms, JS_GetRuntime(ctx));

  return ret;
}

static JSValue
js_deep_select(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  DeepIteratorFlags flags = js_deep_defaultflags;
  ValueType mask = TYPE_ALL;
  JSValue pointer = JS_UNDEFINED;
  Vector frames = VECTOR(ctx), atoms = VECTOR(ctx);

  if(argc > 2)
    flags = js_touint32(ctx, argv[2]);

  uint32_t i = 0, max_depth = FLAGS_MAXDEPTH(flags);
  flags &= ~MAXDEPTH_MASK;

  if(argc > 3)
    JS_ToUint32(ctx, &mask, argv[3]);

  if(argc > 4)
    atoms_from_iterable(ctx, argv[4], &atoms);

  if(!predicate_callable(ctx, argv[1]))
    return JS_ThrowTypeError(ctx, "argument 1 (predicate) is not a function");

  JSValue ret = JS_NewArray(ctx);
  PropertyEnumeration* it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);

  if(flags & PATH_AS_POINTER)
    pointer = js_pointer_new(ctx);

  while(it) {
    JSValue value = property_recursion_value(&frames, ctx);
    ValueType type = 1 << js_value_type_get(ctx, value);
    JSAtom atom = property_enumeration_atom(property_recursion_top(&frames));

    if(flags & PATH_AS_POINTER)
      property_recursion_pointer(&frames, js_pointer_data(pointer), ctx);

    int r = 0;
    BOOL filter = FLAGS_FILTER(flags) == FILTER_KEY_OF && /*!vector_empty(&atoms) &&*/ (atom_skip(&atoms, atom) ^ FLAGS_NEGATE_FILTER(flags));

    if(!filter) {
      r = (type & mask) ? js_deep_predicate(ctx, argv[1], value, &frames, pointer) : 0;

      JS_FreeValue(ctx, value);

      if((r & YIELD))
        JS_SetPropertyUint32(ctx, ret, i++, js_deep_return(ctx, &frames, flags & ~MAXDEPTH_MASK, 0));
    }

    if(!STATUS_RECURSE(r) || property_recursion_depth(&frames) >= max_depth)
      property_recursion_skip(&frames, ctx);
    else
      property_recursion_next(&frames, ctx);

    it = property_recursion_top(&frames);
  }

  JS_FreeValue(ctx, pointer);

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
    ret = pointer_deref(ptr, NULL, argv[0], ctx);

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
    Pointer ptr = POINTER_INIT();

    if(!js_pointer_from(&ptr, argv[1], ctx))
      return JS_EXCEPTION;

    JSValue obj = pointer_acquire(&ptr, ptr.n - 1, argv[0], ctx);
    JSAtom prop = pointer_popatom(&ptr);

    if(!JS_IsException(obj))
      JS_SetProperty(ctx, obj, prop, JS_DupValue(ctx, argv[2]));

    JS_FreeAtom(ctx, prop);
    pointer_reset(&ptr, JS_GetRuntime(ctx));

    return JS_DupValue(ctx, obj);
  }

  return JS_NewCFunctionData(ctx, js_deep_set2, 2, 0, 1, &argv[0]);
}

static JSValue js_deep_unset(JSContext*, JSValueConst, int, JSValueConst[]);

static JSValue
js_deep_unset2(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValueConst func_data[]) {
  JSValue args[argc + 1];

  args[0] = func_data[0];

  memcpy(&args[1], argv, sizeof(JSValue) * argc);
  return js_deep_unset(ctx, this_val, countof(args), args);
}

static JSValue
js_deep_unset(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  if(argc > 1) {
    Pointer ptr = POINTER_INIT();

    if(!js_pointer_from(&ptr, argv[1], ctx))
      return JS_EXCEPTION;

    JSAtom prop = pointer_popatom(&ptr);
    JSValue obj = pointer_deref(&ptr, NULL, argv[0], ctx);

    if(!JS_IsException(obj)) {
      if(0 > JS_DeleteProperty(ctx, obj, prop, argc > 2 && JS_ToBool(ctx, argv[2]) ? JS_PROP_THROW : 0)) {
        JS_FreeValue(ctx, obj);
        obj = JS_EXCEPTION;
      }
    }

    JS_FreeAtom(ctx, prop);
    pointer_reset(&ptr, JS_GetRuntime(ctx));

    return obj;
  }

  return JS_NewCFunctionData(ctx, js_deep_unset2, 1, 0, 1, &argv[0]);
}

static JSValue
js_deep_flatten(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  uint32_t mask = TYPE_ALL;
  DynBuf dbuf;

  dbuf_init_ctx(ctx, &dbuf);

  JSValue ret = argc > 1 && JS_IsObject(argv[1]) ? JS_DupValue(ctx, argv[1]) : JS_NewObject(ctx);
  VirtualProperties vmap = virtual_properties(ctx, ret);

  Vector frames = VECTOR(ctx), offsets = VECTOR(ctx);
  PropertyEnumeration* it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);

  if(argc > 2)
    JS_ToUint32(ctx, &mask, argv[2]);

  do {
    JSValue value = JS_UNINITIALIZED, path;

    if(mask != TYPE_ALL) {
      value = property_enumeration_value(it, ctx);
      ValueType type = 1 << js_value_type_get(ctx, value);

      if((mask & type) == 0) {
        JS_FreeValue(ctx, value);
        property_recursion_next(&frames, ctx);
        continue;
      }
    }

    if(JS_IsUninitialized(value))
      value = property_enumeration_value(it, ctx);
    path = property_recursion_pathstr_value(&frames, ctx);

    virtual_set(&vmap, ctx, path, value);

    JS_FreeValue(ctx, value);
    JS_FreeValue(ctx, path);

    property_recursion_next(&frames, ctx);

  } while((it = property_recursion_top(&frames)));

  property_recursion_free(&frames, JS_GetRuntime(ctx));
  virtual_properties_free(&vmap, ctx);
  return ret;
}

static JSValue
js_deep_pathof(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  ValueType type = js_value_type(ctx, argv[1]);
  JSValueCompareFunc* cmp_fn = (type & TYPE_OBJECT) ? js_object_same2 : js_value_equals;
  Vector frames = VECTOR(ctx);
  uint32_t flags = js_deep_defaultflags;

  if(argc > 2)
    JS_ToUint32(ctx, &flags, argv[2]);

  PropertyEnumeration* it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);

  do {
    JSValue value = property_enumeration_value(it, ctx);
    int result = js_value_type(ctx, value) != type ? FALSE : cmp_fn(ctx, argv[1], value, FALSE);
    JS_FreeValue(ctx, value);

    if(result == -1) {
      ret = JS_ThrowInternalError(ctx, "cmp_fn() returned -1");
      break;
    }

    if(result) {
      path_func_type* fn = js_deep_pathfunc(flags);

      ret = fn(&frames, ctx, 0);
      break;
    }

    property_recursion_next(&frames, ctx);
  } while((it = property_recursion_top(&frames)));

  property_recursion_free(&frames, JS_GetRuntime(ctx));
  return ret;
}

static JSValue
js_deep_foreach(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  DeepIteratorFlags flags = js_deep_defaultflags;
  ValueType type_mask = TYPE_ALL;
  JSValueConst fn = argv[1], this_arg = argc > 2 ? argv[2] : JS_UNDEFINED;

  if(argc > 3)
    flags = js_touint32(ctx, argv[3]);

  uint32_t max_depth = FLAGS_MAXDEPTH(flags);
  flags &= ~MAXDEPTH_MASK;

  if(argc > 4)
    JS_ToUint32(ctx, &type_mask, argv[4]);

  Vector frames = VECTOR(ctx);
  PropertyEnumeration* it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);

  while(it) {
    if(property_enumeration_length(it)) {
      JSValue args[] = {
          property_enumeration_value(it, ctx),
          JS_UNDEFINED,
          argv[0],
      };

      ValueType type = 1 << js_value_type_get(ctx, args[0]);

      if((type & type_mask) != 0) {
        args[1] = property_recursion_path(&frames, ctx);

        JS_Call(ctx, fn, this_arg, countof(args), args);

        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
      }
    }

    if(property_recursion_depth(&frames) >= max_depth)
      property_recursion_skip(&frames, ctx);
    else
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

  if(!JS_IsObject(argv[0]) || !JS_IsObject(argv[1])) {
    int r = js_value_equals(ctx, argv[0], argv[1], FALSE);

    return r >= 0 ? JS_NewBool(ctx, r) : JS_ThrowInternalError(ctx, "js_value_equals returned -1");
  }

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

    result = (JS_IsObject(a.val) && JS_IsObject(b.val)) ? TRUE : js_value_equals(ctx, a.val, b.val, FALSE) > 0;

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
  DeepIteratorFlags flags = js_deep_defaultflags;
  ValueType mask = TYPE_ALL;
  JSValueConst pred = JS_UNDEFINED;
  JSValue pointer = JS_UNDEFINED;
  int argi = 1;

  if(!JS_IsObject(argv[0]))
    return js_value_clone(ctx, argv[0]);

  if(argi < argc && !JS_IsNumber(argv[argi]))
    pred = argv[argi++];

  if(argi < argc)
    flags = js_touint32(ctx, argv[argi++]);

  uint32_t max_depth = FLAGS_MAXDEPTH(flags);
  flags &= ~MAXDEPTH_MASK;

  if(argi < argc)
    JS_ToUint32(ctx, &mask, argv[argi++]);

  /*if(!predicate_callable(ctx, pred))
    return JS_ThrowTypeError(ctx, "argument 2 (predicate) is not a function");*/

  JSValue ret = JS_IsArray(ctx, argv[0]) ? JS_NewArray(ctx) : JS_NewObject(ctx);
  Vector frames = VECTOR(ctx);
  Vector stack = VECTOR(ctx);
  PropertyEnumeration* it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);

  vector_push(&stack, ret);

  if(flags & PATH_AS_POINTER)
    pointer = js_pointer_new(ctx);

  while(it) {
    JSValue value = property_recursion_value(&frames, ctx);
    ValueType type = 1 << js_value_type_get(ctx, value);

    if(flags & PATH_AS_POINTER)
      property_recursion_pointer(&frames, js_pointer_data(pointer), ctx);

    int r = (type & mask) ? (js_is_null_or_undefined(pred) ? 1 : js_deep_predicate(ctx, pred, value, &frames, pointer)) : 0;

    JS_FreeValue(ctx, value);

    JSValue prop = JS_UNDEFINED;

    if((r & YIELD)) {
      prop = JS_IsObject(value) ? (JS_IsArray(ctx, value) ? JS_NewArray(ctx) : JS_NewObject(ctx)) : js_value_clone(ctx, value);

      JS_SetProperty(ctx, *(JSValue*)vector_back(&stack, sizeof(JSValue)), property_enumeration_atom(it), prop);
    }

    if(!STATUS_RECURSE(r) || property_recursion_depth(&frames) >= max_depth) {
      property_recursion_skip(&frames, ctx);
    } else {
      property_recursion_next(&frames, ctx);
    }

    it = property_recursion_top(&frames);

    uint32_t depth = property_recursion_depth(&frames);

    if(depth > vector_size(&stack, sizeof(JSValue))) {
      vector_push(&stack, prop);
    } else
      while(depth < vector_size(&stack, sizeof(JSValue)))
        vector_pop(&stack, sizeof(JSValue));
  }

  JS_FreeValue(ctx, pointer);

  property_recursion_free(&frames, JS_GetRuntime(ctx));
  return ret;
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
    JS_CONSTANT_U32(YIELD),
    JS_CONSTANT_U32(YIELD_NO_RECURSE),
    JS_CONSTANT_U32(RECURSE),
    JS_CONSTANT_U32(NO_RECURSE),
    JS_CONSTANT_U32(RETURN_VALUE_PATH),
    JS_CONSTANT_U32(RETURN_PATH),
    JS_CONSTANT_U32(RETURN_VALUE),
    JS_CONSTANT_U32(RETURN_PATH_VALUE),
    JS_CONSTANT_U32(PATH_AS_ARRAY),
    JS_CONSTANT_U32(PATH_AS_POINTER),
    JS_CONSTANT_U32(PATH_AS_STRING),
    JS_CONSTANT_U32(FILTER_KEY_OF),
    JS_CONSTANT_U32(FILTER_HAS_KEY),
    JS_CONSTANT_U32(FILTER_NEGATE),
    JS_CONSTANT_U32(NO_THROW),
    JS_CONSTANT_U32(TYPE_UNDEFINED),
    JS_CONSTANT_U32(TYPE_NULL),
    JS_CONSTANT_U32(TYPE_BOOL),
    JS_CONSTANT_U32(TYPE_INT),
    JS_CONSTANT_U32(TYPE_OBJECT),
    JS_CONSTANT_U32(TYPE_STRING),
    JS_CONSTANT_U32(TYPE_SYMBOL),
#ifdef CONFIG_BIGNUM
    JS_CONSTANT_U32(TYPE_BIG_FLOAT),
    JS_CONSTANT_U32(TYPE_BIG_DECIMAL),
#endif
    JS_CONSTANT_U32(TYPE_BIG_INT),
    JS_CONSTANT_U32(TYPE_FLOAT64),
    JS_CONSTANT_U32(TYPE_NAN),
    JS_CONSTANT_U32(TYPE_NUMBER),
    JS_CONSTANT_U32(TYPE_PRIMITIVE),
    JS_CONSTANT_U32(TYPE_ALL),
    JS_CONSTANT_U32(TYPE_FUNCTION),
    JS_CONSTANT_U32(TYPE_ARRAY),
    JS_CONSTANT_U32(TYPE_MODULE),
    JS_CONSTANT_U32(TYPE_FUNCTION_BYTECODE),
    JS_CONSTANT_U32(TYPE_UNINITIALIZED),
    JS_CONSTANT_U32(TYPE_CATCH_OFFSET),
    JS_CONSTANT_U32(TYPE_EXCEPTION),
};

static const JSCFunctionListEntry js_deep_iterator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_deep_iterator_next, 0),
    JS_ITERATOR_NEXT_DEF("return", 0, js_deep_iterator_return, 0),
    JS_CFUNC_MAGIC_DEF("leave", 0, js_deep_iterator_method, METHOD_LEAVE),
    JS_CFUNC_MAGIC_DEF("skip", 0, js_deep_iterator_method, METHOD_SKIP),
    JS_CGETSET_MAGIC_DEF("path", js_deep_iterator_get, 0, PROPERTY_PATH),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_deep_iterator_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Deep Iterator", JS_PROP_CONFIGURABLE),
};

static int
js_deep_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_deep_iterator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_deep_iterator_class_id, &js_deep_iterator_class);

  JSValue generator_proto = js_generator_prototype(ctx);
  deep_iterator_proto = JS_NewObjectProto(ctx, generator_proto);
  JS_FreeValue(ctx, generator_proto);
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
