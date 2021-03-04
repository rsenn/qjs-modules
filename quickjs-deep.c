#define _GNU_SOURCE

#include "cutils.h"
#include "property-enumeration.h"
#include "quickjs.h"
#include "utils.h"
#include "vector.h"

#include <stdint.h>

static JSValue
js_deep_find(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED, root;
  JSValueConst this_arg = JS_UNDEFINED;
  PropertyEnumeration* it;
  vector frames;
  vector_init(&frames);

  if(!JS_IsFunction(ctx, argv[1]))
    return JS_ThrowTypeError(ctx, "argument 1 (predicate) is not a function");

  if(argc > 2)
    this_arg = argv[2];

  root = JS_DupValue(ctx, argv[0]);
  it = property_enumeration_push(&frames, ctx, root, PROPENUM_DEFAULT_FLAGS);
  while((it = property_enumeration_recurse(&frames, ctx))) {
    BOOL result = property_enumeration_predicate(it, ctx, argv[1], this_arg);
    if(result) {
      ret = property_enumeration_value(it, ctx);
      break;
    }
  }
  property_enumeration_free(&frames, JS_GetRuntime(ctx));
  return ret;
}

static JSValue
js_deep_get(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
}

static JSValue
js_deep_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
}

static const JSCFunctionListEntry js_deep_funcs[] = {JS_CFUNC_DEF("find", 2, js_deep_find),
                                                     JS_CFUNC_DEF("get", 2, js_deep_get),
                                                     JS_CFUNC_DEF("set", 3, js_deep_set)};

static int
js_deep_init(JSContext* ctx, JSModuleDef* m) {
  JS_SetModuleExportList(ctx, m, js_deep_funcs, countof(js_deep_funcs));
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_deep
#endif

JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_deep_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_deep_funcs, countof(js_deep_funcs));
  JS_AddModuleExport(ctx, m, "default");
  return m;
}
