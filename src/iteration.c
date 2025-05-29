#include "iteration.h"

BOOL
iteration_init_free(Iteration* it, JSContext* ctx, JSValue iterator) {
  JSValue next = JS_GetPropertyStr(ctx, iterator, "next");

  if(JS_IsFunction(ctx, next)) {
    it->iter = iterator;
    it->next = next;
    it->data = JS_UNDEFINED;
    it->done = FALSE;
    return TRUE;
  }

  JS_FreeValue(ctx, next);
  return FALSE;
}

BOOL
iteration_method_atom(Iteration* it, JSContext* ctx, JSValueConst object, JSAtom atom) {
  BOOL ret = FALSE;
  JSValue method = JS_GetProperty(ctx, object, atom);

  if(JS_IsFunction(ctx, method)) {
    JSValue iterator = JS_Call(ctx, method, object, 0, 0);
    ret = iteration_init_free(it, ctx, iterator);
  }

  JS_FreeValue(ctx, method);
  return ret;
}

BOOL
iteration_method_symbol(Iteration* it, JSContext* ctx, JSValueConst object, const char* sym) {
  JSAtom atom = js_symbol_static_atom(ctx, sym);
  BOOL ret = iteration_method_atom(it, ctx, object, atom);

  JS_FreeAtom(ctx, atom);
  return ret;
}

void
iteration_reset_rt(Iteration* it, JSRuntime* rt) {
  if(!JS_IsUninitialized(it->iter))
    JS_FreeValueRT(rt, it->iter);

  if(!JS_IsUninitialized(it->next))
    JS_FreeValueRT(rt, it->next);

  if(!JS_IsUninitialized(it->data))
    JS_FreeValueRT(rt, it->data);

  it->data = it->iter = it->next = JS_UNINITIALIZED;
  it->done = FALSE;
}

BOOL
iteration_next(Iteration* it, JSContext* ctx) {
  assert(!it->done);

  if(JS_IsObject(it->data))
    JS_FreeValue(ctx, it->data);

  it->data = JS_Call(ctx, it->next, it->iter, 0, 0);
  it->done = js_get_propertystr_bool(ctx, it->data, "done");

  return it->done;
}

JSValue
iteration_array(JSContext* ctx, JSValueConst iterable) {
  Iteration iter = ITERATION_INIT();
  JSValue ret;
  uint32_t i = 0;

  if(!iteration_method_symbol(&iter, ctx, iterable, "iterator")) {
    ret = JS_ThrowInternalError(ctx, "Failed to iterate");
  } else {
    ret = JS_NewArray(ctx);

    while(!iteration_next(&iter, ctx)) {
      JSValue value = iteration_value(&iter, ctx);

      JS_SetPropertyUint32(ctx, ret, i++, value);
    }
  }

  iteration_reset(&iter, ctx);
  return ret;
}
