#define _GNU_SOURCE

#include "cutils.h"
#include "property-enumeration.h"
#include "quickjs.h"
#include "utils.h"
#include "pointer.h"

#include <stdint.h>

static JSValue
js_deep_find(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;
  JSValueConst this_arg = argc > 2 ? argv[2] : JS_UNDEFINED;
  PropertyEnumeration* it;
  vector frames;

  if(!JS_IsFunction(ctx, argv[1]))
    return JS_ThrowTypeError(ctx, "argument 1 (predicate) is not a function");
  vector_init(&frames);

  it = property_enumeration_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);
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
  Pointer* ptr;
  JSValue ret;
  if(!(ptr = pointer_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  pointer_from(ptr, ctx, argv[1], 0);
  ret = pointer_deref(ptr, ctx, argv[0]);
  pointer_free(ptr, ctx);
  return ret;
}

static JSValue
js_deep_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Pointer* ptr;
  JSValue obj;
  JSAtom prop;
  if(!(ptr = pointer_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  pointer_from(ptr, ctx, argv[1], 0);
  prop = pointer_pop(ptr);
  obj = pointer_acquire(ptr, ctx, argv[0]);

  if(!JS_IsException(obj))
    JS_SetProperty(ctx, obj, prop, argv[2]);
  JS_FreeAtom(ctx, prop);
  pointer_free(ptr, ctx);
  // return JS_UNDEFINED;
  return JS_DupValue(ctx, obj);
}

static JSValue
js_deep_unset(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Pointer* ptr;
  JSValue obj;
  JSAtom prop;
  if(!(ptr = pointer_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  pointer_from(ptr, ctx, argv[1], 0);
  prop = pointer_pop(ptr);
  obj = pointer_deref(ptr, ctx, argv[0]);

  if(!JS_IsException(obj))
    JS_DeleteProperty(ctx, obj, prop, 0);

  JS_FreeAtom(ctx, prop);
  pointer_free(ptr, ctx);
  return JS_DupValue(ctx, obj);
}

static JSValue
js_deep_clone(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
}

static const JSCFunctionListEntry js_deep_funcs[] = {JS_CFUNC_DEF("find", 2, js_deep_find),
                                                     JS_CFUNC_DEF("get", 2, js_deep_get),
                                                     JS_CFUNC_DEF("set", 3, js_deep_set),
                                                     JS_CFUNC_DEF("unset", 2, js_deep_unset),
                                                     JS_CFUNC_DEF("clone", 1, js_deep_clone)};

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
