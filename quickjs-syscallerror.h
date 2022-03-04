#include "include/defines.h"
#ifndef QUICKJS_SYSCALLERROR_H
#define QUICKJS_SYSCALLERROR_H

#include "include/utils.h"

/**
 * \defgroup quickjs-syscallerror QuickJS module: syscallerror - System-call error object
 * @{
 */
typedef struct {
  char* syscall;
  int number;
  char* stack;
} SyscallError;

#define js_syscall(name, retval) js_syscall_return(name, retval, JS_NewInt32(ctx, result))

#define js_syscall_return(name, retval, successval) \
  do { \
    int prev_errno = errno, result = retval; \
    if(result == -1) { \
      ret = js_syscallerror_new(ctx, name, errno); \
      errno = prev_errno; \
    } else { \
      ret = successval; \
    } \
  } while(0)

SyscallError* js_syscallerror_data(JSValue);
SyscallError* js_syscallerror_data2(JSContext*, JSValue);
SyscallError* syscallerror_new(JSContext*, const char* syscall, int number);
JSValue js_syscallerror_wrap(JSContext*, SyscallError* err);
JSValue js_syscallerror_new(JSContext*, const char* syscall, int number);
JSValue js_syscallerror_throw(JSContext*, const char* syscall);

extern VISIBLE const char* const errors[];
extern VISIBLE const size_t errors_size;

/**
 * @}
 */
#endif /* defined(QUICKJS_SYSCALLERROR_H) */
