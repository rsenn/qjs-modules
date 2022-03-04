#include "include/defines.h"
#ifndef QUICKJS_SOCKETS_H
#define QUICKJS_SOCKETS_H

#if defined(_WIN32) || defined(__MSYS__)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include <quickjs.h>

#include "include/utils.h"

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

#define SOCKET_PROPS() \
  uint16_t fd; \
  unsigned error : 8; \
  int syscall : 4; \
  BOOL nonblock : 1, async : 1; \
  int32_t ret

union __attribute__((packed)) socket_state {
  struct {
    SOCKET_PROPS();
  };
  uint64_t u64;
  void* ptr;
};

struct socket_handlers {
  JSValue close, connect, data, drain, end, error, lookup, ready, timeout;
};

struct async_closure {
  JSCFunctionMagic* set_mux;
};

struct __attribute__((packed)) async_socket_state {
  SOCKET_PROPS();
  struct socket_handlers handlers;
};

typedef union socket_state Socket;
typedef struct async_socket_state AsyncSocket;

extern thread_local VISIBLE JSClassID js_sockaddr_class_id, js_socket_class_id, js_async_socket_class_id;
extern thread_local JSValue sockaddr_proto, sockaddr_ctor, socket_proto, socket_ctor, async_socket_proto, async_socket_ctor;

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
  JSClassID id = JS_GetClassID(value);
  if(id > 0 && (id == js_socket_class_id || id == js_async_socket_class_id)) {
    void* ptr;
    if((ptr = JS_GetOpaque(value, id))) {
      Socket sock;
      if(id == js_async_socket_class_id)
        sock = *(Socket*)ptr;
      else
        sock.ptr = ptr;
      return sock;
    }
  }
  return (Socket){{-1, 0, -1}};
}

static inline Socket*
js_socket_ptr(JSValueConst value) {
  Socket sock;
  JSClassID id = JS_GetClassID(value);
  if(id > 0) {
    if(id == js_socket_class_id) {
      struct JSObject* obj;
      obj = JS_VALUE_GET_OBJ(value);
      return (Socket*)&obj->u.opaque;
    } else if(id == js_async_socket_class_id) {
      return JS_GetOpaque(value, id);
    }
  }
  return 0;
}

static inline AsyncSocket*
js_async_socket_ptr(JSValueConst value) {
  Socket sock;
  JSClassID id = JS_GetClassID(value);
  if(id > 0 && id == js_async_socket_class_id)
    return JS_GetOpaque(value, id);

  return 0;
}

static inline Socket
js_socket_data2(JSContext* ctx, JSValueConst value) {
  Socket sock = {{-1, 0, -1}};
  JSClassID id = JS_GetClassID(value);
  assert(id == js_socket_class_id || id == js_async_socket_class_id);
  if(id > 0 && (id == js_socket_class_id || id == js_async_socket_class_id)) {
    void** ptr = JS_GetOpaque2(ctx, value, id);
    if(ptr)

      sock.ptr = id == js_async_socket_class_id ? *ptr : ptr;
  }
  return sock;
}

/**
 * @}
 */

#endif /* defined(QUICKJS_SOCKETS_H) */
