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

typedef enum {
  WANT_READ = 1,
  WANT_WRITE = 2,
  WANT_NONE = 0,
} AsyncEvent;

struct AsyncHandlerClosure {
  int ref_count, fd;
  AsyncEvent state : 2;
  CClosureFunc* ccfunc;
  JSContext* ctx;
  JSValue result, set_handler;
  Promise promise;
  void* opaque;
  void (*opaque_free)(JSRuntime*, void*);
  struct list_head link;
};

typedef struct AsyncHandlerClosure AsyncClosure;

AsyncClosure* asyncclosure_new(JSContext*, int fd, AsyncEvent state, JSValueConst result, CClosureFunc*);
AsyncClosure* asyncclosure_dup(AsyncClosure*);
JSValue asyncclosure_promise(AsyncClosure*);
void asyncclosure_opaque(AsyncClosure*, void*, void (*opaque_free)(JSRuntime*, void*));
void asyncclosure_free(JSRuntime*, void*);
JSValue asyncclosure_promise(AsyncClosure*);
BOOL asyncclosure_change_event(AsyncClosure*, AsyncEvent);
void asyncclosure_resolve(AsyncClosure*);
void asyncclosure_error(AsyncClosure*, JSValueConst);
void asyncclosure_done(AsyncClosure*);
AsyncClosure* asyncclosure_lookup(int fd);

extern VISIBLE struct list_head asyncclosure_list;
extern VISIBLE JSClassID js_asyncclosure_class_id;

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
