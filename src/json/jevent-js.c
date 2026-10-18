/* jevent-js.c: js_jsonevent_value(), js_jsonevent_new(), js_jsonevent_throw(), json_read_value().
 * depends on: jevent.h (the event), char-utils.h (scan_double).
 * rule: the event text is only read, the value owns its copy. */

#define _GNU_SOURCE 1

#include "json/jevent-js.h"
#include "json/json.h"
#include "json/jbuilder.h"
#include "char-utils.h"
#include "quickjs-location.h"
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

  /* a comment is its text, a number also keeps its source text */
  if(ev->type == JSON_EVENT_COMMENT)
    value = JS_NewStringLen(ctx, ev->str ? ev->str : "", ev->len);
  else if(ev->type == JSON_EVENT_NUMBER && ev->str)
    JS_SetPropertyStr(ctx, obj, "raw", JS_NewStringLen(ctx, ev->str, ev->len));

  JS_SetPropertyStr(ctx, obj, "type", JS_NewInt32(ctx, ev->type));

  if(!JS_IsUndefined(value))
    JS_SetPropertyStr(ctx, obj, "value", value);

  JS_SetPropertyStr(ctx, loc, "line", JS_NewUint32(ctx, ev->at.line));
  JS_SetPropertyStr(ctx, loc, "column", JS_NewUint32(ctx, ev->at.column));
  JS_SetPropertyStr(ctx, loc, "offset", JS_NewInt64(ctx, ev->at.char_offset));
  JS_SetPropertyStr(ctx, obj, "loc", loc);
  return obj;
}

void
js_jsonevent_throw(JSContext* ctx, const Location* base, const JsonEvent* ev) {
  Location* at;

  if((at = location_clone(base, ctx))) {
    location_from_srcpos(at, &ev->at);
    js_throw_syntax_error(ctx, at, ev->span, ev->code ? ev->code : "unexpected-token", "%s", ev->str ? ev->str : "parse error");
    location_free(at, JS_GetRuntime(ctx));
  }
}

JSValue
json_read_value(JSContext* ctx, const uint8_t* data, size_t size, const char* name) {
  JsonParser* p = json_new(reader_from_bytes(data, size), 0, ctx);
  JsonBuilder b;
  JsonEventSink sink;
  JsonEvent ev;
  JsonPull st;
  JSValue ret = JS_EXCEPTION;

  if(!p)
    return ret;

  p->flags = JR_JSON;
  jbuild_init(&b, ctx);
  sink = jbuild_sink(&b);

  /* the input ends when the reader is dry, then the end events follow */
  do {
    st = json_next(p, &ev);

    if(st == JSON_PULL_AGAIN)
      st = json_next_end(p, &ev);

    if(st != JSON_PULL_EVENT)
      break;

    if(ev.type == JSON_EVENT_ERROR) {
      js_throw_syntax_error_at(ctx, name ? name : "<json>", &ev.at, ev.span, ev.code, ev.str ? ev.str : "parse error");
      goto done;
    }

    jevent_send(&sink, &ev);
  } while(1);

  ret = b.has_root ? jbuild_root(&b) : JS_UNDEFINED;
done:
  jbuild_free(&b, JS_GetRuntime(ctx));
  json_free(p, JS_GetRuntime(ctx));
  return ret;
}

/** @} */
