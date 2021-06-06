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
#include <threads.h>

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

static void
js_bytecode_free_func(JSRuntime* rt, void* opaque, void* ptr) {
  js_free_rt(rt, ptr);
}

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

  if(argc >= 2 && JS_IsNumber(argv[1]))
    JS_ToInt64(ctx, &offset, argv[1]);
  if(argc >= 3 && JS_IsNumber(argv[2]))
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
js_misc_resizearraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  if(js_is_arraybuffer(ctx, argv[0])) {
    JSObject* obj = JS_VALUE_GET_OBJ(argv[0]);
    JSArrayBuffer* arraybuf = obj->u.array_buffer;
    uint64_t newlen;
    JS_ToIndex(ctx, &newlen, argv[1]);

    if(arraybuf->shared)
      ret = JS_ThrowTypeError(ctx, "ArrayBuffer must not be shared");
    else if(arraybuf->shared)
      ret = JS_ThrowTypeError(ctx, "ArrayBuffer must have opaque == 0");
    else {
      arraybuf->data = js_realloc(ctx, arraybuf->data, newlen);
      arraybuf->byte_length = newlen;

      ret = JS_MKPTR(JS_TAG_OBJECT, obj);
    }
  } else {
    ret = JS_ThrowTypeError(ctx, "Expecting ArrayBuffer");
  }

  return ret;
}

static JSValue
js_misc_concatarraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  int i;
  size_t total_len = 0, pos = 0;
  uint8_t* buf;

  for(i = 0; i < argc; i++) {
    if(!js_is_arraybuffer(ctx, argv[0]))
      return JS_ThrowTypeError(ctx, "argument %d is not ArrayBuffer", i + 1);
    total_len += js_arraybuffer_length(ctx, argv[i]);
  }

  buf = js_malloc(ctx, total_len);

  for(i = 0; i < argc; i++) {
    size_t len;
    uint8_t* ptr;
    ptr = JS_GetArrayBuffer(ctx, &len, argv[i]);

    memcpy(&buf[pos], ptr, len);
    pos += len;
  }

  return JS_NewArrayBuffer(ctx, buf, total_len, js_bytecode_free_func, 0, FALSE);
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

  ret = JS_NewArrayBufferCopy(ctx, (const uint8_t*)decbuf, declen);
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
  int eval_flags = JS_EVAL_FLAG_COMPILE_ONLY;

  if(argc >= 2)
    module = JS_ToBool(ctx, argv[1]);
  else if(str_end(filename, ".jsm"))
    module = TRUE;

  /* load JS from file to buffer */
  if((buf = js_load_file(ctx, &buf_len, filename))) {

    if(!module && JS_DetectModule((const char*)buf, buf_len))
      module = TRUE;

    eval_flags |= (module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL);

    ret = JS_Eval(ctx, (const char*)buf, buf_len, filename, eval_flags);
  }

  return ret;
}

static JSValue
js_misc_write_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  size_t size;
  uint8_t* bytecode;

  if((bytecode = JS_WriteObject(ctx, &size, argv[0], JS_WRITE_OBJ_BYTECODE))) {
    ret = JS_NewArrayBuffer(ctx, bytecode, size, js_bytecode_free_func, 0, FALSE);
  }
  return ret;
}

static JSValue
js_misc_read_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer input = js_input_buffer(ctx, argv[0]);

  return JS_ReadObject(ctx, input.data, input.size, JS_READ_OBJ_BYTECODE);
}
enum { VALUE_TYPE = 0, VALUE_TAG, VALUE_PTR };

static JSValue
js_misc_valuetype(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case VALUE_TYPE: {
      const char* typestr = js_value_typestr(ctx, argv[0]);
      ret = JS_NewString(ctx, typestr);
      break;
    }
    case VALUE_TAG: {
      ret = JS_NewInt32(ctx, JS_VALUE_GET_TAG(argv[0]));
      break;
    }
    case VALUE_PTR: {
      void* ptr = JS_VALUE_GET_PTR(argv[0]);
      char buf[128];

      snprintf(buf, sizeof(buf), "%p", ptr);
      ret = JS_NewString(ctx, buf);
      break;
    }
  }
  return ret;
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

