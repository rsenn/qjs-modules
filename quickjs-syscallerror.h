#ifndef QUICKJS_SYSCALLERROR_H
#define QUICKJS_SYSCALLERROR_H

#include "defines.h"
#include "utils.h"

/**
 * \defgroup quickjs-syscallerror quickjs-syscallerror: System-call error object
 * @{
 */
typedef struct {
  char* syscall;
  int number;
  JSValue stack;
} SyscallError;

#define js_syscall(name, retval) js_syscall_ret(name, retval, JS_NewInt32(ctx, result))

#define js_syscall_ret(name, retval, successval) \
  do { \
    int prev_errno = errno, result = retval; \
    if(result == -1) { \
      ret = js_syscallerror_new(ctx, name, errno); \
      errno = prev_errno; \
    } else { \
      ret = successval; \
    } \
  } while(0)

VISIBLE SyscallError* syscallerror_new(JSContext*, const char* syscall, int number);
VISIBLE void syscallerror_free(SyscallError*, JSRuntime*);

VISIBLE SyscallError* js_syscallerror_data(JSValue);
VISIBLE SyscallError* js_syscallerror_data2(JSContext*, JSValue);
VISIBLE JSValue js_syscallerror_wrap(JSContext*, SyscallError* err);
VISIBLE JSValue js_syscallerror_new(JSContext*, const char* syscall, int number);
VISIBLE JSValue js_syscallerror_throw(JSContext*, const char* syscall);
VISIBLE JSValue js_syscallerror_throw_free(JSContext*, const char* syscall, JSValue);

VISIBLE int js_syscallerror_init(JSContext* ctx, JSModuleDef* m);

static inline JSValue
js_syscallerror_result(JSContext* ctx, const char* syscall, int result) {
  if(result == -1)
    return js_syscallerror_throw(ctx, syscall);
  return JS_NewInt32(ctx, result);
}

#define js_syscall_result(ctx, name, args...) js_syscallerror_result(ctx, #name, name(args))

#define js_syscall_throw(namestr, ret) \
  { \
    if(ret != -1) \
      return JS_NewInt32(ctx, ret); \
\
    return js_syscallerror_throw(ctx, namestr); \
  }

#define js_syscall_return(name, args...) \
  { \
    int ret = name(args); \
\
    js_syscall_throw(#name, ret); \
  }

/**
 * @}
 */
#endif /* defined(QUICKJS_SYSCALLERROR_H) */
