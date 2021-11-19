#ifndef QUICKJS_SOCKETS_H
#define QUICKJS_SOCKETS_H

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include <quickjs.h>

#include "utils.h"

/**
 * \defgroup quickjs-sockets QuickJS module: sockets - Network sockets
 * @{
 */

typedef union {
  uint16_t family;
  struct sockaddr s;
  struct sockaddr_in sai;
  struct sockaddr_in6 sai6;
} SockAddr;

union __attribute__((packed)) socket_state {
  struct {
    uint16_t fd;
    unsigned error : 8;
    unsigned syscall : 4;
    BOOL nonblock : 1;
    int32_t ret;
  };
  uint64_t u64;
  void* ptr;
};

struct async_closure {
  JSCFunctionMagic* set_mux;
};

typedef union socket_state Socket;

extern thread_local VISIBLE JSClassID js_sockaddr_class_id, js_socket_class_id;
extern thread_local JSValue sockaddr_proto, sockaddr_ctor, socket_proto, socket_ctor;

enum SocketCalls {
  SYSCALL_SOCKET = 1,
  SYSCALL_GETSOCKNAME,
  SYSCALL_GETPEERNAME,
  SYSCALL_FCNTL,
  SYSCALL_BIND,
  SYSCALL_ACCEPT,
  SYSCALL_CONNECT,
  SYSCALL_LISTEN,
  SYSCALL_RECV,
  SYSCALL_RECVFROM,
  SYSCALL_SEND,
  SYSCALL_SENDTO,
  SYSCALL_SHUTDOWN,
  SYSCALL_CLOSE,
  SYSCALL_GETSOCKOPT,
  SYSCALL_SETSOCKOPT
};

#define socket_closed(sock) ((sock).syscall == SYSCALL_CLOSE && (sock).ret == 0)
#define socket_eof(sock) (((sock).syscall == SYSCALL_RECV || (sock).syscall == SYSCALL_RECVFROM) && (sock).ret == 0)
#define socket_open(sock) ((sock).fd != UINT16_MAX && !socket_closed(sock))
#define socket_error(sock) ((sock).ret < 0 ? (sock).error : 0)
#define socket_syscall(sock) socket_syscalls[(sock).syscall]

static inline int
sockaddr_port(const SockAddr* sa) {
  switch(sa->family) {
    case AF_INET: return ntohs(sa->sai.sin_port);
    case AF_INET6: return ntohs(sa->sai6.sin6_port);
  }
  return -1;
}

static inline void*
sockaddr_addr(SockAddr* sa) {
  switch(sa->family) {
    case AF_INET: return &sa->sai.sin_addr;
    case AF_INET6: return &sa->sai6.sin6_addr;
  }
  return 0;
}

static inline socklen_t
sockaddr_addrlen(const SockAddr* sa) {
  switch(sa->family) {
    case AF_INET: return sizeof(sa->sai.sin_addr);
    case AF_INET6: return sizeof(sa->sai6.sin6_addr);
  }
  return 0;
}

static inline size_t
sockaddr_size(const SockAddr* sa) {
  switch(sa->family) {
    case AF_INET: return sizeof(struct sockaddr_in);
    case AF_INET6: return sizeof(struct sockaddr_in6);
  }
  return 0;
}

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

/**
 * @}
 */

#endif /* defined(QUICKJS_SOCKETS_H) */
