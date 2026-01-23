#include "defines.h"
#include "quickjs-sockets.h"
#include "quickjs-syscallerror.h"
#include "utils.h"
#include "buffer-utils.h"
#include "debug.h"
#include "iteration.h"

#if defined(_WIN32) && !defined(__MSYS__) && !defined(__CYGWIN__)
int socketpair(int, int, int, SOCKET[2]);

#define socket_handle(sock) ((SOCKET)_get_osfhandle(socket_fd(sock)))
#else
typedef int SOCKET;
#define socket_handle(sock) socket_fd(sock)
#define closesocket(sock) close(sock)
#include <sys/select.h>
/*#include <sys/syscall.h>*/
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#endif

#include <fcntl.h>
#include <assert.h>
#include <errno.h>

/**
 * \addtogroup quickjs-sockets
 * @{
 */

#define JS_SOCKETCALL(socketcall_no, sock, result) \
  JS_SOCKETCALL_RETURN(socketcall_no, sock, result, JS_NewInt32(ctx, (sock)->ret), js_socket_error(ctx, *(sock)))

#define JS_SOCKETCALL_FAIL(socketcall_no, sock, on_fail) JS_SOCKETCALL_RETURN(socketcall_no, sock, result, JS_NewInt32(ctx, (sock)->ret), on_fail)

#define JS_SOCKETCALL_RETURN(socketcall_no, sock, result, on_success, on_fail) \
  do { \
    socketcall_return((sock), (socketcall_no), (result)); \
    ret = (sock)->ret < 0 ? (on_fail) : (on_success); \
  } while(0)

static JSValue socket_adopt(JSContext*, int, BOOL);
static int js_sockets_init(JSContext*, JSModuleDef*);
static int js_socket_type(JSContext*, SockType*, int, JSValueConst[]);
static JSValue js_socket_method(JSContext*, JSValueConst, int, JSValueConst[], int);
static int js_socket_fd(JSValueConst);
static BOOL js_socket_check_open(JSContext*, Socket);
static JSValue js_socket_create(JSContext*, JSValueConst, int, JSValueConst[], BOOL);
static JSValue js_socket_new_proto(JSContext*, JSValueConst, int, BOOL, BOOL);
static int js_socket_address_family(JSValueConst);
static int js_sockaddr_init(JSContext*, int, JSValueConst[], SockAddr*);

JSClassID js_sockaddr_class_id = 0, js_socket_class_id = 0, js_asyncsocket_class_id = 0;
static JSValue sockaddr_proto, sockaddr_ctor, socket_proto, asyncsocket_proto, socket_ctor, asyncsocket_ctor;

static const char* socketcall_names[] = {
    "socket",      "getsockname", "getpeername",
#ifdef _WIN32
    "ioctlsocket",
#else
    "fcntl",
#endif
    "bind",        "accept",      "accept4",     "connect",    "listen",  "recv",    "recvfrom", "send",     "sendto",
    "shutdown",    "close",       "getsockopt",  "setsockopt", "recvmsg", "sendmsg", "recvmmsg", "sendmmsg",
};

static const char*
socketcall_name(int socketcall_number) {
  assert(socketcall_number >= 0);
  assert(socketcall_number < (int)countof(socketcall_names));

  return socketcall_names[socketcall_number];
}

static void
socketcall_return(Socket* sock, int sysno, int retval) {
  (sock)->sysno = sysno;
  (sock)->ret = retval;
  (sock)->error = retval < 0 ?
#if defined(_WIN32) && !defined(__MSYS__) && !defined(__CYGWIN__)
                             WSAGetLastError() - WSABASEERR
#else
                             errno
#endif
                             : 0;

#ifdef DEBUG_OUTPUT
  printf("%s(): syscall %s returned %d (%d)\n", __func__, (sock)->sysno ? socketcall_name((sock)->sysno) : "0", (sock)->ret, (sock)->error);
#endif
}

static SockAddr*
js_sockaddr_data(JSValueConst value) {
  return JS_GetOpaque(value, js_sockaddr_class_id);
}

static SockAddr*
js_sockaddr_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_sockaddr_class_id);
}

static void
js_sockaddr_free_buffer(JSRuntime* rt, void* opaque, void* ptr) {
  JS_FreeValueRT(rt, JS_MKPTR(JS_TAG_OBJECT, opaque));
}

static JSValue
js_sockaddr_wrap(JSContext* ctx, SockAddr* a) {
  JSValue obj;

  if(js_sockaddr_class_id == 0 && js_socket_class_id == 0 && js_asyncsocket_class_id == 0)
    js_sockets_init(ctx, 0);

  obj = JS_NewObjectProtoClass(ctx, sockaddr_proto, js_sockaddr_class_id);
  JS_SetOpaque(obj, a);

  return obj;
}

static JSValue
js_sockaddr_new(JSContext* ctx, int family) {
  SockAddr *a, addr = {family};

  if(!(a = js_mallocz(ctx, sockaddr_len(&addr))))
    return JS_EXCEPTION;

  a->family = family;

  return js_sockaddr_wrap(ctx, a);
}

static JSValue
js_sockaddr_args(JSContext* ctx, int* argcp, JSValueConst** argvp) {
  JSValue ret = js_sockaddr_new(ctx, 0);

  if(!JS_IsException(ret)) {
    SockAddr* sa;

    if((sa = js_sockaddr_data(ret))) {
      int r;

      if((r = js_sockaddr_init(ctx, *argcp, *argvp, sa)) > 0) {
        *argcp -= r;
        *argvp += r;
      }
    }
  }

  return ret;
}

static JSValue
js_sockaddr_clone(JSContext* ctx, SockAddr addr) {
  JSValue obj = js_sockaddr_new(ctx, addr.family);
  SockAddr* a = js_sockaddr_data(obj);

  memcpy(a, &addr, sockaddr_len(&addr));

  return obj;
}

static int
js_sockaddr_init(JSContext* ctx, int argc, JSValueConst argv[], SockAddr* a) {
  JSValueConst* old_argv = argv;
  int old_argc = argc;

  if(argc == 1 && js_is_arraybuffer(ctx, argv[0])) {
    uint8_t* data;
    size_t len;

    if((data = JS_GetArrayBuffer(ctx, &len, argv[0])))
      if(len > 0)
        memcpy(a, data, MIN_NUM(len, sizeof(SockAddr)));

    argv++;
    argc--;

    goto end;
  }

  if(argc >= 1 && JS_IsNumber(argv[0])) {
    int32_t family;

    JS_ToInt32(ctx, &family, argv[0]);
    a->family = family;

    argc--;
    argv++;
  }

  if(argc >= 1) {
    const char* str;

    if((str = JS_ToCString(ctx, argv[0]))) {
#ifdef HAVE_LINUX_NETWORK_HEADERS

      if(a->family == AF_IPX) {
        str_copyn((char*)a->ipx.sipx_node, str, sizeof(a->ipx.sipx_node));
      } else if(a->family == AF_UNIX) {
        str_copyn(a->un.sun_path, str, sizeof(a->un.sun_path));
      } else if(a->family == AF_NETLINK) {
        uint32_t n = 0;

        JS_ToUint32(ctx, &n, argv[0]);
        a->nl.nl_pid = n;
      } else if(a->family == AF_CAN) {
        int64_t name;
        uint32_t pgn = 0, addr = 0;

        JS_ToInt64Ext(ctx, &name, argv[0]);

        if(argc >= 2)
          JS_ToUint32(ctx, &pgn, argv[1]);
        if(argc >= 3)
          JS_ToUint32(ctx, &addr, argv[2]);

        a->can.can_addr.j1939.name = name;
        a->can.can_addr.j1939.pgn = pgn;
        a->can.can_addr.j1939.addr = addr;
      } else if(a->family == AF_NFC) {
        uint32_t dev_idx, target_idx = 0, nfc_protocol = 0;

        JS_ToUint32(ctx, &dev_idx, argv[0]);

        if(argc >= 2)
          JS_ToUint32(ctx, &target_idx, argv[1]);

        if(argc >= 3)
          JS_ToUint32(ctx, &nfc_protocol, argv[2]);

        a->nfc.dev_idx = dev_idx;
        a->nfc.target_idx = target_idx;
        a->nfc.nfc_protocol = nfc_protocol;
      } else if(a->family == AF_AX25) {
      } else if(a->family == AF_X25) {
        str_copyn(a->x25.sx25_addr.x25_addr, str, sizeof(a->x25.sx25_addr.x25_addr));
      } else if(a->family == AF_APPLETALK) {
      } else if(a->family == AF_PHONET) {
      } else if(a->family == AF_ALG) {
      } else
#endif
          if(a->family == 0) {
        if(inet_pton(AF_INET, str, &a->ip4.sin_addr) > 0)
          a->family = AF_INET;
        else if(inet_pton(AF_INET6, str, &a->ip6.sin6_addr) > 0)
          a->family = AF_INET6;
#ifdef HAVE_AF_UNIX
      } else if(a->family == AF_UNIX) {
        str_copyn(a->un.sun_path, str, sizeof(a->un.sun_path));
#endif
      } else if(!inet_pton(a->family, str, sockaddr_addr(a)) && a->family == AF_INET6) {
        struct in_addr in;

        if(inet_pton(AF_INET, str, &in)) {
          struct in6_addr* in6p = &a->ip6.sin6_addr;

          if(in.s_addr == 0) {
            /* IN6ADDR_ANY_INIT */
            memset(in6p, 0, sizeof(struct in6_addr));
          } else if(in.s_addr == htonl(INADDR_LOOPBACK)) {
            /* IN6ADDR_LOOPBACK_INIT */
            memset(in6p, 0, sizeof(struct in6_addr));
            ((char*)in6p)[sizeof(struct in6_addr) - 1] = 1;
          } else {
            uint32_t* addr32 = (uint32_t*)in6p;

            addr32[0] = 0;
            addr32[1] = 0;
            addr32[2] = htonl(0xffff);
            addr32[3] = in.s_addr;
          }
        }
      }

      JS_FreeCString(ctx, str);
    }

    argc--;
    argv++;
  }

  if(argc >= 1) {
    uint32_t port;

    JS_ToUint32(ctx, &port, argv[0]);

    if(a->family == AF_INET)
      a->ip4.sin_port = htons(port);
    else if(a->family == AF_INET6)
      a->ip6.sin6_port = htons(port);
  }

end:
  return argv - old_argv;
}

static JSValue
js_sockaddr_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  SockAddr *a, *other;

  if(js_sockaddr_class_id == 0 && js_socket_class_id == 0 && js_asyncsocket_class_id == 0)
    js_sockets_init(ctx, 0);

  if(!(a = js_mallocz(ctx, sizeof(SockAddr))))
    return JS_EXCEPTION;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_sockaddr_class_id);
  JS_FreeValue(ctx, proto);

  if(argc >= 1 && (other = js_sockaddr_data(argv[0]))) {
    *a = *other;
  } else {
    js_sockaddr_init(ctx, argc, argv, a);
  }

  JS_SetOpaque(obj, a);
  return obj;

fail:
  js_free(ctx, a);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  SOCKADDR_CLONE,
  SOCKADDR_TOSTRING,
};

static JSValue
js_sockaddr_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  SockAddr* a;

  if(!(a = js_sockaddr_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SOCKADDR_CLONE: {
      SockAddr* other;
      size_t sz = sockaddr_len(a);

      if((other = js_malloc(ctx, sz))) {
        memcpy(other, a, sz);
        ret = js_sockaddr_wrap(ctx, other);
      }

      break;
    }

    case SOCKADDR_TOSTRING: {
      char port[FMT_ULONG];
      DynBuf dbuf;

#ifdef HAVE_AF_UNIX
      if(a->family == AF_UNIX) {
        dbuf_init_ctx(ctx, &dbuf);
        dbuf_putstr(&dbuf, "unix://");
        dbuf_putstr(&dbuf, a->un.sun_path);
        ret = dbuf_tostring_free(&dbuf, ctx);
      } else
#endif
      {
        dbuf_init_ctx(ctx, &dbuf);
        dbuf_realloc(&dbuf, INET6_ADDRSTRLEN);
        inet_ntop(a->family, sockaddr_addr(a), (char*)dbuf.buf, INET6_ADDRSTRLEN);
        dbuf.size = strlen((const char*)dbuf.buf);
        dbuf_putc(&dbuf, ':');
        dbuf_put(&dbuf, (const uint8_t*)port, fmt_ulong(port, sockaddr_port(a)));
        ret = dbuf_tostring_free(&dbuf, ctx);
      }

      break;
    }
  }

  return ret;
}

enum {
  SOCKADDR_FAMILY,
  SOCKADDR_ADDR,
  SOCKADDR_PORT,
  SOCKADDR_PATH,
  SOCKADDR_BYTELENGTH,
  SOCKADDR_BUFFER,
};

