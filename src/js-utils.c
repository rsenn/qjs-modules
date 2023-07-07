#include "js-utils.h"
#include "defines.h"
#include "utils.h"

/**
 * \addtogroup js-utils
 * @{
 */
static inline void
js_resolve_functions_zero(ResolveFunctions* funcs) {
  funcs->array[0] = JS_NULL;
  funcs->array[1] = JS_NULL;
}

static inline BOOL
js_resolve_functions_is_null(ResolveFunctions* funcs) {
  return JS_IsNull(funcs->array[0]) && JS_IsNull(funcs->array[1]);
}

void
js_resolve_functions_free(JSContext* ctx, ResolveFunctions* funcs) {
  JS_FreeValue(ctx, funcs->array[0]);
  JS_FreeValue(ctx, funcs->array[1]);
  js_resolve_functions_zero(funcs);
}

void
promise_free_funcs(JSRuntime* rt, ResolveFunctions* funcs) {
  JS_FreeValueRT(rt, funcs->array[0]);
  JS_FreeValueRT(rt, funcs->array[1]);
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
    *promise = JS_EXCEPTION;
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

/*void
promise_free(JSContext* ctx, Promise* pr) {
  JS_FreeValue(ctx, pr->value);
  pr->value = JS_UNDEFINED;
  js_resolve_functions_free(ctx, &pr->funcs);
}*/

void
promise_free(JSRuntime* rt, Promise* pr) {
  JS_FreeValueRT(rt, pr->value);
  pr->value = JS_UNDEFINED;
  promise_free_funcs(rt, &pr->funcs);
}

BOOL
promise_init(JSContext* ctx, Promise* pr) {
  pr->value = JS_NewPromiseCapability(ctx, pr->funcs.array);
  return !JS_IsException(pr->value);
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
promise_zero(Promise* pr) {
  pr->value = JS_UNDEFINED;
  js_resolve_functions_zero(&pr->funcs);
}

BOOL
promise_pending(ResolveFunctions* funcs) {
  return !js_resolve_functions_is_null(funcs);
}

BOOL
promise_done(ResolveFunctions* funcs) {
  return js_resolve_functions_is_null(funcs);
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

JSValue
promise_forward(JSContext* ctx, JSValueConst promise, Promise* receiver) {
  JSValue ret, ret2;

  ret = promise_then(ctx, promise, receiver->funcs.array[0]);
  ret2 = promise_catch(ctx, ret, receiver->funcs.array[1]);
  JS_FreeValue(ctx, ret);
  return ret2;
}

/**
 * @}
 */
