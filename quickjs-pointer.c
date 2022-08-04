#include "include/defines.h"
#include "quickjs-pointer.h"
#include "include/buffer-utils.h"
#include "include/utils.h"
#include <string.h>
#include "include/debug.h"

/**
 * \addtogroup quickjs-pointer
 * @{
 */

thread_local VISIBLE JSClassID js_pointer_class_id = 0;
thread_local JSValue pointer_proto = {{JS_TAG_UNDEFINED}}, pointer_ctor = {{JS_TAG_UNDEFINED}};

enum {
  METHOD_DEREF = 0,
  METHOD_TO_STRING,
  METHOD_TO_ARRAY,
  METHOD_INSPECT,
  METHOD_SHIFT,
  METHOD_PUSH,
  METHOD_CONCAT,
  METHOD_SLICE,
  METHOD_UP,
  METHOD_DOWN,
  METHOD_KEYS,
  METHOD_VALUES,
  METHOD_HIER,
};
enum {
  STATIC_FROM = 0,
  STATIC_FROM_ATOMS,
  STATIC_OF,
  STATIC_OF_ATOMS,
  STATIC_IS_POINTER,
};
enum {
  PROP_LENGTH = 0,
  PROP_PATH,
  PROP_ATOMS,
};

VISIBLE JSValue
js_pointer_new(JSContext* ctx, JSValueConst proto, JSValueConst value) {
  Pointer* ptr;
  JSValue obj = JS_UNDEFINED;

  if(!(ptr = js_mallocz(ctx, sizeof(Pointer))))
    return JS_EXCEPTION;

  pointer_reset(ptr, ctx);

  obj = JS_NewObjectProtoClass(ctx, proto, js_pointer_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ptr);

  if(!pointer_from(ptr, ctx, value)) {
    JS_FreeValue(ctx, obj);
    obj = JS_ThrowTypeError(ctx, "Pointer: argument 1 unknown type");
  }

  return obj;
fail:
  js_free(ctx, ptr);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

VISIBLE JSValue
js_pointer_wrap(JSContext* ctx, Pointer* ptr) {
  JSValue obj;

  obj = JS_NewObjectProtoClass(ctx, pointer_proto, js_pointer_class_id);

  JS_SetOpaque(obj, ptr);

  return obj;
}

static JSValue
js_pointer_tostring(JSContext* ctx, JSValueConst this_val) {
  Pointer* ptr;
  DynBuf dbuf;
  JSValue ret;

  if(!(ptr = js_pointer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  js_dbuf_init(ctx, &dbuf);
  pointer_tostring(ptr, ctx, &dbuf);
  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
  return ret;
}

static JSValue
js_pointer_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  Pointer* ptr;
  DynBuf dbuf;
  BOOL color = FALSE;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_pointer_class_id)))
    return JS_EXCEPTION;

  if(argc > 1 && JS_IsObject(argv[1])) {
    color = js_get_propertystr_bool(ctx, argv[1], "colors");
  }

  js_dbuf_init(ctx, &dbuf);
  pointer_dump(ptr, ctx, &dbuf, color, -1);
  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
  return ret;
}

static JSValue
js_pointer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, pointer_proto);

  return js_pointer_new(ctx, proto, argc > 0 ? argv[0] : JS_UNDEFINED);
}

static JSValue
js_pointer_deref(JSContext* ctx, Pointer* ptr, JSValueConst this_arg, JSValueConst arg) {
  return pointer_deref(ptr, ctx, arg);
}

