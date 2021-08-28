#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <quickjs-libc.h>
#include "quickjs-internal.h"
#include "quickjs-location.h"
#include "quickjs-stringdecoder.h"
#include "utils.h"
#include "path.h"
#include "base64.h"
#include <time.h>
#include <sys/utsname.h>
#include <errno.h>

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

enum {
  FUNC_GETEXECUTABLE = 0,
  FUNC_GETCWD,
  FUNC_GETROOT,
  FUNC_GETFD,
  FUNC_GETCOMMANDLINE,
  FUNC_GETPROCMAPS,
  FUNC_GETPROCMOUNTS,
  FUNC_GETPROCSTAT,
  FUNC_GETPID,
  FUNC_GETPPID,
  FUNC_GETSID,
  FUNC_GETUID,
  FUNC_GETGID,
  FUNC_GETEUID,
  FUNC_GETEGID,
  FUNC_SETUID,
  FUNC_SETGID,
  FUNC_SETEUID,
  FUNC_SETEGID
};

typedef struct pcg_state_setseq_64 {
  uint64_t state, inc;
} pcg32_random_t;

static pcg32_random_t pcg32_global = {0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL};

static inline uint32_t
pcg32_random_r(pcg32_random_t* rng) {
  uint64_t oldstate = rng->state;
  rng->state = oldstate * 6364136223846793005ULL + rng->inc;
  uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
  uint32_t rot = oldstate >> 59u;
  return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

static uint32_t
pcg32_random(void) {
  return pcg32_random_r(&pcg32_global);
}

static void
pcg32_init_state(uint32_t state) {
  pcg32_global.state = state;
}

static uint32_t
pcg32_random_bounded_divisionless(uint32_t range) {
  uint64_t random32bit, multiresult;
  uint32_t leftover;
  uint32_t threshold;
  random32bit = pcg32_random();
  multiresult = random32bit * range;
  leftover = (uint32_t)multiresult;
  if(leftover < range) {
    threshold = -range % range;
    while(leftover < threshold) {
      random32bit = pcg32_random();
      multiresult = random32bit * range;
      leftover = (uint32_t)multiresult;
    }
  }
  return multiresult >> 32; // [0, range)
}

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

static void
js_bytecode_free_func(JSRuntime* rt, void* opaque, void* ptr) {
  js_free_rt(rt, ptr);
}

static void
js_string_free_func(JSRuntime* rt, void* opaque, void* ptr) {
  JSValue value = js_cstring_value(opaque);

  JS_FreeValueRT(rt, value);
}

static void
js_arraybuffer_free_func(JSRuntime* rt, void* opaque, void* ptr) {
  JSValue value = JS_MKPTR(JS_TAG_OBJECT, ptr);

  JS_FreeValueRT(rt, value);
}

typedef struct OffsetLength {
  int64_t offset;
  int64_t length;
} OffsetLength;

static OffsetLength
get_offset_length(JSContext* ctx, int64_t len, int argc, JSValueConst argv[]) {
  int64_t offset = 0, length = len;

  if(argc >= 2 && JS_IsNumber(argv[1]))
    JS_ToInt64(ctx, &offset, argv[1]);
  if(argc >= 3 && JS_IsNumber(argv[2]))
    JS_ToInt64(ctx, &length, argv[2]);

  if(offset >= 0)
    offset = min(offset, len);
  else
    offset = ((offset % len) + offset) % len;

  if(length >= 0)
    length = min(length, len - offset);
  else
    length = len - offset;

  return (OffsetLength){.offset = offset, .length = length};
}

thread_local VISIBLE JSClassID js_syscallerror_class_id = 0;
thread_local JSValue syscallerror_proto = {JS_TAG_UNDEFINED}, syscallerror_ctor = {JS_TAG_UNDEFINED};

enum {
  SYSCALLERROR_TOSTRING = 0,
  SYSCALLERROR_SYSCALL,
  SYSCALLERROR_CODE,
  SYSCALLERROR_ERRNO,
  SYSCALLERROR_STACK,
  SYSCALLERROR_MESSAGE
};

extern thread_local JSClassID js_syscallerror_class_id;

typedef struct {
  const char* syscall;
  int errnum;
  const char* stack;
} SyscallError;

SyscallError*
js_syscallerror_data(JSContext* ctx, JSValueConst value) {
  SyscallError* err;
  err = JS_GetOpaque(value, js_syscallerror_class_id);
  return err;
}

static JSValue
js_syscallerror_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  SyscallError* err;
  JSValue obj = JS_UNDEFINED, proto = JS_UNDEFINED, error = js_global_new(ctx, "Error", 0, 0), st;
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

static JSValue
js_syscallerror_new(JSContext* ctx, const char* syscall, int errnum) {

  SyscallError* err;
  JSValue obj = JS_UNDEFINED, proto = JS_UNDEFINED, error = js_global_new(ctx, "Error", 0, 0), st;
  if(!(err = js_mallocz(ctx, sizeof(SyscallError))))
    return JS_EXCEPTION;

  obj = JS_NewObjectProtoClass(ctx, syscallerror_proto, js_syscallerror_class_id);
  if(JS_IsException(obj))
    goto fail;

  err->syscall = js_strdup(ctx, syscall);
  err->errnum = errnum;

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

static JSValue
js_syscallerror_throw(JSContext* ctx, const char* syscall) {
  JSValue error = js_syscallerror_new(ctx, syscall, errno);
  return JS_Throw(ctx, error);
}

static void
js_syscallerror_dump(JSContext* ctx, JSValueConst this_val, DynBuf* dbuf) {
  SyscallError* err;
  if(!(err = js_syscallerror_data(ctx, this_val)))
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
js_syscallerror_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  SyscallError* err;
  if(!(err = js_syscallerror_data(ctx, this_val)))
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
js_syscallerror_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  SyscallError* err;
  const char* code = 0;

  if(!(err = js_syscallerror_data(ctx, this_val)))
    return JS_EXCEPTION;
  JSValue obj = JS_NewObjectProto(ctx, syscallerror_proto);
  if(err->syscall)
    JS_DefinePropertyValueStr(ctx, obj, "syscall", JS_NewString(ctx, err->syscall), JS_PROP_ENUMERABLE);
  if((code = get_error(err->errnum))) {
    JS_DefinePropertyValueStr(ctx, obj, "code", JS_NewString(ctx, code), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, obj, "errno", JS_NewInt32(ctx, err->errnum), JS_PROP_ENUMERABLE);
  }
  if(js_has_propertystr(ctx, obj, "stack"))
    JS_DefinePropertyValueStr(ctx, obj, "stack", JS_GetPropertyStr(ctx, this_val, "stack"), JS_PROP_ENUMERABLE);

  JS_DefinePropertyValueStr(ctx, obj, "message", JS_GetPropertyStr(ctx, this_val, "message"), JS_PROP_ENUMERABLE);
  return obj;
}

static JSValue
js_syscallerror_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue ret = JS_UNDEFINED;
  SyscallError* err;

  if(!(err = js_syscallerror_data(ctx, this_val)))
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
    JS_CFUNC_MAGIC_DEF("toString", 0, js_syscallerror_method, SYSCALLERROR_TOSTRING),
    JS_CGETSET_MAGIC_DEF("syscall", js_syscallerror_get, 0, SYSCALLERROR_SYSCALL),
    JS_CGETSET_MAGIC_DEF("code", js_syscallerror_get, 0, SYSCALLERROR_CODE),
    JS_CGETSET_MAGIC_DEF("errno", js_syscallerror_get, 0, SYSCALLERROR_ERRNO),
    JS_CGETSET_MAGIC_DEF("stack", js_syscallerror_get, 0, SYSCALLERROR_STACK),
    JS_CGETSET_MAGIC_DEF("message", js_syscallerror_get, 0, SYSCALLERROR_MESSAGE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SyscallError", JS_PROP_CONFIGURABLE),
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

static JSValue
js_misc_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  JSValue arraybuffer_ctor = js_global_get(ctx, "ArrayBuffer");

  if(js_value_isclass(ctx, argv[0], JS_CLASS_ARRAY_BUFFER) || js_is_arraybuffer(ctx, argv[0]) ||
     JS_IsInstanceOf(ctx, argv[0], arraybuffer_ctor)) {
    uint8_t* data;
    size_t len;

    if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
      OffsetLength ol;

      ol = get_offset_length(ctx, len, argc, argv);

      ret = JS_NewStringLen(ctx, (const char*)data + ol.offset, ol.length);
    }
  }

  JS_FreeValue(ctx, arraybuffer_ctor);

  return ret;
}

static JSValue
js_misc_topointer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  void* ptr = 0;
  char buf[128];

  if(js_value_isclass(ctx, argv[0], JS_CLASS_ARRAY_BUFFER) || js_is_arraybuffer(ctx, argv[0])) {
    size_t len;
    ptr = JS_GetArrayBuffer(ctx, &len, argv[0]);
  } else if(JS_IsString(argv[0])) {
    ptr = js_cstring_ptr(argv[0]);
  }

  if(ptr) {
    snprintf(buf, sizeof(buf), "%p", ptr);
    ret = JS_NewString(ctx, buf);
  } else {
    ret = JS_NULL;
  }

  return ret;
}

