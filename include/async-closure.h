#ifndef ASYNC_CLOSURE_H
#define ASYNC_CLOSURE_H

#include <quickjs.h>
#include <list.h>
#include "js-utils.h"
#include "utils.h"

/**
 * \defgroup async-closure async-closure: Async Handler Closure
 * @{
 */

/* which fd readiness a closure waits for; an or of the two bits. */
typedef enum {
  WANT_READ = 1,
  WANT_WRITE = 2,
  WANT_NONE = 0,
} AsyncEvent;

/* a promise settled from an fd handler: `ccfunc` runs when the fd is ready.
 *
 * refcount: `ref_count` counts the owner and each installed handler. */
typedef struct AsyncHandlerClosure {
  int ref_count, fd;
  AsyncEvent state : 2; /* events a handler is installed for */
  CClosureFunc* ccfunc; /* the handler, called with the event as magic */
  JSContext* ctx;
  JSValue result;      /* what the promise resolves with */
  JSValue set_handler; /* the io function that installed the handler */
  Promise promise;
  void* opaque; /* owner data, released by `opaque_free` */
  void (*opaque_free)(JSRuntime*, void*);
  struct list_head link; /* in asyncclosure_list while active */
} AsyncClosure;

/* creates the closure and, for a `state` other than WANT_NONE, installs the
 * handler. `result` is what the promise will resolve with.
 * returns NULL with an exception pending. */
AsyncClosure* asyncclosure_new(JSContext*, int fd, AsyncEvent state, JSValueConst result, CClosureFunc*);

/* adds a reference. never throws. */
AsyncClosure* asyncclosure_dup(AsyncClosure*);

/* drops a reference; the last one detaches and frees the closure.
 * refcount: also the finalizer of the handler function. */
void asyncclosure_free(JSRuntime*, void*);

/* the promise, a new reference. never throws. */
JSValue asyncclosure_promise(AsyncClosure*);

/* hands `opaque` over; released with `opaque_free` (may be NULL) together
 * with the closure. set at most once. */
void asyncclosure_opaque(AsyncClosure*, void* opaque, void (*opaque_free)(JSRuntime*, void*));

/* moves the handler to the events in `state` (WANT_NONE removes it).
 * returns TRUE if it changed; an io error rejects the promise. */
BOOL asyncclosure_change_event(AsyncClosure*, AsyncEvent state);

/* settles the promise (with `result`, or rejected with the error) and
 * detaches the closure. */
void asyncclosure_resolve(AsyncClosure*);
void asyncclosure_error(AsyncClosure*, JSValueConst error);

/* removes the handler and the list entry, leaving the promise as it is;
 * safe to call twice. */
void asyncclosure_done(AsyncClosure*);

/* the active closure waiting on `fd`, or NULL. never throws. */
AsyncClosure* asyncclosure_lookup(int fd);

extern VISIBLE struct list_head asyncclosure_list;

/* resolves the promise with `value` instead of `result`. */
static inline void
asyncclosure_yield(AsyncClosure* ac, JSValueConst value) {
  JS_FreeValue(ac->ctx, ac->result);
  ac->result = JS_DupValue(ac->ctx, value);
  asyncclosure_resolve(ac);
}

/**
 * @}
 */

#endif /* defined(ASYNC_CLOSURE_H) */
