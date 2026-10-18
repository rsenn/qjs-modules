/* jconnect.c: js_jsonsink_resolve(), js_jsonsink_define_handshake(); the json handshake.
 * depends on: jsink.h (the JS-object adapter), utils.h (symbols).
 * rule: a sink from another module is found by the address it gives, never by class id. */

#define _GNU_SOURCE 1

#include "json/jconnect.h"
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

/** @} */