static JSValue
js_misc_toarraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  if(JS_IsString(argv[0])) {
    JSValueConst value = argv[0]; // JS_DupValue(ctx, argv[0]);
    size_t len;
    const char* str;
    if((str = JS_ToCStringLen(ctx, &len, value))) {
      OffsetLength ol;
      ol = get_offset_length(ctx, len, argc, argv);
      ret = JS_NewArrayBuffer(ctx, (uint8_t*)str + ol.offset, ol.length, js_string_free_func, (void*)str, FALSE);
    }
  }

  return ret;
}

static JSValue
js_misc_duparraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  JSValue arraybuffer_ctor = js_global_get(ctx, "ArrayBuffer");

  if(JS_IsInstanceOf(ctx, argv[0], arraybuffer_ctor)) {
    JSValue value = JS_DupValue(ctx, argv[0]);
    uint8_t* data;
    size_t len;

    if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
      OffsetLength ol = get_offset_length(ctx, len, argc, argv);
      JSObject* obj = JS_VALUE_GET_OBJ(value);
      ret = JS_NewArrayBuffer(ctx, data + ol.offset, ol.length, js_arraybuffer_free_func, (void*)obj, FALSE);
    }
  }

  JS_FreeValue(ctx, arraybuffer_ctor);

  return ret;
}

