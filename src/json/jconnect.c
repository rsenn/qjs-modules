/* jconnect.c: js_jsonsink_resolve(), js_jsonsink_define_handshake(); the json handshake.
 * depends on: jsink.h (the JS-object adapter), utils.h (symbols).
 * rule: a sink from another module is found by the address it gives, never by class id. */

#define _GNU_SOURCE 1

#include "json/jconnect.h"
#include "json/jevent-js.h"
#include "utils.h"

/**
 * \addtogroup jsink
 * @{
 */

int
js_jsonsink_resolve(JSContext* ctx, JSValueConst obj, JsonEventSink* sink, JsonJsSink** adapter) {
  JSAtom atom;
  JSValue fn;

  *adapter = NULL;

  if(!JS_IsObject(obj)) {
    JS_ThrowTypeError(ctx, "JsonEventSink: an object is required");
    return -1;
  }

  atom = js_symbol_for_atom(ctx, "qjs.jsonsink");
  fn = JS_GetProperty(ctx, obj, atom);
  JS_FreeAtom(ctx, atom);

  if(JS_IsException(fn))
    return -1;

  if(JS_IsFunction(ctx, fn)) {
    JSValue addr = JS_Call(ctx, fn, obj, 0, NULL);
    int64_t p = 0;

    JS_FreeValue(ctx, fn);

    if(JS_IsException(addr))
      return -1;

    if(JS_ToInt64(ctx, &p, addr)) {
      JS_FreeValue(ctx, addr);
      return -1;
    }

    if(!p) {
      JS_FreeValue(ctx, addr);
      JS_ThrowTypeError(ctx, "JsonEventSink: bad handshake address");
      return -1;
    }

    JS_FreeValue(ctx, addr);
    *sink = *(JsonEventSink*)(intptr_t)p;
    return 0;
  }

  JS_FreeValue(ctx, fn);
  return (*adapter = js_jsonsink_from_js(ctx, obj, sink)) ? 0 : -1;
}

int
js_jsonsink_define_handshake(JSContext* ctx, JSValueConst proto, JSCFunction* fn) {
  JSAtom atom = js_symbol_for_atom(ctx, "qjs.jsonsink");
  int r = JS_DefinePropertyValue(ctx, proto, atom, JS_NewCFunction(ctx, fn, "jsonsink", 0), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);

  JS_FreeAtom(ctx, atom);
  return r < 0 ? -1 : 0;
}

void
js_jsoncallbacks_init(JSContext* ctx, JsonJsCallbacks* cb, JSValueConst arg) {
  static const char* const names[] = {"error", "value", "value", "value", "value", "value", "arrayStart", "arrayEnd", "objectStart", "objectEnd", "key"};

  cb->ctx = ctx;
  cb->fn = cb->obj = JS_UNDEFINED;

  for(int i = 0; i < JSON_CALLBACKS; i++)
    cb->by_type[i] = JS_UNDEFINED;

  if(JS_IsFunction(ctx, arg)) {
    cb->fn = JS_DupValue(ctx, arg);
  } else if(JS_IsObject(arg)) {
    cb->obj = JS_DupValue(ctx, arg);

    for(int i = 0; i < JSON_CALLBACKS; i++)
      cb->by_type[i] = JS_GetPropertyStr(ctx, arg, names[i]);
  }
}

void
js_jsoncallbacks_free_rt(JSRuntime* rt, JsonJsCallbacks* cb) {
  JS_FreeValueRT(rt, cb->fn);
  JS_FreeValueRT(rt, cb->obj);

  for(int i = 0; i < JSON_CALLBACKS; i++)
    JS_FreeValueRT(rt, cb->by_type[i]);
}

int
js_jsoncallbacks_complete(const JsonJsCallbacks* cb) {
  if(!JS_IsUndefined(cb->fn))
    return 1;

  for(int t = JSON_EVENT_NULL; t <= JSON_EVENT_KEY; t++)
    if(!JS_IsFunction(cb->ctx, cb->by_type[t - JSON_EVENT_ERROR]))
      return 0;

  return 1;
}

/* js_jsoncallbacks_call: calls fn, an exception it throws is dropped. */
static void
js_jsoncallbacks_call(JSContext* ctx, JSValueConst fn, JSValueConst this_obj, int argc, JSValueConst argv[]) {
  JS_FreeValue(ctx, JS_Call(ctx, fn, this_obj, argc, argv));
  JS_FreeValue(ctx, JS_GetException(ctx));
}

ssize_t
js_jsoncallbacks_event(void* user, const JsonEvent* ev) {
  JsonJsCallbacks* c = user;
  JSContext* ctx = c->ctx;

  if(!JS_IsUndefined(c->fn)) {
    JSValue args[2] = {JS_NewInt32(ctx, ev->type), js_jsonevent_value(ctx, ev)};

    js_jsoncallbacks_call(ctx, c->fn, JS_UNDEFINED, 2, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
  }

  if(ev->type >= JSON_EVENT_ERROR && ev->type <= JSON_EVENT_KEY) {
    JSValue cb = c->by_type[ev->type - JSON_EVENT_ERROR];

    if(JS_IsFunction(ctx, cb)) {
      JSValue val = js_jsonevent_value(ctx, ev);

      js_jsoncallbacks_call(ctx, cb, c->obj, 1, &val);
      JS_FreeValue(ctx, val);
    }
  }

  return 0;
}

void
js_jsontee_free_rt(JSRuntime* rt, JsonJsTee* t) {
  for(size_t i = 0; i < t->len; i++) {
    JS_FreeValueRT(rt, t->objs[i]);
    js_jsonsink_from_js_free_rt(rt, t->adapters[i]);
  }

  js_free_rt(rt, t->sinks);
  js_free_rt(rt, t->adapters);
  js_free_rt(rt, t->objs);
  js_free_rt(rt, t);
}

JsonJsTee*
js_jsontee_new(JSContext* ctx, int argc, JSValueConst argv[]) {
  JsonJsTee* t;

  if(!(t = js_mallocz(ctx, sizeof(JsonJsTee))))
    return NULL;

  t->sinks = js_mallocz(ctx, sizeof(JsonEventSink) * (argc ? argc : 1));
  t->adapters = js_mallocz(ctx, sizeof(JsonJsSink*) * (argc ? argc : 1));
  t->objs = js_mallocz(ctx, sizeof(JSValue) * (argc ? argc : 1));

  if(!t->sinks || !t->adapters || !t->objs)
    goto fail;

  for(int i = 0; i < argc; i++) {
    if(js_jsonsink_resolve(ctx, argv[i], &t->sinks[i], &t->adapters[i]))
      goto fail;

    t->objs[i] = JS_DupValue(ctx, argv[i]);
    t->len++;
  }

  t->tee = (JsonTee){t->sinks, t->len};
  t->sink = jtee_sink(&t->tee);
  return t;

fail:
  js_jsontee_free_rt(JS_GetRuntime(ctx), t);
  return NULL;
}

/** @} */
