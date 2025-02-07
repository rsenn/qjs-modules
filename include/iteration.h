#ifndef ITERATION_H
#define ITERATION_H

#include "utils.h"
#include <stdbool.h>
#include <quickjs.h>
#include <assert.h>

/**
 * \defgroup iteration iteration: QuickJS iteration helpers
 * @{
 */
typedef struct Iteration {
  JSValue iter, next, data;
  bool done;
} Iteration;

static inline bool
iteration_init_free(Iteration* it, JSContext* ctx, JSValue iterator) {
  it->iter = iterator;
  it->next = JS_GetPropertyStr(ctx, it->iter, "next");
  it->data = JS_UNDEFINED;
  it->done = false;

  return JS_IsFunction(ctx, it->next);
}

static inline bool
iteration_init(Iteration* it, JSContext* ctx, JSValueConst iterator) {
  return iteration_init_free(it, ctx, JS_DupValue(ctx, iterator));
}

static inline bool
iteration_method_atom(Iteration* it, JSContext* ctx, JSValueConst object, JSAtom atom) {
  bool ret = false;
  JSValue method = JS_GetProperty(ctx, object, atom);

  if(JS_IsFunction(ctx, method)) {
    JSValue iterator = JS_Call(ctx, method, object, 0, 0);
    ret = iteration_init_free(it, ctx, iterator);
  }

  JS_FreeValue(ctx, method);
  return ret;
}

static inline bool
iteration_method_symbol(Iteration* it, JSContext* ctx, JSValueConst object, const char* sym) {
  JSAtom atom = js_symbol_static_atom(ctx, sym);
  bool ret = iteration_method_atom(it, ctx, object, atom);

  JS_FreeAtom(ctx, atom);
  return ret;
}

static inline void
iteration_reset_rt(Iteration* it, JSRuntime* rt) {
  if(!JS_IsUninitialized(it->iter))
    JS_FreeValueRT(rt, it->iter);

  if(!JS_IsUninitialized(it->next))
    JS_FreeValueRT(rt, it->next);

  if(!JS_IsUninitialized(it->data))
    JS_FreeValueRT(rt, it->data);

  it->data = it->iter = it->next = JS_UNINITIALIZED;
  it->done = false;
}

static inline void
iteration_reset(Iteration* it, JSContext* ctx) {
  iteration_reset_rt(it, JS_GetRuntime(ctx));
}

static inline bool
iteration_next(Iteration* it, JSContext* ctx) {
  assert(!it->done);

  if(JS_IsObject(it->data))
    JS_FreeValue(ctx, it->data);

  it->data = JS_Call(ctx, it->next, it->iter, 0, 0);
  it->done = js_get_propertystr_bool(ctx, it->data, "done");

  return it->done;
}

static inline JSValue
iteration_value(Iteration* it, JSContext* ctx) {
  assert(!it->done);

  return JS_GetPropertyStr(ctx, it->data, "value");
}

static inline const char*
iteration_valuestr(Iteration* it, JSContext* ctx) {
  JSValue value = iteration_value(it, ctx);
  const char* str = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);

  return str;
}

/**
 * @}
 */
#endif /* defined(ITERATION_H) */
