#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "quickjs-stringdecoder.h"
#include "utils.h"

#define max(a, b) ((a) > (b) ? (a) : (b))

thread_local VISIBLE JSClassID js_stringdecoder_class_id = 0;
thread_local JSValue stringdecoder_proto = {JS_TAG_UNDEFINED}, stringdecoder_ctor = {JS_TAG_UNDEFINED};

enum { STRINGDECODER_WRITE, STRINGDECODER_END };

StringDecoder*
js_stringdecoder_data(JSContext* ctx, JSValueConst value) {
  StringDecoder* dec;
  dec = JS_GetOpaque(value, js_stringdecoder_class_id);
  return dec;
}

static JSValue
js_stringdecoder_get(JSContext* ctx, JSValueConst this_val, int magic) {
  StringDecoder* dec;
  JSValue ret = JS_UNDEFINED;
  if(!(dec = js_stringdecoder_data(ctx, this_val)))
    return ret;
  switch(magic) {}
  return ret;
}

static JSValue
js_stringdecoder_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  StringDecoder* dec;
  JSValue ret = JS_UNDEFINED;
  if(!(dec = js_stringdecoder_data(ctx, this_val)))
    return ret;
  switch(magic) {}
  return ret;
}

JSValue
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

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_stringdecoder_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, dec);

  return obj;

fail:
  js_free(ctx, dec);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_stringdecoder_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  StringDecoder* dec;
  JSValue ret = JS_UNDEFINED;

  if(!(dec = js_stringdecoder_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

void
js_stringdecoder_finalizer(JSRuntime* rt, JSValue val) {
  StringDecoder* dec = JS_GetOpaque(val, js_stringdecoder_class_id);
  if(dec) {
    js_free_rt(rt, dec);
  }
  JS_FreeValueRT(rt, val);
}

static JSClassDef js_stringdecoder_class = {
    .class_name = "StringDecoder",
    .finalizer = js_stringdecoder_finalizer,
};

static const JSCFunctionListEntry js_stringdecoder_funcs[] = {
    JS_CFUNC_MAGIC_DEF("write", 1, js_stringdecoder_method, STRINGDECODER_WRITE),
    JS_CFUNC_MAGIC_DEF("end", 1, js_stringdecoder_method, STRINGDECODER_END),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StringDecoder", JS_PROP_CONFIGURABLE),
};

int
js_stringdecoder_init(JSContext* ctx, JSModuleDef* m) {

  if(js_stringdecoder_class_id == 0) {
    JS_NewClassID(&js_stringdecoder_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_stringdecoder_class_id, &js_stringdecoder_class);

    stringdecoder_ctor =
        JS_NewCFunction2(ctx, js_stringdecoder_constructor, "StringDecoder", 1, JS_CFUNC_constructor, 0);
    stringdecoder_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, stringdecoder_proto, js_stringdecoder_funcs, countof(js_stringdecoder_funcs));
    JS_SetClassProto(ctx, js_stringdecoder_class_id, stringdecoder_proto);

    // js_set_inspect_method(ctx, stringdecoder_proto, js_stringdecoder_inspect);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "StringDecoder", stringdecoder_ctor);
  }

  return 0;
}

#if 0 // def JS_SHARED_LIBRARY
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
  return m;
}
