#include "async-closure.h"

static JSValue
asyncclosure_function(AsyncClosure* ac, CClosureFunc* func, int magic) {
  return js_function_cclosure(ac->ctx, func, 0, magic, asyncclosure_dup(ac), asyncclosure_free);
}

AsyncClosure*
asyncclosure_new(JSContext* ctx, int fd, AsyncEvent state, JSValueConst this_val, CClosureFunc* func) {
  AsyncClosure* ac;

  if(!(ac = js_malloc(ctx, sizeof(AsyncClosure))))
    return 0;

  ac->ref_count = 1;
  ac->fd = fd;
  ac->state = 0;
  ac->ccfunc = func;
  ac->ctx = ctx;
  ac->result = JS_DupValue(ctx, this_val);
  ac->set_handler = JS_NULL;

  promise_init(ctx, &ac->promise);

  ac->opaque = NULL;
  ac->opaque_free = NULL;

  if(state)
    asyncclosure_change_event(ac, state);

  return ac;
}

JSValue
asyncclosure_promise(AsyncClosure* ac) {
  return JS_DupValue(ac->ctx, ac->promise.value);
}

AsyncClosure*
asyncclosure_dup(AsyncClosure* ac) {
  ++ac->ref_count;
  return ac;
}

static void
asyncclosure_free_opaque(AsyncClosure* ac) {
  if(ac->opaque && ac->opaque_free)
    ac->opaque_free(ac->ctx, ac->opaque);
}

void
asyncclosure_set_opaque(AsyncClosure* ac, void* opaque, void (*opaque_free)(JSContext* ctx, void*)) {

  if(ac->opaque)
    asyncclosure_free_opaque(ac);

  ac->opaque = opaque;
  ac->opaque_free = opaque_free;
}

void
asyncclosure_free(void* ptr) {
  AsyncClosure* ac = ptr;

  if(--ac->ref_count == 0) {
    JSContext* ctx = ac->ctx;

    JS_FreeValue(ctx, ac->result);
    JS_FreeValue(ctx, ac->set_handler);

    promise_free(JS_GetRuntime(ctx), &ac->promise);
    asyncclosure_free_opaque(ac);

    js_free(ctx, ac);
  }
}

void
asyncclosure_resolve(AsyncClosure* ac) {
  promise_resolve(ac->ctx, &ac->promise.funcs, ac->result);
  asyncclosure_done(ac);
}

void
asyncclosure_error(AsyncClosure* ac, JSValueConst obj) {
  promise_reject(ac->ctx, &ac->promise.funcs, obj);
  asyncclosure_done(ac);
}

void
asyncclosure_done(AsyncClosure* ac) {
  asyncclosure_change_event(ac, WANT_NONE);
}

BOOL
asyncclosure_change_event(AsyncClosure* ac, AsyncEvent new_state) {
  new_state &= 0b11u;

  if(ac->state != new_state) {
    JSContext* ctx = ac->ctx;

    /* Had a previous handler? */
    if(ac->state) {
      if(!js_iohandler_set(ctx, ac->set_handler, ac->fd, JS_NULL))
        promise_reject(ctx, &ac->promise.funcs, JS_GetException(ctx));

      JS_FreeValue(ctx, ac->set_handler);
    }

    ac->state = new_state;
    ac->set_handler = new_state == 0 ? JS_NULL : js_iohandler_fn(ctx, 0 != (new_state & WANT_WRITE));

    if(new_state)
      if(!js_iohandler_set(ctx, ac->set_handler, ac->fd, asyncclosure_function(ac, ac->ccfunc, new_state)))
        promise_reject(ctx, &ac->promise.funcs, JS_GetException(ctx));

    return TRUE;
  }

  return FALSE;
}
