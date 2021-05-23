#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "quickjs-internal.h"
#include "utils.h"
#include <sys/socket.h>
#include <netinet/in.h>

#define JS_CONSTANT(name) JS_PROP_INT32_DEF(#name, name, JS_PROP_ENUMERABLE)

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

  JS_ToInt32(ctx, &af, argv[0]);
  if(argc >= 2)
    JS_ToInt32(ctx, &type, argv[1]);

  if(argc >= 3)
    JS_ToInt32(ctx, &proto, argv[2]);

  ret = JS_NewInt64(ctx, socket(af, type, proto));

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

static int
js_sockets_init(JSContext* ctx, JSModuleDef* m) {

  if(m) {
    JS_SetModuleExportList(ctx, m, js_sockets_funcs, countof(js_sockets_funcs));
  }

  return 0;
}

#if defined(JS_SHARED_LIBRARY) && defined(JS_MISC_MODULE)
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_sockets
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_sockets_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_sockets_funcs, countof(js_sockets_funcs));
  return m;
}
