#ifndef QUICKJS_SYSCALLERROR_H
#define QUICKJS_SYSCALLERROR_H

#include "utils.h"

extern VISIBLE /*thread_local*/ JSClassID js_syscallerror_class_id;
extern /*thread_local*/ JSValue syscallerror_proto, syscallerror_ctor;

typedef struct {
  const char* syscall;
  int number;
  const char* stack;
} SyscallError;

#define js_syscall(name, retval) js_syscall_return(name, retval, JS_NewInt32(ctx, result))

#define js_syscall_return(name, retval, successval)                                                                                                                                                                                                                                                        \
  do {                                                                                                                                                                                                                                                                                                     \
    int prev_errno = errno, result = retval;                                                                                                                                                                                                                                                               \
    if(result == -1) {                                                                                                                                                                                                                                                                                     \
      ret = js_syscallerror_new(ctx, name, errno);                                                                                                                                                                                                                                                         \
      errno = prev_errno;                                                                                                                                                                                                                                                                                  \
    } else {                                                                                                                                                                                                                                                                                               \
      ret = successval;                                                                                                                                                                                                                                                                                    \
    }                                                                                                                                                                                                                                                                                                      \
  } while(0)

SyscallError* syscallerror_new(JSContext*, const char*, int number);
JSValue js_syscallerror_wrap(JSContext*, SyscallError*);
JSValue js_syscallerror_new(JSContext*, const char*, int number);
JSValue js_syscallerror_throw(JSContext*, const char*);

extern const JSCFunctionListEntry js_syscallerror_proto_funcs[];
extern const size_t js_syscallerror_proto_funcs_size;
extern const char* const errors[];
extern const size_t errors_size;

static inline SyscallError*
js_syscallerror_data(JSValueConst value) {
  return JS_GetOpaque(value, js_syscallerror_class_id);
}

static inline SyscallError*
js_syscallerror_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_syscallerror_class_id);
}

#endif /* defined(QUICKJS_SYSCALLERROR_H) */
