#ifndef ITERATION_H
#define ITERATION_H

#include "utils.h"
#include <quickjs.h>
#include <assert.h>

/**
 * \defgroup iteration iteration: QuickJS iteration helpers
 * @{
 */
typedef struct Iteration {
  JSValue iter, next, data;
  BOOL done;
} Iteration;

#define ITERATION_INIT() \
  (Iteration) { \
    JS_UNINITIALIZED, JS_UNINITIALIZED, JS_UNINITIALIZED, FALSE \
  }

BOOL iteration_init(Iteration*, JSContext*, JSValueConst);
BOOL iteration_method_atom(Iteration*, JSContext*, JSValueConst, JSAtom);
BOOL iteration_method_symbol(Iteration*, JSContext*, JSValueConst, const char*);
void iteration_reset_rt(Iteration*, JSRuntime*);
BOOL iteration_next(Iteration*, JSContext*);
JSValueConst iteration_array(JSContext*, JSValueConst);

static inline void
iteration_reset(Iteration* it, JSContext* ctx) {
  iteration_reset_rt(it, JS_GetRuntime(ctx));
}

static inline JSValue
iteration_value(Iteration* it, JSContext* ctx) {
  assert(!it->done);

  return JS_GetPropertyStr(ctx, it->data, "value");
}

/**
 * @}
 */
#endif /* defined(ITERATION_H) */