static JSValue
js_sockaddr_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue ret = JS_UNDEFINED;
  SockAddr* a;

  if(!(a = js_sockaddr_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SOCKADDR_FAMILY: {
      ret = JS_NewUint32(ctx, a->family);
      break;
    }

    case SOCKADDR_ADDR: {
      void* ptr;

      if(a->family == AF_INET || a->family == AF_INET6) {
        char buf[INET6_ADDRSTRLEN] = {0};

        inet_ntop(a->family, sockaddr_addr(a), buf, sizeof(buf));
        ret = JS_NewString(ctx, buf);
      } else if((ptr = sockaddr_addr(a))) {
        size_t n = sockaddr_addrlen(a);
        JSObject* obj = JS_VALUE_GET_PTR(JS_DupValue(ctx, this_val));

        ret = JS_NewArrayBuffer(ctx, ptr, n, js_sockaddr_free_buffer, obj, FALSE);
      }

      break;
    }

    case SOCKADDR_PORT: {
      int port = sockaddr_port(a);

      if(port >= 0)
        ret = JS_NewUint32(ctx, port);

      break;
    }

#ifdef HAVE_AF_UNIX
    case SOCKADDR_PATH: {
      if(a->family == AF_UNIX)
        ret = JS_NewString(ctx, a->un.sun_path);

      break;
    }
#endif

    case SOCKADDR_BYTELENGTH: {
      size_t len = sockaddr_len(a);

      ret = JS_NewUint32(ctx, len);
      break;
    }

    case SOCKADDR_BUFFER: {
      JSObject* obj = JS_VALUE_GET_PTR(JS_DupValue(ctx, this_val));
      size_t len = sockaddr_len(a);

      ret = JS_NewArrayBuffer(ctx, (uint8_t*)a, len, js_sockaddr_free_buffer, obj, FALSE);
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

  switch(magic) {
    case SOCKADDR_ADDR: {
      void* ptr;

      if(a->family == AF_INET || a->family == AF_INET6) {
        const char* str = JS_ToCString(ctx, value);

        inet_pton(a->family, str, sockaddr_addr(a));
        JS_FreeCString(ctx, str);
      } else
        ret = JS_ThrowInternalError(ctx, "SockAddr of family = %d has no .addr property", a->family);

      break;
    }

    case SOCKADDR_PORT: {
      uint32_t port;

      JS_ToUint32(ctx, &port, value);

      if(!sockaddr_setport(a, port))
        ret = JS_ThrowInternalError(ctx, "SockAddr of family = %d has no .port property", a->family);

      break;
    }

#ifdef HAVE_AF_UNIX
    case SOCKADDR_PATH: {
      if(a->family == AF_UNIX) {
        const char* str;

        if((str = JS_ToCString(ctx, value))) {
          str_copyn(a->un.sun_path, str, sizeof(a->un.sun_path));
          JS_FreeCString(ctx, str);
        }
      }

      break;
    }
#endif
  }

  return ret;
}

static void
js_sockaddr_finalizer(JSRuntime* rt, JSValue val) {
  SockAddr* a;

  if((a = js_sockaddr_data(val)))
    js_free_rt(rt, a);
}

static const JSCFunctionListEntry js_sockaddr_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("family", js_sockaddr_get, /*js_sockaddr_set*/ 0, SOCKADDR_FAMILY, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("addr", js_sockaddr_get, js_sockaddr_set, SOCKADDR_ADDR, JS_PROP_WRITABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("port", js_sockaddr_get, js_sockaddr_set, SOCKADDR_PORT, JS_PROP_WRITABLE),
    JS_CGETSET_MAGIC_DEF("path", js_sockaddr_get, js_sockaddr_set, SOCKADDR_PATH),
    JS_CGETSET_MAGIC_DEF("buffer", js_sockaddr_get, 0, SOCKADDR_BUFFER),
    JS_CGETSET_MAGIC_DEF("byteLength", js_sockaddr_get, 0, SOCKADDR_BYTELENGTH),
    JS_CFUNC_MAGIC_DEF("clone", 0, js_sockaddr_method, SOCKADDR_CLONE),
    JS_CFUNC_MAGIC_DEF("toString", 0, js_sockaddr_method, SOCKADDR_TOSTRING),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SockAddr", 0),
};

static JSClassDef js_sockaddr_class = {
    .class_name = "SockAddr",
    .finalizer = js_sockaddr_finalizer,
};

static BOOL
msg_read(JSContext* ctx, JSValueConst arg, struct msghdr* m) {
  if(js_is_array(ctx, arg) || js_is_iterable(ctx, arg)) {
    m->msg_iov = 0;
    m->msg_iovlen = 0;

    Iteration iter = ITERATION_INIT();

    if(!iteration_method_symbol(&iter, ctx, arg, "iterator")) {
      JS_ThrowInternalError(ctx, "Failed to iterate");
      return FALSE;
    }

    while(!iteration_next(&iter, ctx)) {
      JSValue value = iteration_value(&iter, ctx);
      InputBuffer buf = js_input_chars(ctx, value);
      JS_FreeValue(ctx, value);

      if(!(m->msg_iov = js_realloc(ctx, m->msg_iov, sizeof(struct iovec) * (m->msg_iovlen + 1))))
        return FALSE;

      m->msg_iov[m->msg_iovlen++] = (struct iovec){buf.data, buf.size};
    }

    iteration_reset(&iter, ctx);

  } else if(js_has_propertystr(ctx, arg, "iov")) {
    JSValue iov = JS_GetPropertyStr(ctx, arg, "iov");

    msg_read(ctx, iov, m);

    JS_FreeValue(ctx, iov);
  }

  if(js_has_propertystr(ctx, arg, "name")) {
    JSValue name = JS_GetPropertyStr(ctx, arg, "name");
    InputBuffer buf = js_input_buffer(ctx, name);
    JS_FreeValue(ctx, name);

    if(buf.data && buf.size) {
      m->msg_name = buf.data;
      m->msg_namelen = buf.size;
    }

    inputbuffer_free(&buf, ctx);
  }

  if(js_has_propertystr(ctx, arg, "flags"))
    m->msg_flags = js_get_propertystr_int32(ctx, arg, "flags");

  return TRUE;
}

static BOOL
msg_write(JSContext* ctx, JSValueConst ret, const struct msghdr* m) {

  if(m->msg_name)
    JS_SetPropertyStr(ctx, ret, "name", JS_NewStringLen(ctx, m->msg_name, m->msg_namelen));

  if(m->msg_iov && m->msg_iovlen) {
    JSValue iov = JS_NewArray(ctx);

    for(uint32_t i = 0; i < m->msg_iovlen; i++) {
      const struct iovec* iv = &m->msg_iov[i];

      JS_SetPropertyUint32(ctx, iov, i, JS_NewArrayBufferCopy(ctx, iv->iov_base, iv->iov_len));
    }

    JS_SetPropertyStr(ctx, ret, "iov", iov);
  }

  if(m->msg_flags)
    JS_SetPropertyStr(ctx, ret, "flags", JS_NewInt32(ctx, m->msg_flags));

  return TRUE;
}

typedef struct {
  struct mmsghdr* msgvec;
  unsigned vlen;
} MultiMessageHeader;

static BOOL
mmsg_read(JSContext* ctx, JSValueConst arg, struct mmsghdr* mmh) {
  JSValue hdr, len;

  if(js_is_array(ctx, arg)) {
    hdr = JS_GetPropertyUint32(ctx, arg, 0);
    len = JS_GetPropertyUint32(ctx, arg, 1);
  } else if(JS_IsObject(arg)) {
    hdr = JS_GetPropertyStr(ctx, arg, "hdr");
    len = JS_GetPropertyStr(ctx, arg, "len");
  } else {
    hdr = JS_UNDEFINED;
    len = JS_UNDEFINED;
  }

  msg_read(ctx, hdr, &mmh->msg_hdr);
  mmh->msg_len = js_touint64(ctx, len);

  JS_FreeValue(ctx, hdr);
  JS_FreeValue(ctx, len);

  return TRUE;
}

static BOOL
mmsg_write(JSContext* ctx, JSValueConst arg, const struct mmsghdr* mmh) {
  if(js_is_array(ctx, arg)) {
    JS_SetPropertyUint32(ctx, arg, 1, JS_NewUint32(ctx, mmh->msg_len));
  } else if(JS_IsObject(arg)) {
    JS_SetPropertyStr(ctx, arg, "len", JS_NewUint32(ctx, mmh->msg_len));
  } else {
    return FALSE;
  }

  return TRUE;
}

static BOOL
mmsgs_read(JSContext* ctx, JSValueConst arg, MultiMessageHeader* mm) {
  if(js_is_array(ctx, arg) || js_is_iterable(ctx, arg)) {
    mm->msgvec = 0;
    mm->vlen = 0;

    Iteration iter = ITERATION_INIT();

    if(!iteration_method_symbol(&iter, ctx, arg, "iterator")) {
      JS_ThrowInternalError(ctx, "Failed to iterate");
      return FALSE;
    }

    while(!iteration_next(&iter, ctx)) {
      JSValue value = iteration_value(&iter, ctx);
      struct mmsghdr mmh = {};

      mmsg_read(ctx, value, &mmh);

      JS_FreeValue(ctx, value);

      if(!(mm->msgvec = js_realloc(ctx, mm->msgvec, sizeof(*mm->msgvec) * (mm->vlen + 1))))
        return FALSE;

      mm->msgvec[mm->vlen++] = mmh;
    }

    iteration_reset(&iter, ctx);
    return TRUE;
  }

  return FALSE;
}

static BOOL
mmsgs_write(JSContext* ctx, JSValueConst arg, MultiMessageHeader mm) {
  for(size_t i = 0; i < mm.vlen; i++) {
    JSValue mmsg = JS_GetPropertyUint32(ctx, arg, i);
    mmsg_write(ctx, mmsg, &mm.msgvec[i]);
    JS_FreeValue(ctx, mmsg);
  }

  return TRUE;
}

static BOOL
timeval_read(JSContext* ctx, JSValueConst arg, struct timeval* tv) {
  if(JS_IsNumber(arg)) {
    int64_t msecs = 0;

    JS_ToInt64Ext(ctx, &msecs, arg);
    tv->tv_sec = msecs / 1000;
    msecs -= tv->tv_sec * 1000;
    tv->tv_usec = msecs * 1000;

    return TRUE;
  }

  if(js_is_array(ctx, arg) && js_array_length(ctx, arg) >= 2) {
    int64_t sec = 0, usec = 0;
    JSValue member = JS_GetPropertyUint32(ctx, arg, 0);

    JS_ToInt64Ext(ctx, &sec, member);
    JS_FreeValue(ctx, member);
    member = JS_GetPropertyUint32(ctx, arg, 1);
    JS_ToInt64Ext(ctx, &usec, member);
    JS_FreeValue(ctx, member);
    tv->tv_sec = sec;
    tv->tv_usec = usec;

    return TRUE;
  }

  if(js_is_arraybuffer(ctx, arg)) {
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
  if(JS_IsNumber(arg))
    return FALSE;

  if(js_is_array(ctx, arg)) {
    js_array_clear(ctx, arg);

    JS_SetPropertyUint32(ctx, arg, 0, JS_NewBigInt64(ctx, tv->tv_sec));
    JS_SetPropertyUint32(ctx, arg, 1, JS_NewBigInt64(ctx, tv->tv_usec));

    return TRUE;
  }

  if(js_is_arraybuffer(ctx, arg)) {
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
timespec_read(JSContext* ctx, JSValueConst arg, struct timespec* ts) {
  if(JS_IsNumber(arg)) {
    int64_t msecs = 0;

    JS_ToInt64Ext(ctx, &msecs, arg);
    ts->tv_sec = msecs / 1000;
    msecs -= ts->tv_sec * 1000;
    ts->tv_nsec = msecs * 1e06;

    return TRUE;
  }

  if(js_is_array(ctx, arg) && js_array_length(ctx, arg) >= 2) {
    int64_t sec = 0, nsec = 0;
    JSValue member = JS_GetPropertyUint32(ctx, arg, 0);

    JS_ToInt64Ext(ctx, &sec, member);
    JS_FreeValue(ctx, member);
    member = JS_GetPropertyUint32(ctx, arg, 1);
    JS_ToInt64Ext(ctx, &nsec, member);
    JS_FreeValue(ctx, member);
    ts->tv_sec = sec;
    ts->tv_nsec = nsec;

    return TRUE;
  }

  if(js_is_arraybuffer(ctx, arg)) {
    uint8_t* data;
    size_t len;

    if((data = JS_GetArrayBuffer(ctx, &len, arg))) {
      if(len >= sizeof(struct timespec)) {
        memcpy(ts, data, sizeof(struct timespec));
        return TRUE;
      }
    }
  }

  return FALSE;
}

static BOOL
timespec_write(JSContext* ctx, const struct timespec* ts, JSValueConst arg) {
  if(JS_IsNumber(arg))
    return FALSE;

  if(js_is_array(ctx, arg)) {
    js_array_clear(ctx, arg);

    JS_SetPropertyUint32(ctx, arg, 0, JS_NewBigInt64(ctx, ts->tv_sec));
    JS_SetPropertyUint32(ctx, arg, 1, JS_NewBigInt64(ctx, ts->tv_nsec));

    return TRUE;
  }

  if(js_is_arraybuffer(ctx, arg)) {
    uint8_t* data;
    size_t len;

    if((data = JS_GetArrayBuffer(ctx, &len, arg))) {
      if(len >= sizeof(struct timespec)) {
        memcpy(data, ts, sizeof(struct timespec));
        return TRUE;
      }
    }
  }

  return FALSE;
}

static int
fdset_fromarray(JSContext* ctx, JSValueConst arr, fd_set* set) {
  size_t len;
  int64_t* v;

  if((v = js_array_to_int64v(ctx, &len, arr))) {
    for(size_t i = 0; i < len; i++) {
      assert(v[i] >= 0);
      FD_SET(v[i], set);
    }

    js_free(ctx, v);
    return 0;
  }

  return -1;
}

static ssize_t
fdset_read(JSContext* ctx, JSValueConst arg, fd_set* set) {
  InputBuffer buf = js_input_buffer(ctx, arg);

  if(buf.data) {
    size_t len = MIN_NUM(buf.size, sizeof(fd_set));
    memcpy(set, buf.data, len);
    inputbuffer_free(&buf, ctx);
    return len;
  }

  if(js_is_array(ctx, arg))
    if(!fdset_fromarray(ctx, arg, set))
      return sizeof(fd_set);

  return 0;
}

static int64_t
fdset_toarray(JSContext* ctx, const fd_set* set, JSValueConst arr) {
  uint32_t idx = 0;

  if(js_array_clear(ctx, arr) < 0)
    return -1;

  for(int fd = 0; fd < FD_SETSIZE; ++fd)
    if(FD_ISSET(fd, set))
      JS_SetPropertyUint32(ctx, arr, idx++, JS_NewUint32(ctx, fd));

  return idx;
}

static ssize_t
fdset_write(JSContext* ctx, const fd_set* set, JSValueConst arg) {
  InputBuffer buf = js_input_buffer(ctx, arg);

  if(buf.data) {
    size_t len = MIN_NUM(buf.size, sizeof(fd_set));
    memcpy(buf.data, set, len);
    inputbuffer_free(&buf, ctx);
    return len;
  }

  if(js_is_array(ctx, arg))
    if(fdset_toarray(ctx, set, arg) >= 0)
      return sizeof(fd_set);

  return 0;
}

#ifndef _WIN32
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
  if(js_is_array(ctx, arg) && js_array_clear(ctx, arg) >= 0) {
    JS_SetPropertyUint32(ctx, arg, 0, JS_NewInt32(ctx, pfd->fd));
    JS_SetPropertyUint32(ctx, arg, 1, JS_NewInt32(ctx, pfd->events));
    JS_SetPropertyUint32(ctx, arg, 2, JS_NewInt32(ctx, pfd->revents));

    return TRUE;
  }

  if(JS_IsObject(arg) && !JS_IsNull(arg)) {
    JS_SetPropertyStr(ctx, arg, "fd", JS_NewInt32(ctx, pfd->fd));
    JS_SetPropertyStr(ctx, arg, "events", JS_NewInt32(ctx, pfd->events));
    JS_SetPropertyStr(ctx, arg, "revents", JS_NewInt32(ctx, pfd->revents));

    return TRUE;
  }

  if(js_is_arraybuffer(ctx, arg)) {
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
#endif

static void*
optval_buf(JSContext* ctx, JSValueConst arg, int32_t** tmp_ptr, socklen_t* lenp) {
  uint32_t len;
  uint8_t* buf;

  if(JS_IsArray(ctx, arg)) {
    int i, n = MAX_NUM(1, MIN_NUM(js_array_length(ctx, arg), 1));

    len = n * sizeof(int32_t);
    *tmp_ptr = (int32_t*)(buf = js_mallocz(ctx, len));

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
js_socketpair(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  SockType st = {AF_UNSPEC, SOCK_STREAM, IPPROTO_IP};
  int result;
  SOCKET s[2];

  switch(js_socket_type(ctx, &st, argc, argv)) {
    case 0: return JS_ThrowTypeError(ctx, "argument 1 must be address family");
    case 1: return JS_ThrowTypeError(ctx, "argument 2 must be socket type");
    /*case 2: return JS_ThrowTypeError(ctx, "argument 3 must be protocol");*/
    default: break;
  }

  if(argc < 4 && !JS_IsArray(ctx, argv[3]))
    return JS_ThrowTypeError(ctx, "argument 4 must be array");

  if((result = socketpair(st.family, st.type, st.protocol, s)) != -1) {
    JS_SetPropertyUint32(ctx, argv[3], 0, JS_NewInt64(ctx, s[0]));
    JS_SetPropertyUint32(ctx, argv[3], 1, JS_NewInt64(ctx, s[1]));
  }

  return JS_NewInt64(ctx, result);
}

static JSValue
js_select(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  uint64_t n;
  fd_set rfds, wfds, efds, *rset = 0, *wset = 0, *eset = 0;
  int ret;
  struct timeval tv = {0, 0}, *timeout = 0;
  ssize_t r;
  static const ssize_t fd_setsize = sizeof(fd_set);

  JS_ToIndex(ctx, &n, argv[0]);

  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  FD_ZERO(&efds);

  if(argc >= 2) {
    if((r = fdset_read(ctx, argv[1], &rfds)) >= fd_setsize)
      rset = &rfds;
    else if(r > 0)
      return JS_ThrowTypeError(ctx, "argument 2 ArrayBuffer needs to be at least %lu bytes in size", (unsigned long)sizeof(fd_set));
  }

  if(argc >= 3) {
    if((r = fdset_read(ctx, argv[2], &wfds)) >= fd_setsize)
      wset = &wfds;
    else if(r > 0)
      return JS_ThrowTypeError(ctx, "argument 3 ArrayBuffer needs to be at least %lu bytes in size", (unsigned long)sizeof(fd_set));
  }

  if(argc >= 4) {
    if((r = fdset_read(ctx, argv[3], &efds)) >= fd_setsize)
      eset = &efds;
    else if(r > 0)
      return JS_ThrowTypeError(ctx, "argument 4 ArrayBuffer needs to be at least %lu bytes in size", (unsigned long)sizeof(fd_set));
  }

  if(argc >= 5)
    if(timeval_read(ctx, argv[4], &tv))
      timeout = &tv;

  ret = select(n, rset, wset, eset, timeout);

  if(ret == -1)
    return JS_Throw(ctx, js_syscallerror_new(ctx, "select", errno));

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

#ifndef _WIN32
static JSValue
js_poll(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  int result;
  uint32_t nfds = 0;
  int32_t timeout = -1;
  struct pollfd* pfds;
  BOOL is_array = js_is_array(ctx, argv[0]), is_arraybuffer = js_is_arraybuffer(ctx, argv[0]);

  if(argc >= 2 && JS_IsNumber(argv[1]))
    JS_ToUint32(ctx, &nfds, argv[1]);

  if(argc >= 3 && JS_IsNumber(argv[2]))
    JS_ToInt32(ctx, &timeout, argv[2]);

  if(nfds == 0)
    nfds = is_array ? js_array_length(ctx, argv[0]) : is_arraybuffer ? js_arraybuffer_bytelength(ctx, argv[0]) / (int64_t)sizeof(struct pollfd) : 0;

  assert(nfds);
  pfds = calloc(sizeof(struct pollfd), nfds);

  if(is_array) {
    uint32_t i;

    for(i = 0; i < nfds; i++) {
      JSValue member = JS_GetPropertyUint32(ctx, argv[0], i);

      if(!pollfd_read(ctx, member, &pfds[i])) {
        JS_FreeValue(ctx, member);
        ret = JS_ThrowInternalError(ctx, "pfds[%i] not valid", i);
        goto end;
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
        ret = JS_ThrowInternalError(ctx, "pfds[] length = %zu", len);
        goto end;
      }
    }
  }

  result = poll(pfds, nfds, timeout);

  if(result == -1) {
    free(pfds);
    return JS_Throw(ctx, js_syscallerror_new(ctx, "poll", errno));
  }

  if(is_array) {
    for(uint32_t i = 0; i < nfds; i++) {
      JSValue member = JS_GetPropertyUint32(ctx, argv[0], i);

      if(!pollfd_write(ctx, &pfds[i], member)) {
        JS_FreeValue(ctx, member);
        ret = JS_ThrowInternalError(ctx, "writing pfds[%i]", i);
        goto end;
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
        ret = JS_ThrowInternalError(ctx, "pfds[] length = %zu", len);
        goto end;
      }
    }
  }

  ret = JS_NewInt32(ctx, result);

end:
  free(pfds);
  return ret;
}
#endif

static BOOL
fd_is_nonblocking(int fd) {
  BOOL ret = 0;
#ifdef _WIN32
#else
  int oldflags, newflags;

  oldflags = fcntl(fd, F_GETFL);

  ret = !!(oldflags & O_NONBLOCK);
#endif

  return ret;
}

static int
fd_address_family(int fd) {
  SockAddr a = {-1};
  socklen_t len = sizeof(a);
  int ret;

  if((ret = getsockname(fd, &a.s, &len)) < 0)
    return ret;

  return a.family;
}

static BOOL
socket_is_nonblocking(Socket s) {
#ifdef _WIN32
  return s.nonblock;
#else
  return fd_is_nonblocking(socket_handle(s));
#endif
}

static BOOL
socket_nonblocking(Socket* s, BOOL nonblock) {
#ifdef _WIN32
  ULONG mode = nonblock;
  socketcall_return(s, SYSCALL_FCNTL, ioctlsocket(socket_handle(*s), FIONBIO, &mode));
#else
  int oldflags, newflags;

  oldflags = fcntl(socket_handle(*s), F_GETFL);
  newflags = nonblock ? (oldflags | O_NONBLOCK) : (oldflags & (~O_NONBLOCK));

  if(oldflags != newflags)
    socketcall_return(s, SYSCALL_FCNTL, fcntl(socket_handle(*s), F_SETFL, newflags));
#endif

  s->nonblock = nonblock;

  return s->ret == 0;
}

static const char*
socket_method(int magic) {
  static const char* const methods[] = {
      "ndelay",
      "bind",
      "accept",
      "connect",
      "listen",
      0,
      0,
      0,
      "recv",
      "send",
      "recvfrom",
      "sendto",
      "recvmsg",
      "sendmsg",
      "getsockopt",
      "setsockopt",
      "shutdown",
      "close",
  };

  return methods[magic & 0x0f];
}

static int
socket_address_family(Socket s) {
  return fd_address_family(socket_handle(s));
}

static JSValue
socket_adopt(JSContext* ctx, int fd, BOOL async) {
  return js_socket_new_proto(ctx, socket_proto, fd, async, FALSE);
}

static AsyncSocket*
js_asyncsocket_data(JSValueConst value) {
  return JS_GetOpaque(value, js_asyncsocket_class_id);
}

static AsyncSocket*
js_asyncsocket_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_asyncsocket_class_id);
}

enum {
  METHOD_NDELAY = 0x00,
  METHOD_BIND = 0x01,
  METHOD_ACCEPT = 0x02,
  METHOD_CONNECT = 0x03,
  METHOD_LISTEN = 0x04,
  METHOD_RECV = 0x08 /* 0b01000 */,
  METHOD_SEND = 0x09 /* 0b01001 */,
  METHOD_RECVFROM = 0x0a /* 0b01010 */,
  METHOD_SENDTO = 0x0b /* 0b01011 */,
  METHOD_RECVMSG = 0x0e /* 0b01110 */,
  METHOD_SENDMSG = 0x0f /* 0b01111 */,
  METHOD_RECVMMSG = 0x1e /* 0b11110 */,
  METHOD_SENDMMSG = 0x1f /* 0b11111 */,
  METHOD_GETSOCKOPT,
  METHOD_SETSOCKOPT,
  METHOD_SHUTDOWN,
  METHOD_CLOSE,
};

enum {
  PROP_FD,
  PROP_OPEN,
  PROP_EOF,
  PROP_MODE,
  /*PROP_SYSCALL,
  PROP_ERRNO,
  PROP_ERROR,
  PROP_RET,
  PROP_AF,*/
  PROP_LOCAL,
  PROP_REMOTE,
  PROP_NONBLOCK,
};

enum {
  ASYNC_READY = 1 << 16,
  ASYNC_WAITONLY = 1 << 17,
};

/**
 *   data[0]   Socket
 *   data[1]   resolve function
 *   data[2]   os.set(Read|Write)Handler function
 *   data[3]   buf
 *   data[4]   len
 *   data[5]   flags
 *   data[6]   dest_addr
 *   data[7]   addrlen
 */
static JSValue
js_asyncsocket_resolve(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue data[]) {
  AsyncSocket* asock = js_asyncsocket_data(data[0]);
  JSValueConst value = data[0];
  BOOL is_exception = FALSE;
  int argn = (magic & 0xe) == 0xa ? 5 : (magic & 0x8) ? 4 : 1;

  assert(JS_VALUE_GET_TAG(data[1]) == JS_TAG_OBJECT);
  assert(JS_VALUE_GET_PTR(data[1]));

  if((magic & 0x0f) == METHOD_CONNECT) {
    int err = 0;
    socklen_t optlen = sizeof(err);

    if(getsockopt(socket_handle(*asock), SOL_SOCKET, SO_ERROR, (void*)&err, &optlen) != 0) {
      asock->ret = -1;
      asock->sysno = SYSCALL_GETSOCKOPT;
      err = errno;
    }

    asock->error = err;

    if(err) {
      value = js_syscallerror_new(ctx, "connect", err);
      is_exception = TRUE;
    } else {
      value = JS_UNDEFINED;
    }
  } else {
    value = js_socket_method(ctx, data[0], argn, &data[4], magic | ASYNC_READY);
  }

  if(js_object_same(data[1], asock->pending[magic & 1])) {
    JSValueConst args[2] = {data[0], JS_NULL};

    JS_Call(ctx, data[3], JS_UNDEFINED, 2, args);

#ifdef DEBUG_OUTPUT
    printf("%s(): [%p] set%sHandler(%d, null)\n", __func__, JS_VALUE_GET_PTR(data[1]), magic & 1 ? "Write" : "Read", socket_handle(*asock));
#endif

    /* free ourselves */
    JS_FreeValue(ctx, asock->pending[magic & 1]);
    asock->pending[magic & 1] = JS_NULL;
  }

  if(JS_IsException(value)) {
    is_exception = TRUE;
    value = JS_GetException(ctx);
  }

  JS_Call(ctx, is_exception ? data[2] : data[1], JS_UNDEFINED, 1, &value);

  return JS_UNDEFINED;
}

static JSValue
js_asyncsocket_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AsyncSocket* s;
  JSValue ret = JS_UNDEFINED, set_handler, args[2], data[9], promise, resolving_funcs[2];
  int data_len;

  if(!(s = js_asyncsocket_data(this_val)))
    return JS_ThrowInternalError(ctx, "Must be an AsyncSocket");

  if(!js_socket_check_open(ctx, *(Socket*)s))
    return JS_EXCEPTION;

  if(JS_IsException((set_handler = js_iohandler_fn(ctx, magic & 1, "io"))))
    return JS_EXCEPTION;

  if(JS_IsObject(s->pending[magic & 1]))
    return JS_ThrowInternalError(ctx, "Already a pending %s", magic & 1 ? "write" : "read");

  promise = JS_NewPromiseCapability(ctx, resolving_funcs);
  if(JS_IsException(promise))
    return promise;

  data[0] = this_val;
  data[1] = resolving_funcs[0];
  data[2] = resolving_funcs[1];
  data[3] = set_handler;
  data_len = 4;

  if(magic >= 2) {
    int argn = (magic & 0xe) == 0xa ? 5 : (magic & 0x8) ? 4 : 1;

    for(int i = 0; i < argn; i++)
      data[data_len++] = i < argc ? argv[i] : JS_UNDEFINED;
  }

  args[0] = JS_NewInt32(ctx, socket_fd(*s));
  args[1] = JS_NewCFunctionData(ctx, js_asyncsocket_resolve, 0, magic, data_len, data);

#ifdef DEBUG_OUTPUT
  printf("%s(): set%sHandler(%d, %p)\n", __func__, magic & 1 ? "Write" : "Read", socket_fd(*s), JS_VALUE_GET_PTR(data[1]));
#endif

  s->pending[magic & 1] = JS_DupValue(ctx, resolving_funcs[0]);

  ret = JS_Call(ctx, set_handler, JS_UNDEFINED, 2, args);

  JS_FreeValue(ctx, ret);
  JS_FreeValue(ctx, set_handler);
  JS_FreeValue(ctx, resolving_funcs[0]);
  JS_FreeValue(ctx, resolving_funcs[1]);

  return promise;
}

static JSValue
js_asyncsocket_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AsyncSocket* s;
  JSValue ret = JS_UNDEFINED;

  if(!(s = js_asyncsocket_data(this_val)))
    return JS_UNDEFINED;

  switch(magic) {
    case PROP_MODE: {
#ifdef _WIN32
      ULONG mode = JS_ToBool(ctx, value);
      ret = JS_NewInt32(ctx, ioctlsocket(socket_handle(*s), FIONBIO, &mode));
#else
      uint32_t mode = 0;

      JS_ToUint32(ctx, &mode, value);
      JS_SOCKETCALL_RETURN(SYSCALL_FCNTL, (Socket*)s, fcntl(socket_handle(*s), F_SETFL, mode), JS_NewInt32(ctx, s->ret), JS_UNDEFINED);
#endif
      break;
    }

    case PROP_NONBLOCK: {
      BOOL nb = JS_ToBool(ctx, value);

      if(nb != s->nonblock)
        socket_nonblocking((Socket*)s, nb);

      break;
    }
  }

  return ret;
}

static JSValue
js_asyncsocket_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  return js_socket_create(ctx, new_target, argc, argv, TRUE);
}

static Socket
js_socket_data(JSValueConst value) {
  Socket sock = SOCKET_INIT();
  void* opaque;

  if((opaque = JS_GetOpaque(value, js_asyncsocket_class_id)))
    sock = *(Socket*)opaque;
  else if((opaque = JS_GetOpaque(value, js_socket_class_id))) {
    union {
      Socket s;
      void* ptr;
    } u;
    u.ptr = opaque;
    sock = u.s;
  }

  return sock;
}

static Socket
js_socket_data2(JSContext* ctx, JSValueConst value) {
  Socket sock = SOCKET_INIT();
  void* opaque;

  if((opaque = JS_GetOpaque(value, js_asyncsocket_class_id)))
    sock = *(Socket*)opaque;
  else if((opaque = JS_GetOpaque2(ctx, value, js_socket_class_id))) {
    union {
      Socket s;
      void* ptr;
    } u;

    u.ptr = opaque;
    sock = u.s;
  }

  return sock;
}

static int
js_socket_type(JSContext* ctx, SockType* st, int argc, JSValueConst argv[]) {
  int index = 0;

  st->family = AF_UNSPEC;
  st->type = SOCK_STREAM;
  st->protocol = IPPROTO_IP;

  if(argc > index) {
    if(!JS_IsNumber(argv[index]) || JS_ToInt32(ctx, &st->family, argv[index]))
      return index;

    if(argc > ++index) {
      if(!JS_IsNumber(argv[index]) || JS_ToInt32(ctx, &st->type, argv[index]))
        return index;

      if(argc > ++index) {
        if(!JS_IsNumber(argv[index]) || JS_ToInt32(ctx, &st->protocol, argv[index]))
          return index;

        ++index;
      }
    }
  }

  return index;
}

static int
js_socket_address_family(JSValueConst obj) {
  return socket_address_family(js_socket_data(obj));
}

static JSValue
js_socket_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  return js_socket_create(ctx, new_target, argc, argv, FALSE);
}

static JSValue
js_socket_error(JSContext* ctx, Socket sock) {
  JSValue ret = JS_NewInt32(ctx, socket_retval(sock));
  int err;

  if((err = socket_error(sock)))
    if(!(sock.nonblock && ((sock.sysno == SYSCALL_RECV && err == EAGAIN) || (sock.sysno == SYSCALL_SEND && err == EWOULDBLOCK) ||
                           (sock.sysno == SYSCALL_CONNECT && err == EINPROGRESS))))
      ret = JS_Throw(ctx, js_syscallerror_new(ctx, socket_syscall(sock), err));

  return ret;
}

static BOOL
js_socket_check_open(JSContext* ctx, Socket sock) {
  if(socket_closed(sock)) {
    JS_ThrowInternalError(ctx, "Socket #%d has already been closed", socket_fd(sock));
    return FALSE;
  }

  if(!socket_open(sock)) {
    JS_ThrowInternalError(ctx, "Socket #%d is not yet open", socket_fd(sock));
    return FALSE;
  }

  return TRUE;
}

static JSValue
js_socket_new_proto(JSContext* ctx, JSValueConst proto, int fd, BOOL async, BOOL owner) {
  JSValue obj;
  Socket* s;

  if(js_sockaddr_class_id == 0 && js_socket_class_id == 0 && js_asyncsocket_class_id == 0)
    js_sockets_init(ctx, 0);

  if(js_is_nullish(ctx, proto))
    proto = async ? asyncsocket_proto : socket_proto;

  if(async && js_object_same(proto, socket_proto))
    proto = asyncsocket_proto;

  obj = JS_NewObjectProtoClass(ctx, proto, async ? js_asyncsocket_class_id : js_socket_class_id);
  if(JS_IsException(obj))
    goto fail;

  if(async) {
    AsyncSocket* asock;

    if(!(asock = js_mallocz(ctx, sizeof(AsyncSocket))))
      return JS_EXCEPTION;

    JS_SetOpaque(obj, asock);
    s = (Socket*)asock;

    s->fd = fd;

    socket_nonblocking(s, TRUE);

  } else {
    Socket sock = SOCKET(fd, 0, -1, FALSE, FALSE, owner);
    assert(sizeof(Socket) == sizeof(sock.ptr));

    JS_SetOpaque(obj, sock.ptr);
  }

  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static int
js_socket_fd(JSValueConst sock) {
  Socket s = js_socket_data(sock);

  return socket_handle(s);
}

static JSValue
js_socket_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Socket sock = js_socket_data(this_val);
  JSValue ret = JS_UNDEFINED, sa = JS_UNDEFINED;
  SockAddr* a = 0;
  socklen_t alen = 0;
  AsyncSocket* asock = js_asyncsocket_data(this_val);
  Socket* s = asock ? (Socket*)asock : &sock;
  BOOL wait = s->nonblock && !(magic & ASYNC_READY);

  magic &= (ASYNC_READY - 1);

  if(magic >= METHOD_BIND && magic <= METHOD_CONNECT) {

    if(!(a = js_sockaddr_data(argv[0])) && magic != METHOD_ACCEPT) {
      int my_argc = argc;
      JSValueConst* my_argv = argv;

      sa = js_sockaddr_args(ctx, &my_argc, &my_argv);

      if(my_argc != argc) {
        if(!JS_IsException(sa))
          a = js_sockaddr_data2(ctx, sa);
      } else if(argc >= 2) {
        JSValueConst args[] = {
            JS_NewInt32(ctx, socket_address_family(*s)),
            argv[0],
            argv[1],
        };

        sa = js_sockaddr_constructor(ctx, sockaddr_ctor, countof(args), args);

        if(!JS_IsException(sa))
          a = js_sockaddr_data2(ctx, sa);
      }
    }

    if(!a)
      return JS_ThrowTypeError(ctx, "argument 1 must be of type SockAddr");

    alen = sockaddr_len(a);
  }

  if(!js_socket_check_open(ctx, *s))
    return JS_EXCEPTION;

  if(wait) {
    switch(magic) {
      case METHOD_ACCEPT:
      case METHOD_RECV:
      case METHOD_RECVFROM:
      case METHOD_SEND:
      case METHOD_SENDTO:
      case METHOD_RECVMSG:
      case METHOD_SENDMSG: {
        return JS_ThrowInternalError(ctx, "socket %s() wait assert", socket_method(magic));
        assert(0);
        break;
      }

      default: break;
    }
  }

  switch(magic) {
    case METHOD_NDELAY: {
      BOOL nonblock = TRUE, prev = s->nonblock;

      if(argc >= 1)
        nonblock = JS_ToBool(ctx, argv[0]);

      if(nonblock != prev) {
        socket_nonblocking(s, nonblock);
        ret = JS_NewBool(ctx, prev);
      }

      break;
    }

    case METHOD_BIND: {
      JS_SOCKETCALL_RETURN(SYSCALL_BIND, s, bind(socket_handle(*s), &a->s, sockaddr_len(a)), JS_UNDEFINED, js_socket_error(ctx, *s));

      break;
    }

    case METHOD_ACCEPT: {
      socklen_t addrlen = sockaddr_len(a);

      if(argc > 1) {
        int32_t flags = 0;
        JS_ToInt32(ctx, &flags, argv[1]);

        JS_SOCKETCALL_RETURN(SYSCALL_ACCEPT4,
                             s,
                             accept4(socket_handle(*s), &a->s, &addrlen, flags),
                             socket_adopt(ctx, s->ret, socket_is_nonblocking(*s)),
                             js_socket_error(ctx, *s));
      } else
        JS_SOCKETCALL_RETURN(
            SYSCALL_ACCEPT, s, accept(socket_handle(*s), &a->s, &addrlen), socket_adopt(ctx, s->ret, socket_is_nonblocking(*s)), js_socket_error(ctx, *s));

      break;
    }

    case METHOD_CONNECT: {
      JS_SOCKETCALL(SYSCALL_CONNECT, s, connect(socket_handle(*s), &a->s, sockaddr_len(a)));

      if(wait)
        ret = js_asyncsocket_method(ctx, this_val, argc, argv, magic);

      break;
    }

    case METHOD_LISTEN: {
      int32_t backlog = 5;

      if(argc >= 1)
        JS_ToInt32(ctx, &backlog, argv[0]);

      JS_SOCKETCALL_RETURN(SYSCALL_LISTEN, s, listen(socket_handle(*s), backlog), JS_UNDEFINED, js_socket_error(ctx, *s));

      break;
    }

    case METHOD_RECV:
    case METHOD_RECVFROM: {
      int32_t flags = 0;
      InputBuffer buf = js_input_buffer(ctx, argv[0]);
      OffsetLength off = OFFSET_LENGTH_0();

      js_offset_length(ctx, buf.size, argc - 1, argv + 1, 0, &off);

#ifdef DEBUG_OUTPUT_
      printf("%s(): recv(%d, %zu, %zu, %zu)\n", __func__, socket_handle(*s), off.offset, off.length, offsetlength_size(off, buf.size));
#endif

      if(argc >= 4)
        JS_ToInt32(ctx, &flags, argv[3]);

      if(magic == METHOD_RECVFROM) {
        if((a = argc >= 5 ? js_sockaddr_data(argv[4]) : 0))
          alen = sockaddr_len(a);

        JS_SOCKETCALL(
            SYSCALL_RECVFROM,
            s,
            recvfrom(socket_handle(*s), offsetlength_begin(off, buf.data), offsetlength_size(off, buf.size), flags, a ? &a->s : NULL, a ? &alen : NULL));
      } else {
        JS_SOCKETCALL(SYSCALL_RECV, s, recv(socket_handle(*s), offsetlength_begin(off, buf.data), offsetlength_size(off, buf.size), flags));
      }

      break;
    }

    case METHOD_SEND:
    case METHOD_SENDTO: {
      int32_t flags = 0;
      InputBuffer buf = js_input_chars(ctx, argv[0]);
      OffsetLength off = {0, buf.size};

      js_offset_length(ctx, buf.size, argc - 1, argv + 1, 0, &off);

      if(argc >= 4)
        JS_ToInt32(ctx, &flags, argv[3]);

      void* ptr = offsetlength_begin(off, buf.data);
      size_t len = offsetlength_size(off, buf.size);

      if(magic == METHOD_SENDTO) {
        if((a = argc >= 5 ? js_sockaddr_data(argv[4]) : 0))
          alen = sockaddr_len(a);

        JS_SOCKETCALL(SYSCALL_SENDTO, s, sendto(socket_handle(*s), ptr, len, flags, a ? &a->s : NULL, a ? alen : 0));
      } else {
        JS_SOCKETCALL(SYSCALL_SEND, s, send(socket_handle(*s), ptr, len, flags));
      }

      break;
    }

    case METHOD_GETSOCKOPT: {
      int32_t level, optname;
      uint32_t optlen = sizeof(int);
      uint8_t* buf;
      int32_t val = -1, *tmp = 0;
      socklen_t len;

      JS_ToInt32(ctx, &level, argv[0]);
      JS_ToInt32(ctx, &optname, argv[1]);

      if(argc >= 4)
        JS_ToUint32(ctx, &optlen, argv[3]);

      JS_ToInt32(ctx, &optname, argv[1]);

      buf = optval_buf(ctx, argv[2], &tmp, &len);

      JS_SOCKETCALL_RETURN(SYSCALL_GETSOCKOPT, s, getsockopt(socket_handle(*s), level, optname, (void*)buf, &len), JS_UNDEFINED, js_socket_error(ctx, *s));

      if(tmp) {
        js_array_clear(ctx, argv[2]);
        JS_SetPropertyUint32(ctx, argv[2], 0, JS_NewInt32(ctx, *(int32_t*)buf));
      }

      if(tmp)
        js_free(ctx, tmp);

#ifdef DEBUG_OUTPUT_
      printf("%s(): SYSCALL_GETSOCKOPT(%d, %d, %d (%d), %lu) = %d\n", __func__, socket_fd(*s), level, optname, val, (unsigned long int)len, s->ret);
#endif

      break;
    }

    case METHOD_SETSOCKOPT: {
      int32_t level, optname, num = 0, *tmp = 0;
      socklen_t len = 0;
      uint8_t* buf = 0;

      if(argc >= 3)
        buf = optval_buf(ctx, argv[2], &tmp, &len);

      if(!buf) {
        buf = (uint8_t*)&num;
        len = sizeof(num);
      }

      JS_ToInt32(ctx, &level, argv[0]);
      JS_ToInt32(ctx, &optname, argv[1]);

      if(argc >= 4) {
        uint32_t newlen = 0;

        if(!JS_ToUint32(ctx, &newlen, argv[3])) {
          newlen = MIN_NUM(newlen, (uint32_t)len);

          if(newlen)
            len = newlen;
        }
      }

      socketcall_return(s, SYSCALL_SETSOCKOPT, setsockopt(socket_handle(*s), level, optname, (const void*)buf, len));

#ifdef DEBUG_OUTPUT_
      printf("%s(): SYSCALL_SETSOCKOPT(%d, %d, %d, %i (%p), %lu) = %d\n",
             __func__,
             socket_fd(*s),
             level,
             optname,
             *(int*)buf,
             buf,
             (unsigned long int)len,
             s->ret);
#endif

      if(tmp)
        js_free(ctx, tmp);

      break;
    }

    case METHOD_SHUTDOWN: {
      int32_t how;

      JS_ToInt32(ctx, &how, argv[0]);
      JS_SOCKETCALL_RETURN(SYSCALL_SHUTDOWN, s, shutdown(socket_handle(*s), how), JS_UNDEFINED, js_socket_error(ctx, *s));
      break;
    }

    case METHOD_CLOSE: {
      JS_SOCKETCALL_RETURN(SYSCALL_CLOSE, s, closesocket(socket_fd(*s)), JS_UNDEFINED, js_socket_error(ctx, *s));

      if(socket_retval(*s) == 0)
        s->fd = UINT16_MAX;

      break;
    }

    case METHOD_RECVMSG:
    case METHOD_SENDMSG: {
      int32_t flags = 0;
      struct msghdr mh = {};

      if(!msg_read(ctx, argv[0], &mh)) {
        ret = JS_ThrowInternalError(ctx, "Error parsing msghdr structure");
        break;
      }

      if(argc > 1)
        JS_ToInt32(ctx, &flags, argv[1]);

      if(magic == METHOD_SENDMSG)
        JS_SOCKETCALL(SYSCALL_SENDMSG, s, sendmsg(socket_handle(*s), &mh, flags));
      else
        JS_SOCKETCALL(SYSCALL_RECVMSG, s, recvmsg(socket_handle(*s), &mh, flags));

      break;
    }

    case METHOD_RECVMMSG:
    case METHOD_SENDMMSG: {
      int r;
      int32_t flags = 0;
      MultiMessageHeader mmh = {0, 0};
      struct timespec ts = {}, *tptr = 0;

      if(!mmsgs_read(ctx, argv[0], &mmh)) {
        ret = JS_ThrowInternalError(ctx, "Error parsing mmsghdr structure");
        break;
      }

      if(argc > 1)
        JS_ToInt32(ctx, &flags, argv[1]);

      if(magic == METHOD_RECVMMSG)
        if(argc > 2)
          if(timespec_read(ctx, argv[2], &ts))
            tptr = &ts;

      if(magic == METHOD_SENDMMSG)
        JS_SOCKETCALL(SYSCALL_SENDMMSG, s, (r = sendmmsg(socket_handle(*s), mmh.msgvec, mmh.vlen, flags)));
      else
        JS_SOCKETCALL(SYSCALL_RECVMMSG, s, (r = recvmmsg(socket_handle(*s), mmh.msgvec, mmh.vlen, flags, tptr)));

      if(r > 0) {
        mmh.vlen = r;
        mmsgs_write(ctx, argv[0], mmh);
      }

      break;
    }
  }

  if(js_asyncsocket_data(this_val) == NULL)
    JS_SetOpaque(this_val, sock.ptr);

  JS_FreeValue(ctx, sa);

  return ret;
}

static JSValue
js_socket_new(JSContext* ctx, JSValueConst new_target, SockType st, BOOL async) {
  JSValue proto;
  int fd = -1;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  for(;;) {
#if defined(_WIN32) && !defined(__MSYS__) && !defined(__CYGWIN__)
    SOCKET h = socket(st.family, st.type, st.protocol);

    fd = h == INVALID_SOCKET ? -1 : _open_osfhandle(h, 0);
#else
    fd = socket(st.family, st.type, st.protocol);
#endif

    if(fd == -1) {
#if defined(_WIN32) && !defined(__MSYS__) && !defined(__CYGWIN__)
      static BOOL initialized;
      int err;
      WSADATA d;

      if(!initialized) {
        initialized++;
        if((err = WSAStartup(MAKEWORD(2, 3), &d)))
          return JS_Throw(ctx, js_syscallerror_new(ctx, "WSAStartup", err));

        continue;
      }

      return JS_Throw(ctx, js_syscallerror_new(ctx, "socket", WSAGetLastError()));
#else
      return JS_Throw(ctx, js_syscallerror_new(ctx, "socket", errno));
#endif
    }

    break;
  }

  return js_socket_new_proto(ctx, proto, fd, async, TRUE);

fail:
  return JS_EXCEPTION;
}

static JSValue
js_socket_create(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[], BOOL async) {
  SockType st = {AF_UNSPEC, SOCK_STREAM, IPPROTO_IP};

  switch(js_socket_type(ctx, &st, argc, argv)) {
    case 0: return JS_ThrowTypeError(ctx, "argument 1 must be address family");
    case 1: return JS_ThrowTypeError(ctx, "argument 2 must be socket type");
    /*case 2: return JS_ThrowTypeError(ctx, "argument 3 must be protocol");*/
    default: break;
  }

  return js_socket_new(ctx, new_target, st, async);
}

static JSValue
js_socket_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Socket s = js_socket_data(this_val);
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case PROP_FD: {
      ret = JS_NewInt32(ctx, socket_fd(s));
      break;
    }

    case PROP_OPEN: {
      ret = JS_NewBool(ctx, socket_open(s));
      break;
    }

    case PROP_EOF: {
      ret = JS_NewBool(ctx, socket_eof(s));
      break;
    }

    case PROP_MODE: {
#ifdef _WIN32
#else
      int fl = fcntl(socket_handle(s), F_GETFL);
      ret = JS_NewInt32(ctx, fl);
#endif
      break;
    }

      /*case PROP_SYSCALL: {
        if(s.sysno > 0) {
          const char* name;

          if((name = socketcall_name(s.sysno)))
            ret = JS_NewString(ctx, name);
        }

        break;
      }

      case PROP_ERRNO: {
        ret = JS_NewUint32(ctx, socket_error(s));
        break;
      }

      case PROP_ERROR: {
        if(s.error)
          ret = js_syscallerror_new(ctx, socket_syscall(s), s.error);

        break;
      }

      case PROP_RET: {
        ret = JS_NewInt32(ctx, s.ret);
        break;
      }

      case PROP_AF: {
        int af = socket_address_family(s);

        if(af >= 0)
          ret = JS_NewInt32(ctx, af);

        break;
      }*/

    case PROP_LOCAL: {
      SockAddr a;
      socklen_t len = sizeof(SockAddr);

      JS_SOCKETCALL_RETURN(SYSCALL_GETSOCKNAME, &s, getsockname(socket_handle(s), &a.s, &len), js_sockaddr_clone(ctx, a), JS_NULL);

      break;
    }

    case PROP_REMOTE: {
      SockAddr a;
      socklen_t len = sizeof(SockAddr);

      JS_SOCKETCALL_RETURN(SYSCALL_GETPEERNAME, &s, getpeername(socket_handle(s), &a.s, &len), js_sockaddr_clone(ctx, a), JS_NULL);

      break;
    }

    case PROP_NONBLOCK: {
      ret = JS_NewBool(ctx, s.nonblock);
      break;
    }
  }

  return ret;
}

static JSValue
js_socket_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Socket s = js_socket_data(this_val);
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case PROP_MODE: {
#ifdef _WIN32
      ULONG mode = JS_ToBool(ctx, value);
      ret = JS_NewInt32(ctx, ioctlsocket(socket_handle(s), FIONBIO, &mode));
#else
      uint32_t mode = 0;
      JS_ToUint32(ctx, &mode, value);
      JS_SOCKETCALL_RETURN(SYSCALL_FCNTL, &s, fcntl(socket_handle(s), F_SETFL, mode), JS_NewInt32(ctx, s.ret), JS_UNDEFINED);
#endif
      break;
    }

    case PROP_NONBLOCK: {
      s.nonblock = JS_ToBool(ctx, value);
      break;
    }
  }

  JS_SetOpaque(this_val, s.ptr);
  return ret;
}

