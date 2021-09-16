#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "quickjs-internal.h"
#include "quickjs-syscallerror.h"
#include "utils.h"
#include "buffer-utils.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>

#define JS_CONSTANT(name) JS_PROP_INT32_DEF(#name, name, JS_PROP_ENUMERABLE)

#define JS_SOCKETCALL(s, name, retval) JS_SOCKETCALL_RETURN(s, name, retval, JS_NewInt32(ctx, result))

#define JS_SOCKETCALL_RETURN(s, name, retval, successval)                                                                                                                                              \
  do {                                                                                                                                                                                                 \
    int prev_errno = errno, result = retval;                                                                                                                                                           \
    if(result == -1) {                                                                                                                                                                                 \
      s.error = errno;                                                                                                                                                                                 \
    } else {                                                                                                                                                                                           \
      s.error = 0;                                                                                                                                                                                     \
    }                                                                                                                                                                                                  \
    ret = successval;                                                                                                                                                                                  \
    errno = prev_errno;                                                                                                                                                                                \
    JS_SetOpaque(this_val, s.ptr);                                                                                                                                                                     \
                                                                                                                                                                                                       \
  } while(0)
thread_local VISIBLE JSClassID js_sockaddr_class_id = 0, js_socket_class_id = 0;
thread_local JSValue sockaddr_proto = {JS_TAG_UNDEFINED}, sockaddr_ctor = {JS_TAG_UNDEFINED}, socket_proto = {JS_TAG_UNDEFINED}, socket_ctor = {JS_TAG_UNDEFINED};

typedef union {
  uint16_t family;
  struct sockaddr_in in;
  struct sockaddr_in6 in6;
} SockAddr;

typedef union {
  struct {
    uint32_t fd, error;
  };
  uint64_t u64;
  void* ptr;
} Socket;

enum { SOCKADDR_PROP_FAMILY = 99, SOCKADDR_PROP_ADDR, SOCKADDR_PROP_PORT };

static SockAddr*
sockaddr_new(JSContext* ctx, int af) {
  SockAddr* sa;

  if(!(sa = js_mallocz(ctx, sizeof(SockAddr))))
    return 0;

  sa->family = af;

  return sa;
}

static inline SockAddr*
js_sockaddr_data(JSValueConst value) {
  return JS_GetOpaque(value, js_sockaddr_class_id);
}

static inline SockAddr*
js_sockaddr_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_sockaddr_class_id);
}

static JSValue
js_sockaddr_wrap(JSContext* ctx, SockAddr* sa) {
  JSValue obj;
  obj = JS_NewObjectProtoClass(ctx, sockaddr_proto, js_sockaddr_class_id);
  JS_SetOpaque(obj, sa);
  return obj;
}

static JSValue
js_sockaddr_new(JSContext* ctx, int af) {
  SockAddr* sa;

  if(!(sa = sockaddr_new(ctx, af)))
    return JS_EXCEPTION;

  return js_sockaddr_wrap(ctx, sa);
}

static JSValue
js_sockaddr_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  int32_t af = -1;
  SockAddr* sa;

  if(!(sa = sockaddr_new(ctx, af)))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = sockaddr_proto;

  obj = JS_NewObjectProtoClass(ctx, proto, js_sockaddr_class_id);

  if(argc >= 1) {
    int32_t family;
    JS_ToInt32(ctx, &family, argv[0]);
    sa->family = family;
  }
  if(argc >= 2) {
    const char* str = JS_ToCString(ctx, argv[1]);

    inet_pton(sa->family, str, sa->family == AF_INET ? &sa->in.sin_addr : sa->family == AF_INET6 ? &sa->in6.sin6_addr : 0);
    JS_FreeCString(ctx, str);
  }
  if(argc >= 3) {
    uint32_t port;
    JS_ToUint32(ctx, &port, argv[2]);

    if(sa->family == AF_INET)
      sa->in.sin_port = htons(port);
    else if(sa->family == AF_INET6)
      sa->in6.sin6_port = htons(port);
  }
  JS_SetOpaque(obj, sa);

  return obj;

