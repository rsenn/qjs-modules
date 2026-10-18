/* jnext.c: json_next(), json_source(); the pull parser as a JsonEvent source.
 * depends on: json.h (json_parse), jevent.h (the event).
 * rule: json_parse() does the parsing, this only names what it found. */

#include "json/json.h"
#include <string.h>

/**
 * \addtogroup json
 * @{
 */

static SourcePos
jnext_pos(const JsonPos* p) {
  return (SourcePos){
      .byte_offset = p->byte_offset,
      .char_offset = p->char_offset,
      .line = p->line + 1,
      .column = p->column + 1,
  };
}

/* jnext_event: names what json_parse() or json_end() returned in `type`. */
static JsonPull
jnext_event(JsonParser* json, int type, JsonEvent* ev) {
  *ev = (JsonEvent){0};

  switch(type) {
    case JSON_NONE: return JSON_PULL_END;
    case JSON_NEED_DATA: return JSON_PULL_AGAIN;
    case JSON_ERROR:
      ev->type = JSON_EVENT_ERROR;
      ev->str = json->error;
      ev->len = json->error ? strlen(json->error) : 0;
      ev->code = json->err_code;
      ev->span = json->err_len;
      ev->at = jnext_pos(&json->err_at);
      ev->has_at = 1;
      return JSON_PULL_EVENT;
    case JSON_EVENT_KEY:
    case JSON_EVENT_STRING:
    case JSON_EVENT_NUMBER:
    case JSON_EVENT_COMMENT:
      ev->str = (const char*)json->token.buf;
      ev->len = json->token.size;
      break;
    default: break;
  }

  ev->type = (JsonEventType)type;
  ev->at = jnext_pos(type == JSON_EVENT_COMMENT ? &json->last_start : &json->tok_start);
  ev->has_at = 1;
  return JSON_PULL_EVENT;
}

JsonPull
json_next(JsonParser* json, JsonEvent* ev) {
  return jnext_event(json, json_parse(json), ev);
}

JsonPull
json_next_end(JsonParser* json, JsonEvent* ev) {
  return jnext_event(json, json_end(json), ev);
}

static JsonPull
jnext_step(void* opaque, JsonEvent* ev) {
  return json_next(opaque, ev);
}

JsonEventSource
json_source(JsonParser* json) {
  return (JsonEventSource){json, jnext_step};
}

/**
 * @}
 */
