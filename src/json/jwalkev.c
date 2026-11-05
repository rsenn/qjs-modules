/* jwalkev.c: JwalkSource, jwalk_event(); the JSON walk as a pull source of JsonEvent.
 * depends on: jwalk.h (the iterator), jevent.h (the event).
 * rule: the walk leaves its event in the iterator, jwalk_event() turns it into text. */

#include "json/jwalk.h"
#include "vector.h"
#include <string.h>

/**
 * \addtogroup jwalk
 * @{
 */

int
jwalk_source_init(JwalkSource* s, JSContext* ctx, JSValueConst root) {
  s->ctx = ctx;
  vector_init_rt(&s->text, JS_GetRuntime(ctx));
  return jwalk_init(&s->it, ctx, root);
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
  WalkIterator* w = &s->it.walk;
  int n;

  *ev = (JsonEvent){0};

  if((n = walk_next(&s->it.walk, ctx)) < 0)
    return JSON_PULL_FAIL;

  if(n == 0)
    return JSON_PULL_END;

  switch(w->event) {
    case WALK_EVENT_OBJECT_START: ev->type = JSON_EVENT_OBJECT_START; return JSON_PULL_EVENT;
    case WALK_EVENT_ARRAY_START: ev->type = JSON_EVENT_ARRAY_START; return JSON_PULL_EVENT;
    case WALK_EVENT_OBJECT_END: ev->type = JSON_EVENT_OBJECT_END; return JSON_PULL_EVENT;
    case WALK_EVENT_ARRAY_END: ev->type = JSON_EVENT_ARRAY_END; return JSON_PULL_EVENT;
    case WALK_EVENT_KEY: ev->type = JSON_EVENT_KEY; break;
    case WALK_EVENT_VALUE: {
      if(w->raw) {
        JSValue text = JS_GetPropertyStr(ctx, w->val, "rawJSON");
        int r = JS_IsException(text) ? -1 : jwalkev_text(s, text, 1);

        JS_FreeValue(ctx, text);

        if(r < 0)
          return JSON_PULL_FAIL;

        ev->type = JSON_EVENT_RAW;
      } else if(JS_IsNull(w->val)) {
        ev->type = JSON_EVENT_NULL;
        return JSON_PULL_EVENT;
      } else if(JS_IsBool(w->val)) {
        ev->type = JS_ToBool(ctx, w->val) ? JSON_EVENT_TRUE : JSON_EVENT_FALSE;
        return JSON_PULL_EVENT;
      } else if(JS_IsNumber(w->val)) {
        if(jwalkev_text(s, w->val, 1) < 0)
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
    if(jwalkev_text(s, w->val, 0) < 0)
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
  dbuf_free(&s->text);
}

void
jwalk_source_close(JwalkSource* s, JSContext* ctx) {
  jwalk_source_close_rt(s, JS_GetRuntime(ctx));
}

/**
 * @}
 */