static JSValue
js_pointer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Pointer* ptr;

  if(!(ptr = js_pointer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_DEREF: {
      return js_pointer_deref(ctx, ptr, this_val, argv[0]);
    }
    case METHOD_TO_STRING: {
      return js_pointer_tostring(ctx, this_val);
    }
    case METHOD_TO_ARRAY: {
      return pointer_toarray(ptr, ctx);
    }
    case METHOD_SLICE: {
      int64_t s = js_int64_default(ctx, argv[0], 0);
      int64_t e = js_int64_default(ctx, argv[1], 0);
      return js_pointer_wrap(ctx, pointer_slice(ptr, ctx, s, e));
    }
    case METHOD_UP: {
      int64_t n = js_int64_default(ctx, argv[0], 0);
      return js_pointer_wrap(ctx, pointer_slice(ptr, ctx, 0, ptr->n - n));
    }
    case METHOD_DOWN: {
      Pointer* res = pointer_clone(ptr, ctx);
      int i;
      for(i = 0; i < argc; i++) pointer_push(res, ctx, argv[i]);
      return js_pointer_wrap(ctx, res);
    }
    case METHOD_KEYS: {
      JSValue array = pointer_toarray(ptr, ctx);
      JSValue iter = js_iterator_new(ctx, array);
      JS_FreeValue(ctx, array);
      return iter;
    }
    case METHOD_VALUES: {
      JSValue array = pointer_toarray(ptr, ctx);
      JSValue iter = js_iterator_new(ctx, array);
      JS_FreeValue(ctx, array);
      return iter;
    }
    case METHOD_SHIFT: {
      return pointer_shift(ptr, ctx, argv[0]);
    }
    case METHOD_PUSH: {
      int i;
      for(i = 0; i < argc; i++) pointer_push(ptr, ctx, argv[i]);
      return JS_DupValue(ctx, this_val);
    }
    case METHOD_CONCAT: {
      Pointer* res = pointer_concat(ptr, ctx, argv[0]);
      return js_pointer_wrap(ctx, res);
    }
    case METHOD_HIER: {
      JSValue ret = JS_NewArray(ctx);
      size_t i, j = 0;

      for(i = 1; i < ptr->n; i++) {
        Pointer* h = pointer_slice(ptr, ctx, 0, i);
        JSValue value = js_pointer_wrap(ctx, h);
        if(argc >= 1) {
          JSValue ret = JS_Call(ctx, argv[0], JS_UNDEFINED, 1, &value);
          JS_FreeValue(ctx, value);
          value = ret;
        }
        JS_SetPropertyUint32(ctx, ret, j++, value);
      }
      JS_SetPropertyUint32(ctx, ret, j++, JS_DupValue(ctx, this_val));
      return ret;
    }
  }
  return JS_EXCEPTION;
}

static JSValue
js_pointer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Pointer* ptr;
  JSValue ret = JS_UNDEFINED;

  if(!(ptr = js_pointer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_LENGTH: {
      ret = JS_NewUint32(ctx, ptr->n);
      break;
    }
    case PROP_PATH: {
      ret = pointer_toarray(ptr, ctx);
      break;
    }
    case PROP_ATOMS: {
      ret = pointer_toatoms(ptr, ctx);
      break;
    }
  }
  return ret;
}

static JSValue
js_pointer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Pointer* ptr;
  JSValue ret = JS_UNDEFINED;
  if(!(ptr = js_pointer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_PATH: {
      pointer_fromiterable(ptr, ctx, value);
      break;
    }
    case PROP_ATOMS: {
      ret = JS_NewInt32(ctx, pointer_fromatoms(ptr, ctx, value));
      break;
    }
  }
  return ret;
}

static JSValue
js_pointer_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret;

  switch(magic) {
    case STATIC_FROM: {
      return js_pointer_new(ctx, pointer_proto, argc > 0 ? argv[0] : JS_UNDEFINED);
    }

    case STATIC_FROM_ATOMS: {
      int i;
      Pointer* ptr;
      if(!(ptr = pointer_new(ctx)))
        return JS_ThrowOutOfMemory(ctx);
      ret = js_pointer_wrap(ctx, ptr);
      pointer_fromatoms(ptr, ctx, argv[0]);
      break;
    }

    case STATIC_OF: {
      int i;
      Pointer* ptr;
      if(!(ptr = pointer_new(ctx)))
        return JS_ThrowOutOfMemory(ctx);
      ret = js_pointer_wrap(ctx, ptr);
      for(i = 0; i < argc; i++) { pointer_push(ptr, ctx, argv[i]); }
      break;
    }

    case STATIC_OF_ATOMS: {
      int i;
      Pointer* ptr;
      if(!(ptr = pointer_new(ctx)))
        return JS_ThrowOutOfMemory(ctx);

      ret = js_pointer_wrap(ctx, ptr);
      for(i = 0; i < argc; i++) {
        uint32_t atom;
        JS_ToUint32(ctx, &atom, argv[i]);
        pointer_pushatom(ptr, ctx, JS_DupAtom(ctx, atom));
      }
      break;
    }

    case STATIC_IS_POINTER: {
      Pointer* ptr;
      ptr = js_pointer_data(argv[0]);
      ret = JS_NewBool(ctx, !!ptr);
      break;
    }
  }
  return ret;
}

static void
js_pointer_finalizer(JSRuntime* rt, JSValue val) {
  Pointer* ptr;

  if((ptr = js_pointer_data(val))) {
    if(ptr->atoms) {
      uint32_t i;
      for(i = 0; i < ptr->n; i++) JS_FreeAtomRT(rt, ptr->atoms[i]);
      js_free_rt(rt, ptr->atoms);
    }
    js_free_rt(rt, ptr);
  }
  // JS_FreeValueRT(rt, val);
}

