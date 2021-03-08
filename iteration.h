#ifndef ITERATION_H
#define ITERATION_H

#define _GNU_SOURCE

#include "quickjs.h"
#include "utils.h"

#include <assert.h>

typedef struct Iteration {
  JSValue iter, next, data;
  BOOL done;
} Iteration;

static inline BOOL
iteration_init_free(Iteration* it, JSContext* ctx, JSValue iterator) {
  it->iter = iterator;
  it->next = JS_GetPropertyStr(ctx, it->iter, "next");
  it->data = JS_UNDEFINED;
  it->done = FALSE;
  return JS_IsFunction(ctx, it->next);
}

static inline BOOL
iteration_init(Iteration* it, JSContext* ctx, JSValueConst iterator) {
  return iteration_init_free(it, ctx, JS_DupValue(ctx, iterator));
}

static inline BOOL
iteration_method(Iteration* it, JSContext* ctx, JSValueConst object, JSAtom atom) {
  BOOL ret = FALSE;
  JSValue method, iterator;
  method = JS_GetProperty(ctx, object, atom);
  if(JS_IsFunction(ctx, method)) {
    iterator = JS_Call(ctx, method, object, 0, 0);
    ret = iteration_init_free(it, ctx, iterator);
  }
  JS_FreeValue(ctx, method);
  return ret;
}

static inline BOOL
iteration_method_str(Iteration* it,
                     JSContext* ctx,
                     JSValueConst object,
                     const char* str) {
  JSAtom atom;
  BOOL ret;
  atom = JS_NewAtom(ctx, str);
  ret = iteration_method(it, ctx, object, atom);
  JS_FreeAtom(ctx, atom);
  return ret;
}

static inline BOOL
iteration_method_symbol(Iteration* it,
                        JSContext* ctx,
                        JSValueConst object,
                        const char* sym) {
  JSAtom atom;
  BOOL ret;
  atom = js_symbol_atom(ctx, sym);
  ret = iteration_method(it, ctx, object, atom);
  JS_FreeAtom(ctx, atom);
  return ret;
}

static inline void
iteration_reset(Iteration* it, JSRuntime* rt) {
  if(JS_IsObject(it->iter))
    JS_FreeValueRT(rt, it->iter);
  if(JS_IsObject(it->next))
    JS_FreeValueRT(rt, it->next);
  if(JS_IsObject(it->data))
    JS_FreeValueRT(rt, it->data);
  it->data = it->iter = it->next = JS_UNDEFINED;
  it->done = FALSE;
}

static inline BOOL
iteration_next(Iteration* it, JSContext* ctx) {
  assert(!it->done);
  if(JS_IsObject(it->data))
    JS_FreeValue(ctx, it->data);
  it->data = JS_Call(ctx, it->next, it->iter, 0, 0);
  it->done = js_object_propertystr_bool(ctx, it->data, "done");
  return it->done;
}

static inline JSValue
iteration_value(Iteration* it, JSContext* ctx) {
  JSValue value;
  assert(!it->done);
  value = JS_GetPropertyStr(ctx, it->data, "value");
  return value;
}

static inline const char*
iteration_valuestr(Iteration* it, JSContext* ctx) {
  JSValue value = iteration_value(it, ctx);
  const char* str = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);
  return str;
}

#endif /* defined(ITERATION_H) */