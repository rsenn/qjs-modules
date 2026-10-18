/* jpush.c: JsonPushParser, the push parser of the JS class without the JS methods.
 * depends on: jread.h (the tokenizer), jbuilder.h, jconnect.h (callbacks, sinks), location.h.
 * rule: one handler per mode, an error event becomes a SyntaxError once, whichever mode. */

#define _GNU_SOURCE 1

#include "json/jpush.h"
#include "json/jopts.h"
#include "json/jevent-js.h"
#include "utils.h"
#include "quickjs-location.h"
#include <string.h>

/**
 * \addtogroup json
 * @{
 */

/* jpush_onerror: the callback `onError` of the options object, or undefined;
 * refcount: a new reference. */
static JSValue
jpush_onerror(JsonPushParser* pp) {
  if(JS_IsUndefined(pp->cb.obj))
    return JS_UNDEFINED;

  JSValue fn = JS_GetPropertyStr(pp->ctx, pp->cb.obj, "onError");

  if(JS_IsFunction(pp->ctx, fn))
    return fn;

  JS_FreeValue(pp->ctx, fn);
  return JS_UNDEFINED;
}

/* jpush_call: calls fn, an exception it throws is dropped. */
static void
jpush_call(JSContext* ctx, JSValueConst fn, JSValueConst this_obj, int argc, JSValueConst argv[]) {
  JSValue ret = JS_Call(ctx, fn, this_obj, argc, argv);

  if(JS_IsException(ret))
    JS_FreeValue(ctx, JS_GetException(ctx));
  else
    JS_FreeValue(ctx, ret);
}

/* jpush_error: records the error event as last_error and passes it to
 * onError(err), runs for both the builder and the callback mode. */
static void
jpush_error(JsonPushParser* pp, const JsonEvent* ev) {
  JSContext* ctx = pp->ctx;
  const char* msg = ev->code && !strcmp(ev->code, "unexpected-end") ? "unexpected end of input" : "parse error";
  JSValue fn, err = js_syntax_error_at(ctx, "<json>", &ev->at, ev->span, ev->code, msg);

  if(JS_IsException(err)) {
    JS_FreeValue(ctx, JS_GetException(ctx));
    return;
  }

  JS_FreeValue(ctx, pp->last_error);
  pp->last_error = JS_DupValue(ctx, err);

  if(JS_IsFunction(ctx, (fn = jpush_onerror(pp))))
    jpush_call(ctx, fn, pp->cb.obj, 1, &err);

  JS_FreeValue(ctx, fn);
  JS_FreeValue(ctx, err);
}

/* jpush_event_build: the handler of the builder mode. */
static ssize_t
jpush_event_build(void* user, const JsonEvent* ev) {
  JsonPushParser* pp = user;

  if(ev->type == JSON_EVENT_ERROR) {
    jpush_error(pp, ev);
    return 0;
  }

  return jevent_send(&pp->sink, ev);
}

/* jpush_event_ext: the handler after connect(), every event goes to the sink. */
static ssize_t
jpush_event_ext(void* user, const JsonEvent* ev) {
  JsonPushParser* pp = user;

  if(ev->type == JSON_EVENT_ERROR)
    jpush_error(pp, ev);

  return jevent_send(&pp->ext, ev);
}

/* jpush_event: the handler of the callback mode, the builder sees every event too. */
static ssize_t
jpush_event(void* user, const JsonEvent* ev) {
  JsonPushParser* pp = user;
  ssize_t r = jpush_event_build(user, ev);

  js_jsoncallbacks_event(&pp->cb, ev);
  return r;
}

/* jpush_handler: the handler for the mode the parser is in. */
static JsonEventHandler*
jpush_handler(const JsonPushParser* pp) {
  return pp->connected ? jpush_event_ext : pp->use_builder ? jpush_event_build : jpush_event;
}

/* jpush_throw: throws the latest error (position and code from jread), unless
 * the options object has `onError`, which already received it; returns JS_UNDEFINED then. */
JSValue
jpush_throw(JSContext* ctx, JsonPushParser* pp, const char* msg) {
  JSValue fn = jpush_onerror(pp);
  BOOL handled = !JS_IsUndefined(fn);

  JS_FreeValue(ctx, fn);

  if(handled)
    return JS_UNDEFINED;

  if(!JS_IsUndefined(pp->last_error))
    return JS_Throw(ctx, JS_DupValue(ctx, pp->last_error));

  return js_throw_syntax_error_at(ctx, "<json>", &pp->jrs.err_pos, pp->jrs.err_len, jr_error_name(pp->jrs.err_code), msg);
}

ssize_t
jpush_feed(JsonPushParser* pp, const uint8_t* data, size_t len) {
  ssize_t r = jr_feed(&pp->jrs, (const char*)data, len, jpush_handler(pp), pp);

  if(pp->loc)
    location_count(pp->loc, data, len);

  return r;
}

ssize_t
jpush_end(JsonPushParser* pp) {
  return jr_end(&pp->jrs, jpush_handler(pp), pp);
}

int
jpush_connect(JsonPushParser* pp, JSContext* ctx, JSValueConst obj) {
  JsonEventSink sink;
  JsonJsSink* adapter;

  if(js_jsonsink_resolve(ctx, obj, &sink, &adapter))
    return -1;

  JS_FreeValue(ctx, pp->ext_obj);
  js_jsonsink_from_js_free(ctx, pp->ext_adapter);
  pp->ext = sink;
  pp->ext_adapter = adapter;
  pp->ext_obj = JS_DupValue(ctx, obj);
  pp->connected = TRUE;
  return 0;
}

int
jpush_init(JsonPushParser* pp, JSContext* ctx, JSValueConst options) {
  memset(pp, 0, sizeof(*pp));
  pp->ctx = ctx;
  pp->last_error = JS_UNDEFINED;
  pp->ext_obj = JS_UNDEFINED;
  jr_state_init(&pp->jrs);
  jbuild_init(&pp->builder, ctx);
  pp->sink = jbuild_sink(&pp->builder);
  js_jsoncallbacks_init(ctx, &pp->cb, options);

  if(!(pp->loc = location_new(ctx)))
    goto fail;

  location_zero(pp->loc);

  if(JS_IsObject(pp->cb.obj)) {
    int flags;

    if(jread_options_from_js(ctx, options, "JsonPushParser", &flags))
      goto fail;

    if(flags >= 0) {
      jr_state_free(&pp->jrs);
      jr_state_init_flags(&pp->jrs, flags);
    }
  }

  pp->use_builder = !js_jsoncallbacks_complete(&pp->cb);
  return 0;

fail:
  jpush_free(pp, JS_GetRuntime(ctx));
  return -1;
}

void
jpush_free(JsonPushParser* pp, JSRuntime* rt) {
  jr_state_free(&pp->jrs);
  jbuild_free(&pp->builder, rt);

  if(pp->loc)
    location_free(pp->loc, rt);

  js_jsoncallbacks_free_rt(rt, &pp->cb);
  JS_FreeValueRT(rt, pp->last_error);
  JS_FreeValueRT(rt, pp->ext_obj);
  js_jsonsink_from_js_free_rt(rt, pp->ext_adapter);
}

/**
 * @}
 */