static JSValue
js_socket_valueof(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Socket s = js_socket_data(this_val);

  return JS_NewInt32(ctx, socket_handle(s));
}

static JSValue
js_socket_adopt(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t fd = -1;
  BOOL async = FALSE;

  JS_ToInt32(ctx, &fd, argv[0]);

  if(argc > 1)
    async = JS_ToBool(ctx, argv[1]);

  return socket_adopt(ctx, fd, async);
}

static void
js_socket_finalizer(JSRuntime* rt, JSValue val) {
  Socket sock = js_socket_data(val);
  AsyncSocket* asock;

  if(!socket_adopted(sock))
    if(socket_open(sock))
      close(socket_fd(sock));

  if((asock = js_asyncsocket_data(val))) {
    JS_FreeValueRT(rt, asock->pending[0]);
    JS_FreeValueRT(rt, asock->pending[1]);
    js_free_rt(rt, asock);
  }
}

static JSClassDef js_socket_class = {
    .class_name = "Socket",
    .finalizer = js_socket_finalizer,
};

static JSClassDef js_asyncsocket_class = {
    .class_name = "AsyncSocket",
    .finalizer = js_socket_finalizer,
};

/*static JSValue
js_sockopt(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  return js_socket_method(ctx, argv[0], argc - 1, argv + 1, magic);
}*/

