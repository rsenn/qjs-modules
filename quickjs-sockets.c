#include "quickjs-sockets.h"
#include "quickjs-syscallerror.h"
#include "utils.h"
#include "buffer-utils.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <linux/net.h>
#include <sys/syscall.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <alloca.h>

extern const uint32_t qjsm_fd_set_size;
extern const uint8_t qjsm_fd_set[1030];
extern const uint32_t qjsm_socklen_t_size;
extern const uint8_t qjsm_socklen_t[1030];

#define JS_SOCKETCALL(syscall_no, sock, result) JS_SOCKETCALL_RETURN(syscall_no, sock, result, JS_NewInt32(ctx, sock.ret), JS_NewInt32(ctx, -1))

#define JS_SOCKETCALL_FAIL(syscall_no, sock, on_fail) JS_SOCKETCALL_RETURN(syscall_no, sock, result, JS_NewInt32(ctx, sock.ret), on_fail)

#define JS_SOCKETCALL_RETURN(syscall_no, sock, result, on_success, on_fail) \
  do { \
    syscall_return(&(sock), syscall_no, (result)); \
    ret = sock.ret < 0 ? on_fail : on_success; \
    JS_SetOpaque(this_val, sock.ptr); \
  } while(0)

thread_local VISIBLE JSClassID js_sockaddr_class_id = 0, js_socket_class_id = 0;
thread_local JSValue sockaddr_proto = {JS_TAG_UNDEFINED}, sockaddr_ctor = {JS_TAG_UNDEFINED}, socket_proto = {JS_TAG_UNDEFINED},
                     socket_ctor = {JS_TAG_UNDEFINED};

static const char* socket_syscalls[] = {
    0,
    "socket",
    "getsockname",
    "getpeername",
    "fcntl",
    "bind",
    "accept",
    "connect",
    "listen",
    "recv",
    "send",
    "shutdown",
    "close",
    "getsockopt",
    "setsockopt",
};
static const size_t socket_syscalls_size = countof(socket_syscalls);

static void
syscall_return(Socket* sock, int syscall, int retval) {
  sock->syscall = syscall;
  sock->ret = retval;
  sock->error = retval < 0 ? errno : 0;
}

static SockAddr*
sockaddr_new(JSContext* ctx) {
  SockAddr* a;
  if(!(a = js_mallocz(ctx, sizeof(SockAddr))))
    return 0;
  return a;
}

static void
js_sockaddr_free_buffer(JSRuntime* rt, void* opaque, void* ptr) {
  JSValue* v = opaque;
  JS_FreeValueRT(rt, *v);
  js_free_rt(rt, v);
}

static JSValue
js_sockaddr_wrap(JSContext* ctx, SockAddr* a) {
  JSValue obj;

  if(js_sockaddr_class_id == 0)
    js_sockets_init(ctx, 0);

  obj = JS_NewObjectProtoClass(ctx, sockaddr_proto, js_sockaddr_class_id);
  JS_SetOpaque(obj, a);
  return obj;
}

static JSValue
js_sockaddr_new(JSContext* ctx, int family) {
  SockAddr* a;
  if(!(a = sockaddr_new(ctx)))
    return JS_EXCEPTION;
  a->family = family;
  return js_sockaddr_wrap(ctx, a);
}

static BOOL
js_sockaddr_init(JSContext* ctx, int argc, JSValueConst argv[], SockAddr* a) {
  size_t size = 0;

  if(argc >= 2 && JS_IsNumber(argv[0])) {
    int32_t family;
    JS_ToInt32(ctx, &family, argv[0]);
    a->family = family;
    size = sockaddr_size(a);
    argc--;
    argv++;
  }

  if(argc >= 2) {
    if(JS_IsString(argv[0])) {
      const char* str = JS_ToCString(ctx, argv[0]);
      if(a->family == 0) {
        if(inet_pton(AF_INET, str, &a->sai.sin_addr) > 0)
          a->family = AF_INET;
        else if(inet_pton(AF_INET6, str, &a->sai6.sin6_addr) > 0)
          a->family = AF_INET;
      } else {
        inet_pton(a->family, str, sockaddr_addr(a));
      }
      JS_FreeCString(ctx, str);
    } else if(js_is_arraybuffer(ctx, argv[0])) {
      uint8_t* data;
      size_t len;
      if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
        if(len > 0) {
          memcpy(sockaddr_addr(a), data, MIN_NUM(len, size));
          if(size == 0)
            size = len;
        }
      }
    }
    if(JS_IsNumber(argv[1])) {
      uint32_t port;
      JS_ToUint32(ctx, &port, argv[1]);
      if(a->family == AF_INET)
        a->sai.sin_port = htons(port);
      else if(a->family == AF_INET6)
        a->sai6.sin6_port = htons(port);
    }
  } else if(argc == 1 && js_is_arraybuffer(ctx, argv[0])) {
    uint8_t* data;
    size_t len;
    if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
      if(len > 0)
        memcpy(sockaddr_addr(a), data, MIN_NUM(len, sizeof(SockAddr)));
    }
  }
  return TRUE;
}

static JSValue
js_sockaddr_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  SockAddr* a;

  if(js_sockaddr_class_id == 0)
    js_sockets_init(ctx, 0);

  if(!(a = sockaddr_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = sockaddr_proto;
  obj = JS_NewObjectProtoClass(ctx, proto, js_sockaddr_class_id);
  if(!js_sockaddr_init(ctx, argc, argv, a)) {
    js_free(ctx, a);
    return JS_ThrowInternalError(ctx, "SockAddr init() failed");
  }
  JS_SetOpaque(obj, a);
  return obj;
fail:
  js_free(ctx, a);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  SOCKADDR_METHOD_CLONE,
  SOCKADDR_METHOD_TOSTRING,
};

static JSValue
js_sockaddr_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  SockAddr* a;

  if(!(a = js_sockaddr_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SOCKADDR_METHOD_CLONE: {
      SockAddr* other = sockaddr_new(ctx);
      memcpy(other, a, sizeof(SockAddr));
      ret = js_sockaddr_wrap(ctx, other);
      break;
    }
    case SOCKADDR_METHOD_TOSTRING: {
      char port[FMT_ULONG];
      DynBuf dbuf;
      js_dbuf_init(ctx, &dbuf);
      dbuf_realloc(&dbuf, INET6_ADDRSTRLEN);
      inet_ntop(a->family, sockaddr_addr(a), dbuf.buf, INET6_ADDRSTRLEN);
      dbuf.size = strlen(dbuf.buf);
      dbuf_putc(&dbuf, ':');
      dbuf_put(&dbuf, port, fmt_ulong(port, sockaddr_port(a)));
      ret = JS_NewStringLen(ctx, dbuf.buf, dbuf.size);
      dbuf_free(&dbuf);
      break;
    }
  }
  return ret;
}

enum {
  SOCKADDR_FAMILY,
  SOCKADDR_ADDR,
  SOCKADDR_PORT,
  SOCKADDR_BYTELENGTH,
  SOCKADDR_BUFFER,
};

static JSValue
js_sockaddr_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue ret = JS_UNDEFINED;
  SockAddr* a = js_sockaddr_data(this_val);

  if(a)
    switch(magic) {
      case SOCKADDR_FAMILY: {
        ret = JS_NewUint32(ctx, a->family);
        break;
      }
      case SOCKADDR_ADDR: {
        char buf[INET6_ADDRSTRLEN] = {0};
        inet_ntop(a->family, sockaddr_addr(a), buf, sizeof(buf));
        ret = JS_NewString(ctx, buf);
        break;
      }
      case SOCKADDR_PORT: {
        ret = JS_NewUint32(ctx, sockaddr_port(a));
        break;
      }
      case SOCKADDR_BYTELENGTH: {
        size_t len;
        if((len = sockaddr_size(a)))
          ret = JS_NewUint32(ctx, len);
        break;
      }
      case SOCKADDR_BUFFER: {
        JSValue* v = js_mallocz(ctx, sizeof(JSValue));
        ret = JS_NewArrayBuffer(ctx, a, sizeof(SockAddr), js_sockaddr_free_buffer, v, FALSE);
        break;
      }
    }
  return ret;
}

