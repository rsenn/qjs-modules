#include "defines.h"
#include "buffer-utils.h"
#include "utils.h"
#include <errno.h>
#include <string.h>
#include "libserialport/libserialport.h"

/**
 * \defgroup quickjs-serial quickjs-serial: libserial MIME types
 * @{
 */
VISIBLE JSClassID js_serialport_class_id = 0, js_serialerror_class_id = 0;
VISIBLE JSValue serialport_proto = {{0}, JS_TAG_UNDEFINED}, serialport_ctor = {{0}, JS_TAG_UNDEFINED}, serial_ctor = {{0}, JS_TAG_UNDEFINED}, serialerror_proto = {{0}, JS_TAG_UNDEFINED},
                serialerror_ctor = {{0}, JS_TAG_UNDEFINED};

static JSValue
js_serialerror_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  JSAtom prop;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    return JS_EXCEPTION;

  if(!JS_IsObject(proto))
    proto = JS_DupValue(ctx, serialerror_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_serialerror_class_id);
  JS_FreeValue(ctx, proto);

  if(argc > 0) {
    prop = JS_NewAtom(ctx, "message");
    JS_DefinePropertyValue(ctx, obj, prop, JS_DupValue(ctx, argv[0]), JS_PROP_C_W_E);
    JS_FreeAtom(ctx, prop);
  }

  if(argc > 1) {
    prop = JS_NewAtom(ctx, "type");
    JS_DefinePropertyValue(ctx, obj, prop, JS_DupValue(ctx, argv[1]), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, prop);
  }

  JSValue stack = js_error_stack(ctx);
  prop = JS_NewAtom(ctx, "stack");
  JS_DefinePropertyValue(ctx, obj, prop, stack, JS_PROP_CONFIGURABLE);
  JS_FreeAtom(ctx, prop);

  return obj;
}

static JSValue
js_serialerror_new(JSContext* ctx, struct sp_port* port, enum sp_return result) {
  JSValue ret, obj;
  char msg[1024];
  JSValue argv[2];

  obj = JS_NewObjectProtoClass(ctx, serialerror_proto, js_serialerror_class_id);
  if(JS_IsException(obj))
    return JS_EXCEPTION;

  switch(result) {
    case SP_ERR_ARG: {
      snprintf(msg, sizeof(msg), "%s: Invalid arguments were passed to the function", sp_get_port_name(port));
      break;
    }
    case SP_ERR_FAIL: {
      char* err;
      err = sp_last_error_message();
      snprintf(msg, sizeof(msg), "%s: %s", sp_get_port_name(port), err);
      sp_free_error_message(err);
      break;
    }
    case SP_ERR_MEM: {
      snprintf(msg, sizeof(msg), "%s: A memory allocation failed while executing the operation", sp_get_port_name(port));
      break;
    }
    case SP_ERR_SUPP: {
      snprintf(msg, sizeof(msg), "%s: The requested operation is not supported by this system or device", sp_get_port_name(port));
      break;
    }
    default: {
      break;
    }
  }

  argv[0] = JS_NewString(ctx, msg);
  argv[1] = JS_NewInt32(ctx, result);
  obj = js_serialerror_constructor(ctx, serialerror_ctor, 2, argv);

  JS_FreeValue(ctx, argv[0]);
  JS_FreeValue(ctx, argv[1]);

  return obj;
}

