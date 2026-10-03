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

void js_resolve_functions_free(JSContext* ctx, ResolveFunctions* funcs);
void promise_free_funcs(JSRuntime* rt, ResolveFunctions* funcs);
BOOL promise_init(JSContext*, Promise*);
BOOL promise_resolve(JSContext*, ResolveFunctions*, JSValueConst);
BOOL promise_reject(JSContext*, ResolveFunctions*, JSValueConst);
BOOL promise_done(ResolveFunctions*);
JSValue promise_then2(JSContext*, JSValueConst, JSValueConst, JSValueConst);

/**
 * @}
 */
#endif /* defined(JS_UTILS_H) */