static const JSCFunctionListEntry js_sockets_funcs[] = {
    JS_CFUNC_DEF("socketpair", 4, js_socketpair),
    JS_CFUNC_DEF("select", 1, js_select),
#ifndef _WIN32
    JS_CFUNC_DEF("poll", 1, js_poll),
#endif
    /*JS_CFUNC_MAGIC_DEF("getsockopt", 4, js_sockopt, METHOD_GETSOCKOPT),
    JS_CFUNC_MAGIC_DEF("setsockopt", 4, js_sockopt, METHOD_SETSOCKOPT),*/
};

static const JSCFunctionListEntry js_socket_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("fd", js_socket_get, 0, PROP_FD, JS_PROP_C_W_E),
    /*JS_CGETSET_MAGIC_FLAGS_DEF("ret", js_socket_get, js_socket_set, PROP_RET,
    JS_PROP_CONFIGURABLE), JS_CGETSET_MAGIC_DEF("errno", js_socket_get, 0, PROP_ERRNO),
    JS_CGETSET_MAGIC_DEF("syscall", js_socket_get, 0, PROP_SYSCALL),
    JS_CGETSET_MAGIC_DEF("error", js_socket_get, 0, PROP_ERROR),
    JS_CGETSET_MAGIC_DEF("errno", js_socket_get, 0, PROP_ERRNO),
    JS_CGETSET_MAGIC_DEF("af", js_socket_get, 0, PROP_AF),*/
    JS_CGETSET_MAGIC_DEF("local", js_socket_get, 0, PROP_LOCAL),
    JS_CGETSET_MAGIC_DEF("remote", js_socket_get, 0, PROP_REMOTE),
    JS_CGETSET_MAGIC_DEF("open", js_socket_get, 0, PROP_OPEN),
    JS_CGETSET_MAGIC_DEF("eof", js_socket_get, 0, PROP_EOF),
    JS_CGETSET_MAGIC_DEF("mode", js_socket_get, js_socket_set, PROP_MODE),
    JS_CGETSET_MAGIC_DEF("nonblock", js_socket_get, js_socket_set, PROP_NONBLOCK),
    JS_CFUNC_MAGIC_DEF("ndelay", 0, js_socket_method, METHOD_NDELAY),
    JS_CFUNC_MAGIC_DEF("bind", 1, js_socket_method, METHOD_BIND),
    JS_CFUNC_MAGIC_DEF("connect", 1, js_socket_method, METHOD_CONNECT),
    JS_CFUNC_MAGIC_DEF("listen", 0, js_socket_method, METHOD_LISTEN),
    JS_CFUNC_MAGIC_DEF("accept", 0, js_socket_method, METHOD_ACCEPT),
    JS_CFUNC_MAGIC_DEF("send", 1, js_socket_method, METHOD_SEND),
    JS_CFUNC_MAGIC_DEF("sendto", 2, js_socket_method, METHOD_SENDTO),
    JS_CFUNC_MAGIC_DEF("recv", 1, js_socket_method, METHOD_RECV),
    JS_CFUNC_MAGIC_DEF("recvfrom", 2, js_socket_method, METHOD_RECVFROM),
    JS_CFUNC_MAGIC_DEF("shutdown", 1, js_socket_method, METHOD_SHUTDOWN),
    JS_CFUNC_MAGIC_DEF("close", 0, js_socket_method, METHOD_CLOSE),
    JS_CFUNC_MAGIC_DEF("getsockopt", 3, js_socket_method, METHOD_GETSOCKOPT),
    JS_CFUNC_MAGIC_DEF("setsockopt", 3, js_socket_method, METHOD_SETSOCKOPT),
    JS_CFUNC_MAGIC_DEF("recvmsg", 1, js_socket_method, METHOD_RECVMSG),
    JS_CFUNC_MAGIC_DEF("sendmsg", 1, js_socket_method, METHOD_SENDMSG),
    JS_CFUNC_MAGIC_DEF("recvmmsg", 1, js_socket_method, METHOD_RECVMMSG),
    JS_CFUNC_MAGIC_DEF("sendmmsg", 1, js_socket_method, METHOD_SENDMMSG),
    JS_CFUNC_DEF("valueOf", 0, js_socket_valueof),
    JS_ALIAS_DEF("[Symbol.toPrimitive]", "valueOf"),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Socket", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_asyncsocket_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("fd", js_socket_get, 0, PROP_FD, JS_PROP_C_W_E),
    /*JS_CGETSET_MAGIC_DEF("ret", js_socket_get, js_asyncsocket_set, PROP_RET),
    JS_CGETSET_MAGIC_DEF("errno", js_socket_get, 0, PROP_ERRNO),
    JS_CGETSET_MAGIC_DEF("syscall", js_socket_get, 0, PROP_SYSCALL),
    JS_CGETSET_MAGIC_DEF("error", js_socket_get, 0, PROP_ERROR),
    JS_CGETSET_MAGIC_DEF("errno", js_socket_get, 0, PROP_ERRNO),
    JS_CGETSET_MAGIC_DEF("af", js_socket_get, 0, PROP_AF),*/
    JS_CGETSET_MAGIC_DEF("local", js_socket_get, 0, PROP_LOCAL),
    JS_CGETSET_MAGIC_DEF("remote", js_socket_get, 0, PROP_REMOTE),
    JS_CGETSET_MAGIC_DEF("open", js_socket_get, 0, PROP_OPEN),
    JS_CGETSET_MAGIC_DEF("eof", js_socket_get, 0, PROP_EOF),
    JS_CGETSET_MAGIC_DEF("mode", js_socket_get, js_asyncsocket_set, PROP_MODE),
    JS_CGETSET_MAGIC_DEF("nonblock", js_socket_get, js_asyncsocket_set, PROP_NONBLOCK),
    JS_CFUNC_MAGIC_DEF("ndelay", 0, js_socket_method, METHOD_NDELAY),
    JS_CFUNC_MAGIC_DEF("bind", 1, js_socket_method, METHOD_BIND),
    JS_CFUNC_MAGIC_DEF("connect", 1, js_socket_method, METHOD_CONNECT),
    JS_CFUNC_MAGIC_DEF("listen", 0, js_socket_method, METHOD_LISTEN),
    JS_CFUNC_MAGIC_DEF("accept", 0, js_asyncsocket_method, METHOD_ACCEPT),
    JS_CFUNC_MAGIC_DEF("send", 1, js_asyncsocket_method, METHOD_SEND),
    JS_CFUNC_MAGIC_DEF("sendto", 2, js_asyncsocket_method, METHOD_SENDTO),
    JS_CFUNC_MAGIC_DEF("recv", 1, js_asyncsocket_method, METHOD_RECV),
    JS_CFUNC_MAGIC_DEF("recvfrom", 2, js_asyncsocket_method, METHOD_RECVFROM),
    JS_CFUNC_MAGIC_DEF("shutdown", 1, js_socket_method, METHOD_SHUTDOWN),
    JS_CFUNC_MAGIC_DEF("close", 0, js_socket_method, METHOD_CLOSE),
    JS_CFUNC_MAGIC_DEF("getsockopt", 3, js_socket_method, METHOD_GETSOCKOPT),
    JS_CFUNC_MAGIC_DEF("setsockopt", 3, js_socket_method, METHOD_SETSOCKOPT),
    JS_CFUNC_MAGIC_DEF("recvmsg", 1, js_asyncsocket_method, METHOD_RECVMSG),
    JS_CFUNC_MAGIC_DEF("sendmsg", 1, js_asyncsocket_method, METHOD_SENDMSG),
    JS_CFUNC_MAGIC_DEF("recvmmsg", 1, js_asyncsocket_method, METHOD_RECVMMSG),
    JS_CFUNC_MAGIC_DEF("sendmmsg", 1, js_asyncsocket_method, METHOD_SENDMMSG),
    JS_CFUNC_DEF("valueOf", 0, js_socket_valueof),
    JS_ALIAS_DEF("[Symbol.toPrimitive]", "valueOf"),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AsyncSocket", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_socket_static_funcs[] = {
    JS_CFUNC_DEF("adopt", 1, js_socket_adopt),
};