static JSValue
js_sockaddr_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  SockAddr* a;
  JSValue ret = JS_UNDEFINED;

  if(!(a = js_sockaddr_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(a)
    switch(magic) {
      case SOCKADDR_FAMILY: {
        int32_t af;
        JS_ToInt32(ctx, &af, value);
        a->family = af;
        break;
      }
      case SOCKADDR_ADDR: {
        const char* str = JS_ToCString(ctx, value);
        inet_pton(a->family, str, sockaddr_addr(a));
        JS_FreeCString(ctx, str);
        break;
      }
      case SOCKADDR_PORT: {
        uint32_t port;
        JS_ToUint32(ctx, &port, value);
        switch(a->family) {
          case AF_INET: a->sai.sin_port = htons(port); break;
          case AF_INET6: a->sai6.sin6_port = htons(port); break;
        }
        break;
      }
    }
  return ret;
}

static JSValue
js_sockaddr_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  SockAddr* a;
  uint16_t port;
  char buf[INET6_ADDRSTRLEN] = {0};

  if(!(a = js_sockaddr_data2(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectProto(ctx, sockaddr_proto);

  if(a->family)
    JS_DefinePropertyValueStr(ctx, obj, "family", JS_NewUint32(ctx, a->family), JS_PROP_ENUMERABLE);

  inet_ntop(a->family, sockaddr_addr(a), buf, sizeof(buf));

  if(buf[0])
    JS_DefinePropertyValueStr(ctx, obj, "addr", JS_NewString(ctx, buf), JS_PROP_ENUMERABLE);

  if((port = sockaddr_port(a)))
    JS_DefinePropertyValueStr(ctx, obj, "port", JS_NewUint32(ctx, port), JS_PROP_ENUMERABLE);

  return obj;
}

static void
js_sockaddr_finalizer(JSRuntime* rt, JSValue val) {
  SockAddr* a;
  if((a = js_sockaddr_data(val))) {
    js_free_rt(rt, a);
  }
  JS_FreeValueRT(rt, val);
}

static const JSCFunctionListEntry js_sockaddr_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("family", js_sockaddr_get, js_sockaddr_set, SOCKADDR_FAMILY),
    JS_CGETSET_MAGIC_DEF("addr", js_sockaddr_get, js_sockaddr_set, SOCKADDR_ADDR),
    JS_CGETSET_MAGIC_DEF("port", js_sockaddr_get, js_sockaddr_set, SOCKADDR_PORT),
    JS_CGETSET_MAGIC_DEF("buffer", js_sockaddr_get, 0, SOCKADDR_BUFFER),
    JS_CGETSET_MAGIC_DEF("byteLength", js_sockaddr_get, 0, SOCKADDR_BYTELENGTH),
    JS_CFUNC_MAGIC_DEF("clone", 0, js_sockaddr_method, SOCKADDR_METHOD_CLONE),
    JS_CFUNC_MAGIC_DEF("toString", 0, js_sockaddr_method, SOCKADDR_METHOD_TOSTRING),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SockAddr", JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE),
};

static JSClassDef js_sockaddr_class = {.class_name = "SockAddr", .finalizer = js_sockaddr_finalizer};

/*static inline ssize_t
socket_send(int sock, const void* buf, size_t len, int flags) {
  return syscall(SYS_sendto, sock, buf, len, flags, 0, 0);
}

static inline ssize_t
socket_recv(int sock, void* buf, size_t len, int flags) {
  return syscall(SYS_recvfrom, sock, buf, len, flags, 0, 0);
}*/

static BOOL
timeval_read(JSContext* ctx, JSValueConst arg, struct timeval* tv) {
  if(JS_IsNumber(arg)) {
    double msecs = 0;
    JS_ToFloat64(ctx, &msecs, arg);
    tv->tv_sec = msecs / 1000;
    msecs -= tv->tv_sec * 1000;
    tv->tv_usec = msecs * 1000;
    return TRUE;
  } else if(js_is_array(ctx, arg) && js_array_length(ctx, arg) >= 2) {
    int64_t sec = 0, usec = 0;
    JSValue member = JS_GetPropertyUint32(ctx, arg, 0);
    JS_ToInt64(ctx, &sec, member);
    JS_FreeValue(ctx, member);
    member = JS_GetPropertyUint32(ctx, arg, 1);
    JS_ToInt64(ctx, &usec, member);
    JS_FreeValue(ctx, member);
    tv->tv_sec = sec;
    tv->tv_usec = usec;
    return TRUE;
  } else if(js_is_arraybuffer(ctx, arg)) {
    uint8_t* data;
    size_t len;
    if((data = JS_GetArrayBuffer(ctx, &len, arg))) {
      if(len >= sizeof(struct timeval)) {
        memcpy(tv, data, sizeof(struct timeval));
        return TRUE;
      }
    }
  }
  return FALSE;
}

static BOOL
timeval_write(JSContext* ctx, const struct timeval* tv, JSValueConst arg) {
  if(JS_IsNumber(arg)) {
    return FALSE;
  } else if(js_is_array(ctx, arg)) {
    js_array_clear(ctx, arg);
    JS_SetPropertyUint32(ctx, arg, 0, JS_NewUint32(ctx, tv->tv_sec));
    JS_SetPropertyUint32(ctx, arg, 1, JS_NewUint32(ctx, tv->tv_usec));
    return TRUE;
  } else if(js_is_arraybuffer(ctx, arg)) {
    uint8_t* data;
    size_t len;
    if((data = JS_GetArrayBuffer(ctx, &len, arg))) {
      if(len >= sizeof(struct timeval)) {
        memcpy(data, tv, sizeof(struct timeval));
        return TRUE;
      }
    }
  }
  return FALSE;
}

static BOOL
fdset_read(JSContext* ctx, JSValueConst arg, fd_set* set) {
  if(js_is_array(ctx, arg)) {
    size_t i, len = js_array_length(ctx, arg);
    for(i = 0; i < len; i++) {
      uint32_t fd;
      JSValue member = JS_GetPropertyUint32(ctx, arg, i);
      if(!JS_ToUint32(ctx, &fd, member))
        FD_SET(fd, set);
      JS_FreeValue(ctx, member);
    }
    return TRUE;
  } else if(js_is_arraybuffer(ctx, arg)) {
    uint8_t* data;
    size_t len;
    if((data = JS_GetArrayBuffer(ctx, &len, arg))) {
      memcpy(set, data, MIN_NUM(len, sizeof(fd_set)));
      return TRUE;
    }
  }
  return FALSE;
}

static BOOL
fdset_write(JSContext* ctx, const fd_set* set, JSValueConst arg) {
  if(js_is_array(ctx, arg)) {
    int fd, i = 0;
    js_array_clear(ctx, arg);
    for(fd = 0; fd < FD_SETSIZE; fd++)
      if(FD_ISSET(fd, set))
        JS_SetPropertyUint32(ctx, arg, i++, JS_NewUint32(ctx, fd));
    return TRUE;
  } else if(js_is_arraybuffer(ctx, arg)) {
    uint8_t* data;
    size_t len;
    if((data = JS_GetArrayBuffer(ctx, &len, arg))) {
      memcpy(data, set, MIN_NUM(len, sizeof(fd_set)));
      return TRUE;
    }
  }
  return FALSE;
}

static JSValue
fdset_toarray(JSContext* ctx, const fd_set* set) {
  uint32_t i = 0;
  int fd;
  JSValue ret = JS_NewArray(ctx);

  for(fd = 0; fd < FD_SETSIZE; fd++) {
    if(FD_ISSET(fd, set))
      JS_SetPropertyUint32(ctx, ret, i++, JS_NewInt64(ctx, fd));
  }
  return ret;
}

static BOOL
pollfd_read(JSContext* ctx, JSValueConst arg, struct pollfd* pfd) {
  if(js_is_array(ctx, arg) && js_array_length(ctx, arg) >= 2) {
    pfd->fd = js_get_propertyint_int32(ctx, arg, 0);
    pfd->events = js_get_propertyint_int32(ctx, arg, 1);
    pfd->revents = js_get_propertyint_int32(ctx, arg, 2);
    return TRUE;
  }

  if(JS_IsObject(arg) && !JS_IsNull(arg)) {
    pfd->fd = js_get_propertystr_int32(ctx, arg, "fd");
    pfd->events = js_get_propertystr_int32(ctx, arg, "events");
    pfd->revents = js_get_propertystr_int32(ctx, arg, "revents");
    return TRUE;
  }

  if(js_is_arraybuffer(ctx, arg)) {
    uint8_t* data;
    size_t len;
    if((data = JS_GetArrayBuffer(ctx, &len, arg))) {
      if(len >= sizeof(struct pollfd)) {
        memcpy(pfd, data, MIN_NUM(len, sizeof(struct pollfd)));
        return TRUE;
      }
    }
  }
  return FALSE;
}

static BOOL
pollfd_write(JSContext* ctx, const struct pollfd* pfd, JSValueConst arg) {
  if(js_is_array(ctx, arg)) {
    js_array_clear(ctx, arg);
    JS_SetPropertyUint32(ctx, arg, 0, JS_NewInt32(ctx, pfd->fd));
    JS_SetPropertyUint32(ctx, arg, 1, JS_NewInt32(ctx, pfd->events));
    JS_SetPropertyUint32(ctx, arg, 2, JS_NewInt32(ctx, pfd->revents));
    return TRUE;
  } else if(JS_IsObject(arg) && !JS_IsNull(arg)) {
    JS_SetPropertyStr(ctx, arg, "fd", JS_NewInt32(ctx, pfd->fd));
    JS_SetPropertyStr(ctx, arg, "events", JS_NewInt32(ctx, pfd->events));
    JS_SetPropertyStr(ctx, arg, "revents", JS_NewInt32(ctx, pfd->revents));
    return TRUE;
  } else if(js_is_arraybuffer(ctx, arg)) {
    uint8_t* data;
    size_t len;
    if((data = JS_GetArrayBuffer(ctx, &len, arg))) {
      if(len >= sizeof(struct pollfd)) {
        memcpy(data, pfd, MIN_NUM(len, sizeof(struct pollfd)));
        return TRUE;
      }
    }
  }
  return FALSE;
}

void*
optval_buf(JSContext* ctx, JSValueConst arg, int32_t** tmp_ptr, size_t* lenp) {
  uint32_t len;
  uint8_t* buf;

  if(JS_IsArray(ctx, arg)) {
    int i, n = MAX_NUM(1, MIN_NUM(js_array_length(ctx, arg), 1));
    len = n * sizeof(int32_t);
    *tmp_ptr = buf = js_mallocz(ctx, len);
    for(i = 0; i < n; i++) {
      JSValue el = JS_GetPropertyUint32(ctx, arg, i);
      JS_ToInt32(ctx, &(*tmp_ptr)[i], el);
      JS_FreeValue(ctx, el);
    }
  } else {
    InputBuffer optval = js_input_buffer(ctx, arg);
    buf = optval.data;
    len = optval.size;
  }

  if(lenp)
    *lenp = len;
  return buf;
}

static JSValue
js_socket(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  int32_t af, type = SOCK_STREAM, proto = IPPROTO_IP;
  Socket sock;

  JS_ToInt32(ctx, &af, argv[0]);
  if(argc >= 2)
    JS_ToInt32(ctx, &type, argv[1]);

  if(argc >= 3)
    JS_ToInt32(ctx, &proto, argv[2]);

  JS_SOCKETCALL(SYSCALL_SOCKET, sock, sock.fd = socket(af, type, proto));

  return ret;
}

static JSValue
js_socketpair(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t af, type = SOCK_STREAM, proto = IPPROTO_IP;
  int result, s[2];

  JS_ToInt32(ctx, &af, argv[0]);
  JS_ToInt32(ctx, &type, argv[1]);

  JS_ToInt32(ctx, &proto, argv[2]);

  if(!JS_IsArray(ctx, argv[3]))
    return JS_ThrowTypeError(ctx, "argument 4 must be array");

  if((result = socketpair(af, type, proto, s)) != -1) {
    JS_SetPropertyUint32(ctx, argv[3], 0, JS_NewInt64(ctx, s[0]));
    JS_SetPropertyUint32(ctx, argv[3], 1, JS_NewInt64(ctx, s[1]));
  }

  return JS_NewInt64(ctx, result);
}

static JSValue
js_select(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  uint64_t n;
  fd_set rfds, wfds, efds;
  fd_set *rset = 0, *wset = 0, *eset = 0;
  int ret;
  struct timeval tv = {0, 0}, *timeout = 0;
  JS_ToIndex(ctx, &n, argv[0]);
  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  FD_ZERO(&efds);

  if(argc >= 2)
    if(fdset_read(ctx, argv[1], &rfds))
      rset = &rfds;
  if(argc >= 3)
    if(fdset_read(ctx, argv[2], &wfds))
      wset = &wfds;
  if(argc >= 4)
    if(fdset_read(ctx, argv[3], &efds))
      eset = &efds;
  if(argc >= 5)
    if(timeval_read(ctx, argv[4], &tv))
      timeout = &tv;

  ret = select(n, rset, wset, eset, timeout);

  // if(ret == -1) return js_syscallerror_new(ctx, "select", errno);

  if(rset)
    fdset_write(ctx, rset, argv[1]);
  if(wset)
    fdset_write(ctx, wset, argv[2]);
  if(eset)
    fdset_write(ctx, eset, argv[3]);
  if(timeout)
    timeval_write(ctx, timeout, argv[4]);

  return JS_NewInt64(ctx, ret);
}

static JSValue
js_poll(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int ret;
  uint32_t nfds = 0;
  int32_t timeout = -1;
  struct pollfd* pfds;
  BOOL is_array = js_is_array(ctx, argv[0]), is_arraybuffer = js_is_arraybuffer(ctx, argv[0]);

  if(argc >= 2 && JS_IsNumber(argv[1]))
    JS_ToUint32(ctx, &nfds, argv[1]);
  if(argc >= 3 && JS_IsNumber(argv[2]))
    JS_ToInt32(ctx, &timeout, argv[2]);
  if(nfds == 0) {
    if(is_array)
      nfds = js_array_length(ctx, argv[0]);
    else if(is_arraybuffer)
      nfds = js_arraybuffer_bytelength(ctx, argv[0]) / sizeof(struct pollfd);
  }
  assert(nfds);
  pfds = alloca(sizeof(struct pollfd) * nfds);

  if(is_array) {
    uint32_t i;
    for(i = 0; i < nfds; i++) {
      JSValue member = JS_GetPropertyUint32(ctx, argv[0], i);
      if(!pollfd_read(ctx, member, &pfds[i])) {
        JS_FreeValue(ctx, member);
        return JS_ThrowInternalError(ctx, "pfds[%i] not valid", i);
      }
      JS_FreeValue(ctx, member);
    }
  } else if(is_arraybuffer) {
    uint8_t* data;
    size_t len;
    if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
      if(len > 0 && (len % sizeof(struct pollfd)) == 0) {
        memcpy(pfds, data, MIN_NUM(len, sizeof(struct pollfd) * nfds));
      } else {
        return JS_ThrowInternalError(ctx, "pfds[] length = %zu", len);
      }
    }
  }

  ret = poll(pfds, nfds, timeout);

  if(is_array) {
    uint32_t i;
    for(i = 0; i < nfds; i++) {
      JSValue member = JS_GetPropertyUint32(ctx, argv[0], i);
      if(!pollfd_write(ctx, &pfds[i], member)) {
        return JS_ThrowInternalError(ctx, "writing pfds[%i]", i);
      }
      JS_FreeValue(ctx, member);
    }
  } else if(is_arraybuffer) {
    uint8_t* data;
    size_t len;
    if((data = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
      if(len > 0 && (len % sizeof(struct pollfd)) == 0) {
        memcpy(data, pfds, MIN_NUM(len, sizeof(struct pollfd) * nfds));
      } else {
        return JS_ThrowInternalError(ctx, "pfds[] length = %zu", len);
      }
    }
  }
  return JS_NewInt32(ctx, ret);
}

static JSValue
js_sockopt(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int32_t sock = -1, level = -1, optname = -1;

  JS_ToInt32(ctx, &sock, argv[0]);
  JS_ToInt32(ctx, &level, argv[1]);
  JS_ToInt32(ctx, &optname, argv[2]);
}

static const JSCFunctionListEntry js_sockets_funcs[] = {
    JS_CFUNC_DEF("socket", 1, js_socket),
    JS_CFUNC_DEF("socketpair", 4, js_socketpair),
    JS_CFUNC_DEF("select", 1, js_select),
    JS_CFUNC_DEF("poll", 1, js_poll),
    JS_CFUNC_MAGIC_DEF("getsockopt", 4, js_sockopt, 0),
    JS_CFUNC_MAGIC_DEF("setsockopt", 4, js_sockopt, 1),
};

static JSValue
js_socket_new_proto(JSContext* ctx, JSValueConst proto, int fd) {
  JSValue obj;
  Socket sock = {fd, 0, 0, -1};

  if(js_socket_class_id == 0)
    js_sockets_init(ctx, 0);

  if(js_is_nullish(ctx, proto))
    proto = socket_proto;

  obj = JS_NewObjectProtoClass(ctx, proto, js_socket_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, sock.ptr);

  return obj;
fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_socket_new(JSContext* ctx, int sock) {
  if(js_socket_class_id == 0)
    js_sockets_init(ctx, 0);

  return js_socket_new_proto(ctx, socket_proto, sock);
}

enum {
  SOCKET_FD,
  SOCKET_OPEN,
  SOCKET_EOF,
  SOCKET_MODE,
  SOCKET_RET,
  SOCKET_ERRNO,
  SOCKET_SYSCALL,
  SOCKET_ERROR,
  SOCKET_LOCAL,
  SOCKET_REMOTE,
};

static JSValue
js_socket_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Socket sock = js_socket_data(this_val);
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case SOCKET_FD: {
      ret = JS_NewInt32(ctx, (int16_t)sock.fd);
      break;
    }
    case SOCKET_OPEN: {
      ret = JS_NewBool(ctx, socket_open(sock));
      break;
    }
    case SOCKET_EOF: {
      ret = JS_NewBool(ctx, socket_eof(sock));
      break;
    }
    case SOCKET_RET: {
      ret = JS_NewInt32(ctx, sock.ret);
      break;
    }
    case SOCKET_MODE: {
      int fl = fcntl(sock.fd, F_GETFL);
      ret = JS_NewInt32(ctx, fl);
      break;
    }
    case SOCKET_ERRNO: {
      ret = JS_NewUint32(ctx, socket_error(sock));
      break;
    }
    case SOCKET_SYSCALL: {
      const char* syscall;
      assert(sock.syscall > 0);
      assert(sock.syscall < socket_syscalls_size);
      if((syscall = socket_syscall(sock)))
        ret = JS_NewString(ctx, syscall);
      break;
    }
    case SOCKET_ERROR: {
      const char* syscall;
      if(sock.error) {
        assert(sock.syscall > 0);
        assert(sock.syscall < socket_syscalls_size);

        syscall = socket_syscall(sock);
        assert(syscall);

        ret = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, ret, "errno", JS_NewUint32(ctx, sock.error));
        JS_SetPropertyStr(ctx, ret, "syscall", JS_NewString(ctx, syscall));
        JS_SetPropertyStr(ctx, ret, "message", JS_NewString(ctx, strerror(sock.error)));

        /*if(JS_IsObject(syscallerror_proto))
          JS_SetPrototype(ctx, ret, syscallerror_proto);*/
      } else {
        ret = JS_NULL;
      }
      break;
    }
    case SOCKET_LOCAL: {
      SockAddr* a = sockaddr_new(ctx);
      socklen_t len = sizeof(SockAddr);
      JS_SOCKETCALL_RETURN(SYSCALL_GETSOCKNAME, sock, getsockname(sock.fd, (struct sockaddr*)a, &len), js_sockaddr_wrap(ctx, a), JS_NULL);
      break;
    }
    case SOCKET_REMOTE: {
      SockAddr* a = sockaddr_new(ctx);
      socklen_t len = sizeof(SockAddr);
      JS_SOCKETCALL_RETURN(SYSCALL_GETPEERNAME, sock, getpeername(sock.fd, (struct sockaddr*)a, &len), js_sockaddr_wrap(ctx, a), JS_NULL);
      break;
    }
  }
  JS_SetOpaque(this_val, sock.ptr);
  return ret;
}

