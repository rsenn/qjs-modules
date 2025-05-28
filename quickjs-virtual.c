#include "virtual-properties.h"

/**
 * \addtogroup quickjs-VirtualProperties
 * @{
 */

VISIBLE JSClassID js_virtual_class_id = 0;
static JSValue virtual_proto, virtual_ctor;

static inline VirtualProperties*
js_virtual_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_virtual_class_id);
}

static inline VirtualProperties*
js_virtual_data(JSValueConst value) {
  return JS_GetOpaque(value, js_virtual_class_id);
}

JSValue
js_virtual_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  VirtualProperties* virt;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_virtual_class_id);
  if(JS_IsException(obj))
    goto fail;

  if(!(virt = js_malloc(ctx, sizeof(VirtualProperties))))
    goto fail;

  *virt = virtual_properties(ctx, argv[0]);

  JS_SetOpaque(obj, virt);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_virtual_wrap(JSContext* ctx, JSValueConst proto, VirtualProperties virt) {
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_virtual_class_id);
  VirtualProperties* v;

  if(!(v = js_malloc(ctx, sizeof(VirtualProperties))))
    goto fail;

  *v = virt;

  JS_SetOpaque(obj, v);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  VIRTUAL_HAS,
  VIRTUAL_GET,
  VIRTUAL_SET,
  VIRTUAL_DELETE,
  VIRTUAL_KEYS,
};

static JSValue
js_virtual_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  VirtualProperties* virt;
  JSValue ret = JS_UNDEFINED;

  if(!(virt = js_virtual_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case VIRTUAL_HAS: {
      ret = JS_NewBool(ctx, virtual_has(virt, ctx, argv[0]));
      break;
    }
    case VIRTUAL_GET: {
      ret = virtual_get(virt, ctx, argv[0]);
      break;
    }
    case VIRTUAL_SET: {
      ret = JS_NewInt32(ctx, virtual_set(virt, ctx, argv[0], argv[1]));
      break;
    }
    case VIRTUAL_DELETE: {
      ret = JS_NewBool(ctx, virtual_delete(virt, ctx, argv[0]));
      break;
    }
    case VIRTUAL_KEYS: {
      int32_t flags = (JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY);

      if(argc > 0)
        JS_ToInt32(ctx, &flags, argv[0]);

      ret = virtual_keys(virt, ctx, flags);
      break;
    }
  }

  return ret;
}

enum {
  VIRTUAL_ARRAY,
  VIRTUAL_MAP,
  VIRTUAL_OBJECT,
  VIRTUAL_FROM,
};

static JSValue
js_virtual_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  VirtualProperties virt = {JS_EXCEPTION, 0, 0, 0, 0, 0, 0, 0, 0};

  switch(magic) {
    case VIRTUAL_ARRAY: {
      virt = virtual_properties_array(ctx, argv[0]);
      break;
    }
    case VIRTUAL_MAP: {
      virt = virtual_properties_map(ctx, argv[0]);
      break;
    }
    case VIRTUAL_OBJECT: {
      virt = virtual_properties_object(ctx, argv[0]);
      break;
    }
    case VIRTUAL_FROM: {
      virt = virtual_properties(ctx, argv[0]);
      break;
    }
  }

  return js_virtual_wrap(ctx, virtual_proto, virt);
}

void
js_virtual_finalizer(JSRuntime* rt, JSValue val) {
  VirtualProperties* virt;

  if((virt = js_virtual_data(val))) {
    virtual_properties_free_rt(virt, rt);
    js_free_rt(rt, virt);
  }
}

static JSClassDef js_virtual_class = {
    .class_name = "VirtualProperties",
    .finalizer = js_virtual_finalizer,
};

static const JSCFunctionListEntry js_virtual_methods[] = {
    JS_CFUNC_MAGIC_DEF("has", 1, js_virtual_method, VIRTUAL_HAS),
    JS_CFUNC_MAGIC_DEF("get", 1, js_virtual_method, VIRTUAL_GET),
    JS_CFUNC_MAGIC_DEF("set", 2, js_virtual_method, VIRTUAL_SET),
    JS_CFUNC_MAGIC_DEF("delete", 1, js_virtual_method, VIRTUAL_DELETE),
    JS_CFUNC_MAGIC_DEF("keys", 0, js_virtual_method, VIRTUAL_KEYS),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "VirtualProperties", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_virtual_functions[] = {
    JS_CFUNC_MAGIC_DEF("array", 1, js_virtual_function, VIRTUAL_ARRAY),
    JS_CFUNC_MAGIC_DEF("map", 1, js_virtual_function, VIRTUAL_MAP),
    JS_CFUNC_MAGIC_DEF("object", 1, js_virtual_function, VIRTUAL_OBJECT),
    JS_CFUNC_MAGIC_DEF("from", 1, js_virtual_function, VIRTUAL_FROM),
};

int
js_virtual_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_virtual_class_id);

  JS_NewClass(JS_GetRuntime(ctx), js_virtual_class_id, &js_virtual_class);

  virtual_ctor = JS_NewCFunction2(ctx, js_virtual_constructor, "VirtualProperties", 1, JS_CFUNC_constructor, 0);
  virtual_proto = JS_NewObjectProto(ctx, JS_NULL);

  JS_SetPropertyFunctionList(ctx, virtual_proto, js_virtual_methods, countof(js_virtual_methods));
  JS_SetPropertyFunctionList(ctx, virtual_ctor, js_virtual_functions, countof(js_virtual_functions));

  JS_SetClassProto(ctx, js_virtual_class_id, virtual_proto);
  JS_SetConstructor(ctx, virtual_ctor, virtual_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "VirtualProperties", virtual_ctor);

  return 0;
}

#ifdef JS_VIRTUAL_MODULE
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_virtual
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_virtual_init)))
    JS_AddModuleExport(ctx, m, "VirtualProperties");

  return m;
}

/**
 * @}
 */