static JSValue
js_misc_resizearraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  if(js_is_arraybuffer(ctx, argv[0])) {
    JSObject* obj = JS_VALUE_GET_OBJ(argv[0]);
    JSArrayBuffer* arraybuf = obj->u.array_buffer;
    uint64_t newlen;
    JS_ToIndex(ctx, &newlen, argv[1]);

    if(arraybuf->shared)
      ret = JS_ThrowTypeError(ctx, "ArrayBuffer must not be shared");
    else if(arraybuf->shared)
      ret = JS_ThrowTypeError(ctx, "ArrayBuffer must have opaque == 0");
    else {
      arraybuf->data = js_realloc(ctx, arraybuf->data, newlen);
      arraybuf->byte_length = newlen;

      ret = JS_MKPTR(JS_TAG_OBJECT, obj);
    }
  } else {
    ret = JS_ThrowTypeError(ctx, "Expecting ArrayBuffer");
  }

  return ret;
}

static JSValue
js_misc_concatarraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  int i;
  size_t total_len = 0, pos = 0;
  uint8_t* buf;

  for(i = 0; i < argc; i++) {
    if(!js_is_arraybuffer(ctx, argv[0]))
      return JS_ThrowTypeError(ctx, "argument %d is not ArrayBuffer", i + 1);
    total_len += js_arraybuffer_length(ctx, argv[i]);
  }

  buf = js_malloc(ctx, total_len);

  for(i = 0; i < argc; i++) {
    size_t len;
    uint8_t* ptr;
    ptr = JS_GetArrayBuffer(ctx, &len, argv[i]);

    memcpy(&buf[pos], ptr, len);
    pos += len;
  }

  return JS_NewArrayBuffer(ctx, buf, total_len, js_bytecode_free_func, 0, FALSE);
}

static JSValue
js_misc_getperformancecounter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  return JS_NewFloat64(ctx, (double)ts.tv_sec * 1000 + ((double)ts.tv_nsec / 1e06));
}

static JSValue
js_misc_proclink(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  DynBuf dbuf = {0};
  const char *link, path[256];
  size_t n;
  ssize_t r;

  switch(magic) {
    case FUNC_GETEXECUTABLE: link = "exe"; break;
    case FUNC_GETCWD: link = "cwd"; break;
    case FUNC_GETROOT: link = "root"; break;
    case FUNC_GETFD: link = "fd/"; break;
  }

  n = snprintf(path, sizeof(path), "/proc/self/%s", link);

  if(magic == FUNC_GETFD) {
    int32_t fd;
    if(argc < 1 || !JS_IsNumber(argv[0]))
      return JS_ThrowTypeError(ctx, "argument 1 must be Number");

    JS_ToInt32(ctx, &fd, argv[0]);
    snprintf(&path[n], sizeof(path) - n, "%d", fd);
  }

  js_dbuf_init(ctx, &dbuf);

  if((r = path_readlink(path, &dbuf)) > 0) {
    ret = dbuf_tostring_free(&dbuf, ctx);
  } else if(r < 0) {
    ret = js_syscallerror_throw(ctx, "readlink");
  }

  return ret;
}

static JSValue
js_misc_procread(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  DynBuf dbuf = {0};
  ssize_t i, j = 0, size, n;
  const char* file;
  char sep = '\n';

  switch(magic) {
    case FUNC_GETCOMMANDLINE:
      file = "/proc/self/cmdline";
      sep = '\0';
      break;
    case FUNC_GETPROCMAPS:
      file = "/proc/self/maps";
      sep = '\n';
      break;
    case FUNC_GETPROCMOUNTS:
      file = "/proc/self/mounts";
      sep = '\n';
      break;
    case FUNC_GETPROCSTAT:
      file = "/proc/self/stat";
      sep = ' ';
      break;
  }

  js_dbuf_init(ctx, &dbuf);

  if((size = dbuf_load(&dbuf, file)) > 0) {

    while(size > 0 && dbuf.buf[size - 1] == '\n') size--;

    ret = JS_NewArray(ctx);
    for(i = 0; i < size; i += n + 1) {
      size_t len;
      len = n = byte_chr(&dbuf.buf[i], size - i, sep);
      while(len > 0 && is_whitespace_char(dbuf.buf[i + len - 1])) len--;
      JS_SetPropertyUint32(ctx, ret, j++, JS_NewStringLen(ctx, &dbuf.buf[i], len));
    }
  }

  dbuf_free(&dbuf);

  return ret;
}

