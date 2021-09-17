#include "quickjs-stringdecoder.h"
#include "utils.h"
#include "buffer-utils.h"

#define max(a, b) ((a) > (b) ? (a) : (b))

thread_local VISIBLE JSClassID js_stringdecoder_class_id = 0;
thread_local JSValue stringdecoder_proto = {JS_TAG_UNDEFINED}, stringdecoder_ctor = {JS_TAG_UNDEFINED};

enum { STRINGDECODER_WRITE, STRINGDECODER_END };
enum { STRINGDECODER_ENCODING, STRINGDECODER_BUFFERED };

const char* const stringdecoder_encodings[] = {"unknown", "utf8", "utf16"};

static size_t
stringdecoder_try(const void* in, size_t len) {
  const uint8_t *x, *y, *end;
  size_t r = 0;
  int ch;
  x = in;
  end = x + len;
  while(x < end) {
    y = x;
    if((ch = unicode_from_utf8(x, end - x, &x)) == -1)
      break;
    r += x - y;
  }
  return r;
}

size_t
stringdecoder_length(StringDecoder* sd) {
  size_t r = 0;

  r += stringdecoder_try(ringbuffer_begin(&sd->buffer), ringbuffer_continuous_length(&sd->buffer));

  if(sd->buffer.head < sd->buffer.tail)
    r += stringdecoder_try(sd->buffer.data, ringbuffer_head(&sd->buffer));

  return r;
}

JSValue
stringdecoder_read(StringDecoder* sd, JSContext* ctx) {
  JSValue ret;
  size_t len = stringdecoder_length(sd);

  if(len > ringbuffer_continuous_length(&sd->buffer))
    ringbuffer_normalize(&sd->buffer);

  ret = JS_NewStringLen(ctx, ringbuffer_begin(&sd->buffer), len);

  sd->buffer.tail += len;
  return ret;
}

static JSValue
js_stringdecoder_get(JSContext* ctx, JSValueConst this_val, int magic) {
  StringDecoder* dec;
  JSValue ret = JS_UNDEFINED;
  if(!(dec = js_stringdecoder_data(ctx, this_val)))
    return ret;
  switch(magic) {
    case STRINGDECODER_ENCODING: {
      ret = JS_NewString(ctx, dec->encoding == UTF8 ? "utf-8" : dec->encoding == UTF16 ? "utf-16" : "unknown");
      break;
    }
    case STRINGDECODER_BUFFERED: {
      ret = JS_NewUint32(ctx, ringbuffer_length(&dec->buffer));
      break;
    }
  }
  return ret;
}

static JSValue
js_stringdecoder_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  StringDecoder* dec;

  if(!(dec = js_mallocz(ctx, sizeof(StringDecoder))))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = stringdecoder_proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_stringdecoder_class_id);
  if(JS_IsException(obj))
    goto fail;

  ringbuffer_init(&dec->buffer, ctx);

  if(argc >= 1) {
    const char* encoding = JS_ToCString(ctx, argv[0]);

    if(!strcasecmp(encoding, "utf16") || !strcasecmp(encoding, "utf-16"))
      dec->encoding = UTF16;
    else if(!strcasecmp(encoding, "utf8") || !strcasecmp(encoding, "utf-8"))
      dec->encoding = UTF8;
    else {
      return JS_ThrowInternalError(ctx, "StringDecoder '%s' is invalid encoding", encoding);
    }
    JS_FreeCString(ctx, encoding);
  } else {
    dec->encoding = UTF8;
  }
  JS_SetOpaque(obj, dec);

  return obj;

