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

BOOL
promise_done(ResolveFunctions* funcs) {
  return js_resolve_functions_is_null(funcs);
}

JSValue
promise_then2(JSContext* ctx, JSValueConst promise, JSValueConst resolve, JSValueConst reject) {
  JSValue fn, ret, args[2] = {resolve, reject};

  fn = JS_GetPropertyStr(ctx, promise, "then");
  ret = JS_Call(ctx, fn, promise, countof(args), args);
  JS_FreeValue(ctx, fn);

  return ret;
}

/**
 * @}
 */
