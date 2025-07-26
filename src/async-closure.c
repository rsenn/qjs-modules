#include "async-closure.h"
#include <assert.h>

/**
 * \addtogroup async-closure
 * @{
 */

VISIBLE struct list_head asyncclosure_list;
VISIBLE JSClassID js_asyncclosure_class_id = 0;

AsyncClosure*
asyncclosure_lookup(int fd) {
  struct list_head* el;

  list_for_each(el, &asyncclosure_list) {
    AsyncClosure* ac = list_entry(el, AsyncClosure, link);

    if(ac->fd >= 0)
      if(ac->fd == fd)
        return ac;
  }

  return NULL;
}

static JSValue
asyncclosure_function(AsyncClosure* ac, CClosureFunc* func, int magic) {
  return js_function_cclosure(ac->ctx, func, 0, magic, asyncclosure_dup(ac), asyncclosure_free);
}

AsyncClosure*
asyncclosure_new(JSContext* ctx, int fd, AsyncEvent state, JSValueConst this_val, CClosureFunc* func) {
  AsyncClosure* ac;

  if(asyncclosure_list.prev == NULL && asyncclosure_list.next == NULL)
    init_list_head(&asyncclosure_list);

  //assert((ac = asyncclosure_lookup(fd)) == NULL || ac->state == 0);

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

  list_add(&ac->link, &asyncclosure_list);

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

void
asyncclosure_opaque(AsyncClosure* ac, void* opaque, void (*opaque_free)(JSRuntime*, void*)) {
  assert(ac->opaque == NULL);
  assert(ac->opaque_free == NULL);

  ac->opaque = opaque;
  ac->opaque_free = opaque_free;
}

void
asyncclosure_free(JSRuntime* rt, void* ptr) {
  AsyncClosure* ac = ptr;

  if(--ac->ref_count == 0) {
    JSContext* ctx = ac->ctx;

    if(ac->state)
      printf("WARNING: %s() has still a handler for fd %d\n", __func__, ac->fd);

    asyncclosure_done(ac);
    JS_FreeValue(ctx, ac->result);
    JS_FreeValue(ctx, ac->promise.value);

    // promise_free(JS_GetRuntime(ctx), &ac->promise);

    if(ac->opaque && ac->opaque_free) {
      ac->opaque_free(rt, ac->opaque);
      ac->opaque = NULL;
      ac->opaque_free = NULL;
    }

    if(ac->link.prev && ac->link.next)
      list_del(&ac->link);

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
  assert(promise_done(&ac->promise.funcs));

  asyncclosure_change_event(ac, WANT_NONE);

  assert(ac->link.prev && ac->link.next);
  list_del(&ac->link);
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
    ac->set_handler = new_state == 0 ? JS_NULL : js_iohandler_fn(ctx, 0 != (new_state & WANT_WRITE), "io");

    if(new_state)
      if(!js_iohandler_set(ctx, ac->set_handler, ac->fd, asyncclosure_function(ac, ac->ccfunc, new_state)))
        promise_reject(ctx, &ac->promise.funcs, JS_GetException(ctx));

    return TRUE;
  }

  return FALSE;
}

/**
 * @}
 */