static JSValue
js_misc_opcode_array(JSContext* ctx, const JSOpCode* opcode) {
  JSValue ret = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, ret, 0, JS_NewUint32(ctx, opcode->size));
  JS_SetPropertyUint32(ctx, ret, 1, JS_NewUint32(ctx, opcode->n_pop));
  JS_SetPropertyUint32(ctx, ret, 2, JS_NewUint32(ctx, opcode->n_push));
  JS_SetPropertyUint32(ctx, ret, 3, JS_NewUint32(ctx, opcode->fmt));
  JS_SetPropertyUint32(ctx, ret, 4, JS_NewString(ctx, opcode->name));
  return ret;
}

static JSValue
js_misc_opcode_object(JSContext* ctx, const JSOpCode* opcode) {
  JSValue ret = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, ret, "size", JS_NewUint32(ctx, opcode->size));
  JS_SetPropertyStr(ctx, ret, "n_pop", JS_NewUint32(ctx, opcode->n_pop));
  JS_SetPropertyStr(ctx, ret, "n_push", JS_NewUint32(ctx, opcode->n_push));
  JS_SetPropertyStr(ctx, ret, "fmt", JS_NewUint32(ctx, opcode->fmt));
  JS_SetPropertyStr(ctx, ret, "name", JS_NewString(ctx, opcode->name));
  return ret;
}

static JSValue
js_misc_opcodes(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_NewArray(ctx);
  size_t i, j, len = countof(js_opcodes);
  BOOL as_object = FALSE;

  if(argc >= 1)
    as_object = JS_ToBool(ctx, argv[0]);

  for(i = 0, j = 0; i < len; i++) {

    if(i >= OP_TEMP_START && i < OP_TEMP_END)
      continue;

    JS_SetPropertyUint32(ctx,
                         ret,
                         j++,
                         (as_object ? js_misc_opcode_object : js_misc_opcode_array)(ctx, &js_opcodes[i]));
  }

  return ret;
}

static JSValue
js_misc_get_bytecode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  if(JS_IsFunction(ctx, argv[0])) {
    JSObject* obj = JS_VALUE_GET_OBJ(argv[0]);
    JSFunctionBytecode* fnbc;

    if((fnbc = obj->u.func.function_bytecode)) {
      ret = JS_NewArrayBufferCopy(ctx, fnbc->byte_code_buf, fnbc->byte_code_len);
    }
  }

  return ret;
}

enum { ATOM_TO_STRING = 0, ATOM_TO_VALUE, VALUE_TO_ATOM };

static JSValue
js_misc_atom(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case ATOM_TO_STRING: {
      int32_t atom;
      JS_ToInt32(ctx, &atom, argv[0]);
      ret = JS_AtomToString(ctx, atom);
      break;
    }
    case ATOM_TO_VALUE: {
      int32_t atom;
      JS_ToInt32(ctx, &atom, argv[0]);
      ret = JS_AtomToValue(ctx, atom);
      break;
    }
    case VALUE_TO_ATOM: {
      JSAtom atom = JS_ValueToAtom(ctx, argv[0]);
      ret = JS_NewUint32(ctx, atom);
      break;
    }
  }
  return ret;
}

enum { GET_CLASS_ID = 0, GET_CLASS_NAME, GET_CLASS_ATOM, GET_CLASS_COUNT, GET_CLASS_PROTO, GET_CLASS_CONSTRUCTOR };

static JSValue
js_misc_classid(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  JSObject* obj;
  int32_t class_id = 0;

  if(argc >= 1) {
    if(JS_IsNumber(argv[0]))
      JS_ToInt32(ctx, &class_id, argv[0]);
    else if((obj = js_value_get_obj(argv[0])))
      class_id = obj->class_id;
  }

  switch(magic) {
    case GET_CLASS_ID: {
      if(class_id > 0)
        ret = JS_NewUint32(ctx, class_id);
      break;
    }
    case GET_CLASS_NAME: {
      if(class_id > 0) {
        JSAtom atom;
        if((atom = js_class_atom(ctx, class_id)))
          ret = JS_AtomToValue(ctx, atom);
      }
      break;
    }
    case GET_CLASS_ATOM: {
      if(class_id > 0) {
        JSAtom atom;
        if((atom = js_class_atom(ctx, class_id)))
          ret = JS_NewInt32(ctx, atom);
      }
      break;
    }
    case GET_CLASS_COUNT: {
      uint32_t i, class_count = ctx->rt->class_count;
      for(i = 1; i < class_count; i++)
        if(!JS_IsRegisteredClass(ctx->rt, i))
          break;

      ret = JS_NewUint32(ctx, i);
      break;
    }
    case GET_CLASS_PROTO: {
      if(class_id > 0)
        ret = JS_GetClassProto(ctx, class_id);
      break;
    }
    case GET_CLASS_CONSTRUCTOR: {
      if(class_id > 0) {
        JSValue proto = JS_GetClassProto(ctx, class_id);
        if(JS_IsObject(proto))
          ret = JS_GetPropertyStr(ctx, proto, "constructor");
        JS_FreeValue(ctx, proto);
      }
      break;
    }
  }
  return ret;
}

