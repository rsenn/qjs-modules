#include "defines.h"
  #include "quickjs-syscallerror.h"
#include "buffer-utils.h"
#include "char-utils.h"
#include <errno.h>
#include "debug.h"
#if defined(_WIN32) && !defined(__MSYS__) && !defined(__CYGWIN__)
#include <windows.h>
#endif

/**
 * \addtogroup quickjs-syscallerror
 * @{
 */
VISIBLE JSClassID js_syscallerror_class_id = 0;
VISIBLE JSValue syscallerror_proto = {{0}, JS_TAG_UNDEFINED}, syscallerror_ctor = {{0}, JS_TAG_UNDEFINED};

int js_syscallerror_init(JSContext*, JSModuleDef*);

static const char*
error_get(int number) {
  if(number >= 0 && number < errors_size)
    return errors[number];
  return 0;
}

static int
error_find(const char* code) {
  int i, len = errors_size;
  for(i = 1; i < len; i++) {
    if(errors[i] && !strcmp(code, errors[i]))
      return i;
  }
  return 0;
}

static char*
stack_get(JSContext* ctx) {
  const char* stack;
  char* ret;
  JSValue st, error = js_global_new(ctx, "Error", 0, 0);
  st = JS_GetPropertyStr(ctx, error, "stack");

  stack = JS_ToCString(ctx, st);
  size_t pos = str_chr(stack, '\n');
  if(stack[pos])
    pos++;
  ret = js_strdup(ctx, stack + pos);
  JS_FreeCString(ctx, stack);
  JS_FreeValue(ctx, error);
  return ret;
}

SyscallError*
js_syscallerror_data(JSValueConst value) {
  return JS_GetOpaque(value, js_syscallerror_class_id);
}

SyscallError*
js_syscallerror_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_syscallerror_class_id);
}

SyscallError*
syscallerror_new(JSContext* ctx, const char* syscall, int number) {
  SyscallError* err;

  if(!(err = js_mallocz(ctx, sizeof(SyscallError))))
    return 0;

  err->syscall = syscall ? js_strdup(ctx, syscall) : 0;
  err->number = number;
  err->stack = stack_get(ctx);
  return err;
}

JSValue
js_syscallerror_wrap(JSContext* ctx, SyscallError* err) {
  JSValue obj;
  obj = JS_NewObjectProtoClass(ctx, syscallerror_proto, js_syscallerror_class_id);
  JS_SetOpaque(obj, err);
  return obj;
}

JSValue
js_syscallerror_new(JSContext* ctx, const char* syscall, int number) {
  SyscallError* err;
  JSValue obj;

  if(js_syscallerror_class_id == 0 /* || JS_IsUndefined(syscallerror_proto) || JS_IsUndefined(syscallerror_ctor)*/)
    js_syscallerror_init(ctx, 0);

  if(!(err = syscallerror_new(ctx, syscall, number)))
    return JS_ThrowOutOfMemory(ctx);

  obj = JS_NewObjectProtoClass(ctx, syscallerror_proto, js_syscallerror_class_id);
  if(JS_IsException(obj))
    goto fail;

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
js_syscallerror_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  SyscallError* err;
  JSValue obj = JS_UNDEFINED, proto = JS_UNDEFINED, st = JS_UNDEFINED;

  if(!(err = js_mallocz(ctx, sizeof(SyscallError))))
    return JS_EXCEPTION;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_syscallerror_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  if(argc >= 2) {
    err->syscall = js_tostring(ctx, argv[0]);
    argc--;
    argv++;
  }

  if(argc >= 1) {
    int32_t number;
    if(JS_IsNumber(argv[0])) {
      JS_ToInt32(ctx, &number, argv[0]);
    } else {
      const char* code = JS_ToCString(ctx, argv[0]);
      number = error_find(code);
      JS_FreeCString(ctx, code);
    }
    err->number = number;
  }
  err->stack = stack_get(ctx);
  JS_FreeValue(ctx, st);

  JS_SetOpaque(obj, err);
  return obj;
fail:
  js_free(ctx, err);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
syscallerror_dump(SyscallError* err, DynBuf* dbuf) {
  if(err->syscall) {
    char buf[FMT_LONG];
    dbuf_putstr(dbuf, err->syscall);
    dbuf_putstr(dbuf, "() = -1 (errno = ");
    dbuf_put(dbuf, buf, fmt_longlong(buf, err->number));
    dbuf_putstr(dbuf, ")");
  }

  if(err->number) {
    if(err->syscall)
      dbuf_putstr(dbuf, ": ");

#if defined(_WIN32) && !defined(__MSYS__) && !defined(__CYGWIN__)
    dbuf->size += FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL,
                                err->number,
                                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                dbuf_reserve(dbuf, 256),
                                256,
                                NULL);

    while(dbuf->size > 0) {
      if(dbuf->buf[dbuf->size - 1] > 0x20)
        break;
      --dbuf->size;
    }

#else
    const char* msg;

    if((msg = strerror(err->number)))
      dbuf_putstr(dbuf, msg);
#endif
  }

  if(err->stack) {
    dbuf_putc(dbuf, '\n');
    dbuf_putstr(dbuf, err->stack);
  }

  dbuf_0(dbuf);
}