static const JSCFunctionListEntry js_pointer_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("deref", 1, js_pointer_method, METHOD_DEREF),
    JS_CFUNC_MAGIC_DEF("toString", 0, js_pointer_method, METHOD_TO_STRING),
    JS_CFUNC_MAGIC_DEF("toArray", 0, js_pointer_method, METHOD_TO_ARRAY),
    JS_CFUNC_MAGIC_DEF("shift", 1, js_pointer_method, METHOD_SHIFT),
    JS_CFUNC_MAGIC_DEF("push", 1, js_pointer_method, METHOD_PUSH),
    JS_CFUNC_MAGIC_DEF("concat", 1, js_pointer_method, METHOD_CONCAT),
    JS_CFUNC_MAGIC_DEF("slice", 2, js_pointer_method, METHOD_SLICE),
    JS_CFUNC_MAGIC_DEF("up", 1, js_pointer_method, METHOD_UP),
    JS_CFUNC_MAGIC_DEF("down", 0, js_pointer_method, METHOD_DOWN),
    JS_CFUNC_MAGIC_DEF("keys", 0, js_pointer_method, METHOD_KEYS),
    JS_CFUNC_MAGIC_DEF("values", 0, js_pointer_method, METHOD_VALUES),
    JS_CFUNC_MAGIC_DEF("hier", 0, js_pointer_method, METHOD_HIER),
    JS_ALIAS_DEF("toPrimitive", "toString"),
    JS_ALIAS_DEF("[Symbol.iterator]", "keys"),
    JS_CGETSET_MAGIC_DEF("length", js_pointer_get, 0, PROP_LENGTH),
    JS_CGETSET_MAGIC_DEF("path", js_pointer_get, js_pointer_set, PROP_PATH),
    JS_CGETSET_MAGIC_DEF("atoms", js_pointer_get, 0, PROP_ATOMS),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Pointer", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_pointer_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("from", 1, js_pointer_funcs, STATIC_FROM),
    JS_CFUNC_MAGIC_DEF("fromAtoms", 1, js_pointer_funcs, STATIC_FROM_ATOMS),
    JS_CFUNC_MAGIC_DEF("of", 0, js_pointer_funcs, STATIC_OF),
    JS_CFUNC_MAGIC_DEF("ofAtoms", 0, js_pointer_funcs, STATIC_OF_ATOMS),
    JS_CFUNC_MAGIC_DEF("isPointer", 1, js_pointer_funcs, STATIC_IS_POINTER),
};

static int
js_pointer_get_own_property(JSContext* ctx, JSPropertyDescriptor* pdesc, JSValueConst obj, JSAtom prop) {
  Pointer* pointer = js_pointer_data2(ctx, obj);

  JSValue value = JS_UNDEFINED;
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    if(index < 0)
      index = ((index % pointer->n) + pointer->n) % pointer->n;

    if(index < (int64_t)pointer->n) {
      JSAtom key = pointer->atoms[index];
      value = (key & (1U << 31)) ? JS_NewUint32(ctx, key & (~(1U << 31))) : JS_AtomToValue(ctx, key);

      if(pdesc) {
        pdesc->flags = JS_PROP_ENUMERABLE;
        pdesc->value = value;
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
      }
      return TRUE;
    }
  }
  return FALSE;
}

static int
js_pointer_get_own_property_names(JSContext* ctx, JSPropertyEnum** ptab, uint32_t* plen, JSValueConst obj) {
  Pointer* pointer;
  uint32_t i, len;
  JSPropertyEnum* props;
  if((pointer = js_pointer_data2(ctx, obj)))
    len = pointer->n;
  else {
    JSValue length = JS_GetPropertyStr(ctx, obj, "length");
    JS_ToUint32(ctx, &len, length);
    JS_FreeValue(ctx, length);
  }

  props = js_malloc(ctx, sizeof(JSPropertyEnum) * (len + 1));

  for(i = 0; i < len; i++) {
    props[i].is_enumerable = TRUE;
    props[i].atom = i | (1U << 31);
  }

  props[len].is_enumerable = TRUE;
  props[len].atom = JS_NewAtom(ctx, "length");

  *ptab = props;
  *plen = len + 1;
  return 0;
}