enum {
  SERIALPORT_OPEN = 0,
  SERIALPORT_CLOSE,
  SERIALPORT_GETINFO,
  SERIALPORT_GETSIGNALS,
  SERIALPORT_SETSIGNALS,
  SERIALPORT_FLUSH,
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
js_serialport_error(JSContext* ctx, struct sp_port* port, enum sp_return result) {
  switch(result) {
    case SP_ERR_ARG: JS_ThrowInternalError(ctx, "libserialport argument error for port '%s'", sp_get_port_name(port)); break;
    case SP_ERR_FAIL: JS_ThrowInternalError(ctx, "libserialport OS error for port '%s': %s", sp_get_port_name(port), sp_last_error_message()); break;
    case SP_ERR_SUPP: JS_ThrowInternalError(ctx, "libserialport operation not supported for port '%s'", sp_get_port_name(port)); break;
    case SP_ERR_MEM: JS_ThrowInternalError(ctx, "libserialport out of memory for port '%s'", sp_get_port_name(port)); break;
    default: return JS_UNDEFINED;
  }
  return JS_GetException(ctx);
}

enum {
  SERIALPORT_READ = 0,
  SERIALPORT_WRITE,
  SERIALPORT_DRAIN,
};

static JSValue
js_serialport_ioready(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue data[]) {
  struct sp_port* port;
  JSValue args[2];
  int64_t fd = -1, ret;
  InputBuffer input;

  if(!(port = js_serialport_data(ctx, data[0])))
    return JS_EXCEPTION;

  if(sp_get_port_handle(port, &fd) != SP_OK)
    return JS_ThrowInternalError(ctx, "could not get serial port file descriptor: %s", sp_last_error_message());

  args[0] = JS_NewInt64(ctx, fd);
  args[1] = JS_NULL;
  JS_Call(ctx, data[3], JS_UNDEFINED, 2, args);
  JS_FreeValue(ctx, args[0]);

  if(magic < SERIALPORT_DRAIN)
    input = js_input_args(ctx, 3, &data[4]);

  switch(magic) {
    case SERIALPORT_READ: ret = sp_nonblocking_read(port, input.data, input.size); break;
    case SERIALPORT_WRITE: ret = sp_nonblocking_write(port, input.data, input.size); break;
    case SERIALPORT_DRAIN: ret = sp_drain(port); break;
  }

  if(magic < SERIALPORT_DRAIN)
    input_buffer_free(&input, ctx);

  if(ret < 0) {
    if(ret == SP_ERR_FAIL) {
      JS_ThrowInternalError(ctx, "could %s serial port '%s': %s", ((const char* [3]){"read", "write", "drain"})[magic - SERIALPORT_READ], sp_get_port_name(port), sp_last_error_message());
      args[0] = JS_GetException(ctx);
    } else
      args[0] = js_serialport_error(ctx, port, ret);

    JS_Call(ctx, data[2], JS_UNDEFINED, 1, args);
    JS_FreeValue(ctx, args[0]);

  } else {
    args[0] = magic == SERIALPORT_DRAIN ? JS_UNDEFINED : JS_NewInt64(ctx, ret);
    JS_Call(ctx, data[1], JS_UNDEFINED, 1, args);
    JS_FreeValue(ctx, args[0]);
  }

  return JS_UNDEFINED;
}

static JSValue
js_serialport_io(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  struct sp_port* port;
  JSValue set_handler, data[7], args[2], ret, promise, resolving_funcs[2];
  int data_len = 0;
  int64_t fd = -1;

  if(!(port = js_serialport_data(ctx, this_val)))
    return JS_EXCEPTION;

  set_handler = js_iohandler_fn(ctx, magic != SERIALPORT_READ);
  if(JS_IsException(set_handler))
    return JS_EXCEPTION;

  if(sp_get_port_handle(port, &fd) != SP_OK)
    return JS_ThrowInternalError(ctx, "could not get serial port file descriptor: %s", sp_last_error_message());

  promise = JS_NewPromiseCapability(ctx, resolving_funcs);
  if(JS_IsException(promise)) {
    JS_FreeValue(ctx, set_handler);
    return JS_EXCEPTION;
  }

  data[0] = this_val;
  data[1] = resolving_funcs[0];
  data[2] = resolving_funcs[1];
  data[3] = set_handler;
  data_len = 4;

  for(int i = 0; i < argc; i++) {
    data[data_len] = i < argc ? argv[i] : JS_UNDEFINED;

    if(++data_len == countof(data))
      break;
  }

  args[0] = JS_NewInt64(ctx, fd);
  args[1] = JS_NewCFunctionData(ctx, js_serialport_ioready, 0, magic, data_len, data);

  JS_FreeValue(ctx, resolving_funcs[0]);
  JS_FreeValue(ctx, resolving_funcs[1]);

  ret = JS_Call(ctx, set_handler, JS_UNDEFINED, countof(args), args);

  JS_FreeValue(ctx, ret);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, set_handler);

  return promise;
}

