/* jevent-js.c: js_jsonevent_value(), a JsonEvent as a JS value.
 * depends on: jevent.h (the event), char-utils.h (scan_double).
 * rule: the event text is only read, the value owns its copy. */

#define _GNU_SOURCE 1

#include "json/jevent-js.h"
#include "char-utils.h"
#include <string.h>

/**
 * \addtogroup jevent
 * @{
 */

JSValue
js_jsonevent_value(JSContext* ctx, const JsonEvent* ev) {
  switch(ev->type) {
    case JSON_EVENT_NULL: return JS_NULL;
    case JSON_EVENT_TRUE: return JS_TRUE;
    case JSON_EVENT_FALSE: return JS_FALSE;
    case JSON_EVENT_NUMBER: {
      double num = ev->str ? jevent_number(ev->str, ev->len) : 0;

      return JS_NewFloat64(ctx, num);
    }
    case JSON_EVENT_STRING:
    case JSON_EVENT_KEY:
    case JSON_EVENT_ERROR: return ev->str ? JS_NewStringLen(ctx, ev->str, ev->len) : JS_NewString(ctx, "");

    case JSON_EVENT_ARRAY_START:
    case JSON_EVENT_ARRAY_END:
    case JSON_EVENT_OBJECT_START:
    case JSON_EVENT_OBJECT_END:
    case JSON_EVENT_COMMENT:
    case JSON_EVENT_RAW: break;
  }

  return JS_UNDEFINED;
}

JSValue
js_jsonevent_new(JSContext* ctx, const JsonEvent* ev) {
  JSValue obj = JS_NewObject(ctx), value = js_jsonevent_value(ctx, ev), loc = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, obj, "type", JS_NewInt32(ctx, ev->type));

  if(!JS_IsUndefined(value))
    JS_SetPropertyStr(ctx, obj, "value", value);

  JS_SetPropertyStr(ctx, loc, "line", JS_NewUint32(ctx, ev->at.line));
  JS_SetPropertyStr(ctx, loc, "column", JS_NewUint32(ctx, ev->at.column));
  JS_SetPropertyStr(ctx, loc, "offset", JS_NewInt64(ctx, ev->at.char_offset));
  JS_SetPropertyStr(ctx, obj, "loc", loc);
  return obj;
}

/** @} */
