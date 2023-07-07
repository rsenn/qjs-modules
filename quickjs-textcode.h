#ifndef QUICKJS_TEXTCODE_H
#define QUICKJS_TEXTCODE_H

#include "utils.h"
#include "ringbuffer.h"

/**
 * \defgroup quickjs-textdecoder quickjs-textdecoder: String Decoder
 * @{
 */

typedef enum utf_encoding { UNKNOWN = 0, UTF8, UTF16, UTF32 } UTFCharset;
typedef enum text_encoding {
  UTF16LE = 2,
  UTF32LE = 3,
  UTF16BE = 6,
  UTF32BE = 7,
  ISO_8859_1,
  ISO_8859_2,
  ISO_8859_3,
  ISO_8859_4,
  ISO_8859_5,
  ISO_8859_6,
  ISO_8859_7,
  ISO_8859_8,
  ISO_8859_9,
  ISO_8859_10,
  ISO_8859_11,
  ISO_8859_13,
  ISO_8859_14,
  ISO_8859_15,
  ISO_8859_16,
  WINDOWS_1250,
  WINDOWS_1251,
  WINDOWS_1252,
  WINDOWS_1253,
  WINDOWS_1254,
  WINDOWS_1255,
  WINDOWS_1256,
  WINDOWS_1257,
  WINDOWS_1258,
} TextEncoding;

#define TextcodeType \
  PACK union { \
    struct { \
      UTFCharset encoding : 2; \
      Endian endian : 1; \
    }; \
    TextEncoding type_code; \
  }; \
  ENDPACK

struct text_coder {
  RingBuffer buffer;
  TextcodeType
};

typedef struct text_coder TextEncoder;
typedef struct text_coder TextDecoder;

extern VISIBLE JSClassID js_decoder_class_id, js_encoder_class_id;
extern VISIBLE JSValue textdecoder_proto, textdecoder_ctor, textencoder_proto, textencoder_ctor;
extern const char* const textcode_encodings[];

size_t textdecoder_length(TextDecoder*);
JSValue textdecoder_read(TextDecoder*, JSContext* ctx);
int js_code_init(JSContext*, JSModuleDef* m);
size_t textencoder_length(TextEncoder*);
JSValue textencoder_read(TextEncoder*, JSContext* ctx);
int js_encoder_init(JSContext*, JSModuleDef* m);

static inline TextDecoder*
js_decoder_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque(value, js_decoder_class_id);
}

static inline TextEncoder*
js_encoder_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque(value, js_encoder_class_id);
}

/**
 * @}
 */
#endif /* defined(QUICKJS_TEXTCODE_H) */
