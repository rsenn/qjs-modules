/* async-closure.c: AsyncClosure, a promise settled when an fd is ready.
 * depends on: js-utils.c (promise_*), utils.c (io handlers, cclosures).
 * rule: a closure sits in asyncclosure_list from creation to done. */
#include "async-closure.h"
#include <assert.h>
#include <stdio.h>

/**
 * \addtogroup async-closure
 * @{
 */

VISIBLE struct list_head asyncclosure_list;

AsyncClosure*
asyncclosure_lookup(int fd) {
  struct list_head* el;

  if(fd < 0)
    return NULL;

  list_for_each(el, &asyncclosure_list) {
    AsyncClosure* ac = list_entry(el, AsyncClosure, link);

    if(ac->fd == fd)
      return ac;
  }

  return NULL;
}

/* the handler function: a cclosure holding its own reference.
 * returns the function; refcount: freed through asyncclosure_free(). */
static JSValue
asyncclosure_function(AsyncClosure* ac, CClosureFunc* func, int magic) {
  return js_function_cclosure(ac->ctx, func, 0, magic, asyncclosure_dup(ac), asyncclosure_free);
}

AsyncClosure*
asyncclosure_new(JSContext* ctx, int fd, AsyncEvent state, JSValueConst this_val, CClosureFunc* func) {
  AsyncClosure* ac;

  if(asyncclosure_list.prev == NULL && asyncclosure_list.next == NULL)
    init_list_head(&asyncclosure_list);

  if(!(ac = js_mallocz(ctx, sizeof(AsyncClosure))))
    return NULL;

  if(!promise_init(ctx, &ac->promise)) {
    js_free(ctx, ac);
    return NULL;
  }

  ac->ref_count = 1;
  ac->fd = fd;
  ac->ccfunc = func;
  ac->ctx = ctx;
  ac->result = JS_DupValue(ctx, this_val);
  ac->set_handler = JS_NULL;

  list_add(&ac->link, &asyncclosure_list);

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
  JSContext* ctx = ac->ctx;

  if(--ac->ref_count > 0)
    return;

  if(ac->state)
    fprintf(stderr, "WARNING: %s() has still a handler for fd %d\n", __func__, ac->fd);

  asyncclosure_done(ac);
  JS_FreeValue(ctx, ac->result);
  promise_free(ctx, &ac->promise);

  if(ac->opaque && ac->opaque_free)
    ac->opaque_free(rt, ac->opaque);

  js_free(ctx, ac);
}

void
asyncclosure_resolve(AsyncClosure* ac) {
  promise_resolve(ac->ctx, &ac->promise, ac->result);
  asyncclosure_done(ac);
}

void
asyncclosure_error(AsyncClosure* ac, JSValueConst error) {
  promise_reject(ac->ctx, &ac->promise, error);
  asyncclosure_done(ac);
}

void
asyncclosure_done(AsyncClosure* ac) {
  asyncclosure_change_event(ac, WANT_NONE);

  if(ac->link.prev && ac->link.next)
    list_del(&ac->link);
}

/* rejects the promise with the pending exception. */
static void
asyncclosure_reject_pending(AsyncClosure* ac) {
  JSValue error = JS_GetException(ac->ctx);

  promise_reject(ac->ctx, &ac->promise, error);
  JS_FreeValue(ac->ctx, error);
}

BOOL
asyncclosure_change_event(AsyncClosure* ac, AsyncEvent new_state) {
  JSContext* ctx = ac->ctx;

  new_state &= 0b11u;

  if(ac->state == new_state)
    return FALSE;

  /* had a previous handler? */
  if(ac->state) {
    if(!js_iohandler_set(ctx, ac->set_handler, ac->fd, JS_NULL))
      asyncclosure_reject_pending(ac);

    JS_FreeValue(ctx, ac->set_handler);
  }

  ac->state = new_state;
  ac->set_handler = new_state == WANT_NONE ? JS_NULL : js_iohandler_fn(ctx, 0 != (new_state & WANT_WRITE), "io");

  if(new_state)
    if(!js_iohandler_set(ctx, ac->set_handler, ac->fd, asyncclosure_function(ac, ac->ccfunc, new_state)))
      asyncclosure_reject_pending(ac);

  return TRUE;
}

/**
 * @}
 */
