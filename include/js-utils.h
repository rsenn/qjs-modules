#ifndef JS_UTILS_H
#define JS_UTILS_H

#include <quickjs.h>
#include <cutils.h>

/**
 * \defgroup js-utils js-utils: Utilities for JS Promises
 * @{
 */

/* the two functions `new Promise(...)` hands out; JS_NULL once spent. */
union ResolveFunctions {
  JSValue array[2];
  struct {
    JSValue resolve, reject;
  };
};

/* a pending promise and the means to settle it from C.
 *
 * refcount: `value` and `funcs` are owned; settling frees `funcs`. */
typedef struct promise {
  union ResolveFunctions funcs;
  JSValue value;
} Promise;

/* creates the promise: `value`, `funcs` filled.
 * returns TRUE, or FALSE with an exception pending and `funcs` spent. */
BOOL promise_init(JSContext*, Promise*);

/* settles the promise; only the first call has an effect.
 *
 * returns TRUE if settled now, FALSE if it was already settled or the
 * call threw (exception pending). */
BOOL promise_resolve(JSContext*, Promise*, JSValueConst);
BOOL promise_reject(JSContext*, Promise*, JSValueConst);

/* tells whether the promise was settled. never throws. */
BOOL promise_done(Promise*);

/* releases `value` and any resolving function left; a promise still
 * pending stays pending forever. never throws. */
void promise_free(JSContext*, Promise*);

/* a promise already resolved (or, with `reject`, rejected) with `arg`.
 * returns a new reference, or JS_EXCEPTION with an exception pending. */
JSValue promise_immediate(JSContext*, BOOL reject, JSValueConst arg);

/* `promise.then(resolved)`, `promise.then(resolved, rejected)`,
 * `promise.catch(rejected)`.
 * returns the new promise, or JS_EXCEPTION with an exception pending. */
JSValue promise_then(JSContext*, JSValueConst promise, JSValueConst resolved);
JSValue promise_then2(JSContext*, JSValueConst promise, JSValueConst resolved, JSValueConst rejected);
JSValue promise_catch(JSContext*, JSValueConst promise, JSValueConst rejected);

/* `Promise.resolve(value_or_promise)`: a promise is passed through.
 * returns a new reference, or JS_EXCEPTION with an exception pending. */
JSValue promise_wrap(JSContext*, JSValueConst value_or_promise);

/* a new pending promise; on failure `value` is JS_EXCEPTION. */
static inline Promise
promise_new(JSContext* ctx) {
  Promise pr;

  promise_init(ctx, &pr);
  return pr;
}

/* `Promise.resolve(promise).then(resolved)`.
 * returns the new promise, or JS_EXCEPTION with an exception pending. */
static inline JSValue
promise_resolve_then(JSContext* ctx, JSValueConst promise, JSValueConst resolved) {
  JSValue tmp = promise_immediate(ctx, FALSE, promise), ret;

  if(JS_IsException(tmp))
    return tmp;

  ret = promise_then(ctx, tmp, resolved);
  JS_FreeValue(ctx, tmp);
  return ret;
}

/**
 * @}
 */
#endif /* defined(JS_UTILS_H) */
