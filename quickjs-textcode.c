#include "defines.h"
#include "quickjs-textcode.h"
#include "utils.h"
#include "buffer-utils.h"
#include "debug.h"
#include <libutf.h>

/**
 * \addtogroup quickjs-textdecoder
 * @{
 */

#define max(a, b) ((a) > (b) ? (a) : (b))

thread_local VISIBLE JSClassID js_textdecoder_class_id = 0, js_textencoder_class_id = 0;
thread_local JSValue textdecoder_proto = {{JS_TAG_UNDEFINED}}, textdecoder_ctor = {{JS_TAG_UNDEFINED}}, textencoder_proto = {{JS_TAG_UNDEFINED}},
                     textencoder_ctor = {{JS_TAG_UNDEFINED}};

const char* const textcode_encodings[] = {
    "unknown",
    "utf8",
    "utf16",
    "utf32",
};

enum {
  TEXTDECODER_DECODE,
  TEXTDECODER_END,
};
enum {
  TEXTDECODER_ENCODING,
  TEXTDECODER_BUFFERED,
};

static size_t
textdecoder_try(const void* in, size_t len) {
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
textdecoder_length(TextDecoder* td) {
  size_t r = 0;

  r += textdecoder_try(ringbuffer_begin(&td->buffer), ringbuffer_continuous_length(&td->buffer));

  if(td->buffer.head < td->buffer.tail)
    r += textdecoder_try(td->buffer.data, ringbuffer_head(&td->buffer));

  return r;
}

JSValue
textdecoder_read(TextDecoder* td, JSContext* ctx) {
  JSValue ret;
  DynBuf dbuf;
  size_t i;
  char tmp[UTF8_CHAR_LEN_MAX];
  int len = 0;
  js_dbuf_init(ctx, &dbuf);

  if(textdecoder_length(td) > ringbuffer_continuous_length(&td->buffer))
    ringbuffer_normalize(&td->buffer);

  switch(td->encoding) {
    case UTF8: {
      ret = JS_NewStringLen(ctx, (const char*)ringbuffer_begin(&td->buffer), textdecoder_length(td));
      break;
    }
    case UTF16: {
      uint_least16_t* ptr = ringbuffer_begin(&td->buffer);
      uint_least16_t* end = ringbuffer_end(&td->buffer);

      for(i = 0; ptr < end; ptr++, i++) {
        uint_least32_t cp = 0;

        if(!libutf_c16_to_c32(ptr, &cp))
          return JS_ThrowInternalError(ctx, "No a valid utf-16 code at (%zu: 0x%04x, 0x%04x): %" PRIu32, i, ptr[0], ptr[1], cp);

        len = unicode_to_utf8((void*)tmp, cp);

        if(dbuf_put(&dbuf, (const void*)tmp, len))
          return JS_ThrowOutOfMemory(ctx);
      }

      break;
    }
    case UTF32: {
      const uint_least32_t* ptr = ringbuffer_begin(&td->buffer);
      const uint_least32_t* end = ringbuffer_end(&td->buffer);

      for(i = 0; ptr < end; ptr++, i++) {
        uint_least32_t cp = *ptr;

        len = unicode_to_utf8((void*)tmp, cp);

        if(dbuf_put(&dbuf, (const void*)tmp, len))
          return JS_ThrowOutOfMemory(ctx);
      }

      break;
    }
    default: {
      ret = JS_ThrowInternalError(ctx, "TextEncoder: unknown encoding");
      break;
    }
  }

  ret = JS_NewStringLen(ctx, (const char*)ringbuffer_begin(&td->buffer), len);

  td->buffer.tail += len;
  return ret;
}

static JSValue
js_textdecoder_get(JSContext* ctx, JSValueConst this_val, int magic) {
  TextDecoder* dec;
  JSValue ret = JS_UNDEFINED;
  if(!(dec = js_textdecoder_data(ctx, this_val)))
    return ret;
  switch(magic) {
    case TEXTDECODER_ENCODING: {
      ret = JS_NewString(ctx, dec->encoding == UTF8 ? "utf-8" : dec->encoding == UTF16 ? "utf-16" : "unknown");
      break;
    }
    case TEXTDECODER_BUFFERED: {
      ret = JS_NewUint32(ctx, ringbuffer_length(&dec->buffer));
      break;
    }
  }
  return ret;
}

static JSValue
js_textdecoder_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  TextDecoder* dec;

  if(!(dec = js_mallocz(ctx, sizeof(TextDecoder))))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = textdecoder_proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_textdecoder_class_id);
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
      return JS_ThrowInternalError(ctx, "TextDecoder '%s' is invalid encoding", encoding);
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
js_textdecoder_decode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  TextDecoder* dec;
  JSValue ret = JS_UNDEFINED;

  if(!(dec = js_textdecoder_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case TEXTDECODER_END:
    case TEXTDECODER_DECODE: {
      InputBuffer in = js_input_chars(ctx, argv[0]);
      uint8_t tmp[UTF8_CHAR_LEN_MAX];
      size_t i;
      if(ringbuffer_write(&dec->buffer, in.data, in.size) < 0)
        return JS_ThrowInternalError(ctx, "TextDecoder: ringbuffer %s failed", magic == TEXTDECODER_DECODE ? "decode" : "end");
      /*
          switch(dec->encoding) {
            case UTF8: {
              if(ringbuffer_write(&dec->buffer, in.data, in.size) < 0)
                return JS_ThrowInternalError(ctx, "TextDecoder: ringbuffer %s failed", magic == TEXTDECODER_DECODE ? "decode" : "end");
              break;
            }
            case UTF16: {
              uint_least16_t* ptr = input_buffer_begin(&in);
              uint_least16_t* end = input_buffer_end(&in);

              for(i = 0; ptr < end; ptr++, i++) {
                int len;
                uint_least32_t cp = 0;

                if(!libutf_c16_to_c32(ptr, &cp))
                  return JS_ThrowInternalError(ctx, "No a valid utf-16 code at (%zu: 0x%04x, 0x%04x): %" PRIu32, i, ptr[0], ptr[1], cp);

                len = unicode_to_utf8(tmp, cp);

                if(ringbuffer_write(&dec->buffer, tmp, len) < 0)
                  return JS_ThrowInternalError(ctx, "TextDecoder: ringbuffer %s failed", magic == TEXTDECODER_DECODE ? "decode" : "end");
              }

              break;
            }
            case UTF32: {
              const uint_least32_t* ptr = input_buffer_begin(&in);
              const uint_least32_t* end = input_buffer_end(&in);

              for(i = 0; ptr < end; ptr++, i++) {
                int len;
                uint_least32_t cp = *ptr;

                len = unicode_to_utf8(tmp, cp);

                if(ringbuffer_write(&dec->buffer, tmp, len) < 0)
                  return JS_ThrowInternalError(ctx, "TextDecoder: ringbuffer %s failed", magic == TEXTDECODER_DECODE ? "decode" : "end");
              }

              break;
            }
            default: {
              ret = JS_ThrowInternalError(ctx, "TextEncoder: unknown encoding");
              break;
            }
          }
    */
      if(ringbuffer_length(&dec->buffer) == 0)
        ret = JS_NULL;
      else
        ret = textdecoder_read(dec, ctx);

      if(magic == TEXTDECODER_END)
        ringbuffer_reset(&dec->buffer);
      break;
    }
  }
  return ret;
}