static JSValue
js_misc_getprototypechain(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  if(argc >= 1 && JS_IsObject(argv[0])) {
    JSValue proto = JS_GetPrototype(ctx, argv[0]);
    size_t j = 0;
    ret = JS_NewArray(ctx);

    do {
      JS_SetPropertyUint32(ctx, ret, j++, proto);
      proto = JS_GetPrototype(ctx, proto);

    } while(JS_IsObject(proto));
  }
  return ret;
}

static JSValue
js_misc_hrtime(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct timespec ts;
  JSValue ret;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  if(argc >= 1 && JS_IsArray(ctx, argv[0])) {
    int64_t sec, nsec;
    JSValue psec, pnsec;

    psec = JS_GetPropertyUint32(ctx, argv[0], 0);
    pnsec = JS_GetPropertyUint32(ctx, argv[0], 1);

    JS_ToInt64(ctx, &sec, psec);
    JS_ToInt64(ctx, &nsec, pnsec);
    JS_FreeValue(ctx, psec);
    JS_FreeValue(ctx, pnsec);

    if(nsec > ts.tv_nsec) {
      ts.tv_sec -= 1;
      ts.tv_nsec += 1000000000;
    }

    ts.tv_sec -= sec;
    ts.tv_nsec -= nsec;
  }

  ret = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, ret, 0, JS_NewInt64(ctx, ts.tv_sec));
  JS_SetPropertyUint32(ctx, ret, 1, JS_NewInt64(ctx, ts.tv_nsec));

  return ret;
}

static JSValue
js_misc_fnmatch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  size_t plen, slen;
  int32_t flags = 0, ret;

  const char* pattern = JS_ToCStringLen(ctx, &plen, argv[0]);
  const char* string = JS_ToCStringLen(ctx, &slen, argv[1]);

  if(argc >= 3)
    JS_ToInt32(ctx, &flags, argv[2]);

  ret = path_fnmatch(pattern, plen, string, slen, flags);
  JS_FreeCString(ctx, pattern);
  JS_FreeCString(ctx, string);
  return JS_NewBool(ctx, !ret);
}

static JSValue
js_misc_uname(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct utsname un;
  JSValue ret = JS_UNDEFINED;

  if(uname(&un) != -1) {
    ret = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, ret, "sysname", JS_NewString(ctx, un.sysname));
    JS_SetPropertyStr(ctx, ret, "nodename", JS_NewString(ctx, un.nodename));
    JS_SetPropertyStr(ctx, ret, "release", JS_NewString(ctx, un.release));
    JS_SetPropertyStr(ctx, ret, "version", JS_NewString(ctx, un.version));
    JS_SetPropertyStr(ctx, ret, "machine", JS_NewString(ctx, un.machine));
  }

  return ret;
}

static JSValue
js_misc_btoa(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  InputBuffer input = js_input_buffer(ctx, argv[0]);
  size_t enclen = b64_get_encoded_buffer_size(input.size);
  uint8_t* encbuf = js_malloc(ctx, enclen);

  b64_encode(input.data, input.size, encbuf);

  ret = JS_NewStringLen(ctx, (const char*)encbuf, enclen);
  js_free(ctx, encbuf);
  return ret;
}

static JSValue
js_misc_atob(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  InputBuffer input = js_input_buffer(ctx, argv[0]);
  size_t declen = b64_get_decoded_buffer_size(input.size);
  uint8_t* decbuf = js_malloc(ctx, declen);

  b64_decode(input.data, input.size, decbuf);

  ret = JS_NewArrayBufferCopy(ctx, (const uint8_t*)decbuf, declen);
  js_free(ctx, decbuf);
  return ret;
}

static JSValue
js_misc_compile_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  const char* filename = JS_ToCString(ctx, argv[0]);
  BOOL module = FALSE;
  uint8_t* buf;
  size_t buf_len;
  int eval_flags = JS_EVAL_FLAG_COMPILE_ONLY;

  if(argc >= 2)
    module = JS_ToBool(ctx, argv[1]);
  else if(str_ends(filename, ".jsm"))
    module = TRUE;

  /* load JS from file to buffer */
  if((buf = js_load_file(ctx, &buf_len, filename))) {

    if(!module && JS_DetectModule((const char*)buf, buf_len))
      module = TRUE;

    eval_flags |= (module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL);

    ret = JS_Eval(ctx, (const char*)buf, buf_len, filename, eval_flags);
  }

  return ret;
}

static JSValue
js_misc_write_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  size_t size;
  uint8_t* bytecode;

  if((bytecode = JS_WriteObject(ctx, &size, argv[0], JS_WRITE_OBJ_BYTECODE))) {
    ret = JS_NewArrayBuffer(ctx, bytecode, size, js_bytecode_free_func, 0, FALSE);
  }
  return ret;
}

static JSValue
js_misc_read_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer input = js_input_buffer(ctx, argv[0]);

  return JS_ReadObject(ctx, input.data, input.size, JS_READ_OBJ_BYTECODE);
}

