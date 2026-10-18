/* jwalkev.c: JwalkSource, jwalk_event(); the JSON walk as a pull source of JsonEvent.
 * depends on: jwalk.h (the iterator), jevent.h (the event).
 * rule: the walk calls a capturing WalkInterface that only remembers the event, jwalk_event()
 * turns it into text. */

#include "json/jwalk.h"
#include "vector.h"
#include <string.h>

/**
 * \addtogroup jwalk
 * @{
 */

static void
jwalkev_capture(JwalkSource* s, JSContext* ctx, int kind, JSValueConst v, int raw) {
  JS_FreeValue(ctx, s->val);
  s->val = JS_DupValue(ctx, v);
  s->kind = kind;
  s->raw = raw;
}

static ssize_t
jwalkev_key(void* o, JSContext* ctx, JSValueConst key) {
  jwalkev_capture(o, ctx, WALK_EVENT_KEY, key, 0);
  return 0;
}

static ssize_t
jwalkev_object_start(void* o, JSContext* ctx) {
  jwalkev_capture(o, ctx, WALK_EVENT_OBJECT_START, JS_UNDEFINED, 0);
  return 0;
}

static ssize_t
jwalkev_array_start(void* o, JSContext* ctx) {
  jwalkev_capture(o, ctx, WALK_EVENT_ARRAY_START, JS_UNDEFINED, 0);
  return 0;
}

static ssize_t
jwalkev_object_end(void* o, JSContext* ctx) {
  jwalkev_capture(o, ctx, WALK_EVENT_OBJECT_END, JS_UNDEFINED, 0);
  return 0;
}

static ssize_t
jwalkev_array_end(void* o, JSContext* ctx) {
  jwalkev_capture(o, ctx, WALK_EVENT_ARRAY_END, JS_UNDEFINED, 0);
  return 0;
}

static ssize_t
jwalkev_value(void* o, JSContext* ctx, JSValueConst v) {
  jwalkev_capture(o, ctx, WALK_EVENT_VALUE, v, 0);
  return 0;
}

static ssize_t
jwalkev_raw(void* o, JSContext* ctx, JSValueConst v) {
  jwalkev_capture(o, ctx, WALK_EVENT_VALUE, v, 1);
  return 0;
}

int
jwalk_source_init(JwalkSource* s, JSContext* ctx, JSValueConst root) {
  WalkInterface iface = {s, jwalkev_key, jwalkev_object_start, jwalkev_array_start, jwalkev_object_end, jwalkev_array_end, jwalkev_value, jwalkev_raw};

  s->ctx = ctx;
  s->val = JS_UNDEFINED;
  s->kind = s->raw = 0;
  vector_init_rt(&s->text, JS_GetRuntime(ctx));
  return jwalk_init(&s->it, ctx, root, iface);
}

/* jwalkev_text: copies the CESU-8 text of the JS string `v` into s->text; returns 0, or -1 with an exception pending. */
static int
jwalkev_text(JwalkSource* s, JSValueConst v, int number) {
  JSContext* ctx = s->ctx;
  size_t len;
  const char* str = JS_ToCStringLen2(ctx, &len, v, !number);

  if(!str)
    return -1;

  s->text.size = 0;
  dbuf_put(&s->text, (const uint8_t*)str, len);
  JS_FreeCString(ctx, str);
  return 0;
}

JsonPull
jwalk_event(JwalkSource* s, JsonEvent* ev) {
  JSContext* ctx = s->ctx;
  int n;

  *ev = (JsonEvent){0};
  s->kind = 0;

  if((n = walk_next(&s->it.walk, ctx)) < 0)
    return JSON_PULL_FAIL;

  if(n == 0)
    return JSON_PULL_END;

  switch(s->kind) {
    case WALK_EVENT_OBJECT_START: ev->type = JSON_EVENT_OBJECT_START; return JSON_PULL_EVENT;
    case WALK_EVENT_ARRAY_START: ev->type = JSON_EVENT_ARRAY_START; return JSON_PULL_EVENT;
    case WALK_EVENT_OBJECT_END: ev->type = JSON_EVENT_OBJECT_END; return JSON_PULL_EVENT;
    case WALK_EVENT_ARRAY_END: ev->type = JSON_EVENT_ARRAY_END; return JSON_PULL_EVENT;
    case WALK_EVENT_KEY: ev->type = JSON_EVENT_KEY; break;
    case WALK_EVENT_VALUE: {
      if(s->raw) {
        JSValue text = JS_GetPropertyStr(ctx, s->val, "rawJSON");
        int r = JS_IsException(text) ? -1 : jwalkev_text(s, text, 1);

        JS_FreeValue(ctx, text);

        if(r < 0)
          return JSON_PULL_FAIL;

        ev->type = JSON_EVENT_RAW;
      } else if(JS_IsNull(s->val)) {
        ev->type = JSON_EVENT_NULL;
        return JSON_PULL_EVENT;
      } else if(JS_IsBool(s->val)) {
        ev->type = JS_ToBool(ctx, s->val) ? JSON_EVENT_TRUE : JSON_EVENT_FALSE;
        return JSON_PULL_EVENT;
      } else if(JS_IsNumber(s->val)) {
        if(jwalkev_text(s, s->val, 1) < 0)
          return JSON_PULL_FAIL;

        ev->type = JSON_EVENT_NUMBER;
      } else {
        ev->type = JSON_EVENT_STRING;
      }

      break;
    }
    default: return JSON_PULL_END;
  }

  if(ev->type == JSON_EVENT_KEY || ev->type == JSON_EVENT_STRING)
    if(jwalkev_text(s, s->val, 0) < 0)
      return JSON_PULL_FAIL;

  ev->str = (const char*)s->text.buf;
  ev->len = s->text.size;
  return JSON_PULL_EVENT;
}

static JsonPull
jwalkev_step(void* opaque, JsonEvent* ev) {
  return jwalk_event(opaque, ev);
}

JsonEventSource
jwalk_source(JwalkSource* s) {
  return (JsonEventSource){s, jwalkev_step};
}

void
jwalk_source_close_rt(JwalkSource* s, JSRuntime* rt) {
  jwalk_close_rt(&s->it, rt);
  JS_FreeValueRT(rt, s->val);
  s->val = JS_UNDEFINED;
  dbuf_free(&s->text);
}

void
jwalk_source_close(JwalkSource* s, JSContext* ctx) {
  jwalk_source_close_rt(s, JS_GetRuntime(ctx));
}

/**
 * @}
 */