enum {
  SYSCALLERROR_TOSTRING,
  SYSCALLERROR_VALUEOF,
};

static JSValue
js_syscallerror_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  SyscallError* err;

  if(!(err = js_syscallerror_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SYSCALLERROR_TOSTRING: {
      DynBuf dbuf = {0};
      js_dbuf_init(ctx, &dbuf);
      syscallerror_dump(err, &dbuf);
      ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
      dbuf_free(&dbuf);
      break;
    }
    case SYSCALLERROR_VALUEOF: {
      ret = JS_NewInt32(ctx, err->number);
      break;
    }
  }
  return ret;
}

enum {
  SYSCALLERROR_NAME,
};

static JSValue
js_syscallerror_functions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case SYSCALLERROR_NAME: {
      int32_t err = 0;
      JS_ToInt32(ctx, &err, argv[0]);
      ret = JS_NewString(ctx, error_get(err));
      break;
    }
  }
  return ret;
}

static JSValue
js_syscallerror_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const char* code = 0;
  SyscallError* err = js_syscallerror_data(this_val);

  /*  if(!err)
      return JS_DupValue(ctx, this_val);*/
  JSValue obj = JS_NewObjectClass(ctx, js_syscallerror_class_id);

  JS_DefinePropertyValueStr(ctx, obj, "errno", JS_NewInt32(ctx, err->number), JS_PROP_C_W_E);
  JS_DefinePropertyValueStr(ctx, obj, "message", JS_GetPropertyStr(ctx, this_val, "message"), JS_PROP_C_W_E);

  if(err->syscall)
    JS_DefinePropertyValueStr(ctx, obj, "syscall", JS_NewString(ctx, err->syscall), JS_PROP_C_W_E);

  if((code = error_get(err->number)))
    JS_DefinePropertyValueStr(ctx, obj, "code", JS_NewString(ctx, code), JS_PROP_C_W_E);

  if(js_has_propertystr(ctx, obj, "stack"))
    JS_DefinePropertyValueStr(ctx, obj, "stack", JS_GetPropertyStr(ctx, this_val, "stack"), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);

  return obj;
}

enum {
  SYSCALLERROR_PROP_SYSCALL,
  SYSCALLERROR_PROP_CODE,
  SYSCALLERROR_PROP_ERRNO,
  SYSCALLERROR_PROP_STACK,
  SYSCALLERROR_PROP_MESSAGE,
};

static JSValue
js_syscallerror_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue ret = JS_UNDEFINED;
  SyscallError* err = js_syscallerror_data(this_val);

  switch(magic) {
    case SYSCALLERROR_PROP_SYSCALL: {
      if(err)
        ret = err->syscall ? JS_NewString(ctx, err->syscall) : JS_NULL;
      break;
    }
    case SYSCALLERROR_PROP_CODE: {
      const char* code;
      if(err)
        ret = (code = error_get(err->number)) ? JS_NewString(ctx, code) : JS_NULL;
      break;
    }
    case SYSCALLERROR_PROP_ERRNO: {
      if(err)
        ret = JS_NewInt32(ctx, err->number);
      break;
    }
    case SYSCALLERROR_PROP_STACK: {
      if(err)
        ret = err->stack ? JS_NewString(ctx, err->stack) : JS_NULL;
      break;
    }
    case SYSCALLERROR_PROP_MESSAGE: {
      DynBuf dbuf = {0};
      js_dbuf_init(ctx, &dbuf);
      syscallerror_dump(err, &dbuf);
      ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, byte_chr(dbuf.buf, dbuf.size, '\n'));
      break;
    }
  }
  return ret;
}

