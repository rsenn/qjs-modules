/* xconnect.c: js_xmlsink_resolve(), js_xmlsink_define_handshake(); the xml handshake.
 * depends on: xsink.h (the JS-object adapter), utils.h (symbols).
 * rule: a sink from another module is found by the address it gives, never by class id. */

#define _GNU_SOURCE 1

#include "xml/xconnect.h"
#include "utils.h"
#include <cutils.h>

/**
 * \addtogroup xsink
 * @{
 */

int
js_xmlsink_resolve(JSContext* ctx, JSValueConst obj, XmlEventSink* sink, XmlJsSink** adapter) {
  JSAtom atom;
  JSValue fn;

  *adapter = NULL;

  if(!JS_IsObject(obj)) {
    JS_ThrowTypeError(ctx, "XmlEventSink: an object is required");
    return -1;
  }

  atom = js_symbol_for_atom(ctx, "qjs.xmlsink");
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
      JS_ThrowTypeError(ctx, "XmlEventSink: bad handshake address");
      return -1;
    }

    JS_FreeValue(ctx, addr);
    *sink = *(XmlEventSink*)(intptr_t)p;
    return 0;
  }

  JS_FreeValue(ctx, fn);
  return (*adapter = js_xmlsink_from_js(ctx, obj, sink)) ? 0 : -1;
}

int
js_xmlsink_define_handshake(JSContext* ctx, JSValueConst proto, JSCFunction* fn) {
  JSAtom atom = js_symbol_for_atom(ctx, "qjs.xmlsink");
  int r = JS_DefinePropertyValue(ctx, proto, atom, JS_NewCFunction(ctx, fn, "xmlsink", 0), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);

  JS_FreeAtom(ctx, atom);
  return r < 0 ? -1 : 0;
}

void
js_xmlcallbacks_init(JSContext* ctx, XmlJsCallbacks* cb, JSValueConst options) {
  int obj = JS_IsObject(options);

  cb->ctx = ctx;
  cb->this_obj = JS_DupValue(ctx, options);
  cb->attribute = obj ? JS_GetPropertyStr(ctx, options, "attribute") : JS_UNDEFINED;
  cb->element_start = obj ? JS_GetPropertyStr(ctx, options, "elementStart") : JS_UNDEFINED;
  cb->element_end = obj ? JS_GetPropertyStr(ctx, options, "elementEnd") : JS_UNDEFINED;
  cb->error = obj ? JS_GetPropertyStr(ctx, options, "error") : JS_UNDEFINED;
  cb->text = obj ? JS_GetPropertyStr(ctx, options, "text") : JS_UNDEFINED;
  cb->on_parse_error = obj ? JS_GetPropertyStr(ctx, options, "onParseError") : JS_UNDEFINED;
}

void
js_xmlcallbacks_free_rt(JSRuntime* rt, XmlJsCallbacks* cb) {
  JS_FreeValueRT(rt, cb->this_obj);
  JS_FreeValueRT(rt, cb->attribute);
  JS_FreeValueRT(rt, cb->element_start);
  JS_FreeValueRT(rt, cb->element_end);
  JS_FreeValueRT(rt, cb->error);
  JS_FreeValueRT(rt, cb->text);
  JS_FreeValueRT(rt, cb->on_parse_error);
}

int
js_xmlcallbacks_any(const XmlJsCallbacks* cb) {
  JSContext* ctx = cb->ctx;

  return JS_IsFunction(ctx, cb->attribute) || JS_IsFunction(ctx, cb->element_start) || JS_IsFunction(ctx, cb->element_end) || JS_IsFunction(ctx, cb->error) ||
         JS_IsFunction(ctx, cb->text);
}

ssize_t
js_xmlcallbacks_event(void* user, const XmlEvent* ev) {
  XmlJsCallbacks* c = user;
  JSContext* ctx = c->ctx;
  JSValue args[2] = {JS_UNDEFINED, JS_UNDEFINED};
  JSValue* cb = 0;

  switch(ev->type) {
    case XE_ELEMENT_START:
    case XE_ELEMENT_END:
      cb = ev->type == XE_ELEMENT_START ? &c->element_start : &c->element_end;
      args[0] = ev->name ? JS_NewStringLen(ctx, ev->name, ev->nlen) : JS_UNDEFINED;
      break;
    case XE_ATTRIBUTE:
      cb = &c->attribute;
      args[0] = JS_NewStringLen(ctx, ev->name, ev->nlen);
      args[1] = ev->value ? JS_NewStringLen(ctx, ev->value, ev->vlen) : JS_TRUE;
      break;
    case XE_TEXT:
      cb = &c->text;
      args[1] = JS_NewStringLen(ctx, ev->str, ev->len);
      break;
    case XE_ERROR:
      cb = &c->error;
      args[0] = JS_NewString(ctx, "Error!");
      args[1] = ev->str ? JS_NewStringLen(ctx, ev->str, ev->len) : JS_UNDEFINED;
      break;
    default: break;
  }

  /* the options object need not define every callback, a missing one is skipped */
  if(cb && JS_IsFunction(ctx, *cb))
    JS_FreeValue(ctx, JS_Call(ctx, *cb, c->this_obj, countof(args), args));

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  return 0;
}

void
js_xmltee_free_rt(JSRuntime* rt, XmlJsTee* t) {
  for(size_t i = 0; i < t->len; i++) {
    JS_FreeValueRT(rt, t->objs[i]);
    js_xmlsink_from_js_free_rt(rt, t->adapters[i]);
  }

  js_free_rt(rt, t->sinks);
  js_free_rt(rt, t->adapters);
  js_free_rt(rt, t->objs);
  js_free_rt(rt, t);
}

XmlJsTee*
js_xmltee_new(JSContext* ctx, int argc, JSValueConst argv[]) {
  XmlJsTee* t;

  if(!(t = js_mallocz(ctx, sizeof(XmlJsTee))))
    return NULL;

  t->sinks = js_mallocz(ctx, sizeof(XmlEventSink) * (argc ? argc : 1));
  t->adapters = js_mallocz(ctx, sizeof(XmlJsSink*) * (argc ? argc : 1));
  t->objs = js_mallocz(ctx, sizeof(JSValue) * (argc ? argc : 1));

  if(!t->sinks || !t->adapters || !t->objs)
    goto fail;

  for(int i = 0; i < argc; i++) {
    if(js_xmlsink_resolve(ctx, argv[i], &t->sinks[i], &t->adapters[i]))
      goto fail;

    t->objs[i] = JS_DupValue(ctx, argv[i]);
    t->len++;
  }

  t->tee = (XmlTee){t->sinks, t->len};
  t->sink = xtee_sink(&t->tee);
  return t;

fail:
  js_xmltee_free_rt(JS_GetRuntime(ctx), t);
  return NULL;
}

/** @} */
