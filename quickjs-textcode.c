#include "defines.h"
#include "quickjs-textcode.h"
#include "utils.h"
#include "buffer-utils.h"
#include "stream-utils.h"
#include "debug.h"
#include <sys/types.h>
#include <libutf.h>
#include "tutf8e/include/tutf8e.h"

/**
 * \addtogroup quickjs-textdecoder
 * @{
 */

VISIBLE JSClassID js_decoder_class_id = 0, js_encoder_class_id = 0;
static JSValue textdecoder_proto, textdecoder_ctor, textencoder_proto, textencoder_ctor;

const TUTF8encoder* tutf8e_coders[] = {
    /* 0, 0, 0, 0, 0, 0, 0, 0, */
    &tutf8e_encoder_iso_8859_1,   &tutf8e_encoder_iso_8859_2,   &tutf8e_encoder_iso_8859_3,   &tutf8e_encoder_iso_8859_4,   &tutf8e_encoder_iso_8859_5,
    &tutf8e_encoder_iso_8859_6,   &tutf8e_encoder_iso_8859_7,   &tutf8e_encoder_iso_8859_8,   &tutf8e_encoder_iso_8859_9,   &tutf8e_encoder_iso_8859_10,
    &tutf8e_encoder_iso_8859_11,  &tutf8e_encoder_iso_8859_13,  &tutf8e_encoder_iso_8859_14,  &tutf8e_encoder_iso_8859_15,  &tutf8e_encoder_iso_8859_16,
    &tutf8e_encoder_windows_1250, &tutf8e_encoder_windows_1251, &tutf8e_encoder_windows_1252, &tutf8e_encoder_windows_1253, &tutf8e_encoder_windows_1254,
    &tutf8e_encoder_windows_1255, &tutf8e_encoder_windows_1256, &tutf8e_encoder_windows_1257, &tutf8e_encoder_windows_1258,
};

const char* const textcode_encodings[] = {
    "unknown",      "UTF-8",        "UTF-16",       "UTF-32",       "unknown",      "UTF-8",        "UTF-16BE",     "UTF-32BE",
    "ISO-8859-1",   "ISO-8859-2",   "ISO-8859-3",   "ISO-8859-4",   "ISO-8859-5",   "ISO-8859-6",   "ISO-8859-7",   "ISO-8859-8",
    "ISO-8859-9",   "ISO-8859-10",  "ISO-8859-11",  "ISO-8859-13",  "ISO-8859-14",  "ISO-8859-15",  "ISO-8859-16",  "WINDOWS-1250",
    "WINDOWS-1251", "WINDOWS-1252", "WINDOWS-1253", "WINDOWS-1254", "WINDOWS-1255", "WINDOWS-1256", "WINDOWS-1257", "WINDOWS-1258",
};

enum {
  DECODER_DECODE,
  DECODER_END,
};
enum {
  DECODER_ENCODING,
  DECODER_ENDIANNESS,
  DECODER_BUFFERED,
};