static JSValue
js_syscallerror_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JSValue ret = JS_UNDEFINED;
  SyscallError* err = js_syscallerror_data(this_val);

  /* if(!(err = js_syscallerror_data2(ctx, this_val)))
     return JS_EXCEPTION;*/

  switch(magic) {
    case SYSCALLERROR_PROP_SYSCALL: {
      break;
    }
    case SYSCALLERROR_PROP_CODE: {
      break;
    }
    case SYSCALLERROR_PROP_ERRNO: {
      break;
    }
    case SYSCALLERROR_PROP_STACK: {
      break;
    }
    case SYSCALLERROR_PROP_MESSAGE: {
      break;
    }
  }

  return ret;
}

const JSCFunctionListEntry js_syscallerror_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("syscall", js_syscallerror_get, js_syscallerror_set, SYSCALLERROR_PROP_SYSCALL),
    JS_CGETSET_MAGIC_DEF("name", js_syscallerror_get, js_syscallerror_set, SYSCALLERROR_PROP_CODE),
    JS_CGETSET_MAGIC_FLAGS_DEF("errno", js_syscallerror_get, js_syscallerror_set, SYSCALLERROR_PROP_ERRNO, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("stack", js_syscallerror_get, js_syscallerror_set, SYSCALLERROR_PROP_STACK),
    JS_CGETSET_MAGIC_FLAGS_DEF("message", js_syscallerror_get, js_syscallerror_set, SYSCALLERROR_PROP_MESSAGE, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    JS_CFUNC_MAGIC_DEF("toString", 0, js_syscallerror_method, SYSCALLERROR_TOSTRING),
    // JS_CFUNC_MAGIC_DEF("valueOf", 0, js_syscallerror_method, SYSCALLERROR_VALUEOF),
    JS_ALIAS_DEF("[Symbol.toPrimitive]", "toString"),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SyscallError", JS_PROP_CONFIGURABLE),
};

const size_t js_syscallerror_proto_funcs_size = countof(js_syscallerror_proto_funcs);

const JSCFunctionListEntry js_syscallerror_defines[] = {
// JS_CFUNC_MAGIC_DEF("name", 1, js_syscallerror_functions, SYSCALLERROR_NAME),
#ifdef EPERM
    JS_CONSTANT(EPERM),
#endif
#ifdef ENOENT
    JS_CONSTANT(ENOENT),
#endif
#ifdef ESRCH
    JS_CONSTANT(ESRCH),
#endif
#ifdef EINTR
    JS_CONSTANT(EINTR),
#endif
#ifdef EIO
    JS_CONSTANT(EIO),
#endif
#ifdef ENXIO
    JS_CONSTANT(ENXIO),
#endif
#ifdef E2BIG
    JS_CONSTANT(E2BIG),
#endif
#ifdef ENOEXEC
    JS_CONSTANT(ENOEXEC),
#endif
#ifdef EBADF
    JS_CONSTANT(EBADF),
#endif
#ifdef ECHILD
    JS_CONSTANT(ECHILD),
#endif
#ifdef EAGAIN
    JS_CONSTANT(EAGAIN),
#endif
#ifdef EWOULDBLOCK
    JS_CONSTANT(EWOULDBLOCK),
#endif
#ifdef ENOMEM
    JS_CONSTANT(ENOMEM),
#endif
#ifdef EACCES
    JS_CONSTANT(EACCES),
#endif
#ifdef EFAULT
    JS_CONSTANT(EFAULT),
#endif
#ifdef EBUSY
    JS_CONSTANT(EBUSY),
#endif
#ifdef EEXIST
    JS_CONSTANT(EEXIST),
#endif
#ifdef EXDEV
    JS_CONSTANT(EXDEV),
#endif
#ifdef ENODEV
    JS_CONSTANT(ENODEV),
#endif
#ifdef ENOTDIR
    JS_CONSTANT(ENOTDIR),
#endif
#ifdef EISDIR
    JS_CONSTANT(EISDIR),
#endif
#ifdef EINVAL
    JS_CONSTANT(EINVAL),
#endif
#ifdef ENFILE
    JS_CONSTANT(ENFILE),
#endif
#ifdef EMFILE
    JS_CONSTANT(EMFILE),
#endif
#ifdef ENOTTY
    JS_CONSTANT(ENOTTY),
#endif
#ifdef ETXTBSY
    JS_CONSTANT(ETXTBSY),
#endif
#ifdef EFBIG
    JS_CONSTANT(EFBIG),
#endif
#ifdef ENOSPC
    JS_CONSTANT(ENOSPC),
#endif
#ifdef ESPIPE
    JS_CONSTANT(ESPIPE),
#endif
#ifdef EROFS
    JS_CONSTANT(EROFS),
#endif
#ifdef EMLINK
    JS_CONSTANT(EMLINK),
#endif
#ifdef EPIPE
    JS_CONSTANT(EPIPE),
#endif
#ifdef EDOM
    JS_CONSTANT(EDOM),
#endif
#ifdef ERANGE
    JS_CONSTANT(ERANGE),
#endif
#ifdef EDEADLK
    JS_CONSTANT(EDEADLK),
#endif
#ifdef ENAMETOOLONG
    JS_CONSTANT(ENAMETOOLONG),
#endif
#ifdef ENOLCK
    JS_CONSTANT(ENOLCK),
#endif
#ifdef ENOSYS
    JS_CONSTANT(ENOSYS),
#endif
#ifdef ENOTEMPTY
    JS_CONSTANT(ENOTEMPTY),
#endif
#ifdef ENOMSG
    JS_CONSTANT(ENOMSG),
#endif
#ifdef EIDRM
    JS_CONSTANT(EIDRM),
#endif
#ifdef ENOLINK
    JS_CONSTANT(ENOLINK),
#endif
#ifdef EPROTO
    JS_CONSTANT(EPROTO),
#endif
#ifdef EBADMSG
    JS_CONSTANT(EBADMSG),
#endif
#ifdef EOVERFLOW
    JS_CONSTANT(EOVERFLOW),
#endif
#ifdef EILSEQ
    JS_CONSTANT(EILSEQ),
#endif
#ifdef ERESTART
    JS_CONSTANT(ERESTART),
#endif
#ifdef ENOTSOCK
    JS_CONSTANT(ENOTSOCK),
#endif
#ifdef EDESTADDRREQ
    JS_CONSTANT(EDESTADDRREQ),
#endif
#ifdef EMSGSIZE
    JS_CONSTANT(EMSGSIZE),
#endif
#ifdef EPROTOTYPE
    JS_CONSTANT(EPROTOTYPE),
#endif
#ifdef ENOPROTOOPT
    JS_CONSTANT(ENOPROTOOPT),
#endif
#ifdef EPROTONOSUPPORT
    JS_CONSTANT(EPROTONOSUPPORT),
#endif
#ifdef EOPNOTSUPP
    JS_CONSTANT(EOPNOTSUPP),
#endif
#ifdef EAFNOSUPPORT
    JS_CONSTANT(EAFNOSUPPORT),
#endif
#ifdef EADDRINUSE
    JS_CONSTANT(EADDRINUSE),
#endif
#ifdef EADDRNOTAVAIL
    JS_CONSTANT(EADDRNOTAVAIL),
#endif
#ifdef ENETDOWN
    JS_CONSTANT(ENETDOWN),
#endif
#ifdef ENETUNREACH
    JS_CONSTANT(ENETUNREACH),
#endif
#ifdef ENETRESET
    JS_CONSTANT(ENETRESET),
#endif
#ifdef ECONNABORTED
    JS_CONSTANT(ECONNABORTED),
#endif
#ifdef ECONNRESET
    JS_CONSTANT(ECONNRESET),
#endif
#ifdef ENOBUFS
    JS_CONSTANT(ENOBUFS),
#endif
#ifdef EISCONN
    JS_CONSTANT(EISCONN),
#endif
#ifdef ENOTCONN
    JS_CONSTANT(ENOTCONN),
#endif
#ifdef ETIMEDOUT
    JS_CONSTANT(ETIMEDOUT),
#endif
#ifdef ECONNREFUSED
    JS_CONSTANT(ECONNREFUSED),
#endif
#ifdef EHOSTUNREACH
    JS_CONSTANT(EHOSTUNREACH),
#endif
#ifdef EALREADY
    JS_CONSTANT(EALREADY),
#endif
#ifdef EINPROGRESS
    JS_CONSTANT(EINPROGRESS),
#endif
#ifdef ECANCELED
    JS_CONSTANT(ECANCELED),
#endif
#ifdef EOWNERDEAD
    JS_CONSTANT(EOWNERDEAD),
#endif
#ifdef ENOTRECOVERABLE
    JS_CONSTANT(ENOTRECOVERABLE),
#endif
};

const size_t js_syscallerror_defines_size = countof(js_syscallerror_defines);

static void
js_syscallerror_finalizer(JSRuntime* rt, JSValue val) {
  SyscallError* err = JS_GetOpaque(val, js_syscallerror_class_id);
  if(err) {
    if(err->syscall)
      js_free_rt(rt, err->syscall);
    if(err->stack)
      js_free_rt(rt, err->stack);
    js_free_rt(rt, err);
  }
}

static JSClassDef js_syscallerror_class = {
    .class_name = "SyscallError",
    .finalizer = js_syscallerror_finalizer,
};

int
js_syscallerror_init(JSContext* ctx, JSModuleDef* m) {
  JSValue error = js_global_prototype(ctx, "Error");

  assert(JS_IsObject(error));

  if(!js_syscallerror_class_id) {
    JS_NewClassID(&js_syscallerror_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_syscallerror_class_id, &js_syscallerror_class);
    syscallerror_ctor = JS_NewCFunction2(ctx, js_syscallerror_constructor, "SyscallError", 1, JS_CFUNC_constructor, 0);

    syscallerror_proto = JS_NewObjectProto(ctx, error);
    // syscallerror_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, syscallerror_ctor, js_syscallerror_defines, countof(js_syscallerror_defines));
    JS_SetPropertyFunctionList(ctx, syscallerror_proto, js_syscallerror_proto_funcs, countof(js_syscallerror_proto_funcs));
    JS_SetClassProto(ctx, js_syscallerror_class_id, syscallerror_proto);

    JS_SetConstructor(ctx, syscallerror_ctor, syscallerror_proto);

    //  js_set_inspect_method(ctx, syscallerror_proto, js_syscallerror_inspect);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "SyscallError", syscallerror_ctor);
    JS_SetModuleExportList(ctx, m, js_syscallerror_defines, countof(js_syscallerror_defines));
  }

  JS_FreeValue(ctx, error);

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

  if((m = JS_NewCModule(ctx, module_name, js_syscallerror_init))) {
    JS_AddModuleExport(ctx, m, "SyscallError");
    JS_AddModuleExportList(ctx, m, js_syscallerror_defines, countof(js_syscallerror_defines));
  }

  return m;
}

const char* const errors[] = {
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
    "EWOULDBLOCK",
    "ENOMEM",
    "EACCES",
    "EFAULT",
    0,
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
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    "ENOLINK",
    0,
    0,
    0,
    "EPROTO",
    0,
    0,
    "EBADMSG",
    "EOVERFLOW",
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    "EILSEQ",
    "ERESTART",
    0,
    0,
    "ENOTSOCK",
    "EDESTADDRREQ",
    "EMSGSIZE",
    "EPROTOTYPE",
    "ENOPROTOOPT",
    "EPROTONOSUPPORT",
    0,
    "EOPNOTSUPP",
    0,
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
    0,
    0,
    "ETIMEDOUT",
    "ECONNREFUSED",
    0,
    "EHOSTUNREACH",
    "EALREADY",
    "EINPROGRESS",
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    "ECANCELED",
    0,
    0,
    0,
    0,
    "EOWNERDEAD",
    "ENOTRECOVERABLE",
};

const size_t errors_size = countof(errors);

/**
 * @}
 */