static const JSCFunctionListEntry js_sockets_errnos[] = {
#ifdef EPERM
    JS_CONSTANT_NONENUMERABLE(EPERM),
#endif
#ifdef ENOENT
    JS_CONSTANT_NONENUMERABLE(ENOENT),
#endif
#ifdef EINTR
    JS_CONSTANT_NONENUMERABLE(EINTR),
#endif
#ifdef EIO
    JS_CONSTANT_NONENUMERABLE(EIO),
#endif
#ifdef EBADF
    JS_CONSTANT_NONENUMERABLE(EBADF),
#endif
#ifdef EAGAIN
    JS_CONSTANT_NONENUMERABLE(EAGAIN),
#endif
#ifdef EWOULDBLOCK
    JS_CONSTANT_NONENUMERABLE(EWOULDBLOCK),
#endif
#ifdef ENOMEM
    JS_CONSTANT_NONENUMERABLE(ENOMEM),
#endif
#ifdef EACCES
    JS_CONSTANT_NONENUMERABLE(EACCES),
#endif
#ifdef EFAULT
    JS_CONSTANT_NONENUMERABLE(EFAULT),
#endif
#ifdef ENOTDIR
    JS_CONSTANT_NONENUMERABLE(ENOTDIR),
#endif
#ifdef EISDIR
    JS_CONSTANT_NONENUMERABLE(EISDIR),
#endif
#ifdef EINVAL
    JS_CONSTANT_NONENUMERABLE(EINVAL),
#endif
#ifdef ENFILE
    JS_CONSTANT_NONENUMERABLE(ENFILE),
#endif
#ifdef EMFILE
    JS_CONSTANT_NONENUMERABLE(EMFILE),
#endif
#ifdef ENOSPC
    JS_CONSTANT_NONENUMERABLE(ENOSPC),
#endif
#ifdef EROFS
    JS_CONSTANT_NONENUMERABLE(EROFS),
#endif
#ifdef EPIPE
    JS_CONSTANT_NONENUMERABLE(EPIPE),
#endif
#ifdef ENAMETOOLONG
    JS_CONSTANT_NONENUMERABLE(ENAMETOOLONG),
#endif
#ifdef ENOLINK
    JS_CONSTANT_NONENUMERABLE(ENOLINK),
#endif
#ifdef EPROTO
    JS_CONSTANT_NONENUMERABLE(EPROTO),
#endif
#ifdef ENOTSOCK
    JS_CONSTANT_NONENUMERABLE(ENOTSOCK),
#endif
#ifdef EDESTADDRREQ
    JS_CONSTANT_NONENUMERABLE(EDESTADDRREQ),
#endif
#ifdef EMSGSIZE
    JS_CONSTANT_NONENUMERABLE(EMSGSIZE),
#endif
#ifdef EPROTOTYPE
    JS_CONSTANT_NONENUMERABLE(EPROTOTYPE),
#endif
#ifdef ENOPROTOOPT
    JS_CONSTANT_NONENUMERABLE(ENOPROTOOPT),
#endif
#ifdef EPROTONOSUPPORT
    JS_CONSTANT_NONENUMERABLE(EPROTONOSUPPORT),
#endif
#ifdef EOPNOTSUPP
    JS_CONSTANT_NONENUMERABLE(EOPNOTSUPP),
#endif
#ifdef EAFNOSUPPORT
    JS_CONSTANT_NONENUMERABLE(EAFNOSUPPORT),
#endif
#ifdef EADDRINUSE
    JS_CONSTANT_NONENUMERABLE(EADDRINUSE),
#endif
#ifdef EADDRNOTAVAIL
    JS_CONSTANT_NONENUMERABLE(EADDRNOTAVAIL),
#endif
#ifdef ENETDOWN
    JS_CONSTANT_NONENUMERABLE(ENETDOWN),
#endif
#ifdef ENETUNREACH
    JS_CONSTANT_NONENUMERABLE(ENETUNREACH),
#endif
#ifdef ECONNABORTED
    JS_CONSTANT_NONENUMERABLE(ECONNABORTED),
#endif
#ifdef ECONNRESET
    JS_CONSTANT_NONENUMERABLE(ECONNRESET),
#endif
#ifdef ENOBUFS
    JS_CONSTANT_NONENUMERABLE(ENOBUFS),
#endif
#ifdef EISCONN
    JS_CONSTANT_NONENUMERABLE(EISCONN),
#endif
#ifdef ENOTCONN
    JS_CONSTANT_NONENUMERABLE(ENOTCONN),
#endif
#ifdef ETIMEDOUT
    JS_CONSTANT_NONENUMERABLE(ETIMEDOUT),
#endif
#ifdef ECONNREFUSED
    JS_CONSTANT_NONENUMERABLE(ECONNREFUSED),
#endif
#ifdef EHOSTUNREACH
    JS_CONSTANT_NONENUMERABLE(EHOSTUNREACH),
#endif
#ifdef EALREADY
    JS_CONSTANT_NONENUMERABLE(EALREADY),
#endif
#ifdef EINPROGRESS
    JS_CONSTANT_NONENUMERABLE(EINPROGRESS),
#endif
};

