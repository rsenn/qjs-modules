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
static const char* error_get(int number);

static char*
stack_get(JSContext* ctx) {
  JSValue error = js_global_new(ctx, "Error", 0, 0);
  JSValue st = JS_GetPropertyStr(ctx, error, "stack");
  const char* stack = JS_ToCString(ctx, st);
  size_t pos = str_chr(stack, '\n');

  if(stack[pos])
    pos++;

  char* ret = js_strdup(ctx, stack + pos);
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
  JSValue obj = JS_NewObjectProtoClass(ctx, syscallerror_proto, js_syscallerror_class_id);

  JS_SetOpaque(obj, err);

  return obj;
}

JSValue
js_syscallerror_new(JSContext* ctx, const char* syscall, int number) {
  SyscallError* err;
  JSValue obj;

  if(js_syscallerror_class_id == 0)
    js_syscallerror_init(ctx, 0);

  if(!(err = syscallerror_new(ctx, syscall, number)))
    return JS_EXCEPTION;

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
  JSValue proto, obj = JS_UNDEFINED, st = JS_UNDEFINED;

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

    JS_ToInt32(ctx, &number, argv[0]);
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
    dbuf_put(dbuf, (const void*)buf, fmt_longlong(buf, err->number));
    dbuf_putstr(dbuf, ")");
  }

  if(err->number) {
    if(err->syscall)
      dbuf_putstr(dbuf, ": ");

#if defined(_WIN32) && !defined(__MSYS__) && !defined(__CYGWIN__)
    dbuf->size += FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err->number, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (void*)dbuf_reserve(dbuf, 256), 256, NULL);

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
      DynBuf dbuf;

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

enum {
  PROP_SYSCALL,
  PROP_CODE,
  PROP_ERRNO,
  PROP_STACK,
  PROP_MESSAGE,
};

static JSValue
js_syscallerror_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue ret = JS_UNDEFINED;
  SyscallError* err = js_syscallerror_data(this_val);

  switch(magic) {
    case PROP_SYSCALL: {
      if(err)
        ret = err->syscall ? JS_NewString(ctx, err->syscall) : JS_NULL;

      break;
    }

    case PROP_CODE: {
      const char* code;

      if(err)
        ret = (code = error_get(err->number)) ? JS_NewString(ctx, code) : JS_NULL;

      break;
    }

    case PROP_ERRNO: {
      if(err)
        ret = JS_NewInt32(ctx, err->number);

      break;
    }

    case PROP_STACK: {
      if(err)
        ret = err->stack ? JS_NewString(ctx, err->stack) : JS_NULL;

      break;
    }

    case PROP_MESSAGE: {
      DynBuf dbuf;

      js_dbuf_init(ctx, &dbuf);
      syscallerror_dump(err, &dbuf);
      ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, byte_chr(dbuf.buf, dbuf.size, '\n'));
      break;
    }
  }

  return ret;
}

