#define _GNU_SOURCE 1

/* xpush.c: XMLPushParser, the push parser of the JS class without the JS methods.
 * depends on: xread.h (the lexer), xbuilder.h, xconnect.h (callbacks, sinks), quickjs-location.h.
 * rule: one handler per mode, an error event becomes a SyntaxError once, whichever mode. */

#include "xml/xpush.h"
#include "xml/xevent-js.h"
#include "quickjs-location.h"
#include "utils.h"
#include <string.h>

/**
 * \addtogroup xml
 * @{
 */

/* xpush_report: builds the SyntaxError for a failure at `pos`, keeps it as
 * `last_error` and passes it to `onParseError(err)`; throws nothing.
 *
 * returns TRUE when onParseError exists (the error is handled). */
static BOOL
xpush_report(XMLPushParser* pp, const SourcePos* pos, size_t length, const char* code, const char* msg) {
  JSContext* ctx = pp->ctx;
  JSValue err = js_syntax_error_at(ctx, "<xml>", pos, length, code, msg);

  if(JS_IsException(err)) {
    JS_FreeValue(ctx, JS_GetException(ctx));
    return FALSE;
  }

  JS_FreeValue(ctx, pp->last_error);
  pp->last_error = JS_DupValue(ctx, err);

  if(JS_IsFunction(ctx, pp->cb.on_parse_error)) {
    JSValue ret = JS_Call(ctx, pp->cb.on_parse_error, pp->cb.this_obj, 1, &err);

    JS_FreeValue(ctx, ret); /* an exception from the callback is dropped */
    JS_FreeValue(ctx, JS_GetException(ctx));
  }

  JS_FreeValue(ctx, err);
  return JS_IsFunction(ctx, pp->cb.on_parse_error);
}

/* xpush_event_build: the handler of the builder mode, an error goes to onParseError. */
static ssize_t
xpush_event_build(void* user, const XmlEvent* ev) {
  XMLPushParser* pp = user;

  if(ev->type == XE_ERROR) {
    xpush_report(pp, &ev->at, ev->span, ev->code, "XMLPushParser: malformed input");
    return 0;
  }

  return xevent_send(&pp->sink, ev);
}

/* xpush_event: the handler of the callback mode, an error goes to onParseError and
 * to the error callback. */
static ssize_t
xpush_event(void* user, const XmlEvent* ev) {
  XMLPushParser* pp = user;

  if(ev->type == XE_ERROR)
    xpush_report(pp, &ev->at, ev->span, ev->code, "XMLPushParser: malformed input");

  return js_xmlcallbacks_event(&pp->cb, ev);
}

/* xpush_event_ext: the handler after connect(), every event goes to the sink. */
static ssize_t
xpush_event_ext(void* user, const XmlEvent* ev) {
  XMLPushParser* pp = user;

  if(ev->type == XE_ERROR)
    xpush_report(pp, &ev->at, ev->span, ev->code, "XMLPushParser: malformed input");

  return xevent_send(&pp->ext, ev);
}

/* xpush_handler: the handler for the mode the parser is in. */
static XmlEventHandler*
xpush_handler(const XMLPushParser* pp) {
  return pp->connected ? xpush_event_ext : pp->use_builder ? xpush_event_build : xpush_event;
}


JSValue
xpush_write(JSContext* ctx, XMLPushParser* pp, const uint8_t* data, size_t len) {
  ssize_t r = xr_feed(&pp->xrs, (const char*)data, len, xpush_handler(pp), pp);

  if(r < 0)
    return js_xmlsink_failed(ctx, pp->ext_adapter != NULL, r);

  /* xr_feed() sets xrs.error and goes inert rather than reporting anything itself. With callbacks
   * the error event already reached them, so only the builder mode has to throw: .root would
   * silently stop growing otherwise. */
  if(pp->xrs.error && pp->use_builder && !JS_IsFunction(ctx, pp->cb.on_parse_error))
    return js_throw_syntax_error_at(ctx, "<xml>", &pp->xrs.err_pos, pp->xrs.err_len, xr_error_name(pp->xrs.err_code), "XMLPushParser: malformed input");

  return JS_UNDEFINED;
}

JSValue
xpush_close(JSContext* ctx, XMLPushParser* pp) {
  ssize_t r = xr_end(&pp->xrs, xpush_handler(pp), pp);

  if(r < 0)
    return js_xmlsink_failed(ctx, pp->ext_adapter != NULL, r);

  if(pp->xrs.error && pp->use_builder && !JS_IsFunction(ctx, pp->cb.on_parse_error))
    return js_throw_syntax_error_at(ctx, "<xml>", &pp->xrs.err_pos, pp->xrs.err_len, xr_error_name(pp->xrs.err_code), "XMLPushParser: malformed or truncated input");

  if(pp->xrs.error) /* the error callback already ran, nothing left open to report */
    return JS_UNDEFINED;

  /* a tree whose elements are not all closed: "<a><b/>" is well-formed for xr_end() */
  if(pp->use_builder && xbuild_depth(&pp->builder)) {
    if(xpush_report(pp, &pp->xrs.pos, 0, "unexpected-end", "XMLPushParser: unexpected end of input (unclosed element)"))
      return JS_UNDEFINED;

    return js_throw_syntax_error_at(ctx, "<xml>", &pp->xrs.pos, 0, "unexpected-end", "XMLPushParser: unexpected end of input (unclosed element)");
  }

  return JS_UNDEFINED;
}

int
xpush_connect(XMLPushParser* pp, JSContext* ctx, JSValueConst obj) {
  XmlEventSink sink;
  XmlJsSink* adapter;

  if(js_xmlsink_resolve(ctx, obj, &sink, &adapter))
    return -1;

  JS_FreeValue(ctx, pp->ext_obj);
  js_xmlsink_from_js_free(ctx, pp->ext_adapter);
  pp->ext = sink;
  pp->ext_adapter = adapter;
  pp->ext_obj = JS_DupValue(ctx, obj);
  pp->connected = TRUE;
  return 0;
}

int
xpush_init(XMLPushParser* pp, JSContext* ctx, JSValueConst options) {
  memset(pp, 0, sizeof(*pp));
  pp->ctx = ctx;
  pp->last_error = JS_UNDEFINED;
  pp->ext_obj = JS_UNDEFINED;
  xr_state_init(&pp->xrs);
  xbuild_init(&pp->builder, ctx, 0);
  pp->sink = xbuild_sink(&pp->builder);
  js_xmlcallbacks_init(ctx, &pp->cb, options);

  /* no real callback given: build a tree (read through .root) instead of dropping the events */
  pp->use_builder = !js_xmlcallbacks_any(&pp->cb);
  return 0;
}

void
xpush_free(XMLPushParser* pp, JSRuntime* rt) {
  xr_state_free(&pp->xrs);
  xbuild_free(&pp->builder, rt);
  js_xmlcallbacks_free_rt(rt, &pp->cb);
  JS_FreeValueRT(rt, pp->last_error);
  JS_FreeValueRT(rt, pp->ext_obj);
  js_xmlsink_from_js_free_rt(rt, pp->ext_adapter);
}

/**
 * @}
 */
