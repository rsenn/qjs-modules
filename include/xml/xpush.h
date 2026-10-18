#ifndef XPUSH_H
#define XPUSH_H

#include <quickjs.h>
#include "xml/xread.h"
#include "xml/xbuilder.h"
#include "xml/xconnect.h"
#include "xml/xsink.h"

/**
 * \addtogroup xml
 * @{
 */

/* XMLPushParser: the push parser of the JS class, xr_feed() into a builder, JS callbacks or a sink.
 *
 *   mode               events go to
 *   builder (default)  an XmlBuilder, root and path read them
 *   callbacks          the callback methods of the options object
 *   connected          the sink of xpush_connect(), nothing else
 *
 * an error event makes a SyntaxError (kept as last_error, passed to onParseError of the options). */
typedef struct xml_push_parser {
  JSContext* ctx;
  struct xr_state xrs;
  XmlJsCallbacks cb;
  JSValue last_error;     /* SyntaxError of the latest error, or JS_UNDEFINED */
  XmlBuilder builder;
  XmlEventSink sink; /* xbuild_sink(&builder) */
  BOOL use_builder; /* build {tagName, attributes, children} tree
                       instead of forwarding events */
  BOOL connected;   /* connect(): every event goes to ext */
  XmlEventSink ext;
  XmlJsSink* ext_adapter;
  JSValue ext_obj;  /* the object behind ext, kept alive */
} XMLPushParser;

/* xpush_init: sets up pp, the options object (any value, others have none) is also the this of its callbacks.
 * returns 0, or -1 with an exception pending and nothing to free. */
int xpush_init(XMLPushParser* pp, JSContext* ctx, JSValueConst options);

/* xpush_free: releases everything pp holds, not pp itself. */
void xpush_free(XMLPushParser* pp, JSRuntime* rt);

/* xpush_write: XMLPushParser.write() of one chunk (UTF-8 bytes).
 * returns undefined, or JS_EXCEPTION: the SyntaxError of a malformed input in builder mode
 * (callback modes report through onParseError), what a sink throws. */
JSValue xpush_write(JSContext* ctx, XMLPushParser* pp, const uint8_t* data, size_t len);

/* xpush_close: XMLPushParser.close(), the end of the input.
 * returns undefined, or JS_EXCEPTION as xpush_write() does, also for an element left open. */
JSValue xpush_close(JSContext* ctx, XMLPushParser* pp);

/* xpush_connect: from now on every event goes to the sink behind obj and nothing else.
 * returns 0, or -1 with an exception pending. */
int xpush_connect(XMLPushParser* pp, JSContext* ctx, JSValueConst obj);

/** @} */
#endif /* XPUSH_H */
