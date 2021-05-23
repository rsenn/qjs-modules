#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "quickjs-internal.h"
#include "utils.h"

static void
js_string_free_func(JSRuntime* rt, void* opaque, void* ptr) {
  JSValue value = js_cstring_value(ptr);

  JS_FreeValueRT(rt, value);
}

static JSValue
js_misc_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;
  if(js_is_arraybuffer(ctx, argv[0])) {
    uint8_t* data;
    size_t len;
    if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
      ret = JS_NewStringLen(ctx, (const char*)data, len);
    }
  }
  return ret;
}

static JSValue
js_misc_toarraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;

  if(JS_IsString(argv[0])) {
    JSValue value = JS_DupValue(ctx, argv[0]);
    size_t len;
    const char* str;
    if((str = JS_ToCStringLen(ctx, &len, value))) {
      ret = JS_NewArrayBuffer(ctx, str, len, js_string_free_func, 0, FALSE);
    }
  }

  return ret;
}

static const JSCFunctionListEntry js_misc_funcs[] = {
    JS_CFUNC_DEF("toString", 1, js_misc_tostring),
    JS_CFUNC_DEF("toArrayBuffer", 1, js_misc_toarraybuffer),
};

static int
js_misc_init(JSContext* ctx, JSModuleDef* m) {

  if(m) {
    JS_SetModuleExportList(ctx, m, js_misc_funcs, countof(js_misc_funcs));
  }

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
  JS_AddModuleExportList(ctx, m, js_misc_funcs, countof(js_misc_funcs));
  return m;
}