static JSValue
js_misc_getx(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {

  int32_t ret = 0;

  switch(magic) {
    case FUNC_GETPID: {
      ret = getpid();
      break;
    }
    case FUNC_GETPPID: {
      ret = getppid();
      break;
    }
    case FUNC_GETSID: {
      // sret = getsid();
      break;
    }
    case FUNC_GETUID: {
      ret = getuid();
      break;
    }
    case FUNC_GETGID: {
      ret = getgid();
      break;
    }
    case FUNC_GETEUID: {
      ret = geteuid();
      break;
    }
    case FUNC_GETEGID: {
      ret = getegid();
      break;
    }
    case FUNC_SETUID: {
      int32_t uid;
      JS_ToInt32(ctx, &uid, argv[0]);
      ret = setuid(uid);
      break;
    }
    case FUNC_SETGID: {
      int32_t gid;
      JS_ToInt32(ctx, &gid, argv[0]);
      ret = setgid(gid);
      break;
    }
    case FUNC_SETEUID: {
      int32_t euid;
      JS_ToInt32(ctx, &euid, argv[0]);
      ret = setuid(euid);
      break;
      break;
    }
    case FUNC_SETEGID: {
      int32_t egid;
      JS_ToInt32(ctx, &egid, argv[0]);
      ret = setgid(egid);
      break;
    }
  }
  if(ret == -1)
    return js_syscallerror_throw(ctx,
                                 ((const char* const[]){
                                     "getpid",
                                     "getppid",
                                     "getsid",
                                     "getuid",
                                     "getgid",
                                     "geteuid",
                                     "getegid",
                                     "setuid",
                                     "setgid",
                                     "seteuid",
                                     "setegid",
                                 })[magic - FUNC_GETPID]);

  return JS_NewInt32(ctx, ret);
}

enum { VALUE_TYPE = 0, VALUE_TAG, VALUE_PTR };

static JSValue
js_misc_valuetype(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case VALUE_TYPE: {
      const char* typestr = js_value_typestr(ctx, argv[0]);
      ret = JS_NewString(ctx, typestr);
      break;
    }
    case VALUE_TAG: {
      ret = JS_NewInt32(ctx, JS_VALUE_GET_TAG(argv[0]));
      break;
    }
    case VALUE_PTR: {
      void* ptr = JS_VALUE_GET_PTR(argv[0]);
      char buf[128];

      snprintf(buf, sizeof(buf), "%p", ptr);
      ret = JS_NewString(ctx, buf);
      break;
    }
  }
  return ret;
}

static JSValue
js_misc_evalbinary(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  BOOL load_only = FALSE;
  JSValueConst obj;

  if(argc >= 2)
    load_only = JS_ToBool(ctx, argv[1]);

  if(JS_VALUE_GET_TAG(argv[0]) != JS_TAG_MODULE && JS_VALUE_GET_TAG(argv[0]) != JS_TAG_FUNCTION_BYTECODE)
    obj = js_misc_read_object(ctx, this_val, argc, argv);
  else
    obj = argv[0];

  if(JS_IsException(obj))
    return obj;

  if(load_only) {
    if(JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE)
      js_module_set_import_meta(ctx, obj, FALSE, FALSE);
  } else {
    if(JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE) {
      if(JS_ResolveModule(ctx, obj) < 0) {
        JSModuleDef* m = JS_VALUE_GET_PTR(obj);
        const char* name = JS_AtomToCString(ctx, m->module_name);
        ret = JS_ThrowInternalError(ctx, "Failed resolving module '%s'", name);
        JS_FreeCString(ctx, name);
        JS_FreeValue(ctx, obj);
        return ret;
      }
      js_module_set_import_meta(ctx, obj, FALSE, TRUE);
    }
    ret = JS_EvalFunction(ctx, obj);
  }

  return ret;
}

static JSValue
js_misc_opcode_array(JSContext* ctx, const JSOpCode* opcode) {
  JSValue ret = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, ret, 0, JS_NewUint32(ctx, opcode->size));
  JS_SetPropertyUint32(ctx, ret, 1, JS_NewUint32(ctx, opcode->n_pop));
  JS_SetPropertyUint32(ctx, ret, 2, JS_NewUint32(ctx, opcode->n_push));
  JS_SetPropertyUint32(ctx, ret, 3, JS_NewUint32(ctx, opcode->fmt));
  JS_SetPropertyUint32(ctx, ret, 4, JS_NewString(ctx, opcode->name));
  return ret;
}

static JSValue
js_misc_opcode_object(JSContext* ctx, const JSOpCode* opcode) {
  JSValue ret = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, ret, "size", JS_NewUint32(ctx, opcode->size));
  JS_SetPropertyStr(ctx, ret, "n_pop", JS_NewUint32(ctx, opcode->n_pop));
  JS_SetPropertyStr(ctx, ret, "n_push", JS_NewUint32(ctx, opcode->n_push));
  JS_SetPropertyStr(ctx, ret, "fmt", JS_NewUint32(ctx, opcode->fmt));
  JS_SetPropertyStr(ctx, ret, "name", JS_NewString(ctx, opcode->name));
  return ret;
}