static JSValue
js_socket_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Socket sock = js_socket_data(this_val);
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case SOCKET_MODE: {
      uint32_t mode = 0;
      JS_ToUint32(ctx, &mode, value);
      JS_SOCKETCALL_RETURN(SYSCALL_FCNTL, sock, fcntl(sock.fd, F_SETFL, mode), JS_NewInt32(ctx, sock.ret), JS_UNDEFINED);
      break;
    }
  }
  JS_SetOpaque(this_val, sock.ptr);
  return ret;
}

enum {
  SOCKET_NDELAY,
  SOCKET_BIND,
  SOCKET_ACCEPT,
  SOCKET_CONNECT,
  SOCKET_LISTEN,
  SOCKET_SEND,
  SOCKET_SENDTO,
  SOCKET_RECV,
  SOCKET_RECVFROM,
  SOCKET_SHUTDOWN,
  SOCKET_CLOSE,
  SOCKET_GETSOCKOPT,
  SOCKET_SETSOCKOPT,
};

static JSValue
js_socket_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Socket sock = js_socket_data2(ctx, this_val);
  JSValue ret = JS_UNDEFINED;
  SockAddr* a = 0;
  socklen_t alen = 0;

  if(magic >= SOCKET_BIND && magic <= SOCKET_CONNECT) {
    if(!(a = js_sockaddr_data(argv[0])))
      return JS_ThrowTypeError(ctx, "argument 1 must be of type SockAddr");
  }

  if(!socket_open(sock))
    return JS_ThrowInternalError(ctx, "Socket #%d is not open", sock.fd);

  switch(magic) {
    case SOCKET_NDELAY: {
      BOOL state = TRUE;
      int oldflags, newflags;
      if(argc >= 1)
        state = JS_ToBool(ctx, argv[0]);
      oldflags = fcntl(sock.fd, F_GETFL);
      newflags = state ? oldflags | O_NONBLOCK : oldflags & (~O_NONBLOCK);
      if(oldflags != newflags)
        JS_SOCKETCALL(SYSCALL_FCNTL, sock, fcntl(sock.fd, F_SETFL, newflags));
      break;
    }
    case SOCKET_BIND: {
      JS_SOCKETCALL(SYSCALL_BIND, sock, bind(sock.fd, (struct sockaddr*)a, sockaddr_size(a)));
      break;
    }
    case SOCKET_ACCEPT: {
      socklen_t addrlen = sizeof(SockAddr);
      JS_SOCKETCALL(SYSCALL_ACCEPT, sock, accept(sock.fd, (struct sockaddr*)a, &addrlen));
      break;
    }
    case SOCKET_CONNECT: {
      JS_SOCKETCALL(SYSCALL_CONNECT, sock, connect(sock.fd, (struct sockaddr*)a, sockaddr_size(a)));
      break;
    }
    case SOCKET_LISTEN: {
      int32_t backlog = 5;
      if(argc >= 1)
        JS_ToInt32(ctx, &backlog, argv[0]);
      JS_SOCKETCALL(SYSCALL_LISTEN, sock, listen(sock.fd, backlog));
      break;
    }
    case SOCKET_RECV:
    case SOCKET_RECVFROM: {
      int32_t flags = 0;
      InputBuffer buf = js_input_buffer(ctx, argv[0]);
      OffsetLength off = js_offset_length(ctx, buf.size, argc - 1, argv + 1);
      if(argc >= 4)
        JS_ToInt32(ctx, &flags, argv[3]);

      if(magic == SOCKET_RECVFROM) {
        if((a = argc >= 5 ? js_sockaddr_data(argv[4]) : 0))
          alen = sizeof(SockAddr);

        JS_SOCKETCALL(SYSCALL_RECVFROM, sock, recvfrom(sock.fd, buf.data + off.offset, offset_size(&off, buf.size), flags, &a->s, &alen));
      } else {
        JS_SOCKETCALL(SYSCALL_RECV, sock, recv(sock.fd, buf.data + off.offset, offset_size(&off, buf.size), flags));
      }
      break;
    }
    case SOCKET_SEND:
    case SOCKET_SENDTO: {
      int32_t flags = 0;
      InputBuffer buf = js_input_chars(ctx, argv[0]);
      OffsetLength off = js_offset_length(ctx, buf.size, argc - 1, argv + 1);

      if(argc >= 4)
        JS_ToInt32(ctx, &flags, argv[3]);

      if(magic == SOCKET_SENDTO) {
        if((a = argc >= 5 ? js_sockaddr_data(argv[4]) : 0))
          alen = sockaddr_size(a);
        JS_SOCKETCALL(SYSCALL_SENDTO, sock, sendto(sock.fd, buf.data + off.offset, offset_size(&off, buf.size), flags, &a->s, alen));
      } else {
        JS_SOCKETCALL(SYSCALL_SEND, sock, send(sock.fd, buf.data + off.offset, offset_size(&off, buf.size), flags));
      }
      break;
    }
    case SOCKET_SHUTDOWN: {
      int32_t how;
      JS_ToInt32(ctx, &how, argv[0]);
      JS_SOCKETCALL(SYSCALL_SHUTDOWN, sock, shutdown(sock.fd, how));
      break;
    }
    case SOCKET_CLOSE: {
      JS_SOCKETCALL(SYSCALL_CLOSE, sock, close(sock.fd));
      break;
    }
    case SOCKET_GETSOCKOPT: {
      int32_t level, optname;
      uint32_t optlen = sizeof(int);
      /*  InputBuffer optval = js_input_buffer(ctx, argv[2]);
        socklen_t len = optval.size;*/
      uint8_t* buf;
      int32_t *val, *tmp = 0;
      size_t len;
      JS_ToInt32(ctx, &level, argv[0]);
      JS_ToInt32(ctx, &optname, argv[1]);
      if(argc >= 4)
        JS_ToUint32(ctx, &optlen, argv[3]);
      JS_ToInt32(ctx, &optname, argv[1]);
      /*if(js_is_arraybuffer(ctx, argv[2])) {
        buf = JS_GetArrayBuffer(ctx, &len, argv[2]);
      } else if(js_is_array(ctx, argv[2])) {
        int i, n = js_array_length(ctx, argv[2]);
        len = MAX_NUM(MAX_NUM(n, 16) * sizeof(int), optlen);
        tmp = js_mallocz(ctx, len);
        val = (void*)tmp;

        for(i = 0; i < n; i++) {
          JSValue elem = JS_GetPropertyUint32(ctx, argv[2], i);
          int32_t num;
          JS_ToInt32(ctx, &num, elem);
          val[i] = num;
          JS_FreeValue(ctx, elem);
        }
        for(; i < 16; i++) { val[i] = 2; }

        if(n < i)
          n = i;

        buf = tmp;
      }*/
      buf = optval_buf(ctx, argv[2], &tmp, &len);

      JS_SOCKETCALL(SYSCALL_GETSOCKOPT, sock, getsockopt(sock.fd, level, optname, buf, &len));
      if(tmp) {
        js_array_clear(ctx, argv[2]);
        JS_SetPropertyUint32(ctx, argv[2], 0, JS_NewInt32(ctx, *(int32_t*)buf));
      }
      val = (void*)buf;
      if(tmp)
        js_free(ctx, tmp);
      /*printf("SYSCALL_GETSOCKOPT(%d, %d, %d, %p (%p), %zu) = %d\n", sock.fd, level, optname, ((ptrdiff_t*)val)[0], val, len, sock.ret);*/
      break;
    }
    case SOCKET_SETSOCKOPT: {
      int32_t level, optname, num = 0, *tmp = 0;
      uint32_t len = 0;
      uint8_t* buf = 0;
      if(argc >= 3)
        buf = optval_buf(ctx, argv[2], &tmp, &len);
      if(!buf) {
        buf = &num;
        len = sizeof(num);
      }
      JS_ToInt32(ctx, &level, argv[0]);
      JS_ToInt32(ctx, &optname, argv[1]);
      if(argc >= 4) {
        uint32_t newlen = 0;
        if(!JS_ToUint32(ctx, &newlen, argv[3])) {
          newlen = MIN_NUM(newlen, len);
          if(newlen)
            len = newlen;
        }
      }
      JS_SOCKETCALL(SYSCALL_SETSOCKOPT, sock, setsockopt(sock.fd, level, optname, buf, len));
      /*printf("SYSCALL_SETSOCKOPT(%d, %d, %d, %i (%p), %zu) = %d\n", sock.fd, level, optname, *(int*)buf, buf, len, sock.ret);*/

      if(tmp)
        js_free(ctx, tmp);

      break;
    }
  }
  JS_SetOpaque(this_val, sock.ptr);

  return ret;
}

