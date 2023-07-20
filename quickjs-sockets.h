#include "defines.h"
#ifndef QUICKJS_SOCKETS_H
#define QUICKJS_SOCKETS_H

#if defined(_WIN32) && !defined(__MSYS__) && !defined(__CYGWIN__)
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef HAVE_AFUNIX_H
#include <afunix.h>
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#endif
#include <quickjs.h>

#include "utils.h"

#if !defined(_WIN32) || defined(HAVE_AFUNIX_H)
#define HAVE_AF_UNIX
#endif

/**
 * \defgroup quickjs-sockets quickjs-sockets: Network sockets
 * @{
 */

typedef struct {
  int32_t family, type, protocol;
} SockType;

typedef union {
  uint16_t family;
  struct sockaddr s;
  struct sockaddr_in ip4;
  struct sockaddr_in6 ip6;
#ifdef HAVE_AF_UNIX
  struct sockaddr_un un;
#endif
} SockAddr;

#define SOCKET_PROPS() \
  unsigned fd : 16; \
  unsigned error : 8; \
  unsigned sysno : 4; \
  BOOL nonblock : 1, async : 1, owner : 1; \
  signed ret : 32

union PACK socket_state {
  PACK struct { SOCKET_PROPS(); };
  ENDPACK
  uint64_t u64;
  void* ptr;
};
ENDPACK

struct socket_handlers {
  JSValue close, connect, data, drain, end, error, lookup, ready, timeout;
};

struct async_closure {
  JSCFunctionMagic* set_mux;
};

struct PACK asyncsocket_state {
  SOCKET_PROPS();
  /*struct socket_handlers handlers;*/
  JSValue pending[2];
};
ENDPACK

#define SOCKET(fd, err, sys, nonb, asyn, own) \
  { \
    { (fd), (err), (sys), (nonb), (asyn), (own), (0) } \
  }

#define SOCKET_INIT() SOCKET(-1, 0, -1, FALSE, FALSE, FALSE)

typedef union socket_state Socket;
typedef struct asyncsocket_state AsyncSocket;

VISIBLE SockAddr* js_sockaddr_data(JSValueConst);
VISIBLE SockAddr* js_sockaddr_data2(JSContext*, JSValueConst value);
VISIBLE Socket js_socket_data(JSValueConst);
void* optval_buf(JSContext*, JSValueConst arg, int32_t** tmp_ptr, socklen_t* lenp);

extern VISIBLE JSClassID js_sockaddr_class_id, js_socket_class_id, js_asyncsocket_class_id;
extern VISIBLE JSValue sockaddr_proto, sockaddr_ctor, socket_proto, socket_ctor, asyncsocket_proto, asyncsocket_ctor;

enum SocketCalls {
  SYSCALL_SOCKET = 0,
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

#define socket_fd(sock) ((sock).fd)
#define socket_closed(sock) ((sock).sysno == SYSCALL_CLOSE && (sock).ret == 0)
#define socket_eof(sock) (((sock).sysno == SYSCALL_RECV || (sock).sysno == SYSCALL_RECVFROM) && (sock).ret == 0)
#define socket_open(sock) ((sock).fd != UINT16_MAX && !socket_closed(sock))
#define socket_retval(sock) ((sock).ret)
#if defined(_WIN32) && !defined(__MSYS__) && !defined(__CYGWIN__)
#define socket_error(sock) ((sock).ret < 0 ? (int)(sock).error + WSABASEERR : 0)
#else
#define socket_error(sock) ((sock).ret < 0 ? (int)(sock).error : 0)
#endif
#define socket_syscall(sock) syscall_name((sock).sysno)
#define socket_adopted(sock) (!(sock).owner)

static inline int
sockaddr_port(const SockAddr* sa) {
  switch(sa->family) {
    case AF_INET: return ntohs(sa->ip4.sin_port);
    case AF_INET6: return ntohs(sa->ip6.sin6_port);
  }
  return -1;
}

static inline BOOL
sockaddr_setport(SockAddr* sa, uint16_t port) {
  switch(sa->family) {
    case AF_INET: sa->ip4.sin_port = htons(port); return TRUE;
    case AF_INET6: sa->ip6.sin6_port = htons(port); return TRUE;
  }
  return FALSE;
}

static inline void*
sockaddr_addr(SockAddr* sa) {
  switch(sa->family) {
    case AF_INET: return &sa->ip4.sin_addr;
    case AF_INET6: return &sa->ip6.sin6_addr;
  }

  return 0;
}

static inline socklen_t
sockaddr_addrlen(const SockAddr* sa) {
  switch(sa->family) {
    case AF_INET: return sizeof(sa->ip4.sin_addr);
    case AF_INET6: return sizeof(sa->ip6.sin6_addr);
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

static inline AsyncSocket*
js_asyncsocket_ptr(JSValueConst value) {
  return js_asyncsocket_class_id ? JS_GetOpaque(value, js_asyncsocket_class_id) : 0;
}

/**
 * @}
 */

#endif /* defined(QUICKJS_SOCKETS_H) */
