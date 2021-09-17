#ifndef QUICKJS_SOCKETS_H
#define QUICKJS_SOCKETS_H

#include "utils.h"
#include <sys/socket.h>
#include <netinet/in.h>

typedef union {
  uint16_t family;
  struct sockaddr_in in;
  struct sockaddr_in6 in6;
} SockAddr;

typedef union {
  struct {
    int32_t fd;
    uint16_t error, syscall;
  };
  uint64_t u64;
  void* ptr;
} Socket;

extern thread_local VISIBLE JSClassID js_sockaddr_class_id, js_socket_class_id;
extern thread_local JSValue sockaddr_proto, sockaddr_ctor, socket_proto, socket_ctor;

enum SocketCalls { SYSCALL_SOCKET = 1, SYSCALL_GETSOCKNAME, SYSCALL_GETPEERNAME, SYSCALL_FCNTL, SYSCALL_BIND, SYSCALL_ACCEPT, SYSCALL_CONNECT, SYSCALL_LISTEN, SYSCALL_RECV, SYSCALL_SEND, SYSCALL_SHUTDOWN, SYSCALL_CLOSE, SYSCALL_GETSOCKOPT, SYSCALL_SETSOCKOPT };

JSValue js_socket_new_proto(JSContext*, JSValue, int fd);
JSValue js_socket_new(JSContext*, int);
JSValue js_socket_constructor(JSContext*, JSValue, int argc, JSValue argv[]);

static inline SockAddr*
js_sockaddr_data(JSValueConst value) {
  return JS_GetOpaque(value, js_sockaddr_class_id);
}

static inline SockAddr*
js_sockaddr_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_sockaddr_class_id);
}

static inline Socket
js_socket_data(JSValueConst value) {
  Socket sock;
  sock.ptr = JS_GetOpaque(value, js_socket_class_id);
  return sock;
}

static inline Socket
js_socket_data2(JSContext* ctx, JSValueConst value) {
  Socket sock;
  sock.ptr = JS_GetOpaque2(ctx, value, js_socket_class_id);
  return sock;
}

#endif /* defined(QUICKJS_SOCKETS_H) */