const JSCFunctionListEntry js_syscallerror_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("syscall", js_syscallerror_get, 0, PROP_SYSCALL, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("errno", js_syscallerror_get, 0, PROP_ERRNO, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("message", js_syscallerror_get, 0, PROP_MESSAGE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("name", js_syscallerror_get, 0, PROP_CODE),
    JS_CGETSET_MAGIC_DEF("stack", js_syscallerror_get, 0, PROP_STACK),
    JS_CFUNC_MAGIC_DEF("toString", 0, js_syscallerror_method, SYSCALLERROR_TOSTRING),
    JS_ALIAS_DEF("[Symbol.toPrimitive]", "toString"),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SyscallError", JS_PROP_CONFIGURABLE),
};

const JSCFunctionListEntry js_syscallerror_defines[] = {
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
#ifdef WSAEINTR
    JS_CONSTANT(WSAEINTR),
#endif
#ifdef WSAEBADF
    JS_CONSTANT(WSAEBADF),
#endif
#ifdef WSAEACCES
    JS_CONSTANT(WSAEACCES),
#endif
#ifdef WSAEFAULT
    JS_CONSTANT(WSAEFAULT),
#endif
#ifdef WSAEINVAL
    JS_CONSTANT(WSAEINVAL),
#endif
#ifdef WSAEMFILE
    JS_CONSTANT(WSAEMFILE),
#endif
#ifdef WSAEWOULDBLOCK
    JS_CONSTANT(WSAEWOULDBLOCK),
#endif
#ifdef WSAEINPROGRESS
    JS_CONSTANT(WSAEINPROGRESS),
#endif
#ifdef WSAEALREADY
    JS_CONSTANT(WSAEALREADY),
#endif
#ifdef WSAENOTSOCK
    JS_CONSTANT(WSAENOTSOCK),
#endif
#ifdef WSAEDESTADDRREQ
    JS_CONSTANT(WSAEDESTADDRREQ),
#endif
#ifdef WSAEMSGSIZE
    JS_CONSTANT(WSAEMSGSIZE),
#endif
#ifdef WSAEPROTOTYPE
    JS_CONSTANT(WSAEPROTOTYPE),
#endif
#ifdef WSAENOPROTOOPT
    JS_CONSTANT(WSAENOPROTOOPT),
#endif
#ifdef WSAEPROTONOSUPPORT
    JS_CONSTANT(WSAEPROTONOSUPPORT),
#endif
#ifdef WSAESOCKTNOSUPPORT
    JS_CONSTANT(WSAESOCKTNOSUPPORT),
#endif
#ifdef WSAEOPNOTSUPP
    JS_CONSTANT(WSAEOPNOTSUPP),
#endif
#ifdef WSAEPFNOSUPPORT
    JS_CONSTANT(WSAEPFNOSUPPORT),
#endif
#ifdef WSAEAFNOSUPPORT
    JS_CONSTANT(WSAEAFNOSUPPORT),
#endif
#ifdef WSAEADDRINUSE
    JS_CONSTANT(WSAEADDRINUSE),
#endif
#ifdef WSAEADDRNOTAVAIL
    JS_CONSTANT(WSAEADDRNOTAVAIL),
#endif
#ifdef WSAENETDOWN
    JS_CONSTANT(WSAENETDOWN),
#endif
#ifdef WSAENETUNREACH
    JS_CONSTANT(WSAENETUNREACH),
#endif
#ifdef WSAENETRESET
    JS_CONSTANT(WSAENETRESET),
#endif
#ifdef WSAECONNABORTED
    JS_CONSTANT(WSAECONNABORTED),
#endif
#ifdef WSAECONNRESET
    JS_CONSTANT(WSAECONNRESET),
#endif
#ifdef WSAENOBUFS
    JS_CONSTANT(WSAENOBUFS),
#endif
#ifdef WSAEISCONN
    JS_CONSTANT(WSAEISCONN),
#endif
#ifdef WSAENOTCONN
    JS_CONSTANT(WSAENOTCONN),
#endif
#ifdef WSAESHUTDOWN
    JS_CONSTANT(WSAESHUTDOWN),
#endif
#ifdef WSAETOOMANYREFS
    JS_CONSTANT(WSAETOOMANYREFS),
#endif
#ifdef WSAETIMEDOUT
    JS_CONSTANT(WSAETIMEDOUT),
#endif
#ifdef WSAECONNREFUSED
    JS_CONSTANT(WSAECONNREFUSED),
#endif
#ifdef WSAELOOP
    JS_CONSTANT(WSAELOOP),
#endif
#ifdef WSAENAMETOOLONG
    JS_CONSTANT(WSAENAMETOOLONG),
#endif
#ifdef WSAEHOSTDOWN
    JS_CONSTANT(WSAEHOSTDOWN),
#endif
#ifdef WSAEHOSTUNREACH
    JS_CONSTANT(WSAEHOSTUNREACH),
#endif
#ifdef WSAENOTEMPTY
    JS_CONSTANT(WSAENOTEMPTY),
#endif
#ifdef WSAEPROCLIM
    JS_CONSTANT(WSAEPROCLIM),
#endif
#ifdef WSAEUSERS
    JS_CONSTANT(WSAEUSERS),
#endif
#ifdef WSAEDQUOT
    JS_CONSTANT(WSAEDQUOT),
#endif
#ifdef WSAESTALE
    JS_CONSTANT(WSAESTALE),
#endif
#ifdef WSAEREMOTE
    JS_CONSTANT(WSAEREMOTE),
#endif
#ifdef WSAEDISCON
    JS_CONSTANT(WSAEDISCON),
#endif
#ifdef WSAENOMORE
    JS_CONSTANT(WSAENOMORE),
#endif
#ifdef WSAECANCELLED
    JS_CONSTANT(WSAECANCELLED),
#endif
#ifdef WSAEINVALIDPROCTABLE
    JS_CONSTANT(WSAEINVALIDPROCTABLE),
#endif
#ifdef WSAEINVALIDPROVIDER
    JS_CONSTANT(WSAEINVALIDPROVIDER),
#endif
#ifdef WSAEPROVIDERFAILEDINIT
    JS_CONSTANT(WSAEPROVIDERFAILEDINIT),
#endif
#ifdef WSAEREFUSED
    JS_CONSTANT(WSAEREFUSED),
#endif
};

static void
js_syscallerror_finalizer(JSRuntime* rt, JSValue val) {
  SyscallError* err;

  if((err = JS_GetOpaque(val, js_syscallerror_class_id))) {
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

  JS_NewClassID(&js_syscallerror_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_syscallerror_class_id, &js_syscallerror_class);

  syscallerror_ctor = JS_NewCFunction2(ctx, js_syscallerror_constructor, "SyscallError", 1, JS_CFUNC_constructor, 0);
  syscallerror_proto = JS_NewObjectProto(ctx, error);

  JS_FreeValue(ctx, error);

  JS_SetPropertyFunctionList(ctx, syscallerror_ctor, js_syscallerror_defines, countof(js_syscallerror_defines));
  JS_SetPropertyFunctionList(ctx, syscallerror_proto, js_syscallerror_proto_funcs, countof(js_syscallerror_proto_funcs));

  JS_SetClassProto(ctx, js_syscallerror_class_id, syscallerror_proto);
  JS_SetConstructor(ctx, syscallerror_ctor, syscallerror_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "SyscallError", syscallerror_ctor);
    JS_SetModuleExportList(ctx, m, js_syscallerror_defines, countof(js_syscallerror_defines));
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

  if((m = JS_NewCModule(ctx, module_name, js_syscallerror_init))) {
    JS_AddModuleExport(ctx, m, "SyscallError");
    JS_AddModuleExportList(ctx, m, js_syscallerror_defines, countof(js_syscallerror_defines));
  }

  return m;
}

static const char*
error_get(int number) {
  switch(number) {
#ifdef EPERM
    case EPERM: return "EPERM";
#endif
#ifdef ENOENT
    case ENOENT: return "ENOENT";
#endif
#ifdef ESRCH
    case ESRCH: return "ESRCH";
#endif
#ifdef EINTR
    case EINTR: return "EINTR";
#endif
#ifdef EIO
    case EIO: return "EIO";
#endif
#ifdef ENXIO
    case ENXIO: return "ENXIO";
#endif
#ifdef E2BIG
    case E2BIG: return "E2BIG";
#endif
#ifdef ENOEXEC
    case ENOEXEC: return "ENOEXEC";
#endif
#ifdef EBADF
    case EBADF: return "EBADF";
#endif
#ifdef ECHILD
    case ECHILD: return "ECHILD";
#endif
#ifdef EAGAIN
    case EAGAIN: return "EAGAIN";
#endif
#ifdef ENOMEM
    case ENOMEM: return "ENOMEM";
#endif
#ifdef EACCES
    case EACCES: return "EACCES";
#endif
#ifdef EFAULT
    case EFAULT: return "EFAULT";
#endif
#ifdef EBUSY
    case EBUSY: return "EBUSY";
#endif
#ifdef EEXIST
    case EEXIST: return "EEXIST";
#endif
#ifdef EXDEV
    case EXDEV: return "EXDEV";
#endif
#ifdef ENODEV
    case ENODEV: return "ENODEV";
#endif
#ifdef ENOTDIR
    case ENOTDIR: return "ENOTDIR";
#endif
#ifdef EISDIR
    case EISDIR: return "EISDIR";
#endif
#ifdef EINVAL
    case EINVAL: return "EINVAL";
#endif
#ifdef ENFILE
    case ENFILE: return "ENFILE";
#endif
#ifdef EMFILE
    case EMFILE: return "EMFILE";
#endif
#ifdef ENOTTY
    case ENOTTY: return "ENOTTY";
#endif
#ifdef ETXTBSY
    case ETXTBSY: return "ETXTBSY";
#endif
#ifdef EFBIG
    case EFBIG: return "EFBIG";
#endif
#ifdef ENOSPC
    case ENOSPC: return "ENOSPC";
#endif
#ifdef ESPIPE
    case ESPIPE: return "ESPIPE";
#endif
#ifdef EROFS
    case EROFS: return "EROFS";
#endif
#ifdef EMLINK
    case EMLINK: return "EMLINK";
#endif
#ifdef EPIPE
    case EPIPE: return "EPIPE";
#endif
#ifdef EDOM
    case EDOM: return "EDOM";
#endif
#ifdef ERANGE
    case ERANGE: return "ERANGE";
#endif
#ifdef EDEADLK
    case EDEADLK: return "EDEADLK";
#endif
#ifdef ENAMETOOLONG
    case ENAMETOOLONG: return "ENAMETOOLONG";
#endif
#ifdef ENOLCK
    case ENOLCK: return "ENOLCK";
#endif
#ifdef ENOSYS
    case ENOSYS: return "ENOSYS";
#endif
#ifdef ENOTEMPTY
    case ENOTEMPTY: return "ENOTEMPTY";
#endif
#ifdef ENOMSG
    case ENOMSG: return "ENOMSG";
#endif
#ifdef EIDRM
    case EIDRM: return "EIDRM";
#endif
#ifdef ENOLINK
    case ENOLINK: return "ENOLINK";
#endif
#ifdef EPROTO
    case EPROTO: return "EPROTO";
#endif
#ifdef EBADMSG
    case EBADMSG: return "EBADMSG";
#endif
#ifdef EOVERFLOW
    case EOVERFLOW: return "EOVERFLOW";
#endif
#ifdef EILSEQ
    case EILSEQ: return "EILSEQ";
#endif
#ifdef ERESTART
    case ERESTART: return "ERESTART";
#endif
#ifdef ENOTSOCK
    case ENOTSOCK: return "ENOTSOCK";
#endif
#ifdef EDESTADDRREQ
    case EDESTADDRREQ: return "EDESTADDRREQ";
#endif
#ifdef EMSGSIZE
    case EMSGSIZE: return "EMSGSIZE";
#endif
#ifdef EPROTOTYPE
    case EPROTOTYPE: return "EPROTOTYPE";
#endif
#ifdef ENOPROTOOPT
    case ENOPROTOOPT: return "ENOPROTOOPT";
#endif
#ifdef EPROTONOSUPPORT
    case EPROTONOSUPPORT: return "EPROTONOSUPPORT";
#endif
#ifdef EOPNOTSUPP
    case EOPNOTSUPP: return "EOPNOTSUPP";
#endif
#ifdef EAFNOSUPPORT
    case EAFNOSUPPORT: return "EAFNOSUPPORT";
#endif
#ifdef EADDRINUSE
    case EADDRINUSE: return "EADDRINUSE";
#endif
#ifdef EADDRNOTAVAIL
    case EADDRNOTAVAIL: return "EADDRNOTAVAIL";
#endif
#ifdef ENETDOWN
    case ENETDOWN: return "ENETDOWN";
#endif
#ifdef ENETUNREACH
    case ENETUNREACH: return "ENETUNREACH";
#endif
#ifdef ENETRESET
    case ENETRESET: return "ENETRESET";
#endif
#ifdef ECONNABORTED
    case ECONNABORTED: return "ECONNABORTED";
#endif
#ifdef ECONNRESET
    case ECONNRESET: return "ECONNRESET";
#endif
#ifdef ENOBUFS
    case ENOBUFS: return "ENOBUFS";
#endif
#ifdef EISCONN
    case EISCONN: return "EISCONN";
#endif
#ifdef ENOTCONN
    case ENOTCONN: return "ENOTCONN";
#endif
#ifdef ETIMEDOUT
    case ETIMEDOUT: return "ETIMEDOUT";
#endif
#ifdef ECONNREFUSED
    case ECONNREFUSED: return "ECONNREFUSED";
#endif
#ifdef EHOSTUNREACH
    case EHOSTUNREACH: return "EHOSTUNREACH";
#endif
#ifdef EALREADY
    case EALREADY: return "EALREADY";
#endif
#ifdef EINPROGRESS
    case EINPROGRESS: return "EINPROGRESS";
#endif
#ifdef ECANCELED
    case ECANCELED: return "ECANCELED";
#endif
#ifdef EOWNERDEAD
    case EOWNERDEAD: return "EOWNERDEAD";
#endif
#ifdef ENOTRECOVERABLE
    case ENOTRECOVERABLE: return "ENOTRECOVERABLE";
#endif
#ifdef WSAEINTR
    case WSAEINTR: return "WSAEINTR";
#endif
#ifdef WSAEBADF
    case WSAEBADF: return "WSAEBADF";
#endif
#ifdef WSAEACCES
    case WSAEACCES: return "WSAEACCES";
#endif
#ifdef WSAEFAULT
    case WSAEFAULT: return "WSAEFAULT";
#endif
#ifdef WSAEINVAL
    case WSAEINVAL: return "WSAEINVAL";
#endif
#ifdef WSAEMFILE
    case WSAEMFILE: return "WSAEMFILE";
#endif
#ifdef WSAEWOULDBLOCK
    case WSAEWOULDBLOCK: return "WSAEWOULDBLOCK";
#endif
#ifdef WSAEINPROGRESS
    case WSAEINPROGRESS: return "WSAEINPROGRESS";
#endif
#ifdef WSAEALREADY
    case WSAEALREADY: return "WSAEALREADY";
#endif
#ifdef WSAENOTSOCK
    case WSAENOTSOCK: return "WSAENOTSOCK";
#endif
#ifdef WSAEDESTADDRREQ
    case WSAEDESTADDRREQ: return "WSAEDESTADDRREQ";
#endif
#ifdef WSAEMSGSIZE
    case WSAEMSGSIZE: return "WSAEMSGSIZE";
#endif
#ifdef WSAEPROTOTYPE
    case WSAEPROTOTYPE: return "WSAEPROTOTYPE";
#endif
#ifdef WSAENOPROTOOPT
    case WSAENOPROTOOPT: return "WSAENOPROTOOPT";
#endif
#ifdef WSAEPROTONOSUPPORT
    case WSAEPROTONOSUPPORT: return "WSAEPROTONOSUPPORT";
#endif
#ifdef WSAESOCKTNOSUPPORT
    case WSAESOCKTNOSUPPORT: return "WSAESOCKTNOSUPPORT";
#endif
#ifdef WSAEOPNOTSUPP
    case WSAEOPNOTSUPP: return "WSAEOPNOTSUPP";
#endif
#ifdef WSAEPFNOSUPPORT
    case WSAEPFNOSUPPORT: return "WSAEPFNOSUPPORT";
#endif
#ifdef WSAEAFNOSUPPORT
    case WSAEAFNOSUPPORT: return "WSAEAFNOSUPPORT";
#endif
#ifdef WSAEADDRINUSE
    case WSAEADDRINUSE: return "WSAEADDRINUSE";
#endif
#ifdef WSAEADDRNOTAVAIL
    case WSAEADDRNOTAVAIL: return "WSAEADDRNOTAVAIL";
#endif
#ifdef WSAENETDOWN
    case WSAENETDOWN: return "WSAENETDOWN";
#endif
#ifdef WSAENETUNREACH
    case WSAENETUNREACH: return "WSAENETUNREACH";
#endif
#ifdef WSAENETRESET
    case WSAENETRESET: return "WSAENETRESET";
#endif
#ifdef WSAECONNABORTED
    case WSAECONNABORTED: return "WSAECONNABORTED";
#endif
#ifdef WSAECONNRESET
    case WSAECONNRESET: return "WSAECONNRESET";
#endif
#ifdef WSAENOBUFS
    case WSAENOBUFS: return "WSAENOBUFS";
#endif
#ifdef WSAEISCONN
    case WSAEISCONN: return "WSAEISCONN";
#endif
#ifdef WSAENOTCONN
    case WSAENOTCONN: return "WSAENOTCONN";
#endif
#ifdef WSAESHUTDOWN
    case WSAESHUTDOWN: return "WSAESHUTDOWN";
#endif
#ifdef WSAETOOMANYREFS
    case WSAETOOMANYREFS: return "WSAETOOMANYREFS";
#endif
#ifdef WSAETIMEDOUT
    case WSAETIMEDOUT: return "WSAETIMEDOUT";
#endif
#ifdef WSAECONNREFUSED
    case WSAECONNREFUSED: return "WSAECONNREFUSED";
#endif
#ifdef WSAELOOP
    case WSAELOOP: return "WSAELOOP";
#endif
#ifdef WSAENAMETOOLONG
    case WSAENAMETOOLONG: return "WSAENAMETOOLONG";
#endif
#ifdef WSAEHOSTDOWN
    case WSAEHOSTDOWN: return "WSAEHOSTDOWN";
#endif
#ifdef WSAEHOSTUNREACH
    case WSAEHOSTUNREACH: return "WSAEHOSTUNREACH";
#endif
#ifdef WSAENOTEMPTY
    case WSAENOTEMPTY: return "WSAENOTEMPTY";
#endif
#ifdef WSAEPROCLIM
    case WSAEPROCLIM: return "WSAEPROCLIM";
#endif
#ifdef WSAEUSERS
    case WSAEUSERS: return "WSAEUSERS";
#endif
#ifdef WSAEDQUOT
    case WSAEDQUOT: return "WSAEDQUOT";
#endif
#ifdef WSAESTALE
    case WSAESTALE: return "WSAESTALE";
#endif
#ifdef WSAEREMOTE
    case WSAEREMOTE: return "WSAEREMOTE";
#endif
#ifdef WSAEDISCON
    case WSAEDISCON: return "WSAEDISCON";
#endif
#ifdef WSAENOMORE
    case WSAENOMORE: return "WSAENOMORE";
#endif
#ifdef WSAECANCELLED
    case WSAECANCELLED: return "WSAECANCELLED";
#endif
#ifdef WSAEINVALIDPROCTABLE
    case WSAEINVALIDPROCTABLE: return "WSAEINVALIDPROCTABLE";
#endif
#ifdef WSAEINVALIDPROVIDER
    case WSAEINVALIDPROVIDER: return "WSAEINVALIDPROVIDER";
#endif
#ifdef WSAEPROVIDERFAILEDINIT
    case WSAEPROVIDERFAILEDINIT: return "WSAEPROVIDERFAILEDINIT";
#endif
#ifdef WSAEREFUSED
    case WSAEREFUSED: return "WSAEREFUSED";
#endif
    default: break;
  }

  return 0;
}

/**
 * @}
 */
