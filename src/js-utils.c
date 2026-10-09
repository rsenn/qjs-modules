/* js-utils.c: promise_*(), promises settled from C.
 * depends on: utils.h (js_invoke, js_is_promise).
 * rule: the resolving functions are freed as soon as one of them ran. */
#include "js-utils.h"
#include "utils.h"

/**
 * \addtogroup js-utils
 * @{
 */
static void
resolve_functions_zero(union ResolveFunctions* funcs) {
  funcs->array[0] = JS_NULL;
  funcs->array[1] = JS_NULL;
}

static BOOL
resolve_functions_is_null(const union ResolveFunctions* funcs) {
  return JS_IsNull(funcs->array[0]) && JS_IsNull(funcs->array[1]);
}

static void
resolve_functions_free(JSContext* ctx, union ResolveFunctions* funcs) {
  JS_FreeValue(ctx, funcs->array[0]);
  JS_FreeValue(ctx, funcs->array[1]);
  resolve_functions_zero(funcs);
}

/* calls funcs[index](arg), then frees both functions.
 * returns TRUE if called, FALSE if spent or the call threw (exception pending). */
static BOOL
resolve_functions_call(JSContext* ctx, union ResolveFunctions* funcs, int index, JSValueConst arg) {
  JSValue ret;
  BOOL ok;

  if(JS_IsNull(funcs->array[index]))
    return FALSE;

  ret = JS_Call(ctx, funcs->array[index], JS_UNDEFINED, 1, &arg);
  ok = !JS_IsException(ret);

  resolve_functions_free(ctx, funcs);
  JS_FreeValue(ctx, ret);
  return ok;
}

BOOL
promise_init(JSContext* ctx, Promise* pr) {
  resolve_functions_zero(&pr->funcs);
  pr->value = JS_NewPromiseCapability(ctx, pr->funcs.array);

  if(JS_IsException(pr->value)) {
    resolve_functions_zero(&pr->funcs);
    return FALSE;
  }

  return TRUE;
}

BOOL
promise_resolve(JSContext* ctx, Promise* pr, JSValueConst value) {
  return resolve_functions_call(ctx, &pr->funcs, 0, value);
}

BOOL
promise_reject(JSContext* ctx, Promise* pr, JSValueConst value) {
  return resolve_functions_call(ctx, &pr->funcs, 1, value);
}

BOOL
promise_done(Promise* pr) {
  return resolve_functions_is_null(&pr->funcs);
}

void
promise_free(JSContext* ctx, Promise* pr) {
  resolve_functions_free(ctx, &pr->funcs);
  JS_FreeValue(ctx, pr->value);
  pr->value = JS_UNDEFINED;
}

JSValue
promise_immediate(JSContext* ctx, BOOL reject, JSValueConst value) {
  Promise pr;

  if(!promise_init(ctx, &pr))
    return JS_EXCEPTION;

  if(!resolve_functions_call(ctx, &pr.funcs, !!reject, value)) {
    resolve_functions_free(ctx, &pr.funcs);
    JS_FreeValue(ctx, pr.value);
    return JS_EXCEPTION;
  }

  return pr.value;
}

JSValue
promise_wrap(JSContext* ctx, JSValueConst value_or_promise) {
  if(js_is_promise(ctx, value_or_promise))
    return JS_DupValue(ctx, value_or_promise);

  return promise_immediate(ctx, FALSE, value_or_promise);
}

JSValue
promise_then(JSContext* ctx, JSValueConst promise, JSValueConst resolved) {
  return js_invoke(ctx, promise, "then", 1, &resolved);
}

JSValue
promise_then2(JSContext* ctx, JSValueConst promise, JSValueConst resolved, JSValueConst rejected) {
  JSValueConst args[] = {resolved, rejected};

  return js_invoke(ctx, promise, "then", 2, args);
}

JSValue
promise_catch(JSContext* ctx, JSValueConst promise, JSValueConst rejected) {
  return js_invoke(ctx, promise, "catch", 1, &rejected);
}

/**
 * @}
 */
