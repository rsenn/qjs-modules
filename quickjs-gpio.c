#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "gpio.h"
#include <threads.h>
#include "quickjs-gpio.h"
#include "utils.h"

#define max(a, b) ((a) > (b) ? (a) : (b))

thread_local VISIBLE JSClassID js_gpio_class_id = 0;
thread_local JSValue gpio_proto = {.tag = JS_TAG_UNDEFINED}, gpio_ctor = {.tag = JS_TAG_UNDEFINED};

enum { GPIO_METHOD_READ, GPIO_METHOD_WRITE, GPIO_METHOD_READFILE, GPIO_METHOD_WRITEFILE };
enum { GPIO_PROP_FORMAT, GPIO_PROP_COMPRESSION, GPIO_PROP_FILTERS, GPIO_PROP_FILECOUNT };

struct gpio*
js_gpio_data(JSContext* ctx, JSValueConst value) {
  struct gpio* ar;
  ar = JS_GetOpaque2(ctx, value, js_gpio_class_id);
  return ar;
}

static JSValue
js_gpio_wrap_proto(JSContext* ctx, JSValueConst proto, struct gpio* ar) {
  JSValue obj;

  if(js_gpio_class_id == 0)
    js_gpio_init(ctx, 0);
  if(JS_IsNull(proto) || JS_IsUndefined(proto))
    proto = JS_DupValue(ctx, gpio_proto);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_gpio_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ar);

  return obj;
fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_gpio_wrap(JSContext* ctx, struct gpio* ar) {
  return js_gpio_wrap_proto(ctx, gpio_proto, ar);
}

static JSValue
js_gpio_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  struct gpio* ar = 0;
  JSValue proto = JS_GetPropertyStr(ctx, this_val, "prototype");
  JSValue ret = JS_UNDEFINED;

  switch(magic) {}

  return ret;
}

static JSValue
js_gpio_getter(JSContext* ctx, JSValueConst this_val, int magic) {
  struct gpio* ar;
  JSValue ret = JS_UNDEFINED;

  if(!(ar = js_gpio_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case GPIO_PROP_FORMAT: {
      ret = JS_NewString(ctx, gpio_format_name(ar));
      break;
    }
    case GPIO_PROP_COMPRESSION: {
      ret = JS_NewString(ctx, gpio_compression_name(ar));
      break;
    }
    case GPIO_PROP_FILTERS: {
      int i, num_filters = gpio_filter_count(ar);
      ret = JS_NewArray(ctx);
      for(i = 0; i < num_filters; i++) {
        JS_SetPropertyUint32(ctx, ret, i, JS_NewString(ctx, gpio_filter_name(ar, i)));
      }
      break;
    }
    case GPIO_PROP_FILECOUNT: {
      ret = JS_NewUint32(ctx, gpio_file_count(ar));
      break;
    }
  }
  return ret;
}

static JSValue
js_gpio_setter(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  struct gpio* ar;
  JSValue ret = JS_UNDEFINED;

  if(!(ar = js_gpio_data(ctx, this_val)))
    return ret;

  switch(magic) {}
  return ret;
}

static JSValue
js_gpio_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  return js_gpio_wrap_proto(ctx, proto, 0);

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_gpio_finalizer(JSRuntime* rt, JSValue val) {
  struct gpio* ar = JS_GetOpaque(val, js_gpio_class_id);
  if(ar) {
    gpio_free(ar);
  }
  JS_FreeValueRT(rt, val);
}

static JSClassDef js_gpio_class = {
    .class_name = "GPIO",
    .finalizer = js_gpio_finalizer,
};

static const JSCFunctionListEntry js_gpio_funcs[] = {

    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "GPIO", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_gpio_static_funcs[] = {
    JS_PROP_INT32_DEF("INPUT", 0, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("OUTPUT", 1, JS_PROP_ENUMERABLE),

};

int
js_gpio_init(JSContext* ctx, JSModuleDef* m) {

  if(js_gpio_class_id == 0) {
    JS_NewClassID(&js_gpio_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_gpio_class_id, &js_gpio_class);

    gpio_ctor = JS_NewCFunction2(ctx, js_gpio_constructor, "GPIO", 1, JS_CFUNC_constructor, 0);
    gpio_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, gpio_proto, js_gpio_funcs, countof(js_gpio_funcs));
    JS_SetPropertyFunctionList(ctx, gpio_ctor, js_gpio_static_funcs, countof(js_gpio_static_funcs));
    JS_SetClassProto(ctx, js_gpio_class_id, gpio_proto);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "GPIO", gpio_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_gpio
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  if(!(m = JS_NewCModule(ctx, module_name, &js_gpio_init)))
    return m;
  JS_AddModuleExport(ctx, m, "GPIO");
  return m;
}