fail:
  js_free(ctx, sa);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_sockaddr_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  SockAddr* sa;

  if(!(sa = js_sockaddr_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {}
  return JS_EXCEPTION;
}

static JSValue
js_sockaddr_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue ret = JS_UNDEFINED;
  SockAddr* sa = js_sockaddr_data(this_val);

  /* if(!(sa = js_sockaddr_data2(ctx, this_val)))
     return JS_EXCEPTION;*/

  switch(magic) {
    case SOCKADDR_PROP_FAMILY: {
      if(sa)
        ret = JS_NewUint32(ctx, sa->family);
      break;
    }
    case SOCKADDR_PROP_ADDR: {
      if(sa) {
        char buf[INET6_ADDRSTRLEN] = {0};

        inet_ntop(sa->family, sa->family == AF_INET ? &sa->in.sin_addr : sa->family == AF_INET6 ? &sa->in6.sin6_addr : 0, buf, sizeof(buf));

        ret = JS_NewString(ctx, buf);
      }
      break;
    }
    case SOCKADDR_PROP_PORT: {
      if(sa)
        ret = JS_NewUint32(ctx, ntohs(sa->family == AF_INET ? sa->in.sin_port : sa->family == AF_INET6 ? sa->in6.sin6_port : 0));
      break;
    }
  }
  return ret;
}

static JSValue
js_sockaddr_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  SockAddr* sa;
  JSValue ret = JS_UNDEFINED;
  if(!(sa = js_sockaddr_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SOCKADDR_PROP_FAMILY: {
      int32_t af;
      JS_ToInt32(ctx, &af, value);
      if(sa)
        sa->family = af;
      break;
    }
    case SOCKADDR_PROP_ADDR: {
      const char* str = JS_ToCString(ctx, value);

      if(sa)
        inet_pton(sa->family, str, sa->family == AF_INET ? &sa->in.sin_addr : sa->family == AF_INET6 ? &sa->in6.sin6_addr : 0);
      JS_FreeCString(ctx, str);
      break;
    }
    case SOCKADDR_PROP_PORT: {
      uint32_t port;
      JS_ToUint32(ctx, &port, value);

      if(sa) {
        if(sa->family == AF_INET)
          sa->in.sin_port = htons(port);
        else if(sa->family == AF_INET6)
          sa->in6.sin6_port = htons(port);
      }
      break;
    }
  }
  return ret;
}

static JSValue
js_sockaddr_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret;

  switch(magic) {}
  return ret;
}

