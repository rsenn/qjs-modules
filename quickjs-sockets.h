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

#ifdef HAVE_LINUX_NETWORK_HEADERS
#include <linux/ipx.h>
#include <linux/x25.h>
#include <linux/can.h>
#include <linux/nfc.h>
#include <linux/ax25.h>
#include <linux/netlink.h>
#include <linux/atalk.h>
#include <linux/phonet.h>
#include <linux/if_alg.h>
#endif

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
#ifdef HAVE_LINUX_NETWORK_HEADERS
  struct sockaddr_ipx ipx;
  struct sockaddr_ax25 ax25;
  struct sockaddr_nl nl;
  struct sockaddr_can can;
  struct sockaddr_nfc nfc;
  struct sockaddr_x25 x25;
  struct sockaddr_at at;
  struct sockaddr_pn pn;
  struct sockaddr_alg alg;
#endif
} SockAddr;

#define SOCKET_PROPS() \
  unsigned fd : 16; \
  unsigned error : 8; \
  unsigned sysno : 4; \
  BOOL nonblock : 1, async : 1, owner : 1; \
  signed ret : 32

PACK union socket_state {
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

struct asyncsocket_state {
  union {
    struct {
      union socket_state socket;
      JSValue pending[2];
    };
    struct PACK {
      SOCKET_PROPS();
    };
    ENDPACK
  };
};

#define SOCKET(fd, err, sys, nonb, asyn, own) \
  { \
    { (fd), (err), (sys), (nonb), (asyn), (own), (0) } \
  }

#define SOCKET_INIT() SOCKET(-1, 0, -1, FALSE, FALSE, FALSE)

typedef union socket_state Socket;
typedef struct asyncsocket_state AsyncSocket;

VISIBLE JSModuleDef* js_init_module_sockets(JSContext*, const char*);

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
  SYSCALL_SETSOCKOPT,
  SYSCALL_RECVMSG,
  SYSCALL_SENDMSG,
};

#define socket_fd(sock) ((int16_t)(uint16_t)(sock).fd)
#define socket_closed(sock) ((sock).sysno == SYSCALL_CLOSE && (sock).ret == 0)
#define socket_eof(sock) (((sock).sysno == SYSCALL_RECV || (sock).sysno == SYSCALL_RECVFROM) && (sock).ret == 0)
#define socket_open(sock) ((sock).fd != UINT16_MAX && !socket_closed(sock))
#define socket_retval(sock) ((sock).ret)
#if defined(_WIN32) && !defined(__MSYS__) && !defined(__CYGWIN__)
#define socket_error(sock) ((sock).ret < 0 ? (int)(sock).error + WSABASEERR : 0)
#else
#define socket_error(sock) ((sock).ret < 0 ? (int)(sock).error : 0)
#endif
#define socket_syscall(sock) socketcall_name((sock).sysno)
#define socket_adopted(sock) (!(sock).owner)

static inline int
sockaddr_port(const SockAddr* sa) {
  switch(sa->family) {
    case AF_INET: return ntohs(sa->ip4.sin_port);
    case AF_INET6: return ntohs(sa->ip6.sin6_port);
#ifdef HAVE_LINUX_NETWORK_HEADERS
    case AF_IPX: return ntohs(sa->ipx.sipx_port);
    case AF_APPLETALK: return ntohs(sa->at.sat_port);
#endif
  }

  return -1;
}

static inline BOOL
sockaddr_setport(SockAddr* sa, uint16_t port) {
  switch(sa->family) {
    case AF_INET: sa->ip4.sin_port = htons(port); return TRUE;
    case AF_INET6: sa->ip6.sin6_port = htons(port); return TRUE;
#ifdef HAVE_LINUX_NETWORK_HEADERS
    case AF_IPX: return sa->ipx.sipx_port = htons(port); return TRUE;
    case AF_APPLETALK: return sa->at.sat_port = htons(port); return TRUE;
#endif
  }

  return FALSE;
}

static inline void*
sockaddr_addr(SockAddr* sa) {
  switch(sa->family) {
    case AF_INET: return &sa->ip4.sin_addr;
    case AF_INET6: return &sa->ip6.sin6_addr;
#ifdef HAVE_LINUX_NETWORK_HEADERS

    case AF_IPX: return &sa->ipx.sipx_node;
    case AF_UNIX: return &sa->un.sun_path;
    case AF_NETLINK: return &sa->nl.nl_pid;
    case AF_CAN: return &sa->can.can_addr;
    case AF_NFC: return &sa->nfc.target_idx;
    case AF_AX25: return &sa->ax25.sax25_call;
    case AF_X25: return &sa->x25.sx25_addr;
    case AF_APPLETALK: return &sa->at.sat_addr;
#endif
  }

  return 0;
}

static inline socklen_t
sockaddr_addrlen(const SockAddr* sa) {
  switch(sa->family) {
    case AF_INET: return sizeof(sa->ip4.sin_addr);
    case AF_INET6: return sizeof(sa->ip6.sin6_addr);
#ifdef HAVE_LINUX_NETWORK_HEADERS

    case AF_IPX: return sizeof(sa->ipx.sipx_node);
    case AF_UNIX: return sizeof(sa->un.sun_path);
    case AF_NETLINK: return sizeof(sa->nl.nl_pid);
    case AF_CAN: return sizeof(sa->can.can_addr);
    case AF_NFC: return sizeof(sa->nfc.target_idx);
    case AF_AX25: return sizeof(sa->ax25.sax25_call);
    case AF_X25: return sizeof(sa->x25.sx25_addr);
    case AF_APPLETALK: return sizeof(sa->at.sat_addr);
    case AF_PHONET: return sizeof(sa->pn.spn_obj) + sizeof(sa->pn.spn_dev) + sizeof(sa->pn.spn_resource);
#endif
  }

  return 0;
}

static inline size_t
sockaddr_size(const SockAddr* sa) {
  switch(sa->family) {
    case AF_INET: return sizeof(struct sockaddr_in);
    case AF_INET6: return sizeof(struct sockaddr_in6);
#ifdef HAVE_LINUX_NETWORK_HEADERS
    case AF_IPX: return sizeof(struct sockaddr_ipx);
    case AF_UNIX: return sizeof(struct sockaddr_un);
    case AF_NETLINK: return sizeof(struct sockaddr_nl);
    case AF_CAN: return sizeof(struct sockaddr_can);
    case AF_NFC: return sizeof(struct sockaddr_nfc);
    case AF_AX25: return sizeof(struct sockaddr_ax25);
    case AF_X25: return sizeof(struct sockaddr_x25);
    case AF_APPLETALK: return sizeof(struct sockaddr_at);
    case AF_PHONET: return sizeof(struct sockaddr_pn);
    case AF_ALG: return sizeof(struct sockaddr_alg);
#endif
  }

  return 0;
}

static inline size_t
sockaddr_len(const SockAddr* sa) {
  size_t len = sockaddr_size(sa);

  if(len == 0)
    len = sizeof(SockAddr);
  return len;
}

/**
 * @}
 */

#endif /* defined(QUICKJS_SOCKETS_H) */