static JSValue
js_textdecoder_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  TextDecoder* dec;

  if(!(dec = js_textdecoder_data(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectClass(ctx, js_textdecoder_class_id);

  JS_DefinePropertyValueStr(ctx, obj, "encoding", JS_NewString(ctx, textcode_encodings[dec->encoding]), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "buffered", JS_NewUint32(ctx, ringbuffer_length(&dec->buffer)), JS_PROP_ENUMERABLE);
  return obj;
}

static void
js_textdecoder_finalizer(JSRuntime* rt, JSValue val) {
  TextDecoder* dec = JS_GetOpaque(val, js_textdecoder_class_id);
  if(dec) {
    ringbuffer_free(&dec->buffer);
    js_free_rt(rt, dec);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_textdecoder_class = {
    .class_name = "TextDecoder",
    .finalizer = js_textdecoder_finalizer,
};

static const JSCFunctionListEntry js_textdecoder_funcs[] = {
    JS_CFUNC_MAGIC_DEF("decode", 1, js_textdecoder_decode, TEXTDECODER_DECODE),
    JS_CFUNC_MAGIC_DEF("end", 1, js_textdecoder_decode, TEXTDECODER_END),
    JS_CGETSET_ENUMERABLE_DEF("encoding", js_textdecoder_get, 0, TEXTDECODER_ENCODING),
    JS_CGETSET_ENUMERABLE_DEF("buffered", js_textdecoder_get, 0, TEXTDECODER_BUFFERED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TextDecoder", JS_PROP_CONFIGURABLE),
};

enum {
  TEXTENCODER_ENCODE,
  TEXTENCODER_END,
};
enum {
  TEXTENCODER_ENCODING,
  TEXTENCODER_BUFFERED,
};

/*static size_t
textencoder_try(const void* in, size_t len) {
  const uint8_t *x, *y, *end;
  size_t r = 0;
  int ch;
  for(x = in, end=x+len; x < end; ) {
    y = x;
    if((ch = unicode_from_utf8(x, end - x, &x)) == -1)
      break;
    r += x - y;
  }
  return r;
}

size_t
textencoder_length(TextEncoder* td) {
  size_t r = 0;

  r += textencoder_try(ringbuffer_begin(&td->buffer), ringbuffer_continuous_length(&td->buffer));

  if(td->buffer.head < td->buffer.tail)
    r += textencoder_try(td->buffer.data, ringbuffer_head(&td->buffer));

  return r;
}*/

JSValue
textencoder_read(TextEncoder* te, JSContext* ctx) {
  JSValue ret, buf;
  int bits;
  size_t len = ringbuffer_length(&te->buffer);

  if(len > ringbuffer_continuous_length(&te->buffer))
    ringbuffer_normalize(&te->buffer);

  switch(te->encoding) {
    case UTF8: bits = 8; break;
    case UTF16: bits = 16; break;
    case UTF32: bits = 32; break;
    default: return JS_ThrowInternalError(ctx, "TextEncoder: invalid encoding: %d", te->encoding);
  }

  buf = JS_NewArrayBufferCopy(ctx, ringbuffer_begin(&te->buffer), len);
  ret = js_typedarray_new(ctx, bits, FALSE, FALSE, buf);
  JS_FreeValue(ctx, buf);

  te->buffer.tail += len;
  return ret;
}

static JSValue
js_textencoder_get(JSContext* ctx, JSValueConst this_val, int magic) {
  TextEncoder* enc;
  JSValue ret = JS_UNDEFINED;
  if(!(enc = js_textencoder_data(ctx, this_val)))
    return ret;
  switch(magic) {
    case TEXTENCODER_ENCODING: {
      ret = JS_NewString(ctx, enc->encoding == UTF8 ? "utf-8" : enc->encoding == UTF16 ? "utf-16" : "unknown");
      break;
    }
    case TEXTENCODER_BUFFERED: {
      ret = JS_NewUint32(ctx, ringbuffer_length(&enc->buffer));
      break;
    }
  }
  return ret;
}

static JSValue
js_textencoder_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  TextEncoder* enc;

  if(!(enc = js_mallocz(ctx, sizeof(TextEncoder))))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = textencoder_proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_textencoder_class_id);
  if(JS_IsException(obj))
    goto fail;

  ringbuffer_init(&enc->buffer, ctx);

  if(argc >= 1) {
    const char* encoding = JS_ToCString(ctx, argv[0]);

    if(!strcasecmp(encoding, "utf32") || !strcasecmp(encoding, "utf-32"))
      enc->encoding = UTF32;
    else if(!strcasecmp(encoding, "utf16") || !strcasecmp(encoding, "utf-16"))
      enc->encoding = UTF16;
    else if(!strcasecmp(encoding, "utf8") || !strcasecmp(encoding, "utf-8"))
      enc->encoding = UTF8;
    else {
      return JS_ThrowInternalError(ctx, "TextEncoder '%s' is invalid encoding", encoding);
    }
    JS_FreeCString(ctx, encoding);
  } else {
    enc->encoding = UTF8;
  }
  JS_SetOpaque(obj, enc);

  return obj;

fail:
  js_free(ctx, enc);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_textencoder_encode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  TextEncoder* enc;
  JSValue ret = JS_UNDEFINED;

  if(!(enc = js_textencoder_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case TEXTENCODER_END:
    case TEXTENCODER_ENCODE: {
      size_t i;
      InputBuffer in = js_input_chars(ctx, argv[0]);

      if(!ringbuffer_allocate(&enc->buffer, in.size))
        return JS_ThrowOutOfMemory(ctx);

      switch(enc->encoding) {
        case UTF8: {
          if(ringbuffer_write(&enc->buffer, in.data, in.size) < 0)
            return JS_ThrowInternalError(ctx, "TextEncoder: ringbuffer %s failed", magic == TEXTENCODER_ENCODE ? "write" : "end");
          break;
        }
        case UTF16: {
          const uint8_t* ptr = input_buffer_begin(&in);
          const uint8_t* end = input_buffer_end(&in);

          for(i = 0; ptr < end; i++) {
            uint_least32_t cp = unicode_from_utf8(ptr, end - ptr, &ptr);
            uint_least16_t u16[2];
            int len;

            if(!libutf_c32_to_c16(cp, &len, u16))
              return JS_ThrowInternalError(ctx, "No a valid code point at (%zu): %" PRIu32, i, cp);

            if(ringbuffer_write(&enc->buffer, (const void*)u16, len * sizeof(uint_least16_t)) < 0)
              return JS_ThrowInternalError(ctx, "TextEncoder: ringbuffer %s failed", magic == TEXTENCODER_ENCODE ? "write" : "end");
          }

          break;
        }
        case UTF32: {
          const uint8_t* ptr = input_buffer_begin(&in);
          const uint8_t* end = input_buffer_end(&in);

          for(i = 0; ptr < end; i++) {
            uint_least32_t cp = unicode_from_utf8(ptr, end - ptr, &ptr);

            if(ringbuffer_write(&enc->buffer, (const void*)&cp, sizeof(cp)) < 0)
              return JS_ThrowInternalError(ctx, "TextEncoder: ringbuffer %s failed", magic == TEXTENCODER_ENCODE ? "write" : "end");
          }

          break;
        }
        default: {
          ret = JS_ThrowInternalError(ctx, "TextEncoder: unknown encoding");
          break;
        }
      }

      if(ringbuffer_length(&enc->buffer) == 0)
        ret = JS_NULL;
      else
        ret = textencoder_read(enc, ctx);

      if(magic == TEXTENCODER_END)
        ringbuffer_reset(&enc->buffer);
      break;
    }
  }
  return ret;
}

static JSValue
js_textencoder_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  TextEncoder* enc;

  if(!(enc = js_textencoder_data(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectClass(ctx, js_textencoder_class_id);

  JS_DefinePropertyValueStr(ctx, obj, "encoding", JS_NewString(ctx, textcode_encodings[enc->encoding]), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "buffered", JS_NewUint32(ctx, ringbuffer_length(&enc->buffer)), JS_PROP_ENUMERABLE);
  return obj;
}

static void
js_textencoder_finalizer(JSRuntime* rt, JSValue val) {
  TextEncoder* enc = JS_GetOpaque(val, js_textencoder_class_id);
  if(enc) {
    ringbuffer_free(&enc->buffer);
    js_free_rt(rt, enc);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_textencoder_class = {
    .class_name = "TextEncoder",
    .finalizer = js_textencoder_finalizer,
};

static const JSCFunctionListEntry js_textencoder_funcs[] = {
    JS_CFUNC_MAGIC_DEF("encode", 1, js_textencoder_encode, TEXTENCODER_ENCODE),
    JS_CFUNC_MAGIC_DEF("end", 1, js_textencoder_encode, TEXTENCODER_END),
    JS_CGETSET_ENUMERABLE_DEF("encoding", js_textencoder_get, 0, TEXTENCODER_ENCODING),
    JS_CGETSET_ENUMERABLE_DEF("buffered", js_textencoder_get, 0, TEXTENCODER_BUFFERED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TextEncoder", JS_PROP_CONFIGURABLE),
};

int
js_textcode_init(JSContext* ctx, JSModuleDef* m) {

  if(js_textdecoder_class_id == 0) {
    JS_NewClassID(&js_textdecoder_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_textdecoder_class_id, &js_textdecoder_class);

    textdecoder_ctor = JS_NewCFunction2(ctx, js_textdecoder_constructor, "TextDecoder", 1, JS_CFUNC_constructor, 0);
    textdecoder_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, textdecoder_proto, js_textdecoder_funcs, countof(js_textdecoder_funcs));
    JS_SetClassProto(ctx, js_textdecoder_class_id, textdecoder_proto);

    JS_NewClassID(&js_textencoder_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_textencoder_class_id, &js_textencoder_class);

    textencoder_ctor = JS_NewCFunction2(ctx, js_textencoder_constructor, "TextEncoder", 1, JS_CFUNC_constructor, 0);
    textencoder_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, textencoder_proto, js_textencoder_funcs, countof(js_textencoder_funcs));
    JS_SetClassProto(ctx, js_textencoder_class_id, textencoder_proto);

    // js_set_inspect_method(ctx, textdecoder_proto,
    // js_textdecoder_inspect);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "TextDecoder", textdecoder_ctor);
    JS_SetModuleExport(ctx, m, "TextEncoder", textencoder_ctor);

    /*  const char* module_name = JS_AtomToCString(ctx, m->module_name);

      if(!strcmp(module_name, "textdecoder"))
        JS_SetModuleExport(ctx, m, "default", textdecoder_ctor);

      JS_FreeCString(ctx, module_name);*/
  }

  return 0;
}

#ifdef JS_TEXTCODE_MODULE
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_textdecoder
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  if(!(m = JS_NewCModule(ctx, module_name, &js_textcode_init)))
    return m;
  JS_AddModuleExport(ctx, m, "TextDecoder");
  JS_AddModuleExport(ctx, m, "TextEncoder");
  /*if(!strcmp(module_name, "textdecoder"))
    JS_AddModuleExport(ctx, m, "default");*/
  return m;
}

/**
 * @}
 */