static JSValue
js_misc_opcodes(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_NewArray(ctx);
  size_t i, j, len = countof(js_opcodes);
  BOOL as_object = FALSE;

  if(argc >= 1)
    as_object = JS_ToBool(ctx, argv[0]);

  for(i = 0, j = 0; i < len; i++) {

    if(i >= OP_TEMP_START && i < OP_TEMP_END)
      continue;

    JS_SetPropertyUint32(ctx,
                         ret,
                         j++,
                         (as_object ? js_misc_opcode_object : js_misc_opcode_array)(ctx, &js_opcodes[i]));
  }

  return ret;
}

static JSValue
js_misc_get_bytecode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  if(JS_IsFunction(ctx, argv[0])) {
    JSObject* obj = JS_VALUE_GET_OBJ(argv[0]);
    JSFunctionBytecode* fnbc;

    if((fnbc = obj->u.func.function_bytecode)) {
      ret = JS_NewArrayBufferCopy(ctx, fnbc->byte_code_buf, fnbc->byte_code_len);
    }
  }

  return ret;
}

enum { ATOM_TO_STRING = 0, ATOM_TO_VALUE, VALUE_TO_ATOM };

static JSValue
js_misc_atom(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case ATOM_TO_STRING: {
      int32_t atom;
      JS_ToInt32(ctx, &atom, argv[0]);
      ret = JS_AtomToString(ctx, atom);
      break;
    }
    case ATOM_TO_VALUE: {
      int32_t atom;
      JS_ToInt32(ctx, &atom, argv[0]);
      ret = JS_AtomToValue(ctx, atom);
      break;
    }
    case VALUE_TO_ATOM: {
      JSAtom atom = JS_ValueToAtom(ctx, argv[0]);
      ret = JS_NewUint32(ctx, atom);
      break;
    }
  }
  return ret;
}

enum { GET_CLASS_ID = 0, GET_CLASS_NAME, GET_CLASS_ATOM, GET_CLASS_COUNT, GET_CLASS_PROTO, GET_CLASS_CONSTRUCTOR };

static JSValue
js_misc_classid(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  JSObject* obj;
  int32_t class_id = 0;

  if(argc >= 1) {
    if(JS_IsNumber(argv[0]))
      JS_ToInt32(ctx, &class_id, argv[0]);
    else if((obj = js_value_get_obj(argv[0])))
      class_id = obj->class_id;
  }

  switch(magic) {
    case GET_CLASS_ID: {
      if(class_id > 0)
        ret = JS_NewUint32(ctx, class_id);
      break;
    }
    case GET_CLASS_NAME: {
      if(class_id > 0) {
        JSAtom atom;
        if((atom = js_class_atom(ctx, class_id)))
          ret = JS_AtomToValue(ctx, atom);
      }
      break;
    }
    case GET_CLASS_ATOM: {
      if(class_id > 0) {
        JSAtom atom;
        if((atom = js_class_atom(ctx, class_id)))
          ret = JS_NewInt32(ctx, atom);
      }
      break;
    }
    case GET_CLASS_COUNT: {
      uint32_t i, class_count = ctx->rt->class_count;
      for(i = 1; i < class_count; i++)
        if(!JS_IsRegisteredClass(ctx->rt, i))
          break;

      ret = JS_NewUint32(ctx, i);
      break;
    }
    case GET_CLASS_PROTO: {
      if(class_id > 0)
        ret = JS_GetClassProto(ctx, class_id);
      break;
    }
    case GET_CLASS_CONSTRUCTOR: {
      if(class_id > 0) {
        JSValue proto = JS_GetClassProto(ctx, class_id);
        if(JS_IsObject(proto))
          ret = JS_GetPropertyStr(ctx, proto, "constructor");
        JS_FreeValue(ctx, proto);
      }
      break;
    }
  }
  return ret;
}

static JSValue