static const JSCFunctionListEntry js_sockets_defines[] = {
#ifdef AF_UNSPEC
    JS_CONSTANT_NONENUMERABLE(AF_UNSPEC),
#endif
#ifdef AF_LOCAL
    JS_CONSTANT_NONENUMERABLE(AF_LOCAL),
#endif
#ifdef AF_UNIX
    JS_CONSTANT_NONENUMERABLE(AF_UNIX),
#endif
#ifdef AF_INET
    JS_CONSTANT_NONENUMERABLE(AF_INET),
#endif
#ifdef AF_AX25
    JS_CONSTANT_NONENUMERABLE(AF_AX25),
#endif
#ifdef AF_IPX
    JS_CONSTANT_NONENUMERABLE(AF_IPX),
#endif
#ifdef AF_APPLETALK
    JS_CONSTANT_NONENUMERABLE(AF_APPLETALK),
#endif
#ifdef AF_NETROM
    JS_CONSTANT_NONENUMERABLE(AF_NETROM),
#endif
#ifdef AF_BRIDGE
    JS_CONSTANT_NONENUMERABLE(AF_BRIDGE),
#endif
#ifdef AF_ATMPVC
    JS_CONSTANT_NONENUMERABLE(AF_ATMPVC),
#endif
#ifdef AF_X25
    JS_CONSTANT_NONENUMERABLE(AF_X25),
#endif
#ifdef AF_INET6
    JS_CONSTANT_NONENUMERABLE(AF_INET6),
#endif
#ifdef AF_ROSE
    JS_CONSTANT_NONENUMERABLE(AF_ROSE),
#endif
#ifdef AF_DECnet
    JS_CONSTANT_NONENUMERABLE(AF_DECnet),
#endif
#ifdef AF_NETBEUI
    JS_CONSTANT_NONENUMERABLE(AF_NETBEUI),
#endif
#ifdef AF_SECURITY
    JS_CONSTANT_NONENUMERABLE(AF_SECURITY),
#endif
#ifdef AF_KEY
    JS_CONSTANT_NONENUMERABLE(AF_KEY),
#endif
#ifdef AF_NETLINK
    JS_CONSTANT_NONENUMERABLE(AF_NETLINK),
#endif
#ifdef AF_ROUTE
    JS_CONSTANT_NONENUMERABLE(AF_ROUTE),
#endif
#ifdef AF_PACKET
    JS_CONSTANT_NONENUMERABLE(AF_PACKET),
#endif
#ifdef AF_ASH
    JS_CONSTANT_NONENUMERABLE(AF_ASH),
#endif
#ifdef AF_ECONET
    JS_CONSTANT_NONENUMERABLE(AF_ECONET),
#endif
#ifdef AF_ATMSVC
    JS_CONSTANT_NONENUMERABLE(AF_ATMSVC),
#endif
#ifdef AF_SNA
    JS_CONSTANT_NONENUMERABLE(AF_SNA),
#endif
#ifdef AF_IRDA
    JS_CONSTANT_NONENUMERABLE(AF_IRDA),
#endif
#ifdef AF_PPPOX
    JS_CONSTANT_NONENUMERABLE(AF_PPPOX),
#endif
#ifdef AF_WANPIPE
    JS_CONSTANT_NONENUMERABLE(AF_WANPIPE),
#endif
#ifdef AF_LLC
    JS_CONSTANT_NONENUMERABLE(AF_LLC),
#endif
#ifdef AF_IB
    JS_CONSTANT_NONENUMERABLE(AF_IB),
#endif
#ifdef AF_MPLS
    JS_CONSTANT_NONENUMERABLE(AF_MPLS),
#endif
#ifdef AF_CAN
    JS_CONSTANT_NONENUMERABLE(AF_CAN),
#endif
#ifdef AF_TIPC
    JS_CONSTANT_NONENUMERABLE(AF_TIPC),
#endif
#ifdef AF_BLUETOOTH
    JS_CONSTANT_NONENUMERABLE(AF_BLUETOOTH),
#endif
#ifdef AF_IUCV
    JS_CONSTANT_NONENUMERABLE(AF_IUCV),
#endif
#ifdef AF_RXRPC
    JS_CONSTANT_NONENUMERABLE(AF_RXRPC),
#endif
#ifdef AF_ISDN
    JS_CONSTANT_NONENUMERABLE(AF_ISDN),
#endif
#ifdef AF_PHONET
    JS_CONSTANT_NONENUMERABLE(AF_PHONET),
#endif
#ifdef AF_IEEE802154
    JS_CONSTANT_NONENUMERABLE(AF_IEEE802154),
#endif
#ifdef AF_CAIF
    JS_CONSTANT_NONENUMERABLE(AF_CAIF),
#endif
#ifdef AF_ALG
    JS_CONSTANT_NONENUMERABLE(AF_ALG),
#endif
#ifdef AF_NFC
    JS_CONSTANT_NONENUMERABLE(AF_NFC),
#endif
#ifdef AF_VSOCK
    JS_CONSTANT_NONENUMERABLE(AF_VSOCK),
#endif
#ifdef AF_MAX
    JS_CONSTANT_NONENUMERABLE(AF_MAX),
#endif
#ifdef IPPROTO_HOPOPTS
    JS_CONSTANT_NONENUMERABLE(IPPROTO_HOPOPTS),
#endif
#ifdef IPPROTO_IP
    JS_CONSTANT_NONENUMERABLE(IPPROTO_IP),
#endif
#ifdef IPPROTO_ICMP
    JS_CONSTANT_NONENUMERABLE(IPPROTO_ICMP),
#endif
#ifdef IPPROTO_IGMP
    JS_CONSTANT_NONENUMERABLE(IPPROTO_IGMP),
#endif
#ifdef IPPROTO_IPIP
    JS_CONSTANT_NONENUMERABLE(IPPROTO_IPIP),
#endif
#ifdef IPPROTO_TCP
    JS_CONSTANT_NONENUMERABLE(IPPROTO_TCP),
#endif
#ifdef IPPROTO_EGP
    JS_CONSTANT_NONENUMERABLE(IPPROTO_EGP),
#endif
#ifdef IPPROTO_PUP
    JS_CONSTANT_NONENUMERABLE(IPPROTO_PUP),
#endif
#ifdef IPPROTO_UDP
    JS_CONSTANT_NONENUMERABLE(IPPROTO_UDP),
#endif
#ifdef IPPROTO_IDP
    JS_CONSTANT_NONENUMERABLE(IPPROTO_IDP),
#endif
#ifdef IPPROTO_IPV6
    JS_CONSTANT_NONENUMERABLE(IPPROTO_IPV6),
#endif
#ifdef IPPROTO_ROUTING
    JS_CONSTANT_NONENUMERABLE(IPPROTO_ROUTING),
#endif
#ifdef IPPROTO_FRAGMENT
    JS_CONSTANT_NONENUMERABLE(IPPROTO_FRAGMENT),
#endif
#ifdef IPPROTO_RSVP
    JS_CONSTANT_NONENUMERABLE(IPPROTO_RSVP),
#endif
#ifdef IPPROTO_GRE
    JS_CONSTANT_NONENUMERABLE(IPPROTO_GRE),
#endif
#ifdef IPPROTO_ESP
    JS_CONSTANT_NONENUMERABLE(IPPROTO_ESP),
#endif
#ifdef IPPROTO_AH
    JS_CONSTANT_NONENUMERABLE(IPPROTO_AH),
#endif
#ifdef IPPROTO_ICMPV6
    JS_CONSTANT_NONENUMERABLE(IPPROTO_ICMPV6),
#endif
#ifdef IPPROTO_NONE
    JS_CONSTANT_NONENUMERABLE(IPPROTO_NONE),
#endif
#ifdef IPPROTO_DSTOPTS
    JS_CONSTANT_NONENUMERABLE(IPPROTO_DSTOPTS),
#endif
#ifdef IPPROTO_PIM
    JS_CONSTANT_NONENUMERABLE(IPPROTO_PIM),
#endif
#ifdef IPPROTO_COMP
    JS_CONSTANT_NONENUMERABLE(IPPROTO_COMP),
#endif
#ifdef IPPROTO_SCTP
    JS_CONSTANT_NONENUMERABLE(IPPROTO_SCTP),
#endif
#ifdef IPPROTO_UDPLITE
    JS_CONSTANT_NONENUMERABLE(IPPROTO_UDPLITE),
#endif
#ifdef IPPROTO_RAW
    JS_CONSTANT_NONENUMERABLE(IPPROTO_RAW),
#endif
#ifdef PF_UNSPEC
    JS_CONSTANT_NONENUMERABLE(PF_UNSPEC),
#endif
#ifdef PF_LOCAL
    JS_CONSTANT_NONENUMERABLE(PF_LOCAL),
#endif
#ifdef PF_UNIX
    JS_CONSTANT_NONENUMERABLE(PF_UNIX),
#endif
#ifdef PF_INET
    JS_CONSTANT_NONENUMERABLE(PF_INET),
#endif
#ifdef PF_AX25
    JS_CONSTANT_NONENUMERABLE(PF_AX25),
#endif
#ifdef PF_IPX
    JS_CONSTANT_NONENUMERABLE(PF_IPX),
#endif
#ifdef PF_APPLETALK
    JS_CONSTANT_NONENUMERABLE(PF_APPLETALK),
#endif
#ifdef PF_NETROM
    JS_CONSTANT_NONENUMERABLE(PF_NETROM),
#endif
#ifdef PF_BRIDGE
    JS_CONSTANT_NONENUMERABLE(PF_BRIDGE),
#endif
#ifdef PF_ATMPVC
    JS_CONSTANT_NONENUMERABLE(PF_ATMPVC),
#endif
#ifdef PF_X25
    JS_CONSTANT_NONENUMERABLE(PF_X25),
#endif
#ifdef PF_INET6
    JS_CONSTANT_NONENUMERABLE(PF_INET6),
#endif
#ifdef PF_ROSE
    JS_CONSTANT_NONENUMERABLE(PF_ROSE),
#endif
#ifdef PF_DECnet
    JS_CONSTANT_NONENUMERABLE(PF_DECnet),
#endif
#ifdef PF_NETBEUI
    JS_CONSTANT_NONENUMERABLE(PF_NETBEUI),
#endif
#ifdef PF_SECURITY
    JS_CONSTANT_NONENUMERABLE(PF_SECURITY),
#endif
#ifdef PF_KEY
    JS_CONSTANT_NONENUMERABLE(PF_KEY),
#endif
#ifdef PF_NETLINK
    JS_CONSTANT_NONENUMERABLE(PF_NETLINK),
#endif
#ifdef PF_ROUTE
    JS_CONSTANT_NONENUMERABLE(PF_ROUTE),
#endif
#ifdef PF_PACKET
    JS_CONSTANT_NONENUMERABLE(PF_PACKET),
#endif
#ifdef PF_ASH
    JS_CONSTANT_NONENUMERABLE(PF_ASH),
#endif
#ifdef PF_ECONET
    JS_CONSTANT_NONENUMERABLE(PF_ECONET),
#endif
#ifdef PF_ATMSVC
    JS_CONSTANT_NONENUMERABLE(PF_ATMSVC),
#endif
#ifdef PF_SNA
    JS_CONSTANT_NONENUMERABLE(PF_SNA),
#endif
#ifdef PF_IRDA
    JS_CONSTANT_NONENUMERABLE(PF_IRDA),
#endif
#ifdef PF_PPPOX
    JS_CONSTANT_NONENUMERABLE(PF_PPPOX),
#endif
#ifdef PF_WANPIPE
    JS_CONSTANT_NONENUMERABLE(PF_WANPIPE),
#endif
#ifdef PF_LLC
    JS_CONSTANT_NONENUMERABLE(PF_LLC),
#endif
#ifdef PF_IB
    JS_CONSTANT_NONENUMERABLE(PF_IB),
#endif
#ifdef PF_MPLS
    JS_CONSTANT_NONENUMERABLE(PF_MPLS),
#endif
#ifdef PF_CAN
    JS_CONSTANT_NONENUMERABLE(PF_CAN),
#endif
#ifdef PF_TIPC
    JS_CONSTANT_NONENUMERABLE(PF_TIPC),
#endif
#ifdef PF_BLUETOOTH
    JS_CONSTANT_NONENUMERABLE(PF_BLUETOOTH),
#endif
#ifdef PF_IUCV
    JS_CONSTANT_NONENUMERABLE(PF_IUCV),
#endif
#ifdef PF_RXRPC
    JS_CONSTANT_NONENUMERABLE(PF_RXRPC),
#endif
#ifdef PF_ISDN
    JS_CONSTANT_NONENUMERABLE(PF_ISDN),
#endif
#ifdef PF_PHONET
    JS_CONSTANT_NONENUMERABLE(PF_PHONET),
#endif
#ifdef PF_IEEE802154
    JS_CONSTANT_NONENUMERABLE(PF_IEEE802154),
#endif
#ifdef PF_CAIF
    JS_CONSTANT_NONENUMERABLE(PF_CAIF),
#endif
#ifdef PF_ALG
    JS_CONSTANT_NONENUMERABLE(PF_ALG),
#endif
#ifdef PF_NFC
    JS_CONSTANT_NONENUMERABLE(PF_NFC),
#endif
#ifdef PF_VSOCK
    JS_CONSTANT_NONENUMERABLE(PF_VSOCK),
#endif
#ifdef PF_MAX
    JS_CONSTANT_NONENUMERABLE(PF_MAX),
#endif
#ifdef POLLIN
    JS_CONSTANT_NONENUMERABLE(POLLIN),
#endif
#ifdef POLLPRI
    JS_CONSTANT_NONENUMERABLE(POLLPRI),
#endif
#ifdef POLLOUT
    JS_CONSTANT_NONENUMERABLE(POLLOUT),
#endif
#ifdef POLLERR
    JS_CONSTANT_NONENUMERABLE(POLLERR),
#endif
#ifdef POLLHUP
    JS_CONSTANT_NONENUMERABLE(POLLHUP),
#endif
#ifdef POLLNVAL
    JS_CONSTANT_NONENUMERABLE(POLLNVAL),
#endif
#ifdef POLLRDNORM
    JS_CONSTANT_NONENUMERABLE(POLLRDNORM),
#endif
#ifdef POLLRDBAND
    JS_CONSTANT_NONENUMERABLE(POLLRDBAND),
#endif
#ifdef POLLWRNORM
    JS_CONSTANT_NONENUMERABLE(POLLWRNORM),
#endif
#ifdef POLLWRBAND
    JS_CONSTANT_NONENUMERABLE(POLLWRBAND),
#endif
#ifdef SHUT_WR
    JS_CONSTANT_NONENUMERABLE(SHUT_WR),
#endif
#ifdef SHUT_RDWR
    JS_CONSTANT_NONENUMERABLE(SHUT_RDWR),
#endif
#ifdef SO_TYPE
    JS_CONSTANT_NONENUMERABLE(SO_TYPE),
#endif
#ifdef SO_DEBUG
    JS_CONSTANT_NONENUMERABLE(SO_DEBUG),
#endif
#ifdef SO_REUSEADDR
    JS_CONSTANT_NONENUMERABLE(SO_REUSEADDR),
#endif
#ifdef SO_ERROR
    JS_CONSTANT_NONENUMERABLE(SO_ERROR),
#endif
#ifdef SO_DONTROUTE
    JS_CONSTANT_NONENUMERABLE(SO_DONTROUTE),
#endif
#ifdef SO_BROADCAST
    JS_CONSTANT_NONENUMERABLE(SO_BROADCAST),
#endif
#ifdef SO_SNDBUF
    JS_CONSTANT_NONENUMERABLE(SO_SNDBUF),
#endif
#ifdef SO_RCVBUF
    JS_CONSTANT_NONENUMERABLE(SO_RCVBUF),
#endif
#ifdef SO_KEEPALIVE
    JS_CONSTANT_NONENUMERABLE(SO_KEEPALIVE),
#endif
#ifdef SO_OOBINLINE
    JS_CONSTANT_NONENUMERABLE(SO_OOBINLINE),
#endif
#ifdef SO_NO_CHECK
    JS_CONSTANT_NONENUMERABLE(SO_NO_CHECK),
#endif
#ifdef SO_PRIORITY
    JS_CONSTANT_NONENUMERABLE(SO_PRIORITY),
#endif
#ifdef SO_BSDCOMPAT
    JS_CONSTANT_NONENUMERABLE(SO_BSDCOMPAT),
#endif
#ifdef SO_REUSEPORT
    JS_CONSTANT_NONENUMERABLE(SO_REUSEPORT),
#endif
#ifdef SO_PASSCRED
    JS_CONSTANT_NONENUMERABLE(SO_PASSCRED),
#endif
#ifdef SO_PEERCRED
    JS_CONSTANT_NONENUMERABLE(SO_PEERCRED),
#endif
#ifdef SO_RCVLOWAT
    JS_CONSTANT_NONENUMERABLE(SO_RCVLOWAT),
#endif
#ifdef SO_SNDLOWAT
    JS_CONSTANT_NONENUMERABLE(SO_SNDLOWAT),
#endif
#ifdef SO_RCVTIMEO
    JS_CONSTANT_NONENUMERABLE(SO_RCVTIMEO),
#endif
#ifdef SO_SNDTIMEO
    JS_CONSTANT_NONENUMERABLE(SO_SNDTIMEO),
#endif
#ifdef SO_SECURITY_AUTHENTICATION
    JS_CONSTANT_NONENUMERABLE(SO_SECURITY_AUTHENTICATION),
#endif
#ifdef SO_SECURITY_ENCRYPTION_TRANSPORT
    JS_CONSTANT_NONENUMERABLE(SO_SECURITY_ENCRYPTION_TRANSPORT),
#endif
#ifdef SO_SECURITY_ENCRYPTION_NETWORK
    JS_CONSTANT_NONENUMERABLE(SO_SECURITY_ENCRYPTION_NETWORK),
#endif
#ifdef SO_BINDTODEVICE
    JS_CONSTANT_NONENUMERABLE(SO_BINDTODEVICE),
#endif
#ifdef SO_ATTACH_FILTER
    JS_CONSTANT_NONENUMERABLE(SO_ATTACH_FILTER),
#endif
#ifdef SO_GET_FILTER
    JS_CONSTANT_NONENUMERABLE(SO_GET_FILTER),
#endif
#ifdef SO_DETACH_BPF
    JS_CONSTANT_NONENUMERABLE(SO_DETACH_BPF),
#endif
#ifdef SO_DETACH_FILTER
    JS_CONSTANT_NONENUMERABLE(SO_DETACH_FILTER),
#endif
#ifdef SO_PEERNAME
    JS_CONSTANT_NONENUMERABLE(SO_PEERNAME),
#endif
#ifdef SO_TIMESTAMP
    JS_CONSTANT_NONENUMERABLE(SO_TIMESTAMP),
#endif
#ifdef SO_ACCEPTCONN
    JS_CONSTANT_NONENUMERABLE(SO_ACCEPTCONN),
#endif
#ifdef SO_PEERSEC
    JS_CONSTANT_NONENUMERABLE(SO_PEERSEC),
#endif
#ifdef SO_SNDBUFFORCE
    JS_CONSTANT_NONENUMERABLE(SO_SNDBUFFORCE),
#endif
#ifdef SO_RCVBUFFORCE
    JS_CONSTANT_NONENUMERABLE(SO_RCVBUFFORCE),
#endif
#ifdef SO_PASSSEC
    JS_CONSTANT_NONENUMERABLE(SO_PASSSEC),
#endif
#ifdef SO_TIMESTAMPNS
    JS_CONSTANT_NONENUMERABLE(SO_TIMESTAMPNS),
#endif
#ifdef SO_MARK
    JS_CONSTANT_NONENUMERABLE(SO_MARK),
#endif
#ifdef SO_TIMESTAMPING
    JS_CONSTANT_NONENUMERABLE(SO_TIMESTAMPING),
#endif
#ifdef SO_PROTOCOL
    JS_CONSTANT_NONENUMERABLE(SO_PROTOCOL),
#endif
#ifdef SO_DOMAIN
    JS_CONSTANT_NONENUMERABLE(SO_DOMAIN),
#endif
#ifdef SO_RXQ_OVFL
    JS_CONSTANT_NONENUMERABLE(SO_RXQ_OVFL),
#endif
#ifdef SO_WIFI_STATUS
    JS_CONSTANT_NONENUMERABLE(SO_WIFI_STATUS),
#endif
#ifdef SO_PEEK_OFF
    JS_CONSTANT_NONENUMERABLE(SO_PEEK_OFF),
#endif
#ifdef SO_NOFCS
    JS_CONSTANT_NONENUMERABLE(SO_NOFCS),
#endif
#ifdef SO_LOCK_FILTER
    JS_CONSTANT_NONENUMERABLE(SO_LOCK_FILTER),
#endif
#ifdef SO_SELECT_ERR_QUEUE
    JS_CONSTANT_NONENUMERABLE(SO_SELECT_ERR_QUEUE),
#endif
#ifdef SO_BUSY_POLL
    JS_CONSTANT_NONENUMERABLE(SO_BUSY_POLL),
#endif
#ifdef SO_MAX_PACING_RATE
    JS_CONSTANT_NONENUMERABLE(SO_MAX_PACING_RATE),
#endif
#ifdef SO_BPF_EXTENSIONS
    JS_CONSTANT_NONENUMERABLE(SO_BPF_EXTENSIONS),
#endif
#ifdef SO_INCOMING_CPU
    JS_CONSTANT_NONENUMERABLE(SO_INCOMING_CPU),
#endif
#ifdef SO_ATTACH_BPF
    JS_CONSTANT_NONENUMERABLE(SO_ATTACH_BPF),
#endif
#ifdef SOCK_STREAM
    JS_CONSTANT_NONENUMERABLE(SOCK_STREAM),
#endif
#ifdef SOCK_DGRAM
    JS_CONSTANT_NONENUMERABLE(SOCK_DGRAM),
#endif
#ifdef SOCK_RAW
    JS_CONSTANT_NONENUMERABLE(SOCK_RAW),
#endif
#ifdef SOCK_RDM
    JS_CONSTANT_NONENUMERABLE(SOCK_RDM),
#endif
#ifdef SOCK_SEQPACKET
    JS_CONSTANT_NONENUMERABLE(SOCK_SEQPACKET),
#endif
#ifdef SOCK_DCCP
    JS_CONSTANT_NONENUMERABLE(SOCK_DCCP),
#endif
#ifdef SOCK_PACKET
    JS_CONSTANT_NONENUMERABLE(SOCK_PACKET),
#endif
#ifdef SOCK_NONBLOCK
    JS_CONSTANT_NONENUMERABLE(SOCK_NONBLOCK),
#endif
#ifdef SOCK_CLOEXEC
    JS_CONSTANT_NONENUMERABLE(SOCK_CLOEXEC),
#endif
#ifdef SOL_SOCKET
    JS_CONSTANT_NONENUMERABLE(SOL_SOCKET),
#endif
#ifdef SOL_IPV6
    JS_CONSTANT_NONENUMERABLE(SOL_IPV6),
#endif
#ifdef SOL_ICMPV6
    JS_CONSTANT_NONENUMERABLE(SOL_ICMPV6),
#endif
#ifdef SOL_RAW
    JS_CONSTANT_NONENUMERABLE(SOL_RAW),
#endif
#ifdef SOL_DECNET
    JS_CONSTANT_NONENUMERABLE(SOL_DECNET),
#endif
#ifdef SOL_PACKET
    JS_CONSTANT_NONENUMERABLE(SOL_PACKET),
#endif
#ifdef SOL_ATM
    JS_CONSTANT_NONENUMERABLE(SOL_ATM),
#endif
#ifdef SOL_IRDA
    JS_CONSTANT_NONENUMERABLE(SOL_IRDA),
#endif

    JS_CONSTANT(MSG_EOR),
    JS_CONSTANT(MSG_TRUNC),
    JS_CONSTANT(MSG_CTRUNC),
    JS_CONSTANT(MSG_OOB),
    JS_CONSTANT(MSG_ERRQUEUE),

    JS_CONSTANT(SHUT_RD),
    JS_CONSTANT(SHUT_WR),
    JS_CONSTANT(SHUT_RDWR),
};

