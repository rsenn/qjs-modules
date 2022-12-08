#include "defines.h"
#include "buffer-utils.h"
#include "utils.h"
#include <errno.h>
#include <string.h>
#include "libserialport/libserialport.h"

/**
 * \defgroup quickjs-serial QuickJS module: serial - libserial MIME types
 * @{
 */
thread_local VISIBLE JSClassID js_serialport_class_id = 0;
thread_local JSValue serialport_proto = {{JS_TAG_UNDEFINED}}, serialport_ctor = {{JS_TAG_UNDEFINED}}, serial_ctor = {{JS_TAG_UNDEFINED}};

enum {
  SERIALPORT_GETINFO = 0,
  SERIALPORT_FD,
};

static inline struct sp_port*
js_serialport_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque(value, js_serialport_class_id);
}

JSValue
js_serialport_wrap(JSContext* ctx, struct sp_port* port) {
  JSValue obj;
  obj = JS_NewObjectProtoClass(ctx, serialport_proto, js_serialport_class_id);
  JS_SetOpaque(obj, port);
  return obj;
}

static JSValue
js_serialport_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int serial) {
  struct sp_port* port;
  JSValue ret = JS_UNDEFINED;

  if(!(port = js_serialport_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(serial) {
    case SERIALPORT_GETINFO: {
      int vid = -1, pid = -1;

      if(sp_get_port_usb_vid_pid(port, &vid, &pid) == SP_OK) {
        if(JS_IsUndefined(ret))
          ret = JS_NewObject(ctx);

        JS_SetPropertyStr(ctx, ret, "usbVendorId", JS_NewInt32(ctx, vid));
        JS_SetPropertyStr(ctx, ret, "usbProductId", JS_NewInt32(ctx, pid));
      }

      const char* str;

      if((str = sp_get_port_name(port))) {
        if(JS_IsUndefined(ret))
          ret = JS_NewObject(ctx);

        JS_SetPropertyStr(ctx, ret, "name", JS_NewString(ctx, str));
      }

      if((str = sp_get_port_description(port))) {
        if(JS_IsUndefined(ret))
          ret = JS_NewObject(ctx);

        JS_SetPropertyStr(ctx, ret, "description", JS_NewString(ctx, str));
      }

      break;
    }
  }
  return ret;
}

static JSValue
js_serialport_get(JSContext* ctx, JSValueConst this_val, int serial) {
  struct sp_port* port;
  JSValue ret = JS_UNDEFINED;

  if(!(port = js_serialport_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(serial) {
    case SERIALPORT_FD: {
      int64_t result = 0;
      if(sp_get_port_handle(port, &result) == SP_OK)
        ret = JS_NewInt64(ctx, result);

      break;
    }
  }
  return ret;
}

static void
js_serialport_finalizer(JSRuntime* rt, JSValue val) {
  struct sp_port* port;

  if((port = JS_GetOpaque(val, js_serialport_class_id))) {
    sp_close(port);
    sp_free_port(port);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_serialport_class = {
    .class_name = "SerialPort",
    .finalizer = js_serialport_finalizer,
};

static JSValue
js_serial_getports(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  struct sp_port** ports;

  if(sp_list_ports(&ports) == SP_OK) {
    ret = JS_NewArray(ctx);
    for(int i = 0; ports[i]; i++) { JS_SetPropertyUint32(ctx, ret, i, JS_NewString(ctx, sp_get_port_name(ports[i]))); }

    sp_free_port_list(ports);
  }

  return ret;
}

static JSValue
js_serial_requestport(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  const char* port_name;
  struct sp_port* port;

  if((port_name = JS_ToCString(ctx, argv[0]))) {

    if(sp_get_port_by_name(port_name, &port) == SP_OK) {

      if(sp_open(port, SP_MODE_READ_WRITE) != SP_OK) {
        char* msg = sp_last_error_message();
        sp_free_port(port);
        ret = JS_ThrowInternalError(ctx, "failed opening port '%s': %s", port_name, msg);
        sp_free_error_message(msg);
      } else {
        ret = js_serialport_wrap(ctx, port);
      }
    } else {
      ret = JS_ThrowInternalError(ctx, "Serial port '%s' not found", port_name);
    }

    JS_FreeCString(ctx, port_name);
  }

  return ret;
}

static const JSCFunctionListEntry js_serialport_funcs[] = {
    JS_CFUNC_MAGIC_DEF("getInfo", 0, js_serialport_method, SERIALPORT_GETINFO),
    JS_CGETSET_MAGIC_DEF("fd", js_serialport_get, 0, SERIALPORT_FD),

};
static const JSCFunctionListEntry js_serialport_static[] = {

};

static const JSCFunctionListEntry js_serial_static[] = {
    JS_CFUNC_DEF("getPorts", 0, js_serial_getports),
    JS_CFUNC_DEF("requestPort", 1, js_serial_requestport),

};

int
js_serial_init(JSContext* ctx, JSModuleDef* m) {

  if(js_serialport_class_id == 0) {
    JS_NewClassID(&js_serialport_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_serialport_class_id, &js_serialport_class);

    serialport_ctor = JS_NewObject(ctx); // JS_NewCFunction2(ctx, js_serialport_constructor, "Serial", 1, JS_CFUNC_constructor, 0);
    serialport_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, serialport_proto, js_serialport_funcs, countof(js_serialport_funcs));
    JS_SetPropertyFunctionList(ctx, serialport_ctor, js_serialport_static, countof(js_serialport_static));

    JS_SetClassProto(ctx, js_serialport_class_id, serialport_proto);

    serial_ctor = JS_NewObject(ctx); // JS_NewCFunction2(ctx, js_serial_constructor, "Serial", 1, JS_CFUNC_constructor, 0);

    JS_SetPropertyFunctionList(ctx, serial_ctor, js_serial_static, countof(js_serial_static));
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "SerialPort", serialport_ctor);
    JS_SetModuleExport(ctx, m, "Serial", serial_ctor);

    const char* module_name = JS_AtomToCString(ctx, m->module_name);

    if(!strcmp(module_name, "cookie"))
      JS_SetModuleExport(ctx, m, "default", serial_ctor);

    JS_FreeCString(ctx, module_name);
  }

  return 0;
}

#ifdef JS_SERIAL_MODULE
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_serial
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  if(!(m = JS_NewCModule(ctx, module_name, &js_serial_init)))
    return m;
  JS_AddModuleExport(ctx, m, "Serial");
  JS_AddModuleExport(ctx, m, "SerialPort");

  /* if(!strcmp(module_name, "cookie"))
     JS_AddModuleExport(ctx, m, "default");*/

  return m;
}

/**
 * @}
 */
