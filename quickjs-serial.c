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
  SERIALPORT_OPEN = 0,
  SERIALPORT_CLOSE,
  SERIALPORT_GETINFO,
  SERIALPORT_GETSIGNALS,
  SERIALPORT_SETSIGNALS,
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
    case SERIALPORT_OPEN: {
      int32_t flags = SP_MODE_READ_WRITE, baud_rate = -1, data_bits = -1, stop_bits = -1;
      const char *parity = 0, *flow_control = 0;

      for(int i = 0; i < argc; i++) {
        if(JS_IsNumber(argv[i])) {
          JS_ToInt32(ctx, &flags, argv[i]);
        } else if(JS_IsObject(argv[i])) {
          JSValue br = JS_GetPropertyStr(ctx, argv[i], "baudRate"), db = JS_GetPropertyStr(ctx, argv[i], "dataBits"),
                  sb = JS_GetPropertyStr(ctx, argv[i], "stopBits"), pr = JS_GetPropertyStr(ctx, argv[i], "parity"),
                  fc = JS_GetPropertyStr(ctx, argv[i], "flowControl");

          if(JS_IsNumber(br))
            JS_ToInt32(ctx, &baud_rate, br);
          if(JS_IsNumber(db))
            JS_ToInt32(ctx, &data_bits, db);
          if(JS_IsNumber(sb))
            JS_ToInt32(ctx, &stop_bits, sb);
          if(JS_IsString(pr))
            parity = JS_ToCString(ctx, pr);
          if(JS_IsString(fc))
            flow_control = JS_ToCString(ctx, fc);

          JS_FreeValue(ctx, br);
          JS_FreeValue(ctx, db);
          JS_FreeValue(ctx, sb);
          JS_FreeValue(ctx, pr);
          JS_FreeValue(ctx, fc);
        }
      }

      if(sp_open(port, flags) != SP_OK) {
        char* msg = sp_last_error_message();
        sp_free_port(port);
        ret = JS_ThrowInternalError(ctx, "failed opening port '%s': %s", sp_get_port_name(port), msg);
        sp_free_error_message(msg);
      } else {
        if(baud_rate != -1) {
          if(sp_set_baudrate(port, baud_rate) != SP_OK)
            ret = JS_ThrowInternalError(ctx, "Failed setting baud rate on port '%s' to %" PRId32, sp_get_port_name(port), baud_rate);
        }
        if(parity) {
          if(!strcmp(parity, "none")) {
            sp_set_parity(port, SP_PARITY_NONE);
          } else if(!strcmp(parity, "even")) {
            sp_set_parity(port, SP_PARITY_EVEN);
          } else if(!strcmp(parity, "odd")) {
            sp_set_parity(port, SP_PARITY_ODD);
          }
        }
        if(flow_control) {
          if(!strcmp(flow_control, "none")) {
            sp_set_flowcontrol(port, SP_FLOWCONTROL_NONE);
          } else if(!strcmp(flow_control, "hardware")) {
            sp_set_flowcontrol(port, SP_FLOWCONTROL_RTSCTS);
          } else if(!strcmp(flow_control, "software")) {
            sp_set_flowcontrol(port, SP_FLOWCONTROL_XONXOFF);
          }
        }
      }
      if(parity)
        JS_FreeCString(ctx, parity);
      if(flow_control)
        JS_FreeCString(ctx, flow_control);

      break;
    }
    case SERIALPORT_CLOSE: {
      sp_close(port);
      break;
    }
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
    case SERIALPORT_GETSIGNALS: {
      enum sp_signal signals;

      if(sp_get_signals(port, &signals) == SP_OK) {
        if(JS_IsUndefined(ret))
          ret = JS_NewObject(ctx);

        JS_SetPropertyStr(ctx, ret, "clearToSend", JS_NewBool(ctx, signals & SP_SIG_CTS));
        JS_SetPropertyStr(ctx, ret, "dataCarrierDetect", JS_NewBool(ctx, signals & SP_SIG_DCD));
        JS_SetPropertyStr(ctx, ret, "dataSetReady", JS_NewBool(ctx, signals & SP_SIG_DSR));
        JS_SetPropertyStr(ctx, ret, "ringIndicator", JS_NewBool(ctx, signals & SP_SIG_RI));
      }

      break;
    }
    case SERIALPORT_SETSIGNALS: {
      JSValue dtr, rts, brk;

      dtr = JS_GetPropertyStr(ctx, argv[0], "dataTerminalReady");
      rts = JS_GetPropertyStr(ctx, argv[0], "requestToSend");
      brk = JS_GetPropertyStr(ctx, argv[0], "break");

      if(JS_IsBool(dtr))
        sp_set_dtr(port, JS_ToBool(ctx, dtr) ? SP_DTR_ON : SP_DTR_OFF);

      if(JS_IsBool(rts))
        sp_set_rts(port, JS_ToBool(ctx, rts) ? SP_RTS_ON : SP_RTS_OFF);

      if(JS_IsBool(brk))
        JS_ToBool(ctx, rts) ? sp_start_break(port) : sp_end_break(port);

      JS_FreeValue(ctx, dtr);
      JS_FreeValue(ctx, rts);
      JS_FreeValue(ctx, brk);
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
        ret = result == (int64_t)(uint64_t)(uint32_t)-1 ? JS_NULL : JS_NewInt64(ctx, result);

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

      if(JS_IsUndefined(ret))
        ret = js_serialport_wrap(ctx, port);
    } else {
      ret = JS_ThrowInternalError(ctx, "Serial port '%s' not found", port_name);
    }

    JS_FreeCString(ctx, port_name);
  }

  return ret;
}

static const JSCFunctionListEntry js_serialport_funcs[] = {
    JS_CFUNC_MAGIC_DEF("open", 0, js_serialport_method, SERIALPORT_OPEN),
    JS_CFUNC_MAGIC_DEF("close", 0, js_serialport_method, SERIALPORT_CLOSE),
    JS_CFUNC_MAGIC_DEF("getInfo", 0, js_serialport_method, SERIALPORT_GETINFO),
    JS_CFUNC_MAGIC_DEF("getSignals", 0, js_serialport_method, SERIALPORT_GETSIGNALS),
    JS_CFUNC_MAGIC_DEF("setSignals", 1, js_serialport_method, SERIALPORT_SETSIGNALS),
    JS_CGETSET_MAGIC_DEF("fd", js_serialport_get, 0, SERIALPORT_FD),

};
static const JSCFunctionListEntry js_serialport_static[] = {
    JS_PROP_INT32_DEF("MODE_READ", SP_MODE_READ, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MODE_WRITE", SP_MODE_WRITE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MODE_READ_WRITE", SP_MODE_READ_WRITE, JS_PROP_ENUMERABLE),
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
