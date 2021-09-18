#include "quickjs-syscallerror.h"
#include "buffer-utils.h"
#include "char-utils.h"
#include <errno.h>

/*thread_local */ VISIBLE JSClassID js_syscallerror_class_id = 0;
/*thread_local*/ JSValue syscallerror_proto = {JS_TAG_UNDEFINED}, syscallerror_ctor = {JS_TAG_UNDEFINED};

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

static const char*
stack_get(JSContext* ctx) {
  const char *stack, *ret;
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
  JSValue obj = JS_UNDEFINED, proto = JS_UNDEFINED, st;

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
js_syscallerror_dump(JSContext* ctx, JSValueConst this_val, DynBuf* dbuf) {
  SyscallError* err;
  /*  if(!(err = js_syscallerror_data2(ctx, this_val)))
      return;*/
  if((err = js_syscallerror_data(this_val))) {
    // dbuf_putstr(dbuf, "SyscallError: ");

    if(err->syscall) {
      dbuf_putstr(dbuf, err->syscall);
      dbuf_putstr(dbuf, "() ");
    }
    if(err->number) {
      const char* msg;

      if((msg = strerror(err->number)))
        dbuf_putstr(dbuf, msg);
    }
    if(err->stack) {
      dbuf_putc(dbuf, '\n');
      dbuf_putstr(dbuf, err->stack);
    }
    dbuf_0(dbuf);
  }
}

enum { SYSCALLERROR_TOSTRING, SYSCALLERROR_VALUEOF };

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
      js_syscallerror_dump(ctx, this_val, &dbuf);
      ret = JS_NewStringLen(ctx, dbuf.buf, dbuf.size);
      break;
    }
    case SYSCALLERROR_VALUEOF: {
      ret = JS_NewInt32(ctx, err->number);
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
  JSValue obj = JS_NewObjectProto(ctx, syscallerror_proto);

  JS_DefinePropertyValueStr(ctx, obj, "errno", JS_NewInt32(ctx, err->number), JS_PROP_C_W_E);
  JS_DefinePropertyValueStr(ctx, obj, "message", JS_GetPropertyStr(ctx, this_val, "message"), JS_PROP_C_W_E);

  if(err->syscall)
    JS_DefinePropertyValueStr(ctx, obj, "syscall", JS_NewString(ctx, err->syscall), JS_PROP_C_W_E);

  if((code = error_get(err->number)))
    JS_DefinePropertyValueStr(ctx, obj, "code", JS_NewString(ctx, code), JS_PROP_C_W_E);

  if(js_has_propertystr(ctx, obj, "stack"))
    JS_DefinePropertyValueStr(
        ctx, obj, "stack", JS_GetPropertyStr(ctx, this_val, "stack"), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);

  return obj;
}

