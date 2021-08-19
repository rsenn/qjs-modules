#ifndef QUICKJS_STRING_DECODER_H
#define QUICKJS_STRING_DECODER_H

#include <quickjs.h>
#include <cutils.h>
#include "utils.h"

typedef struct string_decoder {
  DynBuf input;
} StringDecoder;

extern thread_local JSClassID js_stringdecoder_class_id;

StringDecoder* js_stringdecoder_data(JSContext* ctx, JSValue value);
JSValue js_stringdecoder_constructor(JSContext* ctx, JSValue new_target, int argc, JSValue argv[]);
void js_stringdecoder_finalizer(JSRuntime* rt, JSValue val);
int js_stringdecoder_init(JSContext* ctx, JSModuleDef* m);
JSModuleDef* js_init_module_stringdecoder(JSContext* ctx, const char* module_name);

#endif /* defined(QUICKJS_STRING_DECODER_H) */
