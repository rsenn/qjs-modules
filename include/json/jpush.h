#ifndef JPUSH_H
#define JPUSH_H

#include <quickjs.h>
#include "json/jread.h"
#include "json/jbuilder.h"
#include "json/jconnect.h"
#include "json/jsink.h"
#include "location.h"

/**
 * \addtogroup json
 * @{
 */

/* JsonPushParser: the push parser of the JS class, jr_feed() into a builder, JS callbacks or a sink.
 *
 *   mode               events go to
 *   builder (default)  a JsonBuilder, root and path read them
 *   callbacks          the callback methods of the options object, the builder sees them too
 *   connected          the sink of jpush_connect(), nothing else
 *
 * an error event makes a SyntaxError (kept as last_error, passed to onError of the options). */
typedef struct PushParser {
  JSContext* ctx;
  jr_state_t jrs;
  JsonBuilder builder;
  JsonEventSink sink; /* jbuild_sink(&builder) */
  JsonJsCallbacks cb;
  unsigned use_builder : 1, connected : 1;
  Location* loc;
  JSValue last_error; /* SyntaxError of the latest error, or JS_UNDEFINED */
  JsonEventSink ext;  /* connect(): the sink that gets every event */
  JsonJsSink* ext_adapter;
  JSValue ext_obj; /* the object behind ext, kept alive */
} JsonPushParser;

/* jpush_init: sets up pp with the dialect of the options object (any value, others have none).
 * returns 0, or -1 with an exception pending and nothing to free. */
int jpush_init(JsonPushParser* pp, JSContext* ctx, JSValueConst options);

/* jpush_free: releases everything pp holds, not pp itself. */
void jpush_free(JsonPushParser* pp, JSRuntime* rt);

/* jpush_feed: jr_feed() of a chunk to the handler of the mode, and the position of the chunk.
 * returns 0, or the first negative result of a sink. */
ssize_t jpush_feed(JsonPushParser* pp, const uint8_t* data, size_t len);

/* jpush_end: jr_end() to the handler of the mode. returns 0, or the first negative result of a sink. */
ssize_t jpush_end(JsonPushParser* pp);

/* jpush_connect: from now on every event goes to the sink behind obj and nothing else.
 * returns 0, or -1 with an exception pending. */
int jpush_connect(JsonPushParser* pp, JSContext* ctx, JSValueConst obj);

/* jpush_throw: throws the latest error (position and code from jread), unless the options object
 * has onError, which already received it: returns JS_UNDEFINED then. */
JSValue jpush_throw(JSContext* ctx, JsonPushParser* pp, const char* msg);

/** @} */
#endif /* JPUSH_H */
