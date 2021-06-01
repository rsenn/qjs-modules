#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "quickjs-internal.h"
#include "quickjs-location.h"
#include "quickjs-libc.h"
#include "utils.h"
#include "base64.h"
#include <time.h>
#include <sys/utsname.h>

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

static void
js_string_free_func(JSRuntime* rt, void* opaque, void* ptr) {
  JSValue value = js_cstring_value(opaque);

  JS_FreeValueRT(rt, value);
}

static void
js_arraybuffer_free_func(JSRuntime* rt, void* opaque, void* ptr) {
  JSValue value = JS_MKPTR(JS_TAG_OBJECT, ptr);

  JS_FreeValueRT(rt, value);
}

typedef struct OffsetLength {
  int64_t offset;
  int64_t length;
} OffsetLength;

static OffsetLength
get_offset_length(JSContext* ctx, int64_t len, int argc, JSValueConst argv[]) {
  int64_t offset = 0, length = len;
  if(argc >= 2)
    JS_ToInt64(ctx, &offset, argv[1]);
  if(argc >= 3)
    JS_ToInt64(ctx, &length, argv[2]);

  if(offset >= 0)
    offset = min(offset, len);
  else
    offset = ((offset % len) + offset) % len;

  if(length >= 0)
    length = min(length, len - offset);
  else
    length = len - offset;

  return (OffsetLength){.offset = offset, .length = length};
}

static JSValue
js_misc_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  JSValue arraybuffer_ctor = js_global_get(ctx, "ArrayBuffer");

  if(js_value_isclass(ctx, argv[0], JS_CLASS_ARRAY_BUFFER) || js_is_arraybuffer(ctx, argv[0]) ||
     JS_IsInstanceOf(ctx, argv[0], arraybuffer_ctor)) {
    uint8_t* data;
    size_t len;

    if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
      OffsetLength ol;

      ol = get_offset_length(ctx, len, argc, argv);

      ret = JS_NewStringLen(ctx, (const char*)data + ol.offset, ol.length);
    }
  }

  JS_FreeValue(ctx, arraybuffer_ctor);

  return ret;
}

static JSValue
js_misc_topointer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  void* ptr = 0;
  char buf[128];

  if(js_value_isclass(ctx, argv[0], JS_CLASS_ARRAY_BUFFER) || js_is_arraybuffer(ctx, argv[0])) {
    size_t len;
    ptr = JS_GetArrayBuffer(ctx, &len, argv[0]);
  } else if(JS_IsString(argv[0])) {
    ptr = js_cstring_ptr(argv[0]);
  }

  if(ptr) {
    snprintf(buf, sizeof(buf), "%p", ptr);
    ret = JS_NewString(ctx, buf);
  } else {
    ret = JS_NULL;
  }

  return ret;
}

static JSValue
js_misc_toarraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  if(JS_IsString(argv[0])) {
    JSValueConst value = argv[0]; // JS_DupValue(ctx, argv[0]);
    size_t len;
    const char* str;
    if((str = JS_ToCStringLen(ctx, &len, value))) {
      OffsetLength ol;

      ol = get_offset_length(ctx, len, argc, argv);

      ret = JS_NewArrayBuffer(ctx, (uint8_t*)str + ol.offset, ol.length, js_string_free_func, (void*)str, FALSE);
    }
  }

  return ret;
}

static JSValue
js_misc_duparraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  JSValue arraybuffer_ctor = js_global_get(ctx, "ArrayBuffer");

  if(JS_IsInstanceOf(ctx, argv[0], arraybuffer_ctor)) {
    JSValue value = JS_DupValue(ctx, argv[0]);
    uint8_t* data;
    size_t len;

    if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
      OffsetLength ol = get_offset_length(ctx, len, argc, argv);
      JSObject* obj = JS_VALUE_GET_OBJ(value);

      ret = JS_NewArrayBuffer(ctx, data + ol.offset, ol.length, js_arraybuffer_free_func, (void*)obj, FALSE);
    }
  }

  JS_FreeValue(ctx, arraybuffer_ctor);

  return ret;
}

static JSValue
js_misc_getperformancecounter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  return JS_NewFloat64(ctx, (double)ts.tv_sec * 1000 + ((double)ts.tv_nsec / 1e06));
}

static JSValue
js_misc_hrtime(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct timespec ts;
  JSValue ret;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  if(argc >= 1 && JS_IsArray(ctx, argv[0])) {
    int64_t sec, nsec;
    JSValue psec, pnsec;

    psec = JS_GetPropertyUint32(ctx, argv[0], 0);
    pnsec = JS_GetPropertyUint32(ctx, argv[0], 1);

    JS_ToInt64(ctx, &sec, psec);
    JS_ToInt64(ctx, &nsec, pnsec);
    JS_FreeValue(ctx, psec);
    JS_FreeValue(ctx, pnsec);

    if(nsec > ts.tv_nsec) {
      ts.tv_sec -= 1;
      ts.tv_nsec += 1000000000;
    }

    ts.tv_sec -= sec;
    ts.tv_nsec -= nsec;
  }

  ret = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, ret, 0, JS_NewInt64(ctx, ts.tv_sec));
  JS_SetPropertyUint32(ctx, ret, 1, JS_NewInt64(ctx, ts.tv_nsec));

  return ret;
}

static JSValue
js_misc_uname(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct utsname un;
  JSValue ret = JS_UNDEFINED;

  if(uname(&un) != -1) {
    ret = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, ret, "sysname", JS_NewString(ctx, un.sysname));
    JS_SetPropertyStr(ctx, ret, "nodename", JS_NewString(ctx, un.nodename));
    JS_SetPropertyStr(ctx, ret, "release", JS_NewString(ctx, un.release));
    JS_SetPropertyStr(ctx, ret, "version", JS_NewString(ctx, un.version));
    JS_SetPropertyStr(ctx, ret, "machine", JS_NewString(ctx, un.machine));
  }

  return ret;
}