static const JSCFunctionListEntry js_misc_funcs[] = {
    JS_CFUNC_DEF("toString", 1, js_misc_tostring),
    JS_CFUNC_DEF("toPointer", 1, js_misc_topointer),
    JS_CFUNC_DEF("toArrayBuffer", 1, js_misc_toarraybuffer),
    JS_CFUNC_DEF("dupArrayBuffer", 1, js_misc_duparraybuffer),
    JS_CFUNC_DEF("resizeArrayBuffer", 1, js_misc_resizearraybuffer),
    JS_CFUNC_DEF("concatArrayBuffer", 1, js_misc_concatarraybuffer),
    JS_CFUNC_DEF("getPerformanceCounter", 0, js_misc_getperformancecounter),
    JS_CFUNC_DEF("hrtime", 0, js_misc_hrtime),
    JS_CFUNC_DEF("uname", 0, js_misc_uname),
    JS_CFUNC_DEF("btoa", 1, js_misc_btoa),
    JS_CFUNC_DEF("atob", 1, js_misc_atob),
    JS_CFUNC_DEF("compileFile", 1, js_misc_compile_file),
    JS_CFUNC_DEF("writeObject", 1, js_misc_write_object),
    JS_CFUNC_DEF("readObject", 1, js_misc_read_object),
    JS_CFUNC_DEF("getOpCodes", 0, js_misc_opcodes),
    JS_CFUNC_DEF("getByteCode", 1, js_misc_get_bytecode),
    JS_CFUNC_MAGIC_DEF("valueType", 1, js_misc_valuetype, VALUE_TYPE),
    JS_CFUNC_MAGIC_DEF("valueTag", 1, js_misc_valuetype, VALUE_TAG),
    JS_CFUNC_MAGIC_DEF("valuePtr", 1, js_misc_valuetype, VALUE_PTR),
    JS_CFUNC_DEF("evalBinary", 1, js_misc_evalbinary),
    JS_CFUNC_MAGIC_DEF("atomToString", 1, js_misc_atom, ATOM_TO_STRING),
    JS_CFUNC_MAGIC_DEF("atomToValue", 1, js_misc_atom, ATOM_TO_VALUE),
    JS_CFUNC_MAGIC_DEF("valueToAtom", 1, js_misc_atom, VALUE_TO_ATOM),
    JS_CFUNC_MAGIC_DEF("getClassID", 1, js_misc_classid, GET_CLASS_ID),
    JS_CFUNC_MAGIC_DEF("getClassName", 1, js_misc_classid, GET_CLASS_NAME),
    JS_CFUNC_MAGIC_DEF("getClassAtom", 1, js_misc_classid, GET_CLASS_ATOM),
    JS_CFUNC_MAGIC_DEF("getClassCount", 1, js_misc_classid, GET_CLASS_COUNT),
    JS_CFUNC_MAGIC_DEF("getClassProto", 1, js_misc_classid, GET_CLASS_PROTO),
    JS_CFUNC_MAGIC_DEF("getClassConstructor", 1, js_misc_classid, GET_CLASS_CONSTRUCTOR),
};

static int
js_misc_init(JSContext* ctx, JSModuleDef* m) {

  js_location_init(ctx, m);

  if(m) {
    // JS_SetModuleExportList(ctx, m, location_ctor);
    JS_SetModuleExportList(ctx, m, js_misc_funcs, countof(js_misc_funcs));
  }

  // printf("%s\n", js_opcodes[0].name);

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
