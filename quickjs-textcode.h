#ifndef QUICKJS_TEXTDECODER_H
#define QUICKJS_TEXTDECODER_H

#include "include/utils.h"
#include "include/ringbuffer.h"

/**
 * \defgroup quickjs-textdecoder QuickJS module: textdecoder - String Decoder
 * @{
 */

typedef enum string_encoding { UNKNOWN = 0, UTF8, UTF16, UTF32 } Encoding;

typedef struct string_decoder {
  RingBuffer buffer;
  PACK union {
    struct {
      Encoding encoding : 2;
      Endian endian : 1;
    };
    unsigned type_code : 3;
  };
  ENDPACK
} TextDecoder;

typedef struct string_encoder {
  RingBuffer buffer;
  PACK union {
    struct {
      Encoding encoding : 2;
      Endian endian : 1;
    };
    unsigned type_code : 3;
  };
  ENDPACK
} TextEncoder;

extern thread_local JSClassID js_decoder_class_id, js_encoder_class_id;
extern thread_local JSValue textdecoder_proto, textdecoder_ctor, textencoder_proto, textencoder_ctor;
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
#endif /* defined(QUICKJS_TEXTDECODER_H) */