JSValue
textdecoder_decode(TextDecoder* dec, InputBuffer in, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  uint32_t cp;
  uint8_t* tmp;
  size_t i = 0;
  int len = 0;
  DynBuf dbuf = DBUF_INIT_CTX(ctx);
  const uint8_t *ptr = inputbuffer_begin(&in), *end = inputbuffer_end(&in), *next;

  switch(dec->char_set) {
    case UTF8: {
      while(!inputbuffer_eof(&in)) {
        if(inputbuffer_decode(&in, &unicode_decode_utf8, &cp) == 0)
          break;
      }

      ret = JS_NewStringLen(ctx, inputbuffer_data(&in), in.pos);
      break;
    }

    case UTF16: {
      Decoding* decode = dec->endian ? &uint16_decode_be : &uint16_decode_le;

      while(!inputbuffer_eof(&in)) {
        uint16_t u16[2];

        if(inputbuffer_decode(&in, decode, &u16[0]) == 0)
          break;

        if(libutf_c16_type(u16[0]) > 0) {
          if(inputbuffer_decode(&in, decode, &u16[1]) == 0)
            break;
        }

        if(!libutf_c16_to_c32(u16, &cp)) {
          ret = JS_ThrowInternalError(ctx, "TextDecoder.decode: not a valid utf-16 code at (%zu: 0x%04x, 0x%04x): %i", i, ptr[0], ptr[1], cp);
          break;
        }

        dbuf_encode(&dbuf, &unicode_encode_utf8, cp);
      }

      break;
    }

    case UTF32: {
      Decoding* decode = dec->endian ? &uint32_decode_be : &uint32_decode_le;

      while(!inputbuffer_eof(&in)) {
        if(inputbuffer_decode(&in, decode, &cp) == 0)
          break;

        if(!(tmp = dbuf_reserve(&dbuf, UTF8_CHAR_LEN_MAX)))
          return JS_EXCEPTION;

        cp = uint32_get_endian(ptr, dec->endian);

        if(!libutf_c32_to_c8(cp, &len, (char*)tmp)) {
          ret = JS_ThrowInternalError(ctx, "TextDecoder.decode: not a valid utf-32 code at (%zu: 0x%04x, 0x%04x): %i", i, ptr[0], ptr[1], cp);
          break;
        }

        dbuf.size += len;
      }

      break;
    }

    default: {
      TUTF8encoder encoder;

      if((encoder = *tutf8e_coders[dec->type_code - 8])) {
        size_t length = 0;

        if(TUTF8E_OK == tutf8e_encoder_buffer_length(encoder, (const char*)ptr, 0, end - ptr, &length)) {
          uint8_t* dst = dbuf_reserve(&dbuf, length);

          if(TUTF8E_OK == tutf8e_encoder_buffer_encode(encoder, (const char*)ptr, end - ptr, 0, (char*)dst, &length)) {
            dbuf.size += length;
          }
        }

      } else {
        ret = JS_ThrowInternalError(ctx, "TextDecoder.decode: unknown encoding: %s", textcode_encodings[dec->type_code & 0x3]);
      }

      break;
    }
  }

  size_t remain = inputbuffer_remain(&in);
  dec->buflen = 0;

  if(remain > UTF8_CHAR_LEN_MAX) {
    ret = JS_ThrowInternalError(ctx, "TextDecoder.decode: %zu bytes remaining", remain);

  } else {
    memcpy(dec->buf, inputbuffer_pointer(&in), remain);
    dec->buflen = remain;
  }

  if(JS_IsUndefined(ret))
    ret = dbuf_tostring_free(&dbuf, ctx);
  else
    dbuf_free(&dbuf);

  return ret;
}