static JSValue
js_sockaddr_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  SockAddr* sa;
  uint16_t port;
  char buf[INET6_ADDRSTRLEN] = {0};

  if(!(sa = js_sockaddr_data2(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectProto(ctx, sockaddr_proto);

  if(sa->family)
    JS_DefinePropertyValueStr(ctx, obj, "family", JS_NewUint32(ctx, sa->family), JS_PROP_ENUMERABLE);

  if((port = ntohs(sa->family == AF_INET ? sa->in.sin_port : sa->family == AF_INET6 ? sa->in6.sin6_port : 0)))
    JS_DefinePropertyValueStr(ctx, obj, "port", JS_NewUint32(ctx, port), JS_PROP_ENUMERABLE);

  inet_ntop(sa->family, sa->family == AF_INET ? &sa->in.sin_addr : sa->family == AF_INET6 ? &sa->in6.sin6_addr : 0, buf, sizeof(buf));

  if(buf[0])
    JS_DefinePropertyValueStr(ctx, obj, "addr", JS_NewString(ctx, buf), JS_PROP_ENUMERABLE);

  return obj;
}

static void
js_sockaddr_finalizer(JSRuntime* rt, JSValue val) {
  SockAddr* sa;

  if((sa = js_sockaddr_data(val))) {

    js_free_rt(rt, sa);
  }
  JS_FreeValueRT(rt, val);
}

static const JSCFunctionListEntry js_sockaddr_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("family", js_sockaddr_get, js_sockaddr_set, SOCKADDR_PROP_FAMILY),
    JS_ALIAS_DEF("sin_family", "family"),
    JS_CGETSET_MAGIC_DEF("addr", js_sockaddr_get, js_sockaddr_set, SOCKADDR_PROP_ADDR),
    JS_ALIAS_DEF("sin_addr", "addr"),
    JS_CGETSET_MAGIC_DEF("port", js_sockaddr_get, js_sockaddr_set, SOCKADDR_PROP_PORT),
    JS_ALIAS_DEF("sin_port", "port"),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SockAddr", JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE),
};

static JSClassDef js_sockaddr_class = {.class_name = "SockAddr", .finalizer = js_sockaddr_finalizer};

static int
socket_getaf(int sock) {
  unsigned short addr[64];
  socklen_t len = sizeof(addr);

  if(getsockname(sock, (struct sockaddr*)addr, &len) != -1) {
    return addr[0];
  }

  return -1;
}

static JSValue
js_sockets_socket(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  int32_t af, type = SOCK_STREAM, proto = IPPROTO_IP;
  Socket s;

  JS_ToInt32(ctx, &af, argv[0]);
  if(argc >= 2)
    JS_ToInt32(ctx, &type, argv[1]);

  if(argc >= 3)
    JS_ToInt32(ctx, &proto, argv[2]);

  JS_SOCKETCALL(s, "socket", s.fd = socket(af, type, proto));

  return ret;
}

static JSValue
js_sockets_socketpair(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
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

static const JSCFunctionListEntry js_sockets_funcs[] = {
    JS_CFUNC_DEF("socket", 1, js_sockets_socket),
    JS_CFUNC_DEF("socketpair", 4, js_sockets_socketpair),
    JS_CONSTANT(AF_UNSPEC),
    JS_CONSTANT(AF_LOCAL),
    JS_CONSTANT(AF_UNIX),
    JS_CONSTANT(AF_INET),
    JS_CONSTANT(AF_INET6),
    JS_CONSTANT(AF_PACKET),
    JS_CONSTANT(SOCK_DGRAM),
    JS_CONSTANT(SOCK_STREAM),
    JS_CONSTANT(SOCK_RAW),
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
};

static inline Socket
js_socket_data(JSValueConst value) {
  Socket s;
  s.ptr = JS_GetOpaque(value, js_socket_class_id);
  return s;
}

static inline Socket
js_socket_data2(JSContext* ctx, JSValueConst value) {
  Socket s;
  s.ptr = JS_GetOpaque2(ctx, value, js_socket_class_id);
  return s;
}

JSValue
js_socket_new_proto(JSContext* ctx, JSValueConst proto, int sock) {
  JSValue obj;
  Socket s = {sock, 0};

  if(js_socket_class_id == 0)
    js_sockets_init(ctx, 0);

  if(js_is_nullish(ctx, proto))
    proto = socket_proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_socket_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, s.ptr);

  return obj;
fail:
  js_free(ctx, sock);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_socket_new(JSContext* ctx, int sock) {
  if(js_socket_class_id == 0)
    js_sockets_init(ctx, 0);

  return js_socket_new_proto(ctx, socket_proto, sock);
}

enum { SOCKET_PROP_FD, SOCKET_PROP_ERROR, SOCKET_PROP_LOCAL, SOCKET_PROP_REMOTE };

static JSValue
js_socket_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Socket s;
  int sock;
  JSValue ret = JS_UNDEFINED;

  s = js_socket_data2(ctx, this_val);
  sock = s.fd;

  switch(magic) {
    case SOCKET_PROP_FD: {
      ret = JS_NewUint32(ctx, sock);
      break;
    }
    case SOCKET_PROP_ERROR: {
      ret = JS_NewUint32(ctx, s.error);
      break;
    }
    case SOCKET_PROP_LOCAL: {
      SockAddr* sa = sockaddr_new(ctx, 0);
      socklen_t len = sizeof(SockAddr);
      JS_SOCKETCALL_RETURN(s, "getsockname", getsockname(sock, (struct sockaddr*)sa, &len), js_sockaddr_wrap(ctx, sa));
      break;
    }
    case SOCKET_PROP_REMOTE: {
      SockAddr* sa = sockaddr_new(ctx, 0);
      socklen_t len = sizeof(SockAddr);

      JS_SOCKETCALL_RETURN(s, "getpeername", getpeername(sock, (struct sockaddr*)sa, &len), js_sockaddr_wrap(ctx, sa));
      break;
    }
  }
  return ret;
}

enum {
  SOCKET_METHOD_NDELAY,
  SOCKET_METHOD_BIND,
  SOCKET_METHOD_ACCEPT,
  SOCKET_METHOD_CONNECT,
  SOCKET_METHOD_LISTEN,
  SOCKET_METHOD_RECV,
  SOCKET_METHOD_SEND,
  SOCKET_METHOD_SHUTDOWN,
  SOCKET_METHOD_CLOSE,
  SOCKET_METHOD_GETSOCKOPT,
  SOCKET_METHOD_SETSOCKOPT,
};

static JSValue
js_socket_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int sock;
  Socket s;
  JSValue ret = JS_UNDEFINED;

  s = js_socket_data2(ctx, this_val);
  sock = s.fd;

  switch(magic) {
    case SOCKET_METHOD_NDELAY: {
      BOOL state = FALSE;
      int oldflags, newflags;
      if(argc >= 1)
        state = JS_ToBool(ctx, argv[0]);

      oldflags = fcntl(sock, F_GETFL);

      newflags = state ? oldflags | O_NONBLOCK : oldflags & (~O_NONBLOCK);
      if(oldflags != newflags)

        JS_SOCKETCALL(s, "fcntl", fcntl(sock, F_SETFL, newflags));
      break;
    }
    case SOCKET_METHOD_BIND: {
      SockAddr* sa;

      if(!(sa = js_sockaddr_data2(ctx, argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be of type SockAddr");

      JS_SOCKETCALL(s, "bind", bind(sock, (struct sockaddr*)sa, sa->family == AF_INET ? sizeof(struct sockaddr_in) : sa->family == AF_INET6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr)));
      break;
    }
    case SOCKET_METHOD_ACCEPT: {
      SockAddr* sa;
      int result;
      socklen_t addrlen = sizeof(struct sockaddr);

      if(!(sa = js_sockaddr_data2(ctx, argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be of type SockAddr");

      JS_SOCKETCALL(s, "accept", accept(sock, (struct sockaddr*)sa, &addrlen));
      break;
    }
    case SOCKET_METHOD_CONNECT: {
      SockAddr* sa;
      int result;

      if(!(sa = js_sockaddr_data2(ctx, argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be of type SockAddr");

      JS_SOCKETCALL(s,
                    "connect",
                    connect(sock,
                            (struct sockaddr*)sa,
                            sa->family == AF_INET    ? sizeof(struct sockaddr_in)
                            : sa->family == AF_INET6 ? sizeof(struct sockaddr_in6)
                                                     : sizeof(struct sockaddr)));
      break;
    }
    case SOCKET_METHOD_LISTEN: {
      int32_t backlog = 5;
      if(argc >= 1)
        JS_ToInt32(ctx, &backlog, argv[0]);
      JS_SOCKETCALL(s, "listen", listen(sock, backlog));
      break;
    }
    case SOCKET_METHOD_RECV: {
      uint64_t len;
      int32_t flags = 0;
      InputBuffer buf = js_input_buffer(ctx, argv[0]);
      if(argc >= 2)
        JS_ToIndex(ctx, &len, argv[1]);
      else
        len = buf.size;
      if(argc >= 3)
        JS_ToInt32(ctx, &flags, argv[2]);
      JS_SOCKETCALL(s, "recv", recv(sock, buf.data, len, flags));
      break;
    }
    case SOCKET_METHOD_SEND: {
      uint64_t len;
      int32_t flags = 0;
      InputBuffer buf = js_input_buffer(ctx, argv[0]);
      if(argc >= 2)
        JS_ToIndex(ctx, &len, argv[1]);
      else
        len = buf.size;
      if(argc >= 3)
        JS_ToInt32(ctx, &flags, argv[2]);
      JS_SOCKETCALL(s, "send", send(sock, buf.data, len, flags));
      break;
    }
    case SOCKET_METHOD_SHUTDOWN: {
      int32_t how;
      JS_ToInt32(ctx, &how, argv[0]);
      JS_SOCKETCALL(s, "shutdown", shutdown(sock, how));
      break;
    }
    case SOCKET_METHOD_CLOSE: {
      JS_SOCKETCALL(s, "close", close(sock));
      break;
    }
    case SOCKET_METHOD_GETSOCKOPT: {
      int32_t level, optname;
      InputBuffer optval = js_input_buffer(ctx, argv[2]);
      socklen_t len = optval.size;
      JS_ToInt32(ctx, &level, argv[0]);
      JS_ToInt32(ctx, &optname, argv[1]);

      JS_SOCKETCALL(s, "getsockopt", getsockopt(sock, level, optname, optval.data, &len));
      break;
    }
    case SOCKET_METHOD_SETSOCKOPT: {
      int32_t level, optname;
      InputBuffer optval = js_input_buffer(ctx, argv[2]);
      socklen_t len = optval.size;
      JS_ToInt32(ctx, &level, argv[0]);
      JS_ToInt32(ctx, &optname, argv[1]);

      JS_SOCKETCALL(s, "setsockopt", setsockopt(sock, level, optname, optval.data, len));
      break;
    }
  }
  return ret;
}

JSValue
js_socket_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  int32_t af, type = SOCK_STREAM, protocol = IPPROTO_IP;
  int sock = -1;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
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
  int sock;
  Socket s;

  s = js_socket_data2(ctx, this_val);
  sock = s.fd;

  JSValue obj = JS_NewObjectProto(ctx, socket_proto);
  JS_DefinePropertyValueStr(ctx, obj, "fd", JS_NewUint32(ctx, sock), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "error", JS_NewUint32(ctx, s.error), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "stack", JS_NULL, 0);
  return obj;
}

void
js_socket_finalizer(JSRuntime* rt, JSValue val) {
  Socket s;
  int sock;

  s = js_socket_data(val);
  sock = s.fd;

  if(sock >= 0) {
    close(sock);
  }
  JS_FreeValueRT(rt, val);
}

static JSClassDef js_socket_class = {
    .class_name = "Socket",
    .finalizer = js_socket_finalizer,
};

static const JSCFunctionListEntry js_sockets_defines[] = {
    JS_PROP_INT32_DEF("AF_INET", AF_INET, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("AF_INET6", AF_INET6, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SOCK_STREAM", SOCK_STREAM, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SOCK_DGRAM", SOCK_DGRAM, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("IPPROTO_IP", IPPROTO_IP, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("IPPROTO_UDP", IPPROTO_UDP, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("IPPROTO_TCP", IPPROTO_TCP, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SHUT_RD", SHUT_RD, 0),
    JS_PROP_INT32_DEF("SHUT_WR", SHUT_WR, 0),
    JS_PROP_INT32_DEF("SHUT_RDWR", SHUT_RDWR, 0),
    JS_PROP_INT32_DEF("SO_ERROR", SO_ERROR, 0),
    JS_PROP_INT32_DEF("SO_DEBUG", SO_DEBUG, 0),
    JS_PROP_INT32_DEF("SO_REUSEADDR", SO_REUSEADDR, 0),
    JS_PROP_INT32_DEF("SO_KEEPALIVE", SO_KEEPALIVE, 0),
    JS_PROP_INT32_DEF("SO_DONTROUTE", SO_DONTROUTE, 0),
    JS_PROP_INT32_DEF("SO_BROADCAST", SO_BROADCAST, 0),
    JS_PROP_INT32_DEF("SO_OOBINLINE", SO_OOBINLINE, 0),
    JS_PROP_INT32_DEF("SO_REUSEPORT", SO_REUSEPORT, 0),
    JS_PROP_INT32_DEF("SO_SNDBUF", SO_SNDBUF, 0),
    JS_PROP_INT32_DEF("SO_RCVBUF", SO_RCVBUF, 0),
    JS_PROP_INT32_DEF("SO_NO_CHECK", SO_NO_CHECK, 0),
    JS_PROP_INT32_DEF("SO_PRIORITY", SO_PRIORITY, 0),
    JS_PROP_INT32_DEF("SO_BSDCOMPAT", SO_BSDCOMPAT, 0),
    JS_PROP_INT32_DEF("SO_PASSCRED", SO_PASSCRED, 0),
    JS_PROP_INT32_DEF("SO_PEERCRED", SO_PEERCRED, 0),
    JS_PROP_INT32_DEF("SO_SECURITY_AUTHENTICATION", SO_SECURITY_AUTHENTICATION, 0),
    JS_PROP_INT32_DEF("SO_SECURITY_ENCRYPTION_TRANSPORT", SO_SECURITY_ENCRYPTION_TRANSPORT, 0),
    JS_PROP_INT32_DEF("SO_SECURITY_ENCRYPTION_NETWORK", SO_SECURITY_ENCRYPTION_NETWORK, 0),
    JS_PROP_INT32_DEF("SO_BINDTODEVICE", SO_BINDTODEVICE, 0),
    JS_PROP_INT32_DEF("SO_ATTACH_FILTER", SO_ATTACH_FILTER, 0),
    JS_PROP_INT32_DEF("SO_DETACH_FILTER", SO_DETACH_FILTER, 0),
    JS_PROP_INT32_DEF("SO_GET_FILTER", SO_GET_FILTER, 0),
    JS_PROP_INT32_DEF("SO_PEERNAME", SO_PEERNAME, 0),
    JS_PROP_INT32_DEF("SO_TIMESTAMP", SO_TIMESTAMP, 0),
    JS_PROP_INT32_DEF("SO_PEERSEC", SO_PEERSEC, 0),
    JS_PROP_INT32_DEF("SO_PASSSEC", SO_PASSSEC, 0),
    JS_PROP_INT32_DEF("SO_TIMESTAMPNS", SO_TIMESTAMPNS, 0),
    JS_PROP_INT32_DEF("SO_MARK", SO_MARK, 0),
    JS_PROP_INT32_DEF("SO_TIMESTAMPING", SO_TIMESTAMPING, 0),
    JS_PROP_INT32_DEF("SO_RXQ_OVFL", SO_RXQ_OVFL, 0),
    JS_PROP_INT32_DEF("SO_WIFI_STATUS", SO_WIFI_STATUS, 0),
    JS_PROP_INT32_DEF("SO_PEEK_OFF", SO_PEEK_OFF, 0),
    JS_PROP_INT32_DEF("SO_NOFCS", SO_NOFCS, 0),
    JS_PROP_INT32_DEF("SO_LOCK_FILTER", SO_LOCK_FILTER, 0),
    JS_PROP_INT32_DEF("SO_SELECT_ERR_QUEUE", SO_SELECT_ERR_QUEUE, 0),
    JS_PROP_INT32_DEF("SO_BUSY_POLL", SO_BUSY_POLL, 0),
    JS_PROP_INT32_DEF("SO_MAX_PACING_RATE", SO_MAX_PACING_RATE, 0),
    JS_PROP_INT32_DEF("SO_BPF_EXTENSIONS", SO_BPF_EXTENSIONS, 0),
    JS_PROP_INT32_DEF("SO_SNDBUFFORCE", SO_SNDBUFFORCE, 0),
    JS_PROP_INT32_DEF("SO_RCVBUFFORCE", SO_RCVBUFFORCE, 0),
    JS_PROP_INT32_DEF("SO_RCVLOWAT", SO_RCVLOWAT, 0),
    JS_PROP_INT32_DEF("SO_SNDLOWAT", SO_SNDLOWAT, 0),
    JS_PROP_INT32_DEF("SO_RCVTIMEO", SO_RCVTIMEO, 0),
    JS_PROP_INT32_DEF("SO_SNDTIMEO", SO_SNDTIMEO, 0),
    JS_PROP_INT32_DEF("SO_ACCEPTCONN", SO_ACCEPTCONN, 0),
    JS_PROP_INT32_DEF("SO_PROTOCOL", SO_PROTOCOL, 0),
    JS_PROP_INT32_DEF("SO_DOMAIN", SO_DOMAIN, 0),
    JS_PROP_INT32_DEF("SO_INCOMING_CPU", SO_INCOMING_CPU, 0),
    JS_PROP_INT32_DEF("SO_ATTACH_BPF", SO_ATTACH_BPF, 0),
    JS_PROP_INT32_DEF("SO_DETACH_BPF", SO_DETACH_BPF, 0),
    JS_PROP_INT32_DEF("SO_ATTACH_REUSEPORT_CBPF", SO_ATTACH_REUSEPORT_CBPF, 0),
    JS_PROP_INT32_DEF("SO_ATTACH_REUSEPORT_EBPF", SO_ATTACH_REUSEPORT_EBPF, 0),
    JS_PROP_INT32_DEF("SO_CNX_ADVICE", SO_CNX_ADVICE, 0),
    JS_PROP_INT32_DEF("SO_MEMINFO", SO_MEMINFO, 0),
    JS_PROP_INT32_DEF("SO_INCOMING_NAPI_ID", SO_INCOMING_NAPI_ID, 0),
    JS_PROP_INT32_DEF("SO_COOKIE", SO_COOKIE, 0),
    JS_PROP_INT32_DEF("SO_PEERGROUPS", SO_PEERGROUPS, 0),
    JS_PROP_INT32_DEF("SO_ZEROCOPY", SO_ZEROCOPY, 0),
    JS_PROP_INT32_DEF("SOL_SOCKET", SOL_SOCKET, 0),
    JS_PROP_INT32_DEF("SOL_IPV6", SOL_IPV6, 0),
    JS_PROP_INT32_DEF("SOL_ICMPV6", SOL_ICMPV6, 0),
    JS_PROP_INT32_DEF("SOL_RAW", SOL_RAW, 0),
    JS_PROP_INT32_DEF("SOL_DECNET", SOL_DECNET, 0),
    JS_PROP_INT32_DEF("SOL_PACKET", SOL_PACKET, 0),
    JS_PROP_INT32_DEF("SOL_ATM", SOL_ATM, 0),
    JS_PROP_INT32_DEF("SOL_IRDA", SOL_IRDA, 0),
    JS_PROP_INT32_DEF("SOL_NETBEUI", SOL_NETBEUI, 0),
    JS_PROP_INT32_DEF("SOL_LLC", SOL_LLC, 0),
    JS_PROP_INT32_DEF("SOL_DCCP", SOL_DCCP, 0),
    JS_PROP_INT32_DEF("SOL_NETLINK", SOL_NETLINK, 0),
    JS_PROP_INT32_DEF("SOL_TIPC", SOL_TIPC, 0),
    JS_PROP_INT32_DEF("SOL_RXRPC", SOL_RXRPC, 0),
    JS_PROP_INT32_DEF("SOL_PPPOL2TP", SOL_PPPOL2TP, 0),
    JS_PROP_INT32_DEF("SOL_BLUETOOTH", SOL_BLUETOOTH, 0),
    JS_PROP_INT32_DEF("SOL_PNPIPE", SOL_PNPIPE, 0),
    JS_PROP_INT32_DEF("SOL_RDS", SOL_RDS, 0),
    JS_PROP_INT32_DEF("SOL_IUCV", SOL_IUCV, 0),
    JS_PROP_INT32_DEF("SOL_CAIF", SOL_CAIF, 0),
    JS_PROP_INT32_DEF("SOL_ALG", SOL_ALG, 0),
    JS_PROP_INT32_DEF("SOL_NFC", SOL_NFC, 0),
    JS_PROP_INT32_DEF("SOL_KCM", SOL_KCM, 0),

};

static const JSCFunctionListEntry js_socket_funcs[] = {
    JS_CGETSET_MAGIC_DEF("fd", js_socket_get, 0, SOCKET_PROP_FD),
    JS_CGETSET_MAGIC_DEF("error", js_socket_get, 0, SOCKET_PROP_ERROR),
    JS_CGETSET_MAGIC_DEF("local", js_socket_get, 0, SOCKET_PROP_LOCAL),
    JS_CGETSET_MAGIC_DEF("remote", js_socket_get, 0, SOCKET_PROP_REMOTE),
    JS_CFUNC_MAGIC_DEF("ndelay", 0, js_socket_method, SOCKET_METHOD_NDELAY),
    JS_CFUNC_MAGIC_DEF("bind", 1, js_socket_method, SOCKET_METHOD_BIND),
    JS_CFUNC_MAGIC_DEF("connect", 1, js_socket_method, SOCKET_METHOD_CONNECT),
    JS_CFUNC_MAGIC_DEF("listen", 0, js_socket_method, SOCKET_METHOD_LISTEN),
    JS_CFUNC_MAGIC_DEF("send", 1, js_socket_method, SOCKET_METHOD_SEND),
    JS_CFUNC_MAGIC_DEF("recv", 1, js_socket_method, SOCKET_METHOD_RECV),
    JS_CFUNC_MAGIC_DEF("shutdown", 1, js_socket_method, SOCKET_METHOD_SHUTDOWN),
    JS_CFUNC_MAGIC_DEF("close", 0, js_socket_method, SOCKET_METHOD_CLOSE),
    JS_CFUNC_MAGIC_DEF("getsockopt", 3, js_socket_method, SOCKET_METHOD_GETSOCKOPT),
    JS_CFUNC_MAGIC_DEF("setsockopt", 3, js_socket_method, SOCKET_METHOD_SETSOCKOPT),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Socket", JS_PROP_CONFIGURABLE),
};

int
js_sockets_init(JSContext* ctx, JSModuleDef* m) {

  if(js_socket_class_id == 0) {

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

    JS_SetPropertyFunctionList(ctx, socket_proto, js_socket_funcs, countof(js_socket_funcs));
    JS_SetPropertyFunctionList(ctx, socket_ctor, js_sockets_defines, countof(js_sockets_defines));
    JS_SetClassProto(ctx, js_socket_class_id, socket_proto);

    js_set_inspect_method(ctx, socket_proto, js_socket_inspect);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "SockAddr", sockaddr_ctor);
    JS_SetModuleExport(ctx, m, "Socket", socket_ctor);

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

  size_t len, n = str_rchr(module_name, '/');
  if(module_name[n])
    ++n;
  len = str_rchr(&module_name[n], '.');

  printf("JS_INIT_MODULE %.*s\n", len, module_name + n);

  if(!strncmp(&module_name[n], "sockets", len)) {
    JS_AddModuleExport(ctx, m, "default");
    JS_AddModuleExportList(ctx, m, js_sockets_funcs, countof(js_sockets_funcs));
    //  JS_AddModuleExportList(ctx, m, js_sockets_defines, countof(js_sockets_defines));
  }
  return m;
}
