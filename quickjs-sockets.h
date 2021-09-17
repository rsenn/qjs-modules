#ifndef QUICKJS_SOCKETS_H
#define QUICKJS_SOCKETS_H

#include "utils.h"

JSValue      js_socket_new_proto(JSContext*, JSValue, int fd);
JSValue      js_socket_new(JSContext*, int);
JSValue      js_socket_constructor(JSContext*, JSValue, int argc, JSValue argv[]);

#endif /* defined(QUICKJS_SOCKETS_H) */
