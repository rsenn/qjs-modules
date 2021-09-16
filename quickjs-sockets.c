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

thread_local VISIBLE JSClassID js_sockaddr_class_id = 0, js_socket_class_id = 0;
thread_local JSValue sockaddr_proto = {JS_TAG_UNDEFINED},
                     sockaddr_ctor = {JS_TAG_UNDEFINED},
                     socket_proto = {JS_TAG_UNDEFINED},
                     socket_ctor = {JS_TAG_UNDEFINED};

typedef union {
  struct sockaddr_in in;
  struct sockaddr_in6 in6;
} SockAddr;

enum { SOCKADDR_PROP_FAMILY = 99, SOCKADDR_PROP_ADDR, SOCKADDR_PROP_PORT };
enum { SOCKET_PROP_FD, SOCKET_PROP_LOCAL, SOCKET_PROP_REMOTE };
enum {
  SOCKET_METHOD_NDELAY,
  SOCKET_METHOD_BIND,
  SOCKET_METHOD_CONNECT,
  SOCKET_METHOD_RECV,
  SOCKET_METHOD_SEND,
  SOCKET_METHOD_CLOSE
};

static SockAddr*
sockaddr_new(JSContext* ctx, int af) {
  SockAddr* sa;

  if(!(sa = js_mallocz(ctx, sizeof(SockAddr))))
    return 0;

  sa->in.sin_family = af;

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
js_sockaddr_constructor(JSContext* ctx,
                        JSValueConst new_target,
                        int argc,
                        JSValueConst argv[]) {
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
    sa->in.sin_family = family;
  }
  if(argc >= 2) {
    const char* str = JS_ToCString(ctx, argv[1]);

    inet_pton(sa->in.sin_family,
              str,
              sa->in.sin_family == AF_INET    ? &sa->in.sin_addr
              : sa->in.sin_family == AF_INET6 ? &sa->in6.sin6_addr
                                              : 0);
    JS_FreeCString(ctx, str);
  }
  if(argc >= 3) {
    uint32_t port;
    JS_ToUint32(ctx, &port, argv[2]);

    if(sa->in.sin_family == AF_INET)
      sa->in.sin_port = htons(port);
    else if(sa->in.sin_family == AF_INET6)
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
js_sockaddr_method(JSContext* ctx,
                   JSValueConst this_val,
                   int argc,
                   JSValueConst argv[],
                   int magic) {
  SockAddr* sa;

  if(!(sa = js_sockaddr_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {}
  return JS_EXCEPTION;
}

static JSValue
js_sockaddr_get(JSContext* ctx, JSValueConst this_val, int magic) {
  SockAddr* sa;
  JSValue ret = JS_UNDEFINED;

  if(!(sa = js_sockaddr_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SOCKADDR_PROP_FAMILY: {
      ret = JS_NewUint32(ctx, sa->in.sin_family);
      break;
    }
    case SOCKADDR_PROP_ADDR: {
      char buf[INET6_ADDRSTRLEN] = {0};

      inet_ntop(sa->in.sin_family,
                sa->in.sin_family == AF_INET    ? &sa->in.sin_addr
                : sa->in.sin_family == AF_INET6 ? &sa->in6.sin6_addr
                                                : 0,
                buf,
                sizeof(buf));

      ret = JS_NewString(ctx, buf);
      break;
    }
    case SOCKADDR_PROP_PORT: {
      ret =
          JS_NewUint32(ctx,
                       ntohs(sa->in.sin_family == AF_INET    ? sa->in.sin_port
                             : sa->in.sin_family == AF_INET6 ? sa->in6.sin6_port
                                                             : 0));
      break;
    }
  }
  return ret;
}

static JSValue
js_sockaddr_set(JSContext* ctx,
                JSValueConst this_val,
                JSValueConst value,
                int magic) {
  SockAddr* sa;
  JSValue ret = JS_UNDEFINED;
  if(!(sa = js_sockaddr_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SOCKADDR_PROP_FAMILY: {
      int32_t af;
      JS_ToInt32(ctx, &af, value);
      sa->in.sin_family = af;
      break;
    }
    case SOCKADDR_PROP_ADDR: {
      const char* str = JS_ToCString(ctx, value);

      inet_pton(sa->in.sin_family,
                str,
                sa->in.sin_family == AF_INET    ? &sa->in.sin_addr
                : sa->in.sin_family == AF_INET6 ? &sa->in6.sin6_addr
                                                : 0);
      JS_FreeCString(ctx, str);
      break;
    }
    case SOCKADDR_PROP_PORT: {
      uint32_t port;
      JS_ToUint32(ctx, &port, value);

      if(sa->in.sin_family == AF_INET)
        sa->in.sin_port = htons(port);
      else if(sa->in.sin_family == AF_INET6)
        sa->in6.sin6_port = htons(port);

      break;
    }
  }
  return ret;
}

static JSValue
js_sockaddr_funcs(JSContext* ctx,
                  JSValueConst this_val,
                  int argc,
                  JSValueConst argv[],
                  int magic) {
  JSValue ret;

  switch(magic) {}
  return ret;
}

static JSValue
js_sockaddr_inspect(JSContext* ctx,
                    JSValueConst this_val,
                    int argc,
                    JSValueConst argv[]) {
  SockAddr* sa;
  char buf[INET6_ADDRSTRLEN] = {0};

  if(!(sa = js_sockaddr_data2(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj =
      JS_NewObjectProtoClass(ctx, sockaddr_proto, js_sockaddr_class_id);
  JS_DefinePropertyValueStr(ctx,
                            obj,
                            "sin_family",
                            JS_NewUint32(ctx, sa->in.sin_family),
                            JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx,
                            obj,
                            "sin_port",
                            JS_NewUint32(ctx,
                                         ntohs(sa->in.sin_family == AF_INET
                                                   ? sa->in.sin_port
                                               : sa->in.sin_family == AF_INET6
                                                   ? sa->in6.sin6_port
                                                   : 0)),
                            JS_PROP_ENUMERABLE);

  inet_ntop(sa->in.sin_family,
            sa->in.sin_family == AF_INET    ? &sa->in.sin_addr
            : sa->in.sin_family == AF_INET6 ? &sa->in6.sin6_addr
                                            : 0,
            buf,
            sizeof(buf));

  JS_DefinePropertyValueStr(
      ctx, obj, "sin_addr", JS_NewString(ctx, buf), JS_PROP_ENUMERABLE);
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
    JS_CGETSET_MAGIC_DEF(
        "sin_family", js_sockaddr_get, js_sockaddr_set, SOCKADDR_PROP_FAMILY),
    JS_ALIAS_DEF("family", "sin_family"),
    JS_CGETSET_MAGIC_DEF(
        "sin_addr", js_sockaddr_get, js_sockaddr_set, SOCKADDR_PROP_ADDR),
    JS_ALIAS_DEF("addr", "sin_addr"),
    JS_CGETSET_MAGIC_DEF(
        "sin_port", js_sockaddr_get, js_sockaddr_set, SOCKADDR_PROP_PORT),
    JS_ALIAS_DEF("port", "sin_port"),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]",
                       "SockAddr",
                       JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE),
};

static JSClassDef js_sockaddr_class = {.class_name = "SockAddr",
                                       .finalizer = js_sockaddr_finalizer};

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
js_sockets_socket(JSContext* ctx,
                  JSValueConst this_val,
                  int argc,
                  JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  int32_t af, type = SOCK_STREAM, proto = IPPROTO_IP;

  JS_ToInt32(ctx, &af, argv[0]);
  if(argc >= 2)
    JS_ToInt32(ctx, &type, argv[1]);

  if(argc >= 3)
    JS_ToInt32(ctx, &proto, argv[2]);

  ret = JS_NewInt64(ctx, socket(af, type, proto));

  return ret;
}

static JSValue
js_sockets_socketpair(JSContext* ctx,
                      JSValueConst this_val,
                      int argc,
                      JSValueConst argv[]) {
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

int
js_socket_data(JSContext* ctx, JSValueConst value) {
  return (int)JS_GetOpaque(value, js_socket_class_id);
}

JSValue
js_socket_new_proto(JSContext* ctx, JSValueConst proto, int sock) {
  JSValue obj;

  if(js_socket_class_id == 0)
    js_sockets_init(ctx, 0);

  if(js_is_nullish(ctx, proto))
    proto = socket_proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_socket_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, (void*)sock);

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

static JSValue
js_socket_get(JSContext* ctx, JSValueConst this_val, int magic) {
  int sock;
  JSValue ret = JS_UNDEFINED;

  if((sock = js_socket_data(ctx, this_val)) == -1)
    return ret;

  switch(magic) {
    case SOCKET_PROP_FD: {
      ret = JS_NewUint32(ctx, sock);
      break;
    }
    case SOCKET_PROP_LOCAL: {
      SockAddr* sa = sockaddr_new(ctx, -1);
      socklen_t len = sizeof(SockAddr);
      js_syscall_return("getsockname",
                        getpeername(sock, (struct sockaddr*)sa, &len),
                        js_sockaddr_wrap(ctx, sa));
      break;
    }
    case SOCKET_PROP_REMOTE: {
      SockAddr* sa = sockaddr_new(ctx, -1);
      socklen_t len = sizeof(SockAddr);

      js_syscall_return("getpeername",
                        getpeername(sock, (struct sockaddr*)sa, &len),
                        js_sockaddr_wrap(ctx, sa));
      break;
    }
  }
  return ret;
}

static JSValue
js_socket_set(JSContext* ctx,
              JSValueConst this_val,
              JSValueConst value,
              int magic) {
  int sock;
  JSValue ret = JS_UNDEFINED;

  if((sock = js_socket_data(ctx, this_val)) == -1)
    return ret;

  switch(magic) {}
  return ret;
}

static JSValue
js_socket_method(JSContext* ctx,
                 JSValueConst this_val,
                 int argc,
                 JSValueConst argv[],
                 int magic) {
  int sock;
  JSValue ret = JS_UNDEFINED;

  if((sock = js_socket_data(ctx, this_val)) == -1)
    return ret;

  switch(magic) {
    case SOCKET_METHOD_NDELAY: {
      BOOL state = FALSE;
      int oldflags, newflags;
      if(argc >= 1)
        state = JS_ToBool(ctx, argv[0]);

      oldflags = fcntl(sock, F_GETFL);

      newflags = state ? oldflags | O_NONBLOCK : oldflags & (~O_NONBLOCK);
      if(oldflags != newflags)

        js_syscall("fcntl", fcntl(sock, F_SETFL, newflags));
      break;
    }
    case SOCKET_METHOD_BIND: {
      SockAddr* sa;

      if(!(sa = js_sockaddr_data2(ctx, argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be of type SockAddr");

      js_syscall("bind",
                 bind(sock,
                      (struct sockaddr*)sa,
                      sa->in.sin_family == AF_INET ? sizeof(struct sockaddr_in)
                      : sa->in.sin_family == AF_INET6
                          ? sizeof(struct sockaddr_in6)
                          : sizeof(struct sockaddr)));
      break;
    }
    case SOCKET_METHOD_CONNECT: {
      SockAddr* sa;
      int result;

      if(!(sa = js_sockaddr_data2(ctx, argv[0])))
        return JS_ThrowTypeError(ctx, "argument 1 must be of type SockAddr");

      js_syscall("connect",
                 connect(sock,
                         (struct sockaddr*)sa,
                         sa->in.sin_family == AF_INET
                             ? sizeof(struct sockaddr_in)
                         : sa->in.sin_family == AF_INET6
                             ? sizeof(struct sockaddr_in6)
                             : sizeof(struct sockaddr)));
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
      js_syscall("recv", recv(sock, buf.data, len, flags));
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
      js_syscall("send", send(sock, buf.data, len, flags));
      break;
    }
    case SOCKET_METHOD_CLOSE: {
      js_syscall("close", close(sock));
      break;
    }
  }
  return ret;
}

JSValue
js_socket_constructor(JSContext* ctx,
                      JSValueConst new_target,
                      int argc,
                      JSValueConst argv[]) {
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
js_socket_inspect(JSContext* ctx,
                  JSValueConst this_val,
                  int argc,
                  JSValueConst argv[]) {
  int sock;

  if((sock = js_socket_data(ctx, this_val)) == -1)
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectProto(ctx, socket_proto);
  JS_DefinePropertyValueStr(
      ctx, obj, "fd", JS_NewUint32(ctx, sock), JS_PROP_ENUMERABLE);
  return obj;
}

void
js_socket_finalizer(JSRuntime* rt, JSValue val) {
  int sock = (int)JS_GetOpaque(val, js_socket_class_id);
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
};

static const JSCFunctionListEntry js_socket_funcs[] = {
    JS_CGETSET_MAGIC_DEF("fd", js_socket_get, 0, SOCKET_PROP_FD),
    JS_CGETSET_MAGIC_DEF("local", js_socket_get, 0, SOCKET_PROP_LOCAL),
    JS_CGETSET_MAGIC_DEF("remote", js_socket_get, 0, SOCKET_PROP_REMOTE),
    JS_CFUNC_MAGIC_DEF("ndelay", 0, js_socket_method, SOCKET_METHOD_NDELAY),
    JS_CFUNC_MAGIC_DEF("bind", 1, js_socket_method, SOCKET_METHOD_BIND),
    JS_CFUNC_MAGIC_DEF("connect", 1, js_socket_method, SOCKET_METHOD_CONNECT),
    JS_CFUNC_MAGIC_DEF("send", 1, js_socket_method, SOCKET_METHOD_SEND),
    JS_CFUNC_MAGIC_DEF("recv", 1, js_socket_method, SOCKET_METHOD_RECV),
    JS_CFUNC_MAGIC_DEF("close", 0, js_socket_method, SOCKET_METHOD_CLOSE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Socket", JS_PROP_CONFIGURABLE),
};

int
js_sockets_init(JSContext* ctx, JSModuleDef* m) {

  if(js_socket_class_id == 0) {

    JS_NewClassID(&js_sockaddr_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_sockaddr_class_id, &js_sockaddr_class);

    sockaddr_ctor = JS_NewCFunction2(
        ctx, js_sockaddr_constructor, "SockAddr", 1, JS_CFUNC_constructor, 0);
    sockaddr_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx,
                               sockaddr_proto,
                               js_sockaddr_proto_funcs,
                               countof(js_sockaddr_proto_funcs));
    JS_SetClassProto(ctx, js_sockaddr_class_id, sockaddr_proto);

    js_set_inspect_method(ctx, sockaddr_proto, js_sockaddr_inspect);

    JS_NewClassID(&js_socket_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_socket_class_id, &js_socket_class);

    socket_ctor = JS_NewCFunction2(
        ctx, js_socket_constructor, "Socket", 1, JS_CFUNC_constructor, 0);
    socket_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx,
                               socket_proto,
                               js_socket_funcs,
                               countof(js_socket_funcs));
    JS_SetPropertyFunctionList(ctx,
                               socket_ctor,
                               js_sockets_defines,
                               countof(js_sockets_defines));
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
    JS_SetModuleExportList(ctx,
                           m,
                           js_sockets_defines,
                           countof(js_sockets_defines));
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

  if(!strcmp(module_name, "socket"))
    JS_AddModuleExport(ctx, m, "default");
  JS_AddModuleExportList(ctx, m, js_sockets_funcs, countof(js_sockets_funcs));
  JS_AddModuleExportList(ctx,
                         m,
                         js_sockets_defines,
                         countof(js_sockets_defines));
  return m;
}