static JSValue
js_serialport_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  struct sp_port* port;
  JSValue ret = JS_UNDEFINED;

  if(!(port = js_serialport_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SERIALPORT_OPEN: {
      int32_t flags = SP_MODE_READ_WRITE, baud_rate = -1, data_bits = -1, stop_bits = -1;
      const char *parity = 0, *flow_control = 0;
      enum sp_return result;

      for(int i = 0; i < argc; i++) {
        if(JS_IsNumber(argv[i])) {
          JS_ToInt32(ctx, &flags, argv[i]);
        } else if(JS_IsObject(argv[i])) {
          JSValue br = JS_GetPropertyStr(ctx, argv[i], "baudRate"), db = JS_GetPropertyStr(ctx, argv[i], "dataBits"), sb = JS_GetPropertyStr(ctx, argv[i], "stopBits"),
                  pr = JS_GetPropertyStr(ctx, argv[i], "parity"), fc = JS_GetPropertyStr(ctx, argv[i], "flowControl");

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

      if((result = sp_open(port, flags)) != SP_OK) {
        if(result == SP_ERR_FAIL) {
          char* msg = sp_last_error_message();
          sp_free_port(port);
          ret = JS_ThrowInternalError(ctx, "failed opening port '%s': %s", sp_get_port_name(port), msg);
          sp_free_error_message(msg);
        } else {
          ret = JS_Throw(ctx, js_serialport_error(ctx, port, result));
        }
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

      switch(sp_get_port_transport(port)) {
        case SP_TRANSPORT_USB: {
          JS_SetPropertyStr(ctx, ret, "usbManufacturer", JS_NewString(ctx, sp_get_port_usb_manufacturer(port)));
          JS_SetPropertyStr(ctx, ret, "usbProduct", JS_NewString(ctx, sp_get_port_usb_product(port)));
          JS_SetPropertyStr(ctx, ret, "usbSerial", JS_NewString(ctx, sp_get_port_usb_serial(port)));
          break;
        }
        case SP_TRANSPORT_BLUETOOTH: {
          JS_SetPropertyStr(ctx, ret, "bluetoothAddress", JS_NewString(ctx, sp_get_port_bluetooth_address(port)));
          break;
        }
        case SP_TRANSPORT_NATIVE: break;
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
    case SERIALPORT_FLUSH: {
      enum sp_return result;
      int32_t which = SP_BUF_BOTH;
      if(argc > 0)
        JS_ToInt32(ctx, &which, argv[0]);

      if((result = sp_flush(port, which)) != SP_OK)
        JS_Throw(ctx, js_serialport_error(ctx, port, result));
      break;
    }
  }
  return ret;
}

enum {
  SERIALPORT_FD,
  SERIALPORT_INPUTWAITING,
  SERIALPORT_OUTPUTWAITING,
  SERIALPORT_NAME,
  SERIALPORT_DESCRIPTION,
  SERIALPORT_TRANSPORT,
};

static JSValue
js_serialport_get(JSContext* ctx, JSValueConst this_val, int magic) {
  struct sp_port* port;
  JSValue ret = JS_UNDEFINED;

  if(!(port = js_serialport_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SERIALPORT_FD: {
      int64_t result = 0;
      if(sp_get_port_handle(port, &result) == SP_OK)
        ret = result == (int64_t)(uint64_t)(uint32_t)-1 ? JS_NULL : JS_NewInt64(ctx, result);

      break;
    }
    case SERIALPORT_NAME: {
      ret = JS_NewString(ctx, sp_get_port_name(port));
      break;
    }
    case SERIALPORT_DESCRIPTION: {
      ret = JS_NewString(ctx, sp_get_port_description(port));
      break;
    }
    case SERIALPORT_TRANSPORT: {
      ret = JS_NewString(ctx, ((const char* [3]){"native", "usb", "bluetooth"})[sp_get_port_transport(port) - SP_TRANSPORT_NATIVE]);
      break;
    }
    case SERIALPORT_INPUTWAITING: {
      int64_t result;

      if((result = sp_input_waiting(port)) < 0)
        ret = JS_ThrowInternalError(ctx, "Failed getting input bytes on port '%s'", sp_get_port_name(port));
      else
        ret = JS_NewInt64(ctx, result);

      break;
    }
    case SERIALPORT_OUTPUTWAITING: {
      int64_t result;

      if((result = sp_output_waiting(port)) < 0)
        ret = JS_ThrowInternalError(ctx, "Failed getting output bytes on port '%s'", sp_get_port_name(port));
      else
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
    for(int i = 0; ports[i]; i++) {
      JS_SetPropertyUint32(ctx, ret, i, JS_NewString(ctx, sp_get_port_name(ports[i])));
    }

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

static JSClassDef js_serialerror_class = {
    .class_name = "SerialError",
};

static const JSCFunctionListEntry js_serialport_funcs[] = {
    JS_CFUNC_MAGIC_DEF("open", 0, js_serialport_method, SERIALPORT_OPEN),
    JS_CFUNC_MAGIC_DEF("close", 0, js_serialport_method, SERIALPORT_CLOSE),
    JS_CFUNC_MAGIC_DEF("getInfo", 0, js_serialport_method, SERIALPORT_GETINFO),
    JS_CFUNC_MAGIC_DEF("getSignals", 0, js_serialport_method, SERIALPORT_GETSIGNALS),
    JS_CFUNC_MAGIC_DEF("setSignals", 1, js_serialport_method, SERIALPORT_SETSIGNALS),
    JS_CFUNC_MAGIC_DEF("read", 1, js_serialport_io, SERIALPORT_READ),
    JS_CFUNC_MAGIC_DEF("write", 1, js_serialport_io, SERIALPORT_WRITE),
    JS_CFUNC_MAGIC_DEF("drain", 0, js_serialport_io, SERIALPORT_DRAIN),
    JS_CFUNC_MAGIC_DEF("flush", 0, js_serialport_method, SERIALPORT_FLUSH),
    JS_CGETSET_MAGIC_DEF("fd", js_serialport_get, 0, SERIALPORT_FD),
    JS_CGETSET_MAGIC_DEF("name", js_serialport_get, 0, SERIALPORT_NAME),
    JS_CGETSET_MAGIC_DEF("transport", js_serialport_get, 0, SERIALPORT_TRANSPORT),
    JS_CGETSET_MAGIC_DEF("description", js_serialport_get, 0, SERIALPORT_DESCRIPTION),
    JS_CGETSET_MAGIC_DEF("inputWaiting", js_serialport_get, 0, SERIALPORT_INPUTWAITING),
    JS_CGETSET_MAGIC_DEF("outputWaiting", js_serialport_get, 0, SERIALPORT_OUTPUTWAITING),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SerialPort", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_serialport_static[] = {
    JS_PROP_INT32_DEF("MODE_READ", SP_MODE_READ, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MODE_WRITE", SP_MODE_WRITE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MODE_READ_WRITE", SP_MODE_READ_WRITE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("BUF_INPUT", SP_BUF_INPUT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("BUF_OUTPUT", SP_BUF_OUTPUT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("BUF_BOTH", SP_BUF_BOTH, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ERR_ARG", SP_ERR_ARG, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ERR_FAIL", SP_ERR_FAIL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ERR_MEM", SP_ERR_MEM, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ERR_SUPP", SP_ERR_SUPP, JS_PROP_ENUMERABLE),
};

static const JSCFunctionListEntry js_serial_static[] = {
    JS_CFUNC_DEF("getPorts", 0, js_serial_getports),
    JS_CFUNC_DEF("requestPort", 1, js_serial_requestport),
};

static const JSCFunctionListEntry js_serialerror_funcs[] = {
    JS_PROP_STRING_DEF("name", "SerialError", JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("type", 0, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SerialError", JS_PROP_CONFIGURABLE),
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

    JSValue error = JS_NewError(ctx);
    JSValue error_proto = JS_GetPrototype(ctx, error);
    JS_FreeValue(ctx, error);

    JS_NewClassID(&js_serialerror_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_serialerror_class_id, &js_serialerror_class);

    serialerror_ctor = JS_NewCFunction2(ctx, js_serialerror_constructor, "SerialError", 1, JS_CFUNC_constructor, 0);
    serialerror_proto = JS_NewObjectProto(ctx, error_proto);
    JS_FreeValue(ctx, error_proto);

    JS_SetPropertyFunctionList(ctx, serialerror_proto, js_serialerror_funcs, countof(js_serialerror_funcs));

    JS_SetClassProto(ctx, js_serialerror_class_id, serialerror_proto);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "SerialPort", serialport_ctor);
    JS_SetModuleExport(ctx, m, "Serial", serial_ctor);
    JS_SetModuleExport(ctx, m, "SerialError", serialerror_ctor);

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

  if((m = JS_NewCModule(ctx, module_name, js_serial_init))) {
    JS_AddModuleExport(ctx, m, "Serial");
    JS_AddModuleExport(ctx, m, "SerialPort");
    JS_AddModuleExport(ctx, m, "SerialError");

    /* if(!strcmp(module_name, "cookie"))
       JS_AddModuleExport(ctx, m, "default");*/
  }

  return m;
}

/**
 * @}
 */
