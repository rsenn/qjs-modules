#define _GNU_SOURCE

#include "quickjs-pointer.h"
#include "utils.h"
#include <string.h>

VISIBLE JSClassID js_pointer_class_id = 0;
static JSValue pointer_proto, pointer_ctor;

enum pointer_methods {
  METHOD_DEREF = 0,
  METHOD_TO_STRING,
  METHOD_TO_ARRAY,
  METHOD_INSPECT,
  METHOD_SHIFT,
  METHOD_SLICE,
  METHOD_KEYS,
  METHOD_VALUES
};
enum pointer_functions { STATIC_FROM = 0, STATIC_OF };
enum pointer_getters { PROP_LENGTH = 0, PROP_PATH };

JSValue
js_pointer_new(JSContext* ctx, JSValueConst proto, JSValueConst value) {
  Pointer *ptr;
  JSValue obj = JS_UNDEFINED;
  if(!(ptr = js_mallocz(ctx, sizeof(Pointer))))
    return JS_EXCEPTION;
  pointer_reset(ptr, ctx);
  obj = JS_NewObjectProtoClass(ctx, proto, js_pointer_class_id);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, ptr);

  if(!pointer_from(ptr, ctx, value, &js_pointer_data)) {
    JS_FreeValue(ctx, obj);
    obj = JS_ThrowTypeError(ctx, "Pointer: argument 1 unknown type");
  }
  return obj;
fail:
  js_free(ctx, ptr);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
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

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_pointer_class_id)))
    return JS_EXCEPTION;

  js_dbuf_init(ctx, &dbuf);
  pointer_tostring(ptr, ctx, &dbuf);
  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
  return ret;
}

static JSValue
js_pointer_inspect(JSContext* ctx, JSValueConst this_val, BOOL color) {
  Pointer* ptr;
  DynBuf dbuf;
  JSValue ret;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_pointer_class_id)))
    return JS_EXCEPTION;

  js_dbuf_init(ctx, &dbuf);
  pointer_dump(ptr, ctx, &dbuf, color, -1);
  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
  return ret;
}

static JSValue
js_pointer_toarray(JSContext* ctx, Pointer* ptr) {
  size_t i;
  JSValue array = JS_NewArray(ctx);
  for(i = 0; i < ptr->n; i++)
    JS_SetPropertyUint32(ctx, array, i, JS_AtomToValue(ctx, ptr->atoms[i]));
  return array;
}

static JSValue
js_pointer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  JSValue proto;
  /* using new_target to get the prototype is necessary when the
     class is extended. */
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
js_pointer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  Pointer* ptr;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_pointer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_DEREF: {
      return js_pointer_deref(ctx, ptr, this_val, argv[0]);
    }

    case METHOD_TO_STRING: {
      return js_pointer_tostring(ctx, this_val);
    }

    case METHOD_TO_ARRAY: {
      return js_pointer_toarray(ctx, ptr);
    }

    case METHOD_INSPECT: {
      return js_pointer_inspect(ctx, this_val, TRUE);
    }

    case METHOD_SLICE: {
      int64_t s = js_int64_default(ctx, argv[0], 0);
      int64_t e = js_int64_default(ctx, argv[1], 0);

      return js_pointer_wrap(ctx, pointer_slice(ptr, ctx, s, e));
    }

    case METHOD_KEYS: {
      JSValue array = js_pointer_toarray(ctx, ptr);
      JSValue iter = js_iterator_new(ctx, array);
      JS_FreeValue(ctx, array);
      return iter;
    }

    case METHOD_VALUES: {
      JSValue array = js_pointer_toarray(ctx, ptr);
      JSValue iter = js_iterator_new(ctx, array);
      JS_FreeValue(ctx, array);
      return iter;
    }

    case METHOD_SHIFT: {
      return pointer_shift(ptr, ctx, argv[0]);
    }
  }
  return JS_EXCEPTION;
}

static JSValue
js_pointer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Pointer* ptr;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_pointer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_LENGTH: {
      return JS_NewUint32(ctx, ptr->n);
    }

    case PROP_PATH: {
      return js_pointer_toarray(ctx, ptr);
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_pointer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Pointer* ptr;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_pointer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_PATH: {
      pointer_fromiterable(ptr, ctx, value);
      break;
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_pointer_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  JSValue ret;

  switch(magic) {
    case STATIC_FROM: {
      return js_pointer_new(ctx, pointer_proto, argc > 0 ? argv[0] : JS_UNDEFINED);
    }

    case STATIC_OF: {
      int i;
      Pointer* ptr;

      if(!(ptr = pointer_new(ctx)))
        return JS_ThrowOutOfMemory(ctx);

      ret = js_pointer_wrap(ctx, ptr);
      for(i = 0; i < argc; i++) {
        JSAtom atom = JS_ValueToAtom(ctx, argv[i]);
        pointer_push(ptr, atom);
      }
      break;
    }
  }
  return ret;
}

static void
js_pointer_finalizer(JSRuntime* rt, JSValue val) {
  Pointer* ptr;

  if((ptr = JS_GetOpaque(val, js_pointer_class_id))) {
    if(ptr->atoms) {
      uint32_t i;
      for(i = 0; i < ptr->n; i++) JS_FreeAtomRT(rt, ptr->atoms[i]);
      js_free_rt(rt, ptr->atoms);
    }
    js_free_rt(rt, ptr);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_pointer_class = {
    .class_name = "Pointer",
    .finalizer = js_pointer_finalizer,
};

static const JSCFunctionListEntry js_pointer_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("deref", 1, js_pointer_method, METHOD_DEREF),
    JS_CFUNC_MAGIC_DEF("toString", 0, js_pointer_method, METHOD_TO_STRING),
    JS_CFUNC_MAGIC_DEF("toArray", 0, js_pointer_method, METHOD_TO_ARRAY),
    JS_CFUNC_MAGIC_DEF("inspect", 0, js_pointer_method, METHOD_INSPECT),
    JS_CFUNC_MAGIC_DEF("shift", 1, js_pointer_method, METHOD_SHIFT),
    JS_CFUNC_MAGIC_DEF("slice", 2, js_pointer_method, METHOD_SLICE),
    JS_CFUNC_MAGIC_DEF("keys", 0, js_pointer_method, METHOD_KEYS),
    JS_CFUNC_MAGIC_DEF("values", 0, js_pointer_method, METHOD_VALUES),
    JS_ALIAS_DEF("toPrimitive", "toString"),
    JS_ALIAS_DEF("[Symbol.iterator]", "keys"),
    JS_CGETSET_MAGIC_DEF("length", js_pointer_get, 0, PROP_LENGTH),
    JS_CGETSET_MAGIC_DEF("path", js_pointer_get, js_pointer_set, PROP_PATH),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Pointer", JS_PROP_C_W_E),
};

static const JSCFunctionListEntry js_pointer_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("from", 1, js_pointer_funcs, STATIC_FROM),
    JS_CFUNC_MAGIC_DEF("of", 0, js_pointer_funcs, STATIC_OF),
};

static int
js_pointer_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_pointer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_pointer_class_id, &js_pointer_class);

  pointer_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx,
                             pointer_proto,
                             js_pointer_proto_funcs,
                             countof(js_pointer_proto_funcs));
  JS_SetClassProto(ctx, js_pointer_class_id, pointer_proto);

  pointer_ctor =
      JS_NewCFunction2(ctx, js_pointer_constructor, "Pointer", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, pointer_ctor, pointer_proto);
  JS_SetPropertyFunctionList(ctx,
                             pointer_ctor,
                             js_pointer_static_funcs,
                             countof(js_pointer_static_funcs));

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
