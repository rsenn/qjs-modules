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
  YIELD = 1,
  YIELD_NO_RECURSE = 3,
  RECURSE = 0,
  NO_RECURSE = 2,
} DeepIteratorStatus;

#define STATUS_YIELD(status) ((status)&YIELD_MASK)
#define STATUS_RECURSE(status) (!((status)&NO_RECURSE))

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
  FILTER_MASK = 1 << 30,
  MAXDEPTH_MASK = (1 << 24) - 1,
} DeepIteratorFlags;

#define FLAGS_RETURN(flags) ((flags)&RETURN_MASK)
#define FLAGS_FILTER(flags) ((flags)&FILTER_MASK)
#define FLAGS_MAXDEPTH(flags) (((flags)&MAXDEPTH_MASK) ? (flags)&MAXDEPTH_MASK : MAXDEPTH_MASK)
#define FLAGS_PATH_AS(flags) ((flags)&PATH_AS_MASK)

typedef struct DeepIterator {
  Vector frames;
  Pointer ptr;
  JSValue root, pred;
  DeepIteratorFlags flags;
  DeepIteratorStatus status;
  ValueType mask;
  Vector atoms;
  uint32_t seq;
} DeepIterator;

static const uint32_t js_deep_defaultflags = 0;

static BOOL
atom_skip(Vector* vec, JSAtom atom) {
  if(vector_find(vec, sizeof(JSAtom), &atom) != -1)
    return FALSE;

  return TRUE;
}

static BOOL
atoms_skip(Vector* vec, JSAtom* atoms, size_t len) {
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

  vector_foreach_t(atoms, ptr) { JS_FreeAtomRT(rt, *ptr); }

  vector_free(atoms);
}

JSValue
property_recursion_pointer_value(const Vector* vec, JSContext* ctx, void* opaque) {
  DeepIterator* it = opaque;
  Pointer* ptr = pointer_new(ctx);

  if(property_recursion_pointer(vec, ptr, ctx) < 0)
    return JS_EXCEPTION;

  return js_pointer_wrap(ctx, ptr);
}

typedef JSValue path_func_type(const Vector*, JSContext*, void*);

static inline path_func_type*
js_deep_pathfunc(int flags) {
  switch(FLAGS_PATH_AS(flags)) {
    case PATH_AS_STRING: return (path_func_type*)(void*)&property_recursion_pathstr_value;
    case PATH_AS_POINTER: return property_recursion_pointer_value;
    default: return (path_func_type*)(void*)&property_recursion_path;
  }
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

  int32_t result =
      JS_IsBool(ret) ? (js_value_tobool_free(ctx, ret) ? YIELD : RECURSE) : js_value_toint32_free(ctx, ret);

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
js_deep_iterator_new(JSContext* ctx,
                     JSValueConst proto,
                     JSValueConst root,
                     JSValueConst pred,
                     uint32_t flags,
                     ValueType mask,
                     JSValueConst props) {
  JSValue obj = JS_UNDEFINED;
  DeepIterator* it;

  if(!(it = js_mallocz(ctx, sizeof(DeepIterator))))
    return JS_EXCEPTION;

  vector_init(&it->frames, ctx);

  it->root = JS_UNDEFINED;
  it->pred = JS_UNDEFINED;
  it->atoms = VECTOR(ctx);

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

    if(!(obj_type & TYPE_ARRAY))
      if(FLAGS_FILTER(iter->flags) == FILTER_KEY_OF)
        if(!vector_empty(&iter->atoms) && atom_skip(&iter->atoms, atom))
          continue;

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
      if(FLAGS_FILTER(iter->flags) == FILTER_HAS_KEY)
        if(!vector_empty(&iter->atoms)) {
          JSAtom* p;
          BOOL has = FALSE;

          vector_foreach_t(&iter->atoms, p) {
            if(JS_HasProperty(ctx, value, *p)) {
              has = TRUE;
              break;
            }
          }

          if(!has) {
            JS_FreeValue(ctx, value);
            continue;
          }
        }

    if((1 << js_value_type2flag(type)) & iter->mask) {
      if(js_is_null_or_undefined(iter->pred))
        iter->status = YIELD;
      else
        iter->status = js_deep_predicate(ctx, iter->pred, value, &iter->frames);
    } else
      iter->status = RECURSE;

    JS_FreeValue(ctx, value);

    if(!STATUS_YIELD(iter->status))
      continue;

    if(iter->flags & PATH_AS_POINTER)
      property_recursion_pointer(&iter->frames, &iter->ptr, ctx);

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
  pointer_reset(&iter->ptr, JS_GetRuntime(ctx));
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
    pointer_reset(&iter->ptr, rt);
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
  Vector frames;
  Vector atoms = VECTOR(ctx);

  if(argc > 2)
    JS_ToUint32(ctx, &flags, argv[2]);

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

  if((it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS)))
    do {
      int r;

      JSAtom atom = property_enumeration_atom(it);
      ValueType obj_type = js_value_type(ctx, it->obj);

      if((obj_type & TYPE_ARRAY) || !atom_skip(&atoms, atom)) {

        JSValue value = property_recursion_value(&frames, ctx);
        ValueType type = 1 << js_value_type_get(ctx, value);
        // ValueTypeFlag flag = js_value_type2flag(type);

        if(type & mask)
          r = js_deep_predicate(ctx, argv[1], value, &frames);
        else
          r = 0;

        JS_FreeValue(ctx, value);

        if((r & YIELD_MASK)) {
          ret = js_deep_return(ctx, &frames, flags & ~MAXDEPTH_MASK, 0);
          break;
        }
      }

      if(property_recursion_depth(&frames) >= max_depth)
        property_recursion_skip(&frames, ctx);
      else
        property_recursion_next(&frames, ctx);

    } while((it = property_recursion_top(&frames)));

  property_recursion_free(&frames, JS_GetRuntime(ctx));

  JSAtom* p;
  vector_foreach_t(&atoms, p) { JS_FreeAtom(ctx, *p); }
  vector_free(&atoms);

  return ret;
}

