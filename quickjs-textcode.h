#ifndef QUICKJS_TEXTDECODER_H
#define QUICKJS_TEXTDECODER_H

#include "utils.h"
#include "ringbuffer.h"

/**
 * \defgroup quickjs-textdecoder QuickJS module: textdecoder - String Decoder
 * @{
 */

typedef enum string_encoding { UNKNOWN = 0, UTF8, UTF16 } StringEncoding;

typedef struct string_decoder {
  RingBuffer buffer;
  StringEncoding encoding;
} TextDecoder;

typedef struct string_encoder {
  RingBuffer buffer;
  StringEncoding encoding;
} TextEncoder;

extern thread_local JSClassID js_textdecoder_class_id,js_textencoder_class_id;
extern thread_local JSValue textdecoder_proto, textdecoder_ctor,textencoder_proto, textencoder_ctor;
extern const char* const textcode_encodings[3];

size_t textdecoder_length(TextDecoder*);
JSValue textdecoder_read(TextDecoder*, JSContext* ctx);
int js_textcode_init(JSContext*, JSModuleDef* m);
size_t textencoder_length(TextEncoder*);
JSValue textencoder_read(TextEncoder*, JSContext* ctx);
int js_textencoder_init(JSContext*, JSModuleDef* m);

static inline TextDecoder*
js_textdecoder_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque(value, js_textdecoder_class_id);
} 

static inline TextEncoder*
js_textencoder_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque(value, js_textencoder_class_id);
}

/**
 * @}
 */
#endif /* defined(QUICKJS_TEXTDECODER_H) */