static JSValue
js_decoder_get(JSContext* ctx, JSValueConst this_val, int magic) {
  TextDecoder* dec;
  JSValue ret = JS_UNDEFINED;

  if(!(dec = js_decoder_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case DECODER_ENCODING: {
      ret = JS_NewString(ctx, textcode_encodings[dec->type_code]);
      break;
    }

    case DECODER_ENDIANNESS: {
      ret = JS_NewBool(ctx, dec->endian == BIG);
      break;
    }

    case DECODER_BUFFERED: {
      ret = JS_NewUint32(ctx, dec->buflen);
      break;
    }
  }

  return ret;
}

static JSValue
js_decoder_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  TextDecoder* dec;

  if(!(dec = js_mallocz(ctx, sizeof(TextDecoder))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = textdecoder_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_decoder_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  uint32_t defaultSize = 1024;

  if(argc > 1)
    JS_ToUint32(ctx, &defaultSize, argv[1]);

  if(argc >= 1) {
    const char* s = JS_ToCString(ctx, argv[0]);

    if(s[case_finds(s, "utf32")] || s[case_finds(s, "utf-32")])
      dec->char_set = UTF32;
    else if(s[case_finds(s, "utf16")] || s[case_finds(s, "utf-16")])
      dec->char_set = UTF16;
    else if(s[case_finds(s, "utf8")] || s[case_finds(s, "utf-8")])
      dec->char_set = UTF8;
    else {
      return JS_ThrowInternalError(ctx, "TextDecoder.constructor: '%s' is invalid s", s);
    }

    if(s[case_finds(s, "be")] || s[case_finds(s, "be")])
      dec->endian = BIG;

    JS_FreeCString(ctx, s);
  } else {
    dec->type_code = (TextEncoding)UTF8;
  }

  JS_SetOpaque(obj, dec);
  return obj;

fail:
  js_free(ctx, dec);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_decoder_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  TextDecoder* dec;
  JSValue ret = JS_UNDEFINED;

  if(!(dec = js_decoder_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case DECODER_DECODE: {
      InputBuffer in = js_input_chars(ctx, argv[0]);
      BOOL stream = argc > 1 && js_get_propertystr_bool(ctx, argv[1], "stream");

      ret = textdecoder_decode(dec, in, ctx);
      break;
    }
  }

  return ret;
}

static JSValue
js_decoder_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  TextDecoder* dec;

  if(!(dec = js_decoder_data(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectClass(ctx, js_decoder_class_id);

  JS_DefinePropertyValueStr(ctx, obj, "encoding", JS_NewString(ctx, textcode_encodings[dec->type_code]), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "buffered", JS_NewUint32(ctx, dec->buflen), JS_PROP_ENUMERABLE);

  return obj;
}

static void
js_decoder_finalizer(JSRuntime* rt, JSValue val) {
  TextDecoder* dec;

  if((dec = JS_GetOpaque(val, js_decoder_class_id))) {
    js_free_rt(rt, dec);
  }
}

static JSClassDef js_decoder_class = {
    .class_name = "TextDecoder",
    .finalizer = js_decoder_finalizer,
};

static const JSCFunctionListEntry js_decoder_methods[] = {
    JS_CFUNC_MAGIC_DEF("decode", 1, js_decoder_method, DECODER_DECODE),
    JS_CGETSET_ENUMERABLE_DEF("encoding", js_decoder_get, 0, DECODER_ENCODING),
    JS_CGETSET_MAGIC_DEF("endian", js_decoder_get, 0, DECODER_ENDIANNESS),
    JS_CGETSET_MAGIC_DEF("buffered", js_decoder_get, 0, DECODER_BUFFERED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TextDecoder", JS_PROP_CONFIGURABLE),
};

enum {
  ENCODER_ENCODING,
  ENCODER_ENDIANNESS,
  ENCODER_BUFFERED,
};

static JSValue
textencoder_typedarray(TextEncoding type_code, const void* x, size_t n, JSContext* ctx) {
  JSValue ret, buf;
  int bits;

  switch(type_code & 0x3) {
    case UTF8: bits = 8; break;
    case UTF16: bits = 16; break;
    case UTF32: bits = 32; break;
    default: return JS_ThrowInternalError(ctx, "%s: TextEncoder: invalid encoding: %d", __func__, type_code);
  }

  buf = JS_NewArrayBufferCopy(ctx, x, n);
  ret = js_typedarray_new(ctx, bits, FALSE, FALSE, buf);
  JS_FreeValue(ctx, buf);
  return ret;
}

static ssize_t
textencoder_encode(TextEncoder* enc, InputBuffer in, Writer wr, JSContext* ctx) {
  uint8_t u8[UTF8_CHAR_LEN_MAX];
  size_t i = 0, written = 0;
  const uint8_t *ptr = inputbuffer_begin(&in), *end = inputbuffer_end(&in), *next;

  for(i = 0; ptr < end; ptr = next, i++) {
    int cp = unicode_from_utf8(ptr, end - ptr, &next);

    if(cp == -1)
      break;

    if(enc->char_set == UTF8) {
      if(writer_write(&wr, ptr, next - ptr) < 0)
        break;

      continue;
    }

    if(enc->char_set == UTF16) {
      uint16_t u16[2];
      int len;

      if(!libutf_c32_to_c16(cp, &len, u16)) {
        JS_ThrowInternalError(ctx, "TextEncoder.encode: not a valid code point at (%zu) [%ti]: %i", i, (next - ptr), cp);
        return -1;
      }

      for(int j = 0; j < len; j++)
        uint16_put_endian(u8 + j * 2, u16[j], enc->endian);

      if(writer_write(&wr, u8, len * sizeof(uint16_t)) < 0)
        break;

      continue;
    }

    if(enc->char_set == UTF32) {
      uint32_put_endian(u8, cp, enc->endian);

      if(writer_write(&wr, u8, sizeof(uint32_t)) < 0)
        break;

      continue;
    }

    JS_ThrowInternalError(ctx, "TextEncoder.encode: unknown encoding");
    return -1;
  }

  return i;
}

static JSValue
js_encoder_get(JSContext* ctx, JSValueConst this_val, int magic) {
  TextEncoder* enc;
  JSValue ret = JS_UNDEFINED;

  if(!(enc = js_encoder_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case ENCODER_ENCODING: {
      ret = JS_NewString(ctx, textcode_encodings[enc->type_code]);
      break;
    }

    case ENCODER_ENDIANNESS: {
      ret = JS_NewBool(ctx, enc->endian == BIG);
      break;
    }

    case ENCODER_BUFFERED: {
      ret = JS_NewUint32(ctx, enc->buflen);
      break;
    }
  }

  return ret;
}

static JSValue
js_encoder_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  TextEncoder* enc;

  if(!(enc = js_mallocz(ctx, sizeof(TextEncoder))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = textencoder_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_encoder_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  uint32_t defaultSize = 1024;

  if(argc > 1)
    JS_ToUint32(ctx, &defaultSize, argv[1]);

  if(argc >= 1) {
    const char* s = JS_ToCString(ctx, argv[0]);

    if(s[case_finds(s, "utf32")] || s[case_finds(s, "utf-32")])
      enc->char_set = UTF32;
    else if(s[case_finds(s, "utf16")] || s[case_finds(s, "utf-16")])
      enc->char_set = UTF16;
    else if(s[case_finds(s, "utf8")] || s[case_finds(s, "utf-8")])
      enc->char_set = UTF8;
    else
      return JS_ThrowInternalError(ctx, "TextEncoder '%s' is invalid s", s);

    if((int)enc->char_set > (int)UTF8)
      if(s[case_finds(s, "be")] || s[case_finds(s, "be")])
        enc->endian = BIG;

    JS_FreeCString(ctx, s);
  } else {
    enc->char_set = (UTFCharset)UTF8;
  }

  JS_SetOpaque(obj, enc);
  return obj;

fail:
  js_free(ctx, enc);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  ENCODER_ENCODE,
  ENCODER_ENCODE_INTO,
};

static JSValue
js_encoder_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  TextEncoder* enc;
  JSValue ret = JS_UNDEFINED;

  if(!(enc = js_encoder_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case ENCODER_ENCODE: {
      InputBuffer in = js_input_chars(ctx, argv[0]);
      DynBuf db = DBUF_INIT_CTX(ctx);

      if(textencoder_encode(enc, in, writer_from_dynbuf(&db), ctx) < 0) {
        ret = JS_EXCEPTION;
        break;
      }

      ret = textencoder_typedarray(enc->type_code, db.buf, db.size, ctx);
      dbuf_free(&db);
      break;
    }

    case ENCODER_ENCODE_INTO: {
      InputBuffer in = js_input_chars(ctx, argv[0]);
      OutputBuffer out = js_output_typedarray(ctx, argv[1]);
      ssize_t read;

      if((read = textencoder_encode(enc, in, writer_from_buf(&out), ctx)) < 0) {
        ret = JS_EXCEPTION;
        break;
      }

      ret = JS_NewObjectProto(ctx, JS_NULL);
      JS_SetPropertyStr(ctx, ret, "read", JS_NewUint32(ctx, read));
      JS_SetPropertyStr(ctx, ret, "written", JS_NewUint32(ctx, out.pos));

      inputbuffer_free(&in, ctx);
      outputbuffer_free(&out, ctx);
      break;
    }
  }

  return ret;
}

static JSValue
js_encoder_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  TextEncoder* enc;

  if(!(enc = js_encoder_data(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectClass(ctx, js_encoder_class_id);

  JS_DefinePropertyValueStr(ctx, obj, "encoding", JS_NewString(ctx, textcode_encodings[enc->type_code]), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "buffered", JS_NewUint32(ctx, enc->buflen), JS_PROP_ENUMERABLE);

  return obj;
}

static void
js_encoder_finalizer(JSRuntime* rt, JSValue val) {
  TextEncoder* enc;

  if((enc = JS_GetOpaque(val, js_encoder_class_id))) {
    js_free_rt(rt, enc);
  }
}

static JSClassDef js_encoder_class = {
    .class_name = "TextEncoder",
    .finalizer = js_encoder_finalizer,
};

static const JSCFunctionListEntry js_encoder_methods[] = {
    JS_CFUNC_MAGIC_DEF("encode", 1, js_encoder_method, ENCODER_ENCODE),
    JS_CFUNC_MAGIC_DEF("encodeInto", 2, js_encoder_method, ENCODER_ENCODE_INTO),
    JS_CGETSET_ENUMERABLE_DEF("encoding", js_encoder_get, 0, ENCODER_ENCODING),
    JS_CGETSET_MAGIC_DEF("endian", js_encoder_get, 0, ENCODER_ENDIANNESS),
    JS_CGETSET_MAGIC_DEF("buffered", js_encoder_get, 0, ENCODER_BUFFERED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TextEncoder", JS_PROP_CONFIGURABLE),
};

int
js_code_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_decoder_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_decoder_class_id, &js_decoder_class);

  textdecoder_ctor = JS_NewCFunction2(ctx, js_decoder_constructor, "TextDecoder", 1, JS_CFUNC_constructor, 0);
  textdecoder_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, textdecoder_proto, js_decoder_methods, countof(js_decoder_methods));
  JS_SetClassProto(ctx, js_decoder_class_id, textdecoder_proto);

  JS_SetConstructor(ctx, textdecoder_ctor, textdecoder_proto);

  JS_NewClassID(&js_encoder_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_encoder_class_id, &js_encoder_class);

  textencoder_ctor = JS_NewCFunction2(ctx, js_encoder_constructor, "TextEncoder", 1, JS_CFUNC_constructor, 0);
  textencoder_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, textencoder_proto, js_encoder_methods, countof(js_encoder_methods));
  JS_SetClassProto(ctx, js_encoder_class_id, textencoder_proto);

  JS_SetConstructor(ctx, textencoder_ctor, textencoder_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "TextDecoder", textdecoder_ctor);
    JS_SetModuleExport(ctx, m, "TextEncoder", textencoder_ctor);
  }

  return 0;
}

#ifdef JS_TEXTCODE_MODULE
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_textcode
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_code_init))) {
    JS_AddModuleExport(ctx, m, "TextDecoder");
    JS_AddModuleExport(ctx, m, "TextEncoder");
  }

  return m;
}

/**
 * @}
 */
