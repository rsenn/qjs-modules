#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "gpio.h"
//#include <threads.h>
#include "quickjs-gpio.h"
#include "utils.h"

#define max(a, b) ((a) > (b) ? (a) : (b))

thread_local VISIBLE JSClassID js_gpio_class_id = 0;
thread_local JSValue gpio_proto = {JS_TAG_UNDEFINED}, gpio_ctor = {JS_TAG_UNDEFINED};

enum { GPIO_METHOD_INIT_PIN, GPIO_METHOD_SET_PIN, GPIO_METHOD_GET_PIN };

struct gpio*
js_gpio_data(JSContext* ctx, JSValueConst value) {
  struct gpio* gpio;
  gpio = JS_GetOpaque2(ctx, value, js_gpio_class_id);
  return gpio;
}

static JSValue
js_gpio_wrap_proto(JSContext* ctx, JSValueConst proto, struct gpio* gpio) {
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

  JS_SetOpaque(obj, gpio);

  return obj;
fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_gpio_wrap(JSContext* ctx, struct gpio* gpio) {
  return js_gpio_wrap_proto(ctx, gpio_proto, gpio);
}

static JSValue
js_gpio_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  struct gpio* gpio;
  JSValue ret = JS_UNDEFINED;

  if(!(gpio = js_gpio_data(ctx, this_val)))
    return ret;

  switch(magic) {

    case GPIO_METHOD_INIT_PIN: {
      uint32_t pin = 0;
      BOOL output = JS_ToBool(ctx, argv[1]);
      JS_ToUint32(ctx, &pin, argv[0]);
      gpio_init_pin(gpio, pin, output);
      break;
    }
    case GPIO_METHOD_SET_PIN: {
      uint32_t pin = 0;
      BOOL value = JS_ToBool(ctx, argv[1]);
      JS_ToUint32(ctx, &pin, argv[0]);
      gpio_set_pin(gpio, pin, value);
      break;
    }
    case GPIO_METHOD_GET_PIN: {
      uint32_t pin = 0;
      BOOL value;
      JS_ToUint32(ctx, &pin, argv[0]);
      value = gpio_get_pin(gpio, pin);
      ret = JS_NewInt32(ctx, value);
      break;
    }
  }

  return ret;
}

static JSValue
js_gpio_getter(JSContext* ctx, JSValueConst this_val, int magic) {
  struct gpio* gpio;
  JSValue ret = JS_UNDEFINED;

  if(!(gpio = js_gpio_data(ctx, this_val)))
    return ret;

  switch(magic) {}
  return ret;
}

static JSValue
js_gpio_setter(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  struct gpio* gpio;
  JSValue ret = JS_UNDEFINED;

  if(!(gpio = js_gpio_data(ctx, this_val)))
    return ret;

  switch(magic) {}
  return ret;
}

static JSValue
js_gpio_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  struct gpio* gpio;
  JSValue obj = JS_UNDEFINED, proto = JS_UNDEFINED;

  if(!(gpio = js_mallocz(ctx, sizeof(struct gpio))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_gpio_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  if(!gpio_open(gpio))
    return JS_ThrowInternalError(ctx, "gpio_open() failed");

  JS_SetOpaque(obj, gpio);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_gpio_finalizer(JSRuntime* rt, JSValue val) {
  struct gpio* gpio = JS_GetOpaque(val, js_gpio_class_id);
  if(gpio) {
    gpio_close(gpio);
  }
  JS_FreeValueRT(rt, val);
}

static JSClassDef js_gpio_class = {
    .class_name = "GPIO",
    .finalizer = js_gpio_finalizer,
};

static const JSCFunctionListEntry js_gpio_funcs[] = {
    JS_CFUNC_MAGIC_DEF("initPin", 2, js_gpio_functions, GPIO_METHOD_INIT_PIN),
    JS_CFUNC_MAGIC_DEF("setPin", 2, js_gpio_functions, GPIO_METHOD_SET_PIN),
    JS_CFUNC_MAGIC_DEF("getPin", 1, js_gpio_functions, GPIO_METHOD_GET_PIN),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "GPIO", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_gpio_static_funcs[] = {
    JS_PROP_INT32_DEF("INPUT", 0, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("OUTPUT", 1, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("LOW", 0, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("HIGH", 1, JS_PROP_ENUMERABLE),
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
    JS_SetConstructor(ctx, gpio_ctor, gpio_proto);
    // JS_SetClassProto(ctx, js_gpio_class_id, gpio_proto);
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
