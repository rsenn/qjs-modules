#ifndef ASYNC_CLOSURE_H
#define ASYNC_CLOSURE_H

#include <quickjs.h>
#include "js-utils.h"
#include "utils.h"

typedef enum event_flag { WANT_READ = 1, WANT_WRITE = 2 } AsyncEvent;

PACK struct AsyncClosure {
  int ref_count, fd;
  AsyncEvent state : 2;
  CClosureFunc* ccfunc;
  JSContext* ctx;
  JSValue obj, set_handler;
  Promise promise;
  void* opaque;
  void (*opaque_free)(JSContext*, void*);
};
ENDPACK

typedef struct AsyncClosure AsyncClosure;

AsyncClosure* asyncclosure_new(JSContext*, int fd, AsyncEvent state, JSValueConst obj, CClosureFunc*);
AsyncClosure* asyncclosure_dup(AsyncClosure*);
JSValue asyncclosure_promise(AsyncClosure*);
void asyncclosure_set_opaque(AsyncClosure*, void*, void (*opaque_free)(JSContext*, void*));
void asyncclosure_free(void*);
JSValue asyncclosure_promise(AsyncClosure*);
BOOL asyncclosure_change_event(AsyncClosure*, AsyncEvent);

#endif /* defined(ASYNC_CLOSURE_H) */
