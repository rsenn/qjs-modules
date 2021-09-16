#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "quickjs-syscallerror.h"
#include "buffer-utils.h"
#include "char-utils.h"
#include <errno.h>

static const char* const errors[] = {
    0,
    "EPERM",
    "ENOENT",
    "ESRCH",
    "EINTR",
    "EIO",
    "ENXIO",
    "E2BIG",
    "ENOEXEC",
    "EBADF",
    "ECHILD",
    "EAGAIN",
    "ENOMEM",
    "EACCES",
    "EFAULT",
    "ENOTBLK",
    "EBUSY",
    "EEXIST",
    "EXDEV",
    "ENODEV",
    "ENOTDIR",
    "EISDIR",
    "EINVAL",
    "ENFILE",
    "EMFILE",
    "ENOTTY",
    "ETXTBSY",
    "EFBIG",
    "ENOSPC",
    "ESPIPE",
    "EROFS",
    "EMLINK",
    "EPIPE",
    "EDOM",
    "ERANGE",
    "EDEADLK",
    "ENAMETOOLONG",
    "ENOLCK",
    "ENOSYS",
    "ENOTEMPTY",
    0,
    0,
    "ENOMSG",
    "EIDRM",
    "ECHRNG",
    "EL2NSYNC",
    "EL3HLT",
    "EL3RST",
    "ELNRNG",
    "EUNATCH",
    "ENOCSI",
    "EL2HLT",
    "EBADE",
    "EBADR",
    "EXFULL",
    "ENOANO",
    "EBADRQC",
    0,
    0,
    "EBFONT",
    "ENOSTR",
    "ENODATA",
    "ETIME",
    "ENOSR",
    "ENONET",
    "ENOPKG",
    "EREMOTE",
    "ENOLINK",
    "EADV",
    "ESRMNT",
    "ECOMM",
    "EPROTO",
    "EMULTIHOP",
    "EDOTDOT",
    "EBADMSG",
    "EOVERFLOW",
    "ENOTUNIQ",
    "EBADFD",
    "EREMCHG",
    "ELIBACC",
    "ELIBBAD",
    "ELIBSCN",
    "ELIBMAX",
    "ELIBEXEC",
    "EILSEQ",
    "ERESTART",
    "ESTRPIPE",
    "EUSERS",
    "ENOTSOCK",
    "EDESTADDRREQ",
    "EMSGSIZE",
    "EPROTOTYPE",
    "ENOPROTOOPT",
    "EPROTONOSUPPORT",
    "ESOCKTNOSUPPORT",
    "EOPNOTSUPP",
    "EPFNOSUPPORT",
    "EAFNOSUPPORT",
    "EADDRINUSE",
    "EADDRNOTAVAIL",
    "ENETDOWN",
    "ENETUNREACH",
    "ENETRESET",
    "ECONNABORTED",
    "ECONNRESET",
    "ENOBUFS",
    "EISCONN",
    "ENOTCONN",
    "ESHUTDOWN",
    "ETOOMANYREFS",
    "ETIMEDOUT",
    "ECONNREFUSED",
    "EHOSTDOWN",
    "EHOSTUNREACH",
    "EALREADY",
    "EINPROGRESS",
    "ESTALE",
    "EUCLEAN",
    "ENOTNAM",
    "ENAVAIL",
    "EISNAM",
    "EREMOTEIO",
    "EDQUOT",
    "ENOMEDIUM",
    "EMEDIUMTYPE",
    "ECANCELED",
    "ENOKEY",
    "EKEYEXPIRED",
    "EKEYREVOKED",
    "EKEYREJECTED",
    "EOWNERDEAD",
    "ENOTRECOVERABLE",
    "ERFKILL",
};

static const char*
get_error(int errnum) {
  if(errnum >= 0 && errnum < countof(errors))
    return errors[errnum];
  return 0;
}

static int
find_error(const char* code) {
  int i, len = countof(errors);
  for(i = 1; i < len; i++) {
    if(errors[i] && !strcmp(code, errors[i]))
      return i;
  }
  return 0;
}

thread_local VISIBLE JSClassID js_syscallerror_class_id = 0;
thread_local JSValue syscallerror_proto = {JS_TAG_UNDEFINED},
                     syscallerror_ctor = {JS_TAG_UNDEFINED};

enum {
  SYSCALLERROR_TOSTRING = 0,
  SYSCALLERROR_SYSCALL,
  SYSCALLERROR_CODE,
  SYSCALLERROR_ERRNO,
  SYSCALLERROR_STACK,
  SYSCALLERROR_MESSAGE
};

SyscallError*
syscallerror_new(JSContext* ctx, const char* syscall, int errnum) {
  SyscallError* err;

  if(!(err = js_mallocz(ctx, sizeof(SyscallError))))
    return 0;

  err->syscall = syscall ? js_strdup(ctx, syscall) : 0;
  err->errnum = errnum;
  return err;
}

