/* jpump.c: jr_feed(), jr_end(), jread_pump(), jread_pump_finish(), json_pump(), jr_flags_from_name().
 * depends on: jevent.h (sink, handler, source), jread.h (push parser), json.h (pull parser).
 * rule: the first handler failure stops all delivery; the pumps are the handler form with a sink. */

#define _GNU_SOURCE 1

#include "json/jevent.h"
#include "json/json.h"
#include <string.h>

typedef struct {
  JsonEventHandler* handler;
  void* user;
  const jr_state_t* state;
  ssize_t failure; /* first negative result of the handler, 0 if none */
} JsonFeed;

static void
jfeed_callback(JsonEventType type, const jr_str_t* data, void* user_data) {
  JsonFeed* f = user_data;
  JsonEvent ev;
  ssize_t r;

  if(f->failure)
    return;

  ev = (JsonEvent){
      .type = type,
      .str = data ? data->cstr : NULL,
      .len = data ? data->len : 0,
      .at = type == JSON_EVENT_ERROR ? f->state->err_pos : f->state->tok,
      .has_at = 1,
  };

  if(type == JSON_EVENT_ERROR) {
    ev.code = jr_error_name(f->state->err_code);
    ev.span = f->state->err_len;
  }

  if((r = f->handler(f->user, &ev)) < 0)
    f->failure = r;
}

ssize_t
jr_feed(jr_state_t* state, const char* chunk, size_t len, JsonEventHandler* handler, void* user) {
  JsonFeed f = {handler, user, state, 0};

  jr_read(jfeed_callback, chunk, len, &f, state);
  return f.failure;
}

ssize_t
jr_end(jr_state_t* state, JsonEventHandler* handler, void* user) {
  JsonFeed f = {handler, user, state, 0};

  jr_finish(jfeed_callback, &f, state);
  return f.failure;
}

ssize_t
jread_pump(jr_state_t* state, const JsonEventSink* sink, const char* chunk, size_t len) {
  return jr_feed(state, chunk, len, jevent_handler, (void*)sink);
}

ssize_t
jread_pump_finish(jr_state_t* state, const JsonEventSink* sink) {
  return jr_end(state, jevent_handler, (void*)sink);
}

ssize_t
json_pump(JsonParser* json, const JsonEventSink* sink) {
  return jevent_pump(json_source(json), sink);
}

int
jr_flags_from_name(const char* name, size_t len, int* flags) {
  static const struct {
    const char* name;
    int flags;
  } dialects[] = {
      {"json", JR_JSON},
      {"jsonc", JR_JSONC},
      {"json5", JR_JSON5},
      {"jsonl", JR_JSONL},
      {"legacy", JR_LEGACY},
  };

  for(size_t i = 0; i < sizeof(dialects) / sizeof(dialects[0]); i++) {
    if(strlen(dialects[i].name) == len && !memcmp(dialects[i].name, name, len)) {
      *flags = dialects[i].flags;
      return 0;
    }
  }

  return -1;
}