static int
js_pointer_has_property(JSContext* ctx, JSValueConst obj, JSAtom prop) {
  Pointer* pointer = js_pointer_data2(ctx, obj);
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    if(index < 0)
      index = ((index % (int64_t)(pointer->n + 1)) + pointer->n);

    if(index < (int64_t)pointer->n)
      return TRUE;
  } else if(js_atom_is_length(ctx, prop)) {
    return TRUE;
  } else {
    JSValue proto = JS_GetPrototype(ctx, obj);
    if(JS_IsObject(proto) && JS_HasProperty(ctx, proto, prop))
      return TRUE;
  }

  return FALSE;
}

static JSValue
js_pointer_get_property(JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst receiver) {
  Pointer* pointer = js_pointer_data2(ctx, obj);
  JSValue value = JS_UNDEFINED;
  int64_t index;
  int32_t entry;

  if(js_atom_is_index(ctx, &index, prop)) {
    if(index < 0)
      index = ((index % (int64_t)(pointer->n + 1)) + pointer->n);

    if(index < (int64_t)pointer->n) {
      JSAtom key = pointer->atoms[index];
      value = (key & (1U << 31)) ? JS_NewUint32(ctx, key & (~(1U << 31))) : JS_AtomToValue(ctx, key);
    }
  } else if(js_atom_is_length(ctx, prop)) {
    value = JS_NewUint32(ctx, pointer->n);
  } else if((entry = js_find_cfunction_atom(ctx, js_pointer_proto_funcs, countof(js_pointer_proto_funcs), prop, JS_DEF_CGETSET_MAGIC)) >= 0) {

    // printf("entry: %d magic: %d\n", entry,
    // js_pointer_proto_funcs[entry].magic);
    value = js_pointer_get(ctx, obj, js_pointer_proto_funcs[entry].magic);

  } else {
    JSValue proto = JS_IsUndefined(pointer_proto) ? JS_GetPrototype(ctx, obj) : pointer_proto;
    if(JS_IsObject(proto))
      value = JS_GetProperty(ctx, proto, prop);
  }

  return value;
}

static int
js_pointer_set_property(JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst value, JSValueConst receiver, int flags) {
  Pointer* pointer = js_pointer_data2(ctx, obj);
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    if(index < 0)
      index = ((index % (int64_t)(pointer->n + 1)) + pointer->n);

    if(index == (int64_t)pointer->n)
      pointer_push(pointer, ctx, value);
    else if(index < (int64_t)pointer->n)
      pointer->atoms[index] = JS_ValueToAtom(ctx, value);
    return TRUE;
  }

  return FALSE;
}

static JSClassExoticMethods js_pointer_exotic_methods = {
    .get_own_property = js_pointer_get_own_property,
    .get_own_property_names = js_pointer_get_own_property_names,
    .has_property = js_pointer_has_property,
    .get_property = js_pointer_get_property,
    .set_property = js_pointer_set_property,
};

static JSClassDef js_pointer_class = {
    .class_name = "Pointer",
    .finalizer = js_pointer_finalizer,
    .exotic = &js_pointer_exotic_methods,
};

static int
js_pointer_init(JSContext* ctx, JSModuleDef* m) {
  JSAtom inspectAtom;
  JS_NewClassID(&js_pointer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_pointer_class_id, &js_pointer_class);

  pointer_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, pointer_proto, js_pointer_proto_funcs, countof(js_pointer_proto_funcs));

  JSValue array_proto = js_global_prototype(ctx, "Array");

  JS_DefinePropertyValueStr(ctx, pointer_proto, "map", JS_GetPropertyStr(ctx, array_proto, "map"), JS_PROP_CONFIGURABLE);
  JS_DefinePropertyValueStr(ctx, pointer_proto, "reduce", JS_GetPropertyStr(ctx, array_proto, "reduce"), JS_PROP_CONFIGURABLE);
  JS_DefinePropertyValueStr(ctx, pointer_proto, "forEach", JS_GetPropertyStr(ctx, array_proto, "forEach"), JS_PROP_CONFIGURABLE);

  js_set_inspect_method(ctx, pointer_proto, js_pointer_inspect);

  JS_SetClassProto(ctx, js_pointer_class_id, pointer_proto);

  pointer_ctor = JS_NewCFunction2(ctx, js_pointer_constructor, "Pointer", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, pointer_ctor, pointer_proto);
  JS_SetPropertyFunctionList(ctx, pointer_ctor, js_pointer_static_funcs, countof(js_pointer_static_funcs));

  if(m) {
    JS_SetModuleExport(ctx, m, "Pointer", pointer_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_pointer
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, &js_pointer_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "Pointer");
  return m;
}

/**
 * @}
 */
