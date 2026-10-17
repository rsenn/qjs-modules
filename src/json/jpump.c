/* jpump.c: jread_pump(), jread_pump_finish(), json_pump().
 * depends on: jevent.h (the sink), jread.h (push parser), json.h (pull parser).
 * rule: one jevent_send() per parser event; the first sink failure stops all sending. */

#define _GNU_SOURCE 1

#include "json/jevent.h"
#include "json/json.h"
#include <string.h>

typedef struct {
  const JsonEventSink* sink;
  const jr_state_t* state;
  ssize_t failure; /* first negative result of the sink, 0 if none */
} JsonPump;

static void
jpump_send(JsonPump* p, const JsonEvent* ev) {
  ssize_t r;

  if(p->failure)
    return;

  if((r = jevent_send(p->sink, ev)) < 0)
    p->failure = r;
}

static void
jpump_callback(jr_type_t type, const jr_str_t* data, void* user_data) {
  JsonPump* p = user_data;
  JsonEvent ev = {.type = type, .str = data ? data->cstr : NULL, .len = data ? data->len : 0};

  if(type == jr_type_error) {
    ev.at = p->state->err_pos;
    ev.has_at = 1;
  }

  jpump_send(p, &ev);
}

ssize_t
jread_pump(jr_state_t* state, const JsonEventSink* sink, const char* chunk, size_t len) {
  JsonPump p = {sink, state, 0};

  jr_read(jpump_callback, chunk, len, &p, state);
  return p.failure;
}

ssize_t
jread_pump_finish(jr_state_t* state, const JsonEventSink* sink) {
  JsonPump p = {sink, state, 0};

  jr_finish(jpump_callback, &p, state);
  return p.failure;
}

/* the jr_type_t of a JsonValueType, or -1 for one that is not an event */
static int
jpump_type(int type) {
  switch(type) {
    case JSON_TYPE_OBJECT: return jr_type_object_start;
    case JSON_TYPE_OBJECT_END: return jr_type_object_end;
    case JSON_TYPE_ARRAY: return jr_type_array_start;
    case JSON_TYPE_ARRAY_END: return jr_type_array_end;
    case JSON_TYPE_KEY: return jr_type_key;
    case JSON_TYPE_STRING: return jr_type_string;
    case JSON_TYPE_TRUE: return jr_type_true;
    case JSON_TYPE_FALSE: return jr_type_false;
    case JSON_TYPE_NULL: return jr_type_null;
    case JSON_TYPE_NUMBER: return jr_type_number;
  }

  return -1;
}

ssize_t
json_pump(JsonParser* json, const JsonEventSink* sink) {
  JsonPump p = {sink, NULL, 0};

  for(;;) {
    int type = json_parse(json);
    JsonEvent ev = {0};

    if(type == JSON_TYPE_NONE)
      return p.failure ? p.failure : JSON_PUMP_END;

    if(type == JSON_NEED_DATA)
      return p.failure ? p.failure : JSON_PUMP_AGAIN;

    if(type == JSON_ERROR) {
      ev.type = jr_type_error;
      ev.str = json->error;
      ev.len = json->error ? strlen(json->error) : 0;
      ev.at = (SourcePos){.byte_offset = json->err_at.byte_offset,
                          .char_offset = json->err_at.char_offset,
                          .line = json->err_at.line + 1,
                          .column = json->err_at.column + 1};
      ev.has_at = 1;
    } else if((ev.type = jpump_type(type)) < 0) {
      continue; /* a comment */
    } else if(type == JSON_TYPE_KEY || type == JSON_TYPE_STRING || type == JSON_TYPE_NUMBER) {
      ev.str = (const char*)json->token.buf;
      ev.len = json->token.size;
    }

    jpump_send(&p, &ev);

    if(p.failure)
      return p.failure;
  }
}
