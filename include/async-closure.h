#ifndef ASYNC_CLOSURE_H
#define ASYNC_CLOSURE_H

#include <quickjs.h>
#include "js-utils.h"
#include "utils.h"

enum event_flag { WANT_READ = 1, WANT_WRITE = 2 };

struct AsyncClosure {
  int ref_count, fd;
  JSContext* ctx;
  JSValue obj, set_handler;
  ResolveFunctions fns;
  enum event_flag state : 2;
  CClosureFunc* ccfunc;
  void* opaque;
  void (*opaque_free)(JSContext* ctx, void*);
};

struct AsyncClosure* asyncclosure_new(JSContext*, int, enum event_flag, JSValueConst, JSValueConst*, CClosureFunc*);
struct AsyncClosure* asyncclosure_dup(struct AsyncClosure*);
void asyncclosure_freeopaque(struct AsyncClosure*);
void asyncclosure_setopaque(struct AsyncClosure*, void*, void (*opaque_free)(JSContext*, void*));
void asyncclosure_free(void*);
JSValue asyncclosure_function(struct AsyncClosure*, CClosureFunc*, int);
BOOL asyncclosure_setmode(struct AsyncClosure*, enum event_flag);

#endif /* defined(ASYNC_CLOSURE_H) */
