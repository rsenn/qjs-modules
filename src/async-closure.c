#include "async-closure.h"

struct AsyncClosure*
asyncclosure_new(JSContext* ctx, int fd, enum event_flag state, JSValueConst this_val, JSValue* ret, CClosureFunc* func) {
  struct AsyncClosure* ac;

  if(!(ac = js_malloc(ctx, sizeof(struct AsyncClosure))))
    return 0;

  ac->ref_count = 1;
  ac->fd = 1;
  ac->ctx = ctx;
  ac->obj = JS_DupValue(ctx, this_val);
  ac->state = state;
  ac->set_handler = js_iohandler_fn(ctx, 0 != (state & WANT_WRITE));

  *ret = promise_create(ctx, &ac->fns);

  ac->opaque = NULL;
  ac->opaque_free = NULL;
  ac->ccfunc = func;

  return ac;
}

struct AsyncClosure*
asyncclosure_dup(struct AsyncClosure* ac) {
  ++ac->ref_count;
  return ac;
}

void
asyncclosure_freeopaque(struct AsyncClosure* ac) {
  if(ac->opaque && ac->opaque_free)
    ac->opaque_free(ac->ctx, ac->opaque);
}

void
asyncclosure_setopaque(struct AsyncClosure* ac, void* opaque, void (*opaque_free)(JSContext* ctx, void*)) {

  if(ac->opaque)
    asyncclosure_freeopaque(ac);

  ac->opaque = opaque;
  ac->opaque_free = opaque_free;
}

void
asyncclosure_free(void* ptr) {
  struct AsyncClosure* ac = ptr;

  if(--ac->ref_count == 0) {
    JSContext* ctx = ac->ctx;

    JS_FreeValue(ctx, ac->obj);
    JS_FreeValue(ctx, ac->set_handler);

    promise_free_funcs(JS_GetRuntime(ctx), &ac->fns);
    asyncclosure_freeopaque(ac);

    js_free(ctx, ac);
  }
}

JSValue
asyncclosure_function(struct AsyncClosure* ac, CClosureFunc* func, int magic) {
  return js_function_cclosure(ac->ctx, func, 0, magic, asyncclosure_dup(ac), asyncclosure_free);
}

BOOL
asyncclosure_setmode(struct AsyncClosure* ac, enum event_flag new_state) {
  new_state &= 0b11u;

  if(ac->state != new_state) {
    JSContext* ctx = ac->ctx;

    js_iohandler_set(ctx, ac->set_handler, ac->fd, JS_NULL);
    JS_FreeValue(ctx, ac->set_handler);
    ac->state = new_state;
    ac->set_handler = new_state == 0 ? JS_NULL : js_iohandler_fn(ctx, 0 != (new_state & WANT_WRITE));

    if(new_state)
      js_iohandler_set(ctx, ac->set_handler, ac->fd, asyncclosure_function(ac, ac->ccfunc, new_state));

    return TRUE;
  }

  return FALSE;
}