static JSValue
js_socket_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  int32_t af, type = SOCK_STREAM, protocol = IPPROTO_IP;
  int sock = -1;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = socket_proto;

  JS_ToInt32(ctx, &af, argv[0]);
  if(argc >= 2) {
    JS_ToInt32(ctx, &type, argv[1]);
    if(argc >= 3)
      JS_ToInt32(ctx, &protocol, argv[2]);
  }
  sock = socket(af, type, protocol);

  return js_socket_new_proto(ctx, proto, sock);

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_socket_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Socket sock = js_socket_data2(ctx, this_val);

  JSValue obj = /*JS_NewObject(ctx); //*/ JS_NewObjectProto(ctx, socket_proto);
  JS_DefinePropertyValueStr(ctx, obj, "fd", JS_NewInt32(ctx, (int16_t)sock.fd), JS_PROP_ENUMERABLE);
  if(sock.ret >= 0) {
    JS_DefinePropertyValueStr(ctx, obj, "ret", JS_NewUint32(ctx, sock.ret), JS_PROP_ENUMERABLE);
  } else {
    const char* syscall = socket_syscall(sock);
    JS_DefinePropertyValueStr(ctx, obj, "errno", JS_NewUint32(ctx, sock.error), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, obj, "error", JS_NewString(ctx, strerror(sock.error)), JS_PROP_ENUMERABLE);
    // JS_DefinePropertyValueStr(ctx, obj, "error", js_syscallerror_new(ctx,
    // syscall, sock.error), JS_PROP_ENUMERABLE);
  }
  if(sock.syscall > 0 && sock.syscall < socket_syscalls_size)
    JS_DefinePropertyValueStr(ctx, obj, "syscall", JS_NewString(ctx, socket_syscall(sock)), JS_PROP_ENUMERABLE);

  // JS_DefinePropertyValueStr(ctx, obj, "stack", JS_NULL, JS_PROP_ENUMERABLE);
  return obj;
}