static JSValue
js_deep_select(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  DeepIteratorFlags flags = js_deep_defaultflags;
  uint32_t i = 0, max_depth;
  ValueType mask = TYPE_ALL;
  PropertyEnumeration* it;
  Vector frames;

  if(argc > 2)
    JS_ToUint32(ctx, &flags, argv[2]);

  max_depth = FLAGS_MAXDEPTH(flags);
  flags &= ~MAXDEPTH_MASK;

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
    ValueType type = 1 << js_value_type_get(ctx, value);

    if(type & mask)
      r = js_deep_predicate(ctx, argv[1], value, &frames);
    else
      r = 0;

    JS_FreeValue(ctx, value);

    if((r & YIELD_MASK))
      JS_SetPropertyUint32(ctx, ret, i++, js_deep_return(ctx, &frames, flags & ~MAXDEPTH_MASK, 0));

    if(!STATUS_RECURSE(r) || property_recursion_depth(&frames) >= max_depth)
      property_recursion_skip(&frames, ctx);
    else
      property_recursion_next(&frames, ctx);

    it = property_recursion_top(&frames);
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
    Pointer ptr=POINTER_INIT();

    if(!js_pointer_from(&ptr, argv[1], ctx))
      return JS_EXCEPTION;

    JSAtom prop = pointer_popatom(&ptr);
    JSValue obj = pointer_acquire(&ptr, argv[0], ctx);

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
    Pointer ptr =POINTER_INIT();

    if(!js_pointer_from(&ptr, argv[1], ctx))
      return JS_EXCEPTION;

    JSAtom prop = pointer_popatom(&ptr);
    JSValue obj = pointer_deref(&ptr, argv[0], ctx);

    if(!JS_IsException(obj))
      JS_DeleteProperty(ctx, obj, prop, 0);

    JS_FreeAtom(ctx, prop);
    pointer_reset(&ptr, JS_GetRuntime(ctx));

    return JS_DupValue(ctx, obj);
  }

  return JS_NewCFunctionData(ctx, js_deep_unset2, 1, 0, 1, &argv[0]);
}

static JSValue
js_deep_flatten(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  DynBuf dbuf;
  VirtualProperties vmap;
  Vector frames, offsets;
  PropertyEnumeration* it;
  uint32_t mask = TYPE_ALL;

  js_dbuf_init(ctx, &dbuf);

  ret = argc > 1 && JS_IsObject(argv[1]) ? JS_DupValue(ctx, argv[1]) : JS_NewObject(ctx);
  vmap = virtual_properties(ctx, ret);

  vector_init(&frames, ctx);
  vector_init(&offsets, ctx);

  it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);

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
  Vector frames;
  PropertyEnumeration* it;
  uint32_t flags = js_deep_defaultflags;

  if(argc > 2)
    JS_ToUint32(ctx, &flags, argv[2]);

  vector_init(&frames, ctx);

  it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);

  do {
    JSValue value = property_enumeration_value(it, ctx);
    BOOL result = js_value_type(ctx, value) != type ? FALSE : cmp_fn(ctx, argv[1], value, FALSE);
    JS_FreeValue(ctx, value);

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
  PropertyEnumeration* it;
  JSValueConst fn, this_arg;
  Vector frames;
  DeepIteratorFlags flags = js_deep_defaultflags;
  uint32_t max_depth;
  ValueType type_mask = TYPE_ALL;

  vector_init(&frames, ctx);

  fn = argv[1];
  this_arg = argc > 2 ? argv[2] : JS_UNDEFINED;

  if(argc > 3)
    JS_ToUint32(ctx, &flags, argv[3]);

  max_depth = FLAGS_MAXDEPTH(flags);
  flags &= ~MAXDEPTH_MASK;

  if(argc > 4)
    JS_ToUint32(ctx, &type_mask, argv[4]);

  it = property_recursion_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);

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

  if(!JS_IsObject(argv[0]) || !JS_IsObject(argv[1]))
    return JS_NewBool(ctx, js_value_equals(ctx, argv[0], argv[1], FALSE));

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

    result = (JS_IsObject(a.val) && JS_IsObject(b.val)) ? TRUE : js_value_equals(ctx, a.val, b.val, FALSE);

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
    JS_CONSTANT(YIELD),
    JS_CONSTANT(YIELD_NO_RECURSE),
    JS_CONSTANT(RECURSE),
    JS_CONSTANT(NO_RECURSE),
    JS_CONSTANT(RETURN_VALUE_PATH),
    JS_CONSTANT(RETURN_PATH),
    JS_CONSTANT(RETURN_VALUE),
    JS_CONSTANT(RETURN_PATH_VALUE),
    JS_CONSTANT(RETURN_MASK),
    JS_CONSTANT(PATH_AS_ARRAY),
    JS_CONSTANT(PATH_AS_POINTER),
    JS_CONSTANT(PATH_AS_STRING),
    JS_CONSTANT(PATH_AS_MASK),
    JS_CONSTANT(FILTER_KEY_OF),
    JS_CONSTANT(FILTER_HAS_KEY),
    JS_CONSTANT(FILTER_MASK),
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