enum {
  SYSCALLERROR_PROP_SYSCALL,
  SYSCALLERROR_PROP_CODE,
  SYSCALLERROR_PROP_ERRNO,
  SYSCALLERROR_PROP_STACK,
  SYSCALLERROR_PROP_MESSAGE
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
      js_syscallerror_dump(ctx, this_val, &dbuf);
      ret = JS_NewStringLen(ctx, dbuf.buf, byte_chr(dbuf.buf, dbuf.size, '\n'));
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
    //
    JS_CGETSET_MAGIC_DEF("syscall", js_syscallerror_get, js_syscallerror_set, SYSCALLERROR_PROP_SYSCALL),
    JS_CGETSET_MAGIC_DEF("name", js_syscallerror_get, js_syscallerror_set, SYSCALLERROR_PROP_CODE),
    JS_ALIAS_DEF("code", "name"),
    JS_CGETSET_MAGIC_FLAGS_DEF("errno",
                               js_syscallerror_get,
                               js_syscallerror_set,
                               SYSCALLERROR_PROP_ERRNO,
                               JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    JS_ALIAS_DEF("number", "errno"),
    JS_CGETSET_MAGIC_DEF("stack", js_syscallerror_get, js_syscallerror_set, SYSCALLERROR_PROP_STACK),
    JS_CGETSET_MAGIC_FLAGS_DEF("message",
                               js_syscallerror_get,
                               js_syscallerror_set,
                               SYSCALLERROR_PROP_MESSAGE,
                               JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    JS_CFUNC_MAGIC_DEF("toString", 0, js_syscallerror_method, SYSCALLERROR_TOSTRING),
    JS_CFUNC_MAGIC_DEF("valueOf", 0, js_syscallerror_method, SYSCALLERROR_VALUEOF),
    JS_ALIAS_DEF("[Symbol.toPrimitive]", "valueOf"),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SyscallError", JS_PROP_CONFIGURABLE),
};
const size_t js_syscallerror_proto_funcs_size = countof(js_syscallerror_proto_funcs);

static const JSCFunctionListEntry js_syscallerror_defs[] = {
    JS_PROP_INT32_DEF("EPERM", EPERM, 0),
    JS_PROP_INT32_DEF("ENOENT", ENOENT, 0),
    JS_PROP_INT32_DEF("ESRCH", ESRCH, 0),
    JS_PROP_INT32_DEF("EINTR", EINTR, 0),
    JS_PROP_INT32_DEF("EIO", EIO, 0),
    JS_PROP_INT32_DEF("ENXIO", ENXIO, 0),
    JS_PROP_INT32_DEF("E2BIG", E2BIG, 0),
    JS_PROP_INT32_DEF("ENOEXEC", ENOEXEC, 0),
    JS_PROP_INT32_DEF("EBADF", EBADF, 0),
    JS_PROP_INT32_DEF("ECHILD", ECHILD, 0),
    JS_PROP_INT32_DEF("EAGAIN", EAGAIN, 0),
    JS_PROP_INT32_DEF("ENOMEM", ENOMEM, 0),
    JS_PROP_INT32_DEF("EACCES", EACCES, 0),
    JS_PROP_INT32_DEF("EFAULT", EFAULT, 0),
    JS_PROP_INT32_DEF("ENOTBLK", ENOTBLK, 0),
    JS_PROP_INT32_DEF("EBUSY", EBUSY, 0),
    JS_PROP_INT32_DEF("EEXIST", EEXIST, 0),
    JS_PROP_INT32_DEF("EXDEV", EXDEV, 0),
    JS_PROP_INT32_DEF("ENODEV", ENODEV, 0),
    JS_PROP_INT32_DEF("ENOTDIR", ENOTDIR, 0),
    JS_PROP_INT32_DEF("EISDIR", EISDIR, 0),
    JS_PROP_INT32_DEF("EINVAL", EINVAL, 0),
    JS_PROP_INT32_DEF("ENFILE", ENFILE, 0),
    JS_PROP_INT32_DEF("EMFILE", EMFILE, 0),
    JS_PROP_INT32_DEF("ENOTTY", ENOTTY, 0),
    JS_PROP_INT32_DEF("ETXTBSY", ETXTBSY, 0),
    JS_PROP_INT32_DEF("EFBIG", EFBIG, 0),
    JS_PROP_INT32_DEF("ENOSPC", ENOSPC, 0),
    JS_PROP_INT32_DEF("ESPIPE", ESPIPE, 0),
    JS_PROP_INT32_DEF("EROFS", EROFS, 0),
    JS_PROP_INT32_DEF("EMLINK", EMLINK, 0),
    JS_PROP_INT32_DEF("EPIPE", EPIPE, 0),
    JS_PROP_INT32_DEF("EDOM", EDOM, 0),
    JS_PROP_INT32_DEF("ERANGE", ERANGE, 0),
    JS_PROP_INT32_DEF("EDEADLK", EDEADLK, 0),
    JS_PROP_INT32_DEF("ENAMETOOLONG", ENAMETOOLONG, 0),
    JS_PROP_INT32_DEF("ENOLCK", ENOLCK, 0),
    JS_PROP_INT32_DEF("ENOSYS", ENOSYS, 0),
    JS_PROP_INT32_DEF("ENOTEMPTY", ENOTEMPTY, 0),
    JS_PROP_INT32_DEF("ENOMSG", ENOMSG, 0),
    JS_PROP_INT32_DEF("EIDRM", EIDRM, 0),
    JS_PROP_INT32_DEF("ECHRNG", ECHRNG, 0),
    JS_PROP_INT32_DEF("EL2NSYNC", EL2NSYNC, 0),
    JS_PROP_INT32_DEF("EL3HLT", EL3HLT, 0),
    JS_PROP_INT32_DEF("EL3RST", EL3RST, 0),
    JS_PROP_INT32_DEF("ELNRNG", ELNRNG, 0),
    JS_PROP_INT32_DEF("EUNATCH", EUNATCH, 0),
    JS_PROP_INT32_DEF("ENOCSI", ENOCSI, 0),
    JS_PROP_INT32_DEF("EL2HLT", EL2HLT, 0),
    JS_PROP_INT32_DEF("EBADE", EBADE, 0),
    JS_PROP_INT32_DEF("EBADR", EBADR, 0),
    JS_PROP_INT32_DEF("EXFULL", EXFULL, 0),
    JS_PROP_INT32_DEF("ENOANO", ENOANO, 0),
    JS_PROP_INT32_DEF("EBADRQC", EBADRQC, 0),
    JS_PROP_INT32_DEF("EBFONT", EBFONT, 0),
    JS_PROP_INT32_DEF("ENOSTR", ENOSTR, 0),
    JS_PROP_INT32_DEF("ENODATA", ENODATA, 0),
    JS_PROP_INT32_DEF("ETIME", ETIME, 0),
    JS_PROP_INT32_DEF("ENOSR", ENOSR, 0),
    JS_PROP_INT32_DEF("ENONET", ENONET, 0),
    JS_PROP_INT32_DEF("ENOPKG", ENOPKG, 0),
    JS_PROP_INT32_DEF("EREMOTE", EREMOTE, 0),
    JS_PROP_INT32_DEF("ENOLINK", ENOLINK, 0),
    JS_PROP_INT32_DEF("EADV", EADV, 0),
    JS_PROP_INT32_DEF("ESRMNT", ESRMNT, 0),
    JS_PROP_INT32_DEF("ECOMM", ECOMM, 0),
    JS_PROP_INT32_DEF("EPROTO", EPROTO, 0),
    JS_PROP_INT32_DEF("EMULTIHOP", EMULTIHOP, 0),
    JS_PROP_INT32_DEF("EDOTDOT", EDOTDOT, 0),
    JS_PROP_INT32_DEF("EBADMSG", EBADMSG, 0),
    JS_PROP_INT32_DEF("EOVERFLOW", EOVERFLOW, 0),
    JS_PROP_INT32_DEF("ENOTUNIQ", ENOTUNIQ, 0),
    JS_PROP_INT32_DEF("EBADFD", EBADFD, 0),
    JS_PROP_INT32_DEF("EREMCHG", EREMCHG, 0),
    JS_PROP_INT32_DEF("ELIBACC", ELIBACC, 0),
    JS_PROP_INT32_DEF("ELIBBAD", ELIBBAD, 0),
    JS_PROP_INT32_DEF("ELIBSCN", ELIBSCN, 0),
    JS_PROP_INT32_DEF("ELIBMAX", ELIBMAX, 0),
    JS_PROP_INT32_DEF("ELIBEXEC", ELIBEXEC, 0),
    JS_PROP_INT32_DEF("EILSEQ", EILSEQ, 0),
    JS_PROP_INT32_DEF("ERESTART", ERESTART, 0),
    JS_PROP_INT32_DEF("ESTRPIPE", ESTRPIPE, 0),
    JS_PROP_INT32_DEF("EUSERS", EUSERS, 0),
    JS_PROP_INT32_DEF("ENOTSOCK", ENOTSOCK, 0),
    JS_PROP_INT32_DEF("EDESTADDRREQ", EDESTADDRREQ, 0),
    JS_PROP_INT32_DEF("EMSGSIZE", EMSGSIZE, 0),
    JS_PROP_INT32_DEF("EPROTOTYPE", EPROTOTYPE, 0),
    JS_PROP_INT32_DEF("ENOPROTOOPT", ENOPROTOOPT, 0),
    JS_PROP_INT32_DEF("EPROTONOSUPPORT", EPROTONOSUPPORT, 0),
    JS_PROP_INT32_DEF("ESOCKTNOSUPPORT", ESOCKTNOSUPPORT, 0),
    JS_PROP_INT32_DEF("EOPNOTSUPP", EOPNOTSUPP, 0),
    JS_PROP_INT32_DEF("EPFNOSUPPORT", EPFNOSUPPORT, 0),
    JS_PROP_INT32_DEF("EAFNOSUPPORT", EAFNOSUPPORT, 0),
    JS_PROP_INT32_DEF("EADDRINUSE", EADDRINUSE, 0),
    JS_PROP_INT32_DEF("EADDRNOTAVAIL", EADDRNOTAVAIL, 0),
    JS_PROP_INT32_DEF("ENETDOWN", ENETDOWN, 0),
    JS_PROP_INT32_DEF("ENETUNREACH", ENETUNREACH, 0),
    JS_PROP_INT32_DEF("ENETRESET", ENETRESET, 0),
    JS_PROP_INT32_DEF("ECONNABORTED", ECONNABORTED, 0),
    JS_PROP_INT32_DEF("ECONNRESET", ECONNRESET, 0),
    JS_PROP_INT32_DEF("ENOBUFS", ENOBUFS, 0),
    JS_PROP_INT32_DEF("EISCONN", EISCONN, 0),
    JS_PROP_INT32_DEF("ENOTCONN", ENOTCONN, 0),
    JS_PROP_INT32_DEF("ESHUTDOWN", ESHUTDOWN, 0),
    JS_PROP_INT32_DEF("ETOOMANYREFS", ETOOMANYREFS, 0),
    JS_PROP_INT32_DEF("ETIMEDOUT", ETIMEDOUT, 0),
    JS_PROP_INT32_DEF("ECONNREFUSED", ECONNREFUSED, 0),
    JS_PROP_INT32_DEF("EHOSTDOWN", EHOSTDOWN, 0),
    JS_PROP_INT32_DEF("EHOSTUNREACH", EHOSTUNREACH, 0),
    JS_PROP_INT32_DEF("EALREADY", EALREADY, 0),
    JS_PROP_INT32_DEF("EINPROGRESS", EINPROGRESS, 0),
    JS_PROP_INT32_DEF("ESTALE", ESTALE, 0),
    JS_PROP_INT32_DEF("EUCLEAN", EUCLEAN, 0),
    JS_PROP_INT32_DEF("ENOTNAM", ENOTNAM, 0),
    JS_PROP_INT32_DEF("ENAVAIL", ENAVAIL, 0),
    JS_PROP_INT32_DEF("EISNAM", EISNAM, 0),
    JS_PROP_INT32_DEF("EREMOTEIO", EREMOTEIO, 0),
    JS_PROP_INT32_DEF("EDQUOT", EDQUOT, 0),
    JS_PROP_INT32_DEF("ENOMEDIUM", ENOMEDIUM, 0),
    JS_PROP_INT32_DEF("EMEDIUMTYPE", EMEDIUMTYPE, 0),
    JS_PROP_INT32_DEF("ECANCELED", ECANCELED, 0),
    JS_PROP_INT32_DEF("ENOKEY", ENOKEY, 0),
    JS_PROP_INT32_DEF("EKEYEXPIRED", EKEYEXPIRED, 0),
    JS_PROP_INT32_DEF("EKEYREVOKED", EKEYREVOKED, 0),
    JS_PROP_INT32_DEF("EKEYREJECTED", EKEYREJECTED, 0),
    JS_PROP_INT32_DEF("EOWNERDEAD", EOWNERDEAD, 0),
    JS_PROP_INT32_DEF("ENOTRECOVERABLE", ENOTRECOVERABLE, 0),
    JS_PROP_INT32_DEF("ERFKILL", ERFKILL, 0),
};

void
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

  /*if(!js_syscallerror_class_id && !JS_IsObject(syscallerror_ctor))*/ {
    JS_NewClassID(&js_syscallerror_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_syscallerror_class_id, &js_syscallerror_class);
    syscallerror_ctor = JS_NewCFunction2(ctx, js_syscallerror_constructor, "SyscallError", 1, JS_CFUNC_constructor, 0);

    syscallerror_proto = JS_NewObjectProto(ctx, error);
    // syscallerror_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, syscallerror_ctor, js_syscallerror_defs, countof(js_syscallerror_defs));
    JS_SetPropertyFunctionList(ctx,
                               syscallerror_proto,
                               js_syscallerror_proto_funcs,
                               countof(js_syscallerror_proto_funcs));
    JS_SetClassProto(ctx, js_syscallerror_class_id, syscallerror_proto);

    JS_SetConstructor(ctx, syscallerror_ctor, syscallerror_proto);

    //  js_set_inspect_method(ctx, syscallerror_proto, js_syscallerror_inspect);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "SyscallError", syscallerror_ctor);
    JS_SetModuleExportList(ctx, m, js_syscallerror_defs, countof(js_syscallerror_defs));
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
  m = JS_NewCModule(ctx, module_name, js_syscallerror_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "SyscallError");
  JS_AddModuleExportList(ctx, m, js_syscallerror_defs, countof(js_syscallerror_defs));
  return m;
}

const char* const errors[133] = {
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
const size_t errors_size = countof(errors);