js_misc_bitfield_to_array(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const uint8_t* buf;
  size_t len;
  int64_t offset = 0;
  JSValue ret = JS_UNDEFINED;

  if(argc >= 2)
    JS_ToInt64(ctx, &offset, argv[1]);

  if((buf = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
    size_t i, j = 0, bits = len * 8;
    ret = JS_NewArray(ctx);

    for(i = 0; i < bits; i++) {
      if(buf[i >> 3] & (1u << (i & 0x7))) {
        JS_SetPropertyUint32(ctx, ret, j++, JS_NewInt64(ctx, i + offset));
      }
    }
  }
  return ret;
}

static JSValue
js_misc_array_to_bitfield(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  size_t len;
  int64_t offset = 0;
  JSValue ret = JS_UNDEFINED;

  if(argc >= 2)
    JS_ToInt64(ctx, &offset, argv[1]);

  if(!JS_IsArray(ctx, argv[0]))
    return JS_ThrowTypeError(ctx, "argument must be an array");

  if((len = js_array_length(ctx, argv[0]))) {
    size_t i;
    int64_t max = -1;
    uint8_t* bufptr;
    size_t bufsize;

    for(i = 0; i < len; i++) {
      JSValue value = JS_GetPropertyUint32(ctx, argv[0], i);
      uint32_t number;
      JS_ToUint32(ctx, &number, value);
      JS_FreeValue(ctx, value);

      if(max < number)
        max = number;
    }
    bufsize = ((max + 1) + 7) >> 3;
    if((bufptr = js_mallocz(ctx, bufsize)) == 0)
      return JS_ThrowOutOfMemory(ctx);

    for(i = 0; i < len; i++) {
      JSValue value = JS_GetPropertyUint32(ctx, argv[0], i);
      uint32_t number;
      JS_ToUint32(ctx, &number, value);
      JS_FreeValue(ctx, value);

      number -= offset;

      bufptr[number >> 3] |= 1u << (number & 0x7);
    }

    ret = JS_NewArrayBuffer(ctx, bufptr, bufsize, js_arraybuffer_free_func, NULL, FALSE);
  }
  return ret;
}

static JSValue
js_misc_random(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  uint32_t bound = 0;
  if(argc > 0 && JS_IsNumber(argv[0]))
    JS_ToUint32(ctx, &bound, argv[0]);

  switch(magic) {
    case 0: {
      uint32_t num = argc > 0 ? pcg32_random_bounded_divisionless(bound) : pcg32_random();
      return JS_NewUint32(ctx, num);
    }
    case 1: {
      int32_t num = argc > 0 ? pcg32_random_bounded_divisionless(bound * 2) - bound : pcg32_random();
      return JS_NewInt32(ctx, num);
    }

    case 2: {
      uint32_t num = pcg32_random();
      return JS_NewFloat64(ctx, (double)num / UINT32_MAX);
    }
    case 3: {
      uint32_t st = 0;
      JS_ToUint32(ctx, &st, argv[0]);
      pcg32_init_state(st);
      return JS_UNDEFINED;
    }
  }
}

static const JSCFunctionListEntry js_misc_funcs[] = {
    JS_CFUNC_DEF("fnmatch", 3, js_misc_fnmatch),
    JS_CFUNC_DEF("toString", 1, js_misc_tostring),
    JS_CFUNC_DEF("toPointer", 1, js_misc_topointer),
    JS_CFUNC_DEF("toArrayBuffer", 1, js_misc_toarraybuffer),
    JS_CFUNC_DEF("dupArrayBuffer", 1, js_misc_duparraybuffer),
    JS_CFUNC_DEF("resizeArrayBuffer", 1, js_misc_resizearraybuffer),
    JS_CFUNC_DEF("concatArrayBuffer", 1, js_misc_concatarraybuffer),
    JS_CFUNC_DEF("getPerformanceCounter", 0, js_misc_getperformancecounter),
    JS_CFUNC_MAGIC_DEF("getExecutable", 0, js_misc_proclink, FUNC_GETEXECUTABLE),
    JS_CFUNC_MAGIC_DEF("getCurrentWorkingDirectory", 0, js_misc_proclink, FUNC_GETCWD),
    JS_CFUNC_MAGIC_DEF("getRootDirectory", 0, js_misc_proclink, FUNC_GETROOT),
    JS_CFUNC_MAGIC_DEF("getFileDescriptor", 0, js_misc_proclink, FUNC_GETFD),
    JS_CFUNC_MAGIC_DEF("getCommandLine", 0, js_misc_procread, FUNC_GETCOMMANDLINE),
    JS_CFUNC_MAGIC_DEF("getProcMaps", 0, js_misc_procread, FUNC_GETPROCMAPS),
    JS_CFUNC_MAGIC_DEF("getProcMounts", 0, js_misc_procread, FUNC_GETPROCMOUNTS),
    JS_CFUNC_MAGIC_DEF("getProcStat", 0, js_misc_procread, FUNC_GETPROCSTAT),
    JS_CFUNC_DEF("getPrototypeChain", 0, js_misc_getprototypechain),
    JS_CFUNC_MAGIC_DEF("getpid", 0, js_misc_getx, FUNC_GETPID),
    JS_CFUNC_MAGIC_DEF("getppid", 0, js_misc_getx, FUNC_GETPPID),
    JS_CFUNC_MAGIC_DEF("getsid", 0, js_misc_getx, FUNC_GETSID),
    JS_CFUNC_MAGIC_DEF("getuid", 0, js_misc_getx, FUNC_GETUID),
    JS_CFUNC_MAGIC_DEF("getgid", 0, js_misc_getx, FUNC_GETGID),
    JS_CFUNC_MAGIC_DEF("geteuid", 0, js_misc_getx, FUNC_GETEUID),
    JS_CFUNC_MAGIC_DEF("getegid", 0, js_misc_getx, FUNC_GETEGID),
    JS_CFUNC_MAGIC_DEF("setuid", 1, js_misc_getx, FUNC_SETUID),
    JS_CFUNC_MAGIC_DEF("setgid", 1, js_misc_getx, FUNC_SETGID),
    JS_CFUNC_MAGIC_DEF("seteuid", 1, js_misc_getx, FUNC_SETEUID),
    JS_CFUNC_MAGIC_DEF("setegid", 1, js_misc_getx, FUNC_SETEGID),
    JS_CFUNC_DEF("hrtime", 0, js_misc_hrtime),
    JS_CFUNC_DEF("uname", 0, js_misc_uname),
    JS_CFUNC_DEF("btoa", 1, js_misc_btoa),
    JS_CFUNC_DEF("atob", 1, js_misc_atob),
    JS_CFUNC_DEF("bitfieldToArray", 1, js_misc_bitfield_to_array),
    JS_CFUNC_DEF("arrayToBitfield", 1, js_misc_array_to_bitfield),
    JS_CFUNC_DEF("compileFile", 1, js_misc_compile_file),
    JS_CFUNC_DEF("writeObject", 1, js_misc_write_object),
    JS_CFUNC_DEF("readObject", 1, js_misc_read_object),
    JS_CFUNC_DEF("getOpCodes", 0, js_misc_opcodes),
    JS_CFUNC_DEF("getByteCode", 1, js_misc_get_bytecode),
    JS_CFUNC_MAGIC_DEF("valueType", 1, js_misc_valuetype, VALUE_TYPE),
    JS_CFUNC_MAGIC_DEF("valueTag", 1, js_misc_valuetype, VALUE_TAG),
    JS_CFUNC_MAGIC_DEF("valuePtr", 1, js_misc_valuetype, VALUE_PTR),
    JS_CFUNC_DEF("evalBinary", 1, js_misc_evalbinary),
    JS_CFUNC_MAGIC_DEF("atomToString", 1, js_misc_atom, ATOM_TO_STRING),
    JS_CFUNC_MAGIC_DEF("atomToValue", 1, js_misc_atom, ATOM_TO_VALUE),
    JS_CFUNC_MAGIC_DEF("valueToAtom", 1, js_misc_atom, VALUE_TO_ATOM),
    JS_CFUNC_MAGIC_DEF("getClassID", 1, js_misc_classid, GET_CLASS_ID),
    JS_CFUNC_MAGIC_DEF("getClassName", 1, js_misc_classid, GET_CLASS_NAME),
    JS_CFUNC_MAGIC_DEF("getClassAtom", 1, js_misc_classid, GET_CLASS_ATOM),
    JS_CFUNC_MAGIC_DEF("getClassCount", 1, js_misc_classid, GET_CLASS_COUNT),
    JS_CFUNC_MAGIC_DEF("getClassProto", 1, js_misc_classid, GET_CLASS_PROTO),
    JS_CFUNC_MAGIC_DEF("getClassConstructor", 1, js_misc_classid, GET_CLASS_CONSTRUCTOR),
    JS_CFUNC_MAGIC_DEF("rand", 0, js_misc_random, 0),
    JS_CFUNC_MAGIC_DEF("randi", 0, js_misc_random, 1),
    JS_CFUNC_MAGIC_DEF("randf", 0, js_misc_random, 2),
    JS_CFUNC_MAGIC_DEF("srand", 1, js_misc_random, 3),
    //   JS_OBJECT_DEF("StringDecoder", js_stringdecoder_props, countof(js_stringdecoder_props), JS_PROP_CONFIGURABLE),

};

static int
js_misc_init(JSContext* ctx, JSModuleDef* m) {

  if(!js_location_class_id)
    js_location_init(ctx, 0);

  JS_NewClassID(&js_syscallerror_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_syscallerror_class_id, &js_syscallerror_class);

  syscallerror_ctor = JS_NewCFunction2(ctx, js_syscallerror_constructor, "SyscallError", 1, JS_CFUNC_constructor, 0);
  syscallerror_proto = js_global_new(ctx, "Error", 0, 0);

  JS_SetPropertyFunctionList(ctx, syscallerror_proto, js_syscallerror_funcs, countof(js_syscallerror_funcs));
  JS_SetClassProto(ctx, js_syscallerror_class_id, syscallerror_proto);

  js_set_inspect_method(ctx, syscallerror_proto, js_syscallerror_inspect);

  JS_SetConstructor(ctx, syscallerror_ctor, syscallerror_proto);

  if(m) {
    // JS_SetModuleExportList(ctx, m, location_ctor);
    JS_SetModuleExportList(ctx, m, js_misc_funcs, countof(js_misc_funcs));
    JS_SetModuleExport(ctx, m, "SyscallError", syscallerror_ctor);
    JS_SetModuleExport(ctx, m, "Location", location_ctor);
  }

  // js_stringdecoder_init(ctx, m);

  // printf("%s\n", js_opcodes[0].name);

  return 0;
}

#if defined(JS_SHARED_LIBRARY) && defined(JS_MISC_MODULE)
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_misc
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_misc_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_misc_funcs, countof(js_misc_funcs));
  JS_AddModuleExport(ctx, m, "SyscallError");
  JS_AddModuleExport(ctx, m, "Location");
  return m;
}