static JSValue
js_socket_valueof(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t fd = -1, id = JS_GetClassID(this_val);
  JSValue value;
  if(id == js_socket_class_id) {
    Socket sock = js_socket_data(this_val);
    fd = (int16_t)sock.fd;
  } else if(JS_IsNumber((value = JS_GetProperty(ctx, this_val, "fd")))) {
    JS_ToInt32(ctx, &fd, value);
    JS_FreeValue(ctx, value);
  }
  return JS_NewInt32(ctx, fd);
}

static void
js_socket_finalizer(JSRuntime* rt, JSValue val) {
  Socket sock = js_socket_data(val);

  if(sock.fd >= 0) {
    if(socket_open(sock))
      close(sock.fd);
  }
  JS_FreeValueRT(rt, val);
}

static JSClassDef js_socket_class = {
    .class_name = "Socket",
    .finalizer = js_socket_finalizer,
};

static const JSCFunctionListEntry js_socket_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("fd", js_socket_get, 0, SOCKET_FD, JS_PROP_C_W_E),
    JS_CGETSET_MAGIC_DEF("errno", js_socket_get, 0, SOCKET_ERRNO),
    JS_CGETSET_MAGIC_DEF("syscall", js_socket_get, 0, SOCKET_SYSCALL),
    JS_CGETSET_MAGIC_DEF("error", js_socket_get, 0, SOCKET_ERROR),
    JS_CGETSET_MAGIC_DEF("local", js_socket_get, 0, SOCKET_LOCAL),
    JS_CGETSET_MAGIC_DEF("remote", js_socket_get, 0, SOCKET_REMOTE),
    JS_CGETSET_MAGIC_DEF("open", js_socket_get, 0, SOCKET_OPEN),
    JS_CGETSET_MAGIC_DEF("eof", js_socket_get, 0, SOCKET_EOF),
    JS_CGETSET_MAGIC_DEF("mode", js_socket_get, js_socket_set, SOCKET_MODE),
    JS_CGETSET_MAGIC_DEF("ret", js_socket_get, js_socket_set, SOCKET_RET),
    JS_CFUNC_MAGIC_DEF("ndelay", 0, js_socket_method, SOCKET_NDELAY),
    JS_CFUNC_MAGIC_DEF("bind", 1, js_socket_method, SOCKET_BIND),
    JS_CFUNC_MAGIC_DEF("connect", 1, js_socket_method, SOCKET_CONNECT),
    JS_CFUNC_MAGIC_DEF("listen", 0, js_socket_method, SOCKET_LISTEN),
    JS_CFUNC_MAGIC_DEF("send", 1, js_socket_method, SOCKET_SEND),
    JS_CFUNC_MAGIC_DEF("sendto", 2, js_socket_method, SOCKET_SENDTO),
    JS_CFUNC_MAGIC_DEF("recv", 1, js_socket_method, SOCKET_RECV),
    JS_CFUNC_MAGIC_DEF("recvfrom", 2, js_socket_method, SOCKET_RECVFROM),
    JS_CFUNC_MAGIC_DEF("shutdown", 1, js_socket_method, SOCKET_SHUTDOWN),
    JS_CFUNC_MAGIC_DEF("close", 0, js_socket_method, SOCKET_CLOSE),
    JS_CFUNC_MAGIC_DEF("getsockopt", 3, js_socket_method, SOCKET_GETSOCKOPT),
    JS_CFUNC_MAGIC_DEF("setsockopt", 3, js_socket_method, SOCKET_SETSOCKOPT),
    JS_CFUNC_DEF("valueOf", 0, js_socket_valueof),
    JS_ALIAS_DEF("[Symbol.toPrimitive]", "valueOf"),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Socket", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_sockets_defines[] = {
    JS_CONSTANT(SHUT_RD),
    JS_CONSTANT(SHUT_WR),
    JS_CONSTANT(SHUT_RDWR),
    JS_CONSTANT(SO_ERROR),
    JS_CONSTANT(SO_DEBUG),
    JS_CONSTANT(SO_REUSEADDR),
    JS_CONSTANT(SO_KEEPALIVE),
    JS_CONSTANT(SO_DONTROUTE),
    JS_CONSTANT(SO_BROADCAST),
    JS_CONSTANT(SO_OOBINLINE),
    JS_CONSTANT(SO_REUSEPORT),
    JS_CONSTANT(SO_SNDBUF),
    JS_CONSTANT(SO_RCVBUF),
    JS_CONSTANT(SO_NO_CHECK),
    JS_CONSTANT(SO_PRIORITY),
    JS_CONSTANT(SO_BSDCOMPAT),
    JS_CONSTANT(SO_PASSCRED),
    JS_CONSTANT(SO_PEERCRED),
    JS_CONSTANT(SO_SECURITY_AUTHENTICATION),
    JS_CONSTANT(SO_SECURITY_ENCRYPTION_TRANSPORT),
    JS_CONSTANT(SO_SECURITY_ENCRYPTION_NETWORK),
    JS_CONSTANT(SO_BINDTODEVICE),
    JS_CONSTANT(SO_ATTACH_FILTER),
    JS_CONSTANT(SO_DETACH_FILTER),
    JS_CONSTANT(SO_GET_FILTER),
    JS_CONSTANT(SO_PEERNAME),
    JS_CONSTANT(SO_TIMESTAMP),
    JS_CONSTANT(SO_PEERSEC),
    JS_CONSTANT(SO_PASSSEC),
    JS_CONSTANT(SO_TIMESTAMPNS),
    JS_CONSTANT(SO_MARK),
    JS_CONSTANT(SO_TIMESTAMPING),
    JS_CONSTANT(SO_RXQ_OVFL),
    JS_CONSTANT(SO_WIFI_STATUS),
    JS_CONSTANT(SO_PEEK_OFF),
    JS_CONSTANT(SO_NOFCS),
    JS_CONSTANT(SO_LOCK_FILTER),
    JS_CONSTANT(SO_SELECT_ERR_QUEUE),
    JS_CONSTANT(SO_BUSY_POLL),
    JS_CONSTANT(SO_MAX_PACING_RATE),
    JS_CONSTANT(SO_BPF_EXTENSIONS),
    JS_CONSTANT(SO_SNDBUFFORCE),
    JS_CONSTANT(SO_RCVBUFFORCE),
    JS_CONSTANT(SO_RCVLOWAT),
    JS_CONSTANT(SO_SNDLOWAT),
    JS_CONSTANT(SO_RCVTIMEO),
    JS_CONSTANT(SO_SNDTIMEO),
    JS_CONSTANT(SO_ACCEPTCONN),
    JS_CONSTANT(SO_PROTOCOL),
    JS_CONSTANT(SO_DOMAIN),
    JS_CONSTANT(SO_INCOMING_CPU),
    JS_CONSTANT(SO_ATTACH_BPF),
    JS_CONSTANT(SO_DETACH_BPF),
    JS_CONSTANT(SOL_SOCKET),
    JS_CONSTANT(SOL_IPV6),
    JS_CONSTANT(SOL_ICMPV6),
    JS_CONSTANT(SOL_RAW),
    JS_CONSTANT(SOL_DECNET),
    JS_CONSTANT(SOL_PACKET),
    JS_CONSTANT(SOL_ATM),
    JS_CONSTANT(SOL_IRDA),
    JS_CONSTANT(AF_UNSPEC),
    JS_CONSTANT(AF_UNIX),
    JS_CONSTANT(AF_LOCAL),
    JS_CONSTANT(AF_INET),
    JS_CONSTANT(AF_AX25),
    JS_CONSTANT(AF_IPX),
    JS_CONSTANT(AF_APPLETALK),
    JS_CONSTANT(AF_NETROM),
    JS_CONSTANT(AF_BRIDGE),
    JS_CONSTANT(AF_ATMPVC),
    JS_CONSTANT(AF_X25),
    JS_CONSTANT(AF_INET6),
    JS_CONSTANT(AF_ROSE),
    JS_CONSTANT(AF_DECnet),
    JS_CONSTANT(AF_NETBEUI),
    JS_CONSTANT(AF_SECURITY),
    JS_CONSTANT(AF_KEY),
    JS_CONSTANT(AF_NETLINK),
    JS_CONSTANT(AF_ROUTE),
    JS_CONSTANT(AF_PACKET),
    JS_CONSTANT(AF_ASH),
    JS_CONSTANT(AF_ECONET),
    JS_CONSTANT(AF_ATMSVC),
    JS_CONSTANT(AF_SNA),
    JS_CONSTANT(AF_IRDA),
    JS_CONSTANT(AF_PPPOX),
    JS_CONSTANT(AF_WANPIPE),
    JS_CONSTANT(AF_LLC),
    JS_CONSTANT(AF_IB),
    JS_CONSTANT(AF_MPLS),
    JS_CONSTANT(AF_CAN),
    JS_CONSTANT(AF_TIPC),
    JS_CONSTANT(AF_BLUETOOTH),
    JS_CONSTANT(AF_IUCV),
    JS_CONSTANT(AF_RXRPC),
    JS_CONSTANT(AF_ISDN),
    JS_CONSTANT(AF_PHONET),
    JS_CONSTANT(AF_IEEE802154),
    JS_CONSTANT(AF_CAIF),
    JS_CONSTANT(AF_ALG),
    JS_CONSTANT(AF_NFC),
    JS_CONSTANT(AF_VSOCK),
    JS_CONSTANT(AF_MAX),
    JS_CONSTANT(PF_UNSPEC),
    JS_CONSTANT(PF_UNIX),
    JS_CONSTANT(PF_LOCAL),
    JS_CONSTANT(PF_INET),
    JS_CONSTANT(PF_AX25),
    JS_CONSTANT(PF_IPX),
    JS_CONSTANT(PF_APPLETALK),
    JS_CONSTANT(PF_NETROM),
    JS_CONSTANT(PF_BRIDGE),
    JS_CONSTANT(PF_ATMPVC),
    JS_CONSTANT(PF_X25),
    JS_CONSTANT(PF_INET6),
    JS_CONSTANT(PF_ROSE),
    JS_CONSTANT(PF_DECnet),
    JS_CONSTANT(PF_NETBEUI),
    JS_CONSTANT(PF_SECURITY),
    JS_CONSTANT(PF_KEY),
    JS_CONSTANT(PF_NETLINK),
    JS_CONSTANT(PF_ROUTE),
    JS_CONSTANT(PF_PACKET),
    JS_CONSTANT(PF_ASH),
    JS_CONSTANT(PF_ECONET),
    JS_CONSTANT(PF_ATMSVC),
    JS_CONSTANT(PF_SNA),
    JS_CONSTANT(PF_IRDA),
    JS_CONSTANT(PF_PPPOX),
    JS_CONSTANT(PF_WANPIPE),
    JS_CONSTANT(PF_LLC),
    JS_CONSTANT(PF_IB),
    JS_CONSTANT(PF_MPLS),
    JS_CONSTANT(PF_CAN),
    JS_CONSTANT(PF_TIPC),
    JS_CONSTANT(PF_BLUETOOTH),
    JS_CONSTANT(PF_IUCV),
    JS_CONSTANT(PF_RXRPC),
    JS_CONSTANT(PF_ISDN),
    JS_CONSTANT(PF_PHONET),
    JS_CONSTANT(PF_IEEE802154),
    JS_CONSTANT(PF_CAIF),
    JS_CONSTANT(PF_ALG),
    JS_CONSTANT(PF_NFC),
    JS_CONSTANT(PF_VSOCK),
    JS_CONSTANT(PF_MAX),
    JS_CONSTANT(SOCK_NONBLOCK),
    JS_CONSTANT(SOCK_CLOEXEC),
    JS_CONSTANT(SOCK_DGRAM),
    JS_CONSTANT(SOCK_STREAM),
    JS_CONSTANT(SOCK_RAW),
    JS_CONSTANT(SOCK_RDM),
    JS_CONSTANT(SOCK_SEQPACKET),
    JS_CONSTANT(SOCK_DCCP),
    JS_CONSTANT(SOCK_PACKET),
    JS_CONSTANT(IPPROTO_IP),
    JS_CONSTANT(IPPROTO_ICMP),
    JS_CONSTANT(IPPROTO_IGMP),
    JS_CONSTANT(IPPROTO_IPIP),
    JS_CONSTANT(IPPROTO_TCP),
    JS_CONSTANT(IPPROTO_EGP),
    JS_CONSTANT(IPPROTO_PUP),
    JS_CONSTANT(IPPROTO_UDP),
    JS_CONSTANT(IPPROTO_IDP),
    JS_CONSTANT(IPPROTO_RSVP),
    JS_CONSTANT(IPPROTO_GRE),
    JS_CONSTANT(IPPROTO_IPV6),
    JS_CONSTANT(IPPROTO_PIM),
    JS_CONSTANT(IPPROTO_ESP),
    JS_CONSTANT(IPPROTO_AH),
    JS_CONSTANT(IPPROTO_COMP),
    JS_CONSTANT(IPPROTO_SCTP),
    JS_CONSTANT(IPPROTO_UDPLITE),
    JS_CONSTANT(IPPROTO_RAW),
    JS_CONSTANT(IPPROTO_HOPOPTS),
    JS_CONSTANT(IPPROTO_ROUTING),
    JS_CONSTANT(IPPROTO_FRAGMENT),
    JS_CONSTANT(IPPROTO_ICMPV6),
    JS_CONSTANT(IPPROTO_NONE),
    JS_CONSTANT(IPPROTO_DSTOPTS),
    JS_CONSTANT(POLLIN),
    JS_CONSTANT(POLLPRI),
    JS_CONSTANT(POLLOUT),
    JS_CONSTANT(POLLERR),
    JS_CONSTANT(POLLHUP),
    JS_CONSTANT(POLLNVAL),
    JS_CONSTANT(POLLRDNORM),
    JS_CONSTANT(POLLRDBAND),
    JS_CONSTANT(POLLWRBAND),
    /*   JS_CONSTANT(POLLMSG),*/
    JS_CONSTANT(POLLREMOVE),
    JS_CONSTANT(POLLWRNORM),
    JS_CONSTANT(O_ASYNC),
    JS_CONSTANT(O_NONBLOCK),
    JS_CONSTANT(EAGAIN),
};

