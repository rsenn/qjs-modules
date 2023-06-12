#ifndef ASYNC_CLOSURE_H
#define ASYNC_CLOSURE_H

#include <quickjs.h>
#include "js-utils.h"
#include "utils.h"

/**
 * \defgroup asymc-closure Async Handler Closure
 * @{
 */

typedef enum {
  WANT_READ = 1,
  WANT_WRITE = 2,
  WANT_NONE = 0,
} AsyncEvent;

PACK struct AsyncHandlerClosure {
  int ref_count, fd;
  AsyncEvent state : 2;
  CClosureFunc* ccfunc;
  JSContext* ctx;
  JSValue result, set_handler;
  Promise promise;
  void* opaque;
  void (*opaque_free)(JSContext*, void*);
};
ENDPACK

typedef struct AsyncHandlerClosure AsyncClosure;

AsyncClosure* asyncclosure_new(JSContext*, int fd, AsyncEvent state, JSValueConst result, CClosureFunc*);
AsyncClosure* asyncclosure_dup(AsyncClosure*);
JSValue asyncclosure_promise(AsyncClosure*);
void asyncclosure_opaque(AsyncClosure*, void*, void (*opaque_free)(JSContext*, void*));
void asyncclosure_free(void*);
JSValue asyncclosure_promise(AsyncClosure*);
BOOL asyncclosure_change_event(AsyncClosure*, AsyncEvent);
void asyncclosure_resolve(AsyncClosure*);
void asyncclosure_error(AsyncClosure*, JSValueConst);
void asyncclosure_done(AsyncClosure*);

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