static int
js_sockets_init(JSContext* ctx, JSModuleDef* m) {
  js_syscallerror_init(ctx, m);

  JS_NewClass(JS_GetRuntime(ctx), js_sockaddr_class_id, &js_sockaddr_class);

  sockaddr_ctor = JS_NewCFunction2(ctx, js_sockaddr_constructor, "SockAddr", 1, JS_CFUNC_constructor, 0);
  sockaddr_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, sockaddr_proto, js_sockaddr_proto_funcs, countof(js_sockaddr_proto_funcs));

  JS_SetClassProto(ctx, js_sockaddr_class_id, sockaddr_proto);
  JS_SetConstructor(ctx, sockaddr_ctor, sockaddr_proto);

  JS_NewClassID(&js_socket_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_socket_class_id, &js_socket_class);

  socket_ctor = JS_NewCFunction2(ctx, js_socket_constructor, "Socket", 1, JS_CFUNC_constructor, 0);
  socket_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, socket_proto, js_socket_proto_funcs, countof(js_socket_proto_funcs));
  JS_SetPropertyFunctionList(ctx, socket_ctor, js_socket_static_funcs, countof(js_socket_static_funcs));
  // JS_SetPropertyFunctionList(ctx, socket_ctor, js_sockets_errnos, countof(js_sockets_errnos));
  JS_SetPropertyFunctionList(ctx, socket_ctor, js_sockets_defines, countof(js_sockets_defines));

  JS_SetClassProto(ctx, js_socket_class_id, socket_proto);
  JS_SetConstructor(ctx, socket_ctor, socket_proto);

  JS_NewClassID(&js_asyncsocket_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_asyncsocket_class_id, &js_asyncsocket_class);

  asyncsocket_ctor = JS_NewCFunction2(ctx, js_asyncsocket_constructor, "AsyncSocket", 1, JS_CFUNC_constructor, 0);
  asyncsocket_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, asyncsocket_proto, js_asyncsocket_proto_funcs, countof(js_asyncsocket_proto_funcs));
  JS_SetPropertyFunctionList(ctx, asyncsocket_ctor, js_socket_static_funcs, countof(js_socket_static_funcs));
  // JS_SetPropertyFunctionList(ctx, asyncsocket_ctor, js_sockets_errnos,
  // countof(js_sockets_errnos));
  JS_SetPropertyFunctionList(ctx, asyncsocket_ctor, js_sockets_defines, countof(js_sockets_defines));

  JS_SetClassProto(ctx, js_asyncsocket_class_id, asyncsocket_proto);
  JS_SetConstructor(ctx, asyncsocket_ctor, asyncsocket_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "SockAddr", sockaddr_ctor);
    JS_SetModuleExport(ctx, m, "Socket", socket_ctor);
    JS_SetModuleExport(ctx, m, "AsyncSocket", asyncsocket_ctor);

    const char* module_name = module_namecstr(ctx, m);

    if(!strcmp(module_name, "socket"))
      JS_SetModuleExport(ctx, m, "default", socket_ctor);

    JS_FreeCString(ctx, module_name);
    JS_SetModuleExportList(ctx, m, js_sockets_funcs, countof(js_sockets_funcs));
    JS_SetModuleExportList(ctx, m, js_sockets_defines, countof(js_sockets_defines));
    // JS_SetModuleExportList(ctx, m, js_sockets_errnos, countof(js_sockets_errnos));
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

  if((m = JS_NewCModule(ctx, module_name, js_sockets_init))) {
    JS_AddModuleExport(ctx, m, "SyscallError");
    JS_AddModuleExport(ctx, m, "SockAddr");
    JS_AddModuleExport(ctx, m, "Socket");
    JS_AddModuleExport(ctx, m, "AsyncSocket");

    size_t n = str_rchr(module_name, '/');

    if(module_name[n])
      ++n;

    size_t len = str_rchr(&module_name[n], '.');

    if(!strncmp(&module_name[n], "sockets", len)) {
      // JS_AddModuleExport(ctx, m, "default");
      JS_AddModuleExportList(ctx, m, js_sockets_funcs, countof(js_sockets_funcs));
      JS_AddModuleExportList(ctx, m, js_sockets_defines, countof(js_sockets_defines));
      // JS_AddModuleExportList(ctx, m, js_sockets_errnos, countof(js_sockets_errnos));
    }
  }

  return m;
}

/**
 * @}
 */
