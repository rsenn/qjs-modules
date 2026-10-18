/* xevent-js.c: js_xmlevent_new(), an XmlEvent as a JS object.
 * depends on: xevent.h (the event).
 * rule: the event text is only read, the object owns its copy. */

#define _GNU_SOURCE 1

#include "xml/xevent-js.h"

/**
 * \addtogroup xevent
 * @{
 */

JSValue
js_xmlevent_new(JSContext* ctx, const XmlEvent* ev) {
  JSValue obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, obj, "type", JS_NewInt32(ctx, ev->type));

  switch(ev->type) {
    case XE_ELEMENT_START:
    case XE_ELEMENT_END: JS_SetPropertyStr(ctx, obj, "name", ev->name ? JS_NewStringLen(ctx, ev->name, ev->nlen) : JS_UNDEFINED); break;
    case XE_ATTRIBUTE:
      JS_SetPropertyStr(ctx, obj, "name", JS_NewStringLen(ctx, ev->name, ev->nlen));

      if(ev->value)
        JS_SetPropertyStr(ctx, obj, "value", JS_NewStringLen(ctx, ev->value, ev->vlen));

      break;
    case XE_ERROR: break;
    default: JS_SetPropertyStr(ctx, obj, "value", JS_NewStringLen(ctx, ev->str ? ev->str : "", ev->len)); break;
  }

  if(ev->has_at) {
    JSValue loc = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, loc, "line", JS_NewUint32(ctx, ev->at.line));
    JS_SetPropertyStr(ctx, loc, "column", JS_NewUint32(ctx, ev->at.column));
    JS_SetPropertyStr(ctx, obj, "loc", loc);
  }

  return obj;
}

/** @} */