fail:
  js_free(ctx, dec);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_stringdecoder_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StringDecoder* dec;
  JSValue ret = JS_UNDEFINED;

  if(!(dec = js_stringdecoder_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case STRINGDECODER_END:
    case STRINGDECODER_WRITE: {
      InputBuffer in = js_input_chars(ctx, argv[0]);

      // printf("StringDecoder.%s '%.*s' size=%zu\n", magic == STRINGDECODER_WRITE ?
      // "write" : "end", in.size, in.data, in.size);
      if(!ringbuffer_allocate(&dec->buffer, in.size))
        return JS_ThrowOutOfMemory(ctx);
      if(ringbuffer_write(&dec->buffer, in.data, in.size) < 0)
        return JS_ThrowInternalError(ctx, "StringDecoder: ringbuffer %s failed", magic == STRINGDECODER_WRITE ? "write" : "end");
      ret = stringdecoder_read(dec, ctx);

      /*{
        size_t len;
        const char* str = JS_ToCStringLen(ctx, &len, ret);
        printf("StringDecoder.%s ret='%.*s' size=%zu\n", magic == STRINGDECODER_WRITE ?
      "write" : "end", len, str, len); JS_FreeCString(ctx, str);
      }*/

      if(magic == STRINGDECODER_END)
        ringbuffer_reset(&dec->buffer);
      break;
    }
  }
  return ret;
}

static JSValue
js_stringdecoder_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  StringDecoder* dec;

  if(!(dec = js_stringdecoder_data(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectProto(ctx, stringdecoder_proto);
  JS_DefinePropertyValueStr(ctx, obj, "encoding", JS_NewString(ctx, stringdecoder_encodings[dec->encoding]), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "buffered", JS_NewUint32(ctx, ringbuffer_length(&dec->buffer)), JS_PROP_ENUMERABLE);
  return obj;
}

static void
js_stringdecoder_finalizer(JSRuntime* rt, JSValue val) {
  StringDecoder* dec = JS_GetOpaque(val, js_stringdecoder_class_id);
  if(dec) {
    ringbuffer_free(&dec->buffer);
    js_free_rt(rt, dec);
  }
  JS_FreeValueRT(rt, val);
}

static JSClassDef js_stringdecoder_class = {
    .class_name = "StringDecoder",
    .finalizer = js_stringdecoder_finalizer,
};

static const JSCFunctionListEntry js_stringdecoder_funcs[] = {
    JS_CFUNC_MAGIC_DEF("write", 1, js_stringdecoder_write, STRINGDECODER_WRITE),
    JS_CFUNC_MAGIC_DEF("end", 1, js_stringdecoder_write, STRINGDECODER_END),
    JS_CGETSET_ENUMERABLE_DEF("encoding", js_stringdecoder_get, 0, STRINGDECODER_ENCODING),
    JS_CGETSET_ENUMERABLE_DEF("buffered", js_stringdecoder_get, 0, STRINGDECODER_BUFFERED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StringDecoder", JS_PROP_CONFIGURABLE),
};

int
js_stringdecoder_init(JSContext* ctx, JSModuleDef* m) {

  if(js_stringdecoder_class_id == 0) {
    JS_NewClassID(&js_stringdecoder_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_stringdecoder_class_id, &js_stringdecoder_class);

    stringdecoder_ctor = JS_NewCFunction2(ctx, js_stringdecoder_constructor, "StringDecoder", 1, JS_CFUNC_constructor, 0);
    stringdecoder_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, stringdecoder_proto, js_stringdecoder_funcs, countof(js_stringdecoder_funcs));
    JS_SetClassProto(ctx, js_stringdecoder_class_id, stringdecoder_proto);

    // js_set_inspect_method(ctx, stringdecoder_proto, js_stringdecoder_inspect);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "StringDecoder", stringdecoder_ctor);

    const char* module_name = JS_AtomToCString(ctx, m->module_name);

    if(!strcmp(module_name, "stringdecoder"))
      JS_SetModuleExport(ctx, m, "default", stringdecoder_ctor);

    JS_FreeCString(ctx, module_name);
  }

  return 0;
}

#ifdef JS_STRINGDECODER_MODULE
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_stringdecoder
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  if(!(m = JS_NewCModule(ctx, module_name, &js_stringdecoder_init)))
    return m;
  JS_AddModuleExport(ctx, m, "StringDecoder");
  if(!strcmp(module_name, "stringdecoder"))
    JS_AddModuleExport(ctx, m, "default");
  return m;
}