JSValue
js_syscallerror_wrap(JSContext* ctx, SyscallError* err) {
  JSValue obj;
  obj =
      JS_NewObjectProtoClass(ctx, syscallerror_proto, js_syscallerror_class_id);
  JS_SetOpaque(obj, err);
  return obj;
}

JSValue
js_syscallerror_new(JSContext* ctx, const char* syscall, int errnum) {
  SyscallError* err;
  JSValue obj = JS_UNDEFINED, proto = JS_UNDEFINED,
          error = js_global_new(ctx, "Error", 0, 0), st;

  if(!(err = syscallerror_new(ctx, syscall, errnum)))
    return JS_EXCEPTION;

  obj =
      JS_NewObjectProtoClass(ctx, syscallerror_proto, js_syscallerror_class_id);
  if(JS_IsException(obj))
    goto fail;

  if(!JS_IsUndefined((st = JS_GetPropertyStr(ctx, error, "stack")))) {
    const char* stack = JS_ToCString(ctx, st);
    size_t pos = str_chr(stack, '\n');
    if(stack[pos])
      pos++;
    err->stack = js_strdup(ctx, stack + pos);
    JS_FreeCString(ctx, stack);
  }
  JS_FreeValue(ctx, st);
  JS_FreeValue(ctx, error);

  JS_SetOpaque(obj, err);
  return obj;
fail:
  js_free(ctx, err);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_syscallerror_throw(JSContext* ctx, const char* syscall) {
  JSValue error = js_syscallerror_new(ctx, syscall, errno);
  return JS_Throw(ctx, error);
}

static JSValue
js_syscallerror_constructor(JSContext* ctx,
                            JSValueConst new_target,
                            int argc,
                            JSValueConst argv[]) {
  SyscallError* err;
  JSValue obj = JS_UNDEFINED, proto = JS_UNDEFINED,
          error = js_global_new(ctx, "Error", 0, 0), st;

  if(!(err = js_mallocz(ctx, sizeof(SyscallError))))
    return JS_EXCEPTION;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_syscallerror_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  if(argc >= 1)
    err->syscall = js_tostring(ctx, argv[0]);
  if(argc >= 2) {
    int32_t errnum;
    if(JS_IsNumber(argv[1])) {
      JS_ToInt32(ctx, &errnum, argv[1]);
    } else {
      const char* code = JS_ToCString(ctx, argv[1]);
      errnum = find_error(code);
      JS_FreeCString(ctx, code);
    }
    err->errnum = errnum;
  }
  if(!JS_IsUndefined((st = JS_GetPropertyStr(ctx, error, "stack")))) {
    const char* stack = JS_ToCString(ctx, st);
    size_t pos = str_chr(stack, '\n');
    if(stack[pos])
      pos++;
    err->stack = js_strdup(ctx, stack + pos);
    JS_FreeCString(ctx, stack);
  }
  JS_FreeValue(ctx, st);
  JS_FreeValue(ctx, error);

  JS_SetOpaque(obj, err);
  return obj;
fail:
  js_free(ctx, err);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_syscallerror_dump(JSContext* ctx, JSValueConst this_val, DynBuf* dbuf) {
  SyscallError* err;
  if(!(err = js_syscallerror_data2(ctx, this_val)))
    return;

  dbuf_putstr(dbuf, "SyscallError: ");
  if(err->syscall) {
    dbuf_putstr(dbuf, err->syscall);
    dbuf_putstr(dbuf, "() ");
  }
  if(err->errnum) {
    const char* msg;

    if((msg = strerror(err->errnum)))
      dbuf_putstr(dbuf, msg);
  }
  if(err->stack) {
    dbuf_putc(dbuf, '\n');
    dbuf_putstr(dbuf, err->stack);
  }
  dbuf_0(dbuf);
};

static JSValue
js_syscallerror_method(JSContext* ctx,
                       JSValueConst this_val,
                       int argc,
                       JSValueConst argv[],
                       int magic) {
  JSValue ret = JS_UNDEFINED;
  SyscallError* err;
  if(!(err = js_syscallerror_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SYSCALLERROR_TOSTRING: {
      DynBuf dbuf = {0};
      js_dbuf_init(ctx, &dbuf);

      js_syscallerror_dump(ctx, this_val, &dbuf);

      ret = JS_NewStringLen(ctx, dbuf.buf, dbuf.size);
      break;
    }
  }
  return ret;
}

static JSValue
js_syscallerror_inspect(JSContext* ctx,
                        JSValueConst this_val,
                        int argc,
                        JSValueConst argv[]) {
  SyscallError* err;
  const char* code = 0;

  if(!(err = js_syscallerror_data2(ctx, this_val)))
    return JS_EXCEPTION;
  JSValue obj = JS_NewObjectProto(ctx, syscallerror_proto);
  if(err->syscall)
    JS_DefinePropertyValueStr(ctx,
                              obj,
                              "syscall",
                              JS_NewString(ctx, err->syscall),
                              JS_PROP_ENUMERABLE);
  if((code = get_error(err->errnum))) {
    JS_DefinePropertyValueStr(
        ctx, obj, "code", JS_NewString(ctx, code), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(
        ctx, obj, "errno", JS_NewInt32(ctx, err->errnum), JS_PROP_ENUMERABLE);
  }
  if(js_has_propertystr(ctx, obj, "stack"))
    JS_DefinePropertyValueStr(ctx,
                              obj,
                              "stack",
                              JS_GetPropertyStr(ctx, this_val, "stack"),
                              JS_PROP_ENUMERABLE);

  JS_DefinePropertyValueStr(ctx,
                            obj,
                            "message",
                            JS_GetPropertyStr(ctx, this_val, "message"),
                            JS_PROP_ENUMERABLE);
  return obj;
}

static JSValue
js_syscallerror_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue ret = JS_UNDEFINED;
  SyscallError* err;

  if(!(err = js_syscallerror_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SYSCALLERROR_SYSCALL: {
      ret = err->syscall ? JS_NewString(ctx, err->syscall) : JS_NULL;
      break;
    }
    case SYSCALLERROR_CODE: {
      const char* code;
      ret = (code = get_error(err->errnum)) ? JS_NewString(ctx, code) : JS_NULL;
      break;
    }
    case SYSCALLERROR_ERRNO: {
      ret = JS_NewInt32(ctx, err->errnum);
      break;
    }
    case SYSCALLERROR_STACK: {
      ret = err->stack ? JS_NewString(ctx, err->stack) : JS_UNDEFINED;
      break;
    }
    case SYSCALLERROR_MESSAGE: {
      DynBuf dbuf = {0};
      js_dbuf_init(ctx, &dbuf);

      js_syscallerror_dump(ctx, this_val, &dbuf);

      ret = JS_NewStringLen(ctx, dbuf.buf, byte_chr(dbuf.buf, dbuf.size, '\n'));
      break;
    }
  }
  return ret;
}

static const JSCFunctionListEntry js_syscallerror_funcs[] = {
    JS_CFUNC_MAGIC_DEF(
        "toString", 0, js_syscallerror_method, SYSCALLERROR_TOSTRING),
    JS_CGETSET_MAGIC_DEF(
        "syscall", js_syscallerror_get, 0, SYSCALLERROR_SYSCALL),
    JS_CGETSET_MAGIC_DEF("code", js_syscallerror_get, 0, SYSCALLERROR_CODE),
    JS_CGETSET_MAGIC_DEF("errno", js_syscallerror_get, 0, SYSCALLERROR_ERRNO),
    JS_CGETSET_MAGIC_DEF("stack", js_syscallerror_get, 0, SYSCALLERROR_STACK),
    JS_CGETSET_MAGIC_DEF(
        "message", js_syscallerror_get, 0, SYSCALLERROR_MESSAGE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]",
                       "SyscallError",
                       JS_PROP_CONFIGURABLE),
};

void
js_syscallerror_finalizer(JSRuntime* rt, JSValue val) {
  SyscallError* err = JS_GetOpaque(val, js_syscallerror_class_id);
  if(err) {
    if(err->syscall)
      js_free_rt(rt, err->syscall);
    js_free_rt(rt, err);
  }
}

static JSClassDef js_syscallerror_class = {
    .class_name = "SyscallError",
    .finalizer = js_syscallerror_finalizer,
};

int
js_syscallerror_init(JSContext* ctx, JSModuleDef* m) {

  if(!js_syscallerror_class_id) {

    JS_NewClassID(&js_syscallerror_class_id);
    JS_NewClass(JS_GetRuntime(ctx),
                js_syscallerror_class_id,
                &js_syscallerror_class);

    syscallerror_ctor = JS_NewCFunction2(ctx,
                                         js_syscallerror_constructor,
                                         "SyscallError",
                                         1,
                                         JS_CFUNC_constructor,
                                         0);
    syscallerror_proto = js_global_new(ctx, "Error", 0, 0);

    JS_SetPropertyFunctionList(ctx,
                               syscallerror_proto,
                               js_syscallerror_funcs,
                               countof(js_syscallerror_funcs));
    JS_SetClassProto(ctx, js_syscallerror_class_id, syscallerror_proto);

    js_set_inspect_method(ctx, syscallerror_proto, js_syscallerror_inspect);

    JS_SetConstructor(ctx, syscallerror_ctor, syscallerror_proto);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "SyscallError", syscallerror_ctor);
  }

  return 0;
}

#if defined(JS_SHARED_LIBRARY) && defined(JS_SYSCALLERROR_MODULE)
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_syscallerror
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_syscallerror_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "SyscallError");
  return m;
}