int
js_sockets_init(JSContext* ctx, JSModuleDef* m) {
  /* if(js_syscallerror_class_id == 0)
     js_syscallerror_init(ctx, 0);*/
  JSValue fdset_module, fdset_ns, fdset_ctor = JS_UNDEFINED, socklen_module, socklen_ns, socklen_ctor = JS_UNDEFINED;

  /*if(js_socket_class_id == 0)*/ {

    JS_NewClassID(&js_sockaddr_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_sockaddr_class_id, &js_sockaddr_class);

    sockaddr_ctor = JS_NewCFunction2(ctx, js_sockaddr_constructor, "SockAddr", 1, JS_CFUNC_constructor, 0);
    sockaddr_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, sockaddr_proto, js_sockaddr_proto_funcs, countof(js_sockaddr_proto_funcs));
    JS_SetClassProto(ctx, js_sockaddr_class_id, sockaddr_proto);

    js_set_inspect_method(ctx, sockaddr_proto, js_sockaddr_inspect);

    JS_NewClassID(&js_socket_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_socket_class_id, &js_socket_class);

    socket_ctor = JS_NewCFunction2(ctx, js_socket_constructor, "Socket", 1, JS_CFUNC_constructor, 0);
    socket_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, socket_proto, js_socket_proto_funcs, countof(js_socket_proto_funcs));
    JS_SetPropertyFunctionList(ctx, socket_ctor, js_sockets_defines, countof(js_sockets_defines));
    JS_SetClassProto(ctx, js_socket_class_id, socket_proto);

    js_set_inspect_method(ctx, socket_proto, js_socket_inspect);

    /*  fdset_module = js_eval_binary(ctx, qjsm_fd_set, qjsm_fd_set_size,
      FALSE); fdset_ns = js_module_ns(ctx, fdset_module); fdset_ctor =
      js_module_func(ctx, fdset_module);

      socklen_module = js_eval_binary(ctx, qjsm_socklen_t, qjsm_socklen_t_size,
      FALSE); socklen_ns = js_module_ns(ctx, socklen_module); socklen_ctor =
      js_module_func(ctx, socklen_module);*/
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "SockAddr", sockaddr_ctor);
    JS_SetModuleExport(ctx, m, "Socket", socket_ctor);
    JS_SetModuleExport(ctx, m, "fd_set", fdset_ctor);
    JS_SetModuleExport(ctx, m, "socklen_t", socklen_ctor);

    const char* module_name = JS_AtomToCString(ctx, m->module_name);

    if(!strcmp(module_name, "socket"))
      JS_SetModuleExport(ctx, m, "default", socket_ctor);

    JS_FreeCString(ctx, module_name);
    JS_SetModuleExportList(ctx, m, js_sockets_funcs, countof(js_sockets_funcs));
    JS_SetModuleExportList(ctx, m, js_sockets_defines, countof(js_sockets_defines));
  }

  return 0;
}

#ifdef JS_SOCKETS_MODULE
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_sockets
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  if(!(m = JS_NewCModule(ctx, module_name, &js_sockets_init)))
    return m;
  JS_AddModuleExport(ctx, m, "SockAddr");
  JS_AddModuleExport(ctx, m, "Socket");
  JS_AddModuleExport(ctx, m, "fd_set");
  JS_AddModuleExport(ctx, m, "socklen_t");

  size_t len, n = str_rchr(module_name, '/');
  if(module_name[n])
    ++n;
  len = str_rchr(&module_name[n], '.');

  // printf("JS_INIT_MODULE %.*s\n", len, module_name + n);

  if(!strncmp(&module_name[n], "sockets", len)) {
    JS_AddModuleExport(ctx, m, "default");
    JS_AddModuleExportList(ctx, m, js_sockets_funcs, countof(js_sockets_funcs));
    JS_AddModuleExportList(ctx, m, js_sockets_defines, countof(js_sockets_defines));
  }
  return m;
}
