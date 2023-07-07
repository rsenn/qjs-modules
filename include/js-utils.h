#ifndef JS_UTILS_H
#define JS_UTILS_H

#include <quickjs.h>
#include <cutils.h>

/**
 * \defgroup js-utils js-utils: Utilities for JS Promises
 * @{
 */
typedef union resolve_functions {
  JSValue array[2];
  struct {
    JSValue resolve, reject;
  };
} ResolveFunctions;

typedef struct promise {
  ResolveFunctions funcs;
  JSValue value;
} Promise;

ResolveFunctions* promise_new(JSContext*, JSValueConst*);
JSValue promise_create(JSContext*, ResolveFunctions*);
void js_resolve_functions_free(JSContext* ctx, ResolveFunctions* funcs);
void promise_free_funcs(JSRuntime* rt, ResolveFunctions* funcs);
void promise_free(JSRuntime*, Promise*);
BOOL promise_init(JSContext*, Promise*);
BOOL promise_resolve(JSContext*, ResolveFunctions*, JSValueConst);
BOOL promise_reject(JSContext*, ResolveFunctions*, JSValueConst);
void promise_zero(Promise*);
BOOL promise_pending(ResolveFunctions*);
BOOL promise_done(ResolveFunctions*);
JSValue promise_then(JSContext*, JSValueConst, JSValueConst);
JSValue promise_catch(JSContext*, JSValueConst, JSValueConst);
JSValue promise_forward(JSContext* ctx, JSValueConst promise, Promise* receiver);

/**
 * @}
 */
#endif /* defined(JS_UTILS_H) */
