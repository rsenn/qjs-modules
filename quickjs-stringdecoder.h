#ifndef QUICKJS_STRINGDECODER_H
#define QUICKJS_STRINGDECODER_H

#include <threads.h>
#include "ringbuffer.h"

typedef enum string_encoding { UNKNOWN = 0, UTF8, UTF16 } StringEncoding;

typedef struct string_decoder {
  RingBuffer buffer;
  StringEncoding encoding;
} StringDecoder;

extern thread_local JSClassID js_stringdecoder_class_id;
extern thread_local JSValue stringdecoder_proto, stringdecoder_ctor;
extern const char* const stringdecoder_encodings[3];

size_t stringdecoder_length(StringDecoder*);
JSValue stringdecoder_read(StringDecoder*, JSContext* ctx);
int js_stringdecoder_init(JSContext*, JSModuleDef* m);

static inline StringDecoder*
js_stringdecoder_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque(value, js_stringdecoder_class_id);
}

#endif /* defined(QUICKJS_STRINGDECODER_H) */