static JSValue
js_misc_btoa(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  InputBuffer input = js_input_buffer(ctx, argv[0]);
  size_t enclen = b64_get_encoded_buffer_size(input.size);
  uint8_t* encbuf = js_malloc(ctx, enclen);

  b64_encode(input.data, input.size, encbuf);

  ret = JS_NewStringLen(ctx, (const char*)encbuf, enclen);
  js_free(ctx, encbuf);
  return ret;
}

static JSValue
js_misc_atob(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  InputBuffer input = js_input_buffer(ctx, argv[0]);
  size_t declen = b64_get_decoded_buffer_size(input.size);
  uint8_t* decbuf = js_malloc(ctx, declen);

  b64_decode(input.data, input.size, decbuf);

  ret = JS_NewArrayBufferCopy(ctx, (const char*)decbuf, declen);
  js_free(ctx, decbuf);
  return ret;
}

static JSValue
js_misc_compile_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  const char* filename = JS_ToCString(ctx, argv[0]);
  BOOL module = FALSE;
  uint8_t* buf;
  size_t buf_len;

  if(argc >= 2)
    module = JS_ToBool(ctx, argv[1]);
  else if(str_end(filename, ".jsm"))
    module = TRUE;

  /* load JS from file to buffer */
  if((buf = js_load_file(ctx, &buf_len, filename))) {

    int eval_flags = JS_EVAL_FLAG_COMPILE_ONLY | (module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL);

    ret = JS_Eval(ctx, (const char*)buf, buf_len, filename, eval_flags);
  }

  return ret;
}
static void
js_misc_free_bytecode(JSRuntime* rt, void* opaque, void* ptr) {
  js_free_rt(rt, ptr);
}

static JSValue
js_misc_write_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  size_t size;
  uint8_t* bytecode;

  if((bytecode = JS_WriteObject(ctx, &size, argv[0], JS_WRITE_OBJ_BYTECODE))) {
    ret = JS_NewArrayBuffer(ctx, bytecode, size, js_misc_free_bytecode, 0, FALSE);
  }
  return ret;
}

static JSValue
js_misc_read_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer input = js_input_buffer(ctx, argv[0]);

  return JS_ReadObject(ctx, input.data, input.size, JS_READ_OBJ_BYTECODE);
}

static JSValue
js_misc_valuetype(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  const char* typestr = js_value_typestr(ctx, argv[0]);

  return JS_NewString(ctx, typestr);
}

static JSValue
js_misc_evalbinary(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  BOOL load_only = FALSE;
  JSValueConst obj;

  if(argc >= 2)
    load_only = JS_ToBool(ctx, argv[1]);

  if(JS_VALUE_GET_TAG(argv[0]) != JS_TAG_MODULE && JS_VALUE_GET_TAG(argv[0]) != JS_TAG_FUNCTION_BYTECODE)
    obj = js_misc_read_object(ctx, this_val, argc, argv);
  else
    obj = argv[0];

  if(JS_IsException(obj))
    return obj;

  if(load_only) {
    if(JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE)
      js_module_set_import_meta(ctx, obj, FALSE, FALSE);
  } else {
    if(JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE) {
      if(JS_ResolveModule(ctx, obj) < 0) {
        JSModuleDef* m = JS_VALUE_GET_PTR(obj);
        const char* name = JS_AtomToCString(ctx, m->module_name);
        ret = JS_ThrowInternalError(ctx, "Failed resolving module '%s'", name);
        JS_FreeCString(ctx, name);
        JS_FreeValue(ctx, obj);
        return ret;
      }
      js_module_set_import_meta(ctx, obj, FALSE, TRUE);
    }
    ret = JS_EvalFunction(ctx, obj);
  }

  return ret;
}

static const JSCFunctionListEntry js_misc_funcs[] = {
    JS_CFUNC_DEF("toString", 1, js_misc_tostring),
    JS_CFUNC_DEF("toPointer", 1, js_misc_topointer),
    JS_CFUNC_DEF("toArrayBuffer", 1, js_misc_toarraybuffer),
    JS_CFUNC_DEF("dupArrayBuffer", 1, js_misc_duparraybuffer),
    JS_CFUNC_DEF("getPerformanceCounter", 0, js_misc_getperformancecounter),
    JS_CFUNC_DEF("hrtime", 0, js_misc_hrtime),
    JS_CFUNC_DEF("uname", 0, js_misc_uname),
    JS_CFUNC_DEF("btoa", 1, js_misc_btoa),
    JS_CFUNC_DEF("atob", 1, js_misc_atob),
    JS_CFUNC_DEF("compileFile", 1, js_misc_compile_file),
    JS_CFUNC_DEF("writeObject", 1, js_misc_write_object),
    JS_CFUNC_DEF("readObject", 1, js_misc_read_object),
    JS_CFUNC_DEF("valueType", 1, js_misc_valuetype),
    JS_CFUNC_DEF("evalBinary", 1, js_misc_evalbinary),
};

static int
js_misc_init(JSContext* ctx, JSModuleDef* m) {

  js_location_init(ctx, m);

  if(m) {
    // JS_SetModuleExportList(ctx, m, location_ctor);
    JS_SetModuleExportList(ctx, m, js_misc_funcs, countof(js_misc_funcs));
  }

  return 0;
}

#if defined(JS_SHARED_LIBRARY) && defined(JS_MISC_MODULE)
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_misc
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_misc_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "Location");
  JS_AddModuleExportList(ctx, m, js_misc_funcs, countof(js_misc_funcs));
  return m;
}
