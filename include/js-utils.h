#ifndef JS_UTILS_H
#define JS_UTILS_H

#include <stdbool.h>
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
bool promise_init(JSContext*, Promise*);
bool promise_resolve(JSContext*, ResolveFunctions*, JSValueConst);
bool promise_reject(JSContext*, ResolveFunctions*, JSValueConst);
void promise_zero(Promise*);
bool promise_pending(ResolveFunctions*);
bool promise_done(ResolveFunctions*);
JSValue promise_then(JSContext*, JSValueConst, JSValueConst);
JSValue promise_catch(JSContext*, JSValueConst, JSValueConst);
JSValue promise_forward(JSContext* ctx, JSValueConst promise, Promise* receiver);

/**
 * @}
 */
#endif /* defined(JS_UTILS_H) */
