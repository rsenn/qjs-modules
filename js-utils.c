#include "js-utils.h"

static inline Promise
js_resolve_functions_zero(ResolveFunctions* funcs) {
  funcs->array[0] = JS_NULL;
  funcs->array[1] = JS_NULL;
}
static inline BOOL
js_resolve_functions_is_null(ResolveFunctions* funcs) {
  return JS_IsNull(funcs->array[0]) && JS_IsNull(funcs->array[1]);
}

static Promise
js_resolve_functions_free(JSContext* ctx, ResolveFunctions* funcs) {
  JS_FreeValue(ctx, funcs->array[0]);
  JS_FreeValue(ctx, funcs->array[1]);
  js_resolve_functions_zero(funcs);
}

static inline BOOL
js_resolve_functions_call(JSContext* ctx, ResolveFunctions* funcs, int index, JSValueConst arg) {
  JSValue ret = JS_UNDEFINED;
  if(!JS_IsNull(funcs->array[index])) {
    ret = JS_Call(ctx, funcs->array[index], JS_UNDEFINED, 1, &arg);
    js_resolve_functions_free(ctx, funcs);
    JS_FreeValue(ctx, ret);
    return TRUE;
  }
  return FALSE;
}

ResolveFunctions*
promise_new(JSContext* ctx, JSValue* promise) {
  ResolveFunctions* funcs;

  if(!(funcs = js_mallocz(ctx, sizeof(ResolveFunctions)))) {
    *promise = JS_ThrowOutOfMemory(ctx);
    return 0;
  }

  *promise = JS_NewPromiseCapability(ctx, funcs->array);

  return funcs;
}

JSValue
promise_create(JSContext* ctx, ResolveFunctions* funcs) {
  JSValue ret;

  ret = JS_NewPromiseCapability(ctx, funcs->array);
  return ret;
}

Promise
promise_free(JSContext* ctx, Promise* prom) {
  JS_FreeValue(ctx, prom->promise);
  prom->promise = JS_UNDEFINED;
  js_resolve_functions_free(ctx, &prom->funcs);
}

BOOL
promise_init(JSContext* ctx, Promise* prom) {
  prom->promise = JS_NewPromiseCapability(ctx, prom->funcs.array);
  return !JS_IsException(prom->promise);
}

BOOL
promise_resolve(JSContext* ctx, ResolveFunctions* funcs, JSValueConst value) {
  return js_resolve_functions_call(ctx, funcs, 0, value);
}

BOOL
promise_reject(JSContext* ctx, ResolveFunctions* funcs, JSValueConst value) {
  return js_resolve_functions_call(ctx, funcs, 1, value);
}

void
promise_zero(Promise* prom) {
  prom->promise = JS_UNDEFINED;
  js_resolve_functions_zero(&prom->funcs);
}

BOOL
promise_pending(Promise* prom) {
  return !JS_IsUndefined(prom->promise) && !js_resolve_functions_is_null(&prom->funcs);
}

BOOL
promise_done(Promise* prom) {
  return !JS_IsUndefined(prom->promise) && js_resolve_functions_is_null(&prom->funcs);
}

JSValue
promise_then(JSContext* ctx, JSValueConst promise, JSValueConst handler) {
  JSValue fn, ret;

  fn = JS_GetPropertyStr(ctx, promise, "then");
  ret = JS_Call(ctx, fn, promise, 1, &handler);
  JS_FreeValue(ctx, fn);

  return ret;
}

JSValue
promise_catch(JSContext* ctx, JSValueConst promise, JSValueConst handler) {
  JSValue fn, ret;

  fn = JS_GetPropertyStr(ctx, promise, "catch");
  ret = JS_Call(ctx, fn, promise, 1, &handler);
  JS_FreeValue(ctx, fn);

  return ret;
}
