/* jnext.c: JsonParser, json_next(), json_source(); the pull parser as a JsonEvent source.
 * depends on: jread.h (the tokenizer), jevent.h (the event), location.h (the position for errors).
 * rule: jread does the parsing, this reads blocks, queues what it made and hands it out one by one. */

#include "json/json.h"
#include "location.h"
#include <stdlib.h>
#include <string.h>

/**
 * \addtogroup json
 * @{
 */

BOOL
json_init(JsonParser* json, Reader reader, const char* filename, JSContext* ctx) {
  memset(json, 0, sizeof(*json));
  json->reader = reader;
  json->flags = JR_LEGACY;

  if(!(json->loc = location_new(ctx)))
    return FALSE;

  location_zero(json->loc);

  if(filename)
    location_set_filename(json->loc, filename, ctx);

  return json->reader.read != NULL;
}

JsonParser*
json_new(Reader reader, const char* filename, JSContext* ctx) {
  JsonParser* json;

  if(!(json = js_malloc(ctx, sizeof(JsonParser))))
    return 0;

  if(!json_init(json, reader, filename, ctx)) {
    js_free(ctx, json);
    return 0;
  }

  return json;
}

void
json_clear(JsonParser* json, JSRuntime* rt) {
  reader_free(&json->reader);

  if(json->started)
    jr_state_free(&json->st);

  free(json->queue.ev);
  free(json->queue.text);

  if(json->loc)
    location_free(json->loc, rt);
}

void
json_free(JsonParser* json, JSRuntime* rt) {
  json_clear(json, rt);
  js_free_rt(rt, json);
}

/* the message of an error event, by its code */
static const char*
jnext_message(const char* code) {
  static const char* const table[][2] = {
      {"unexpected-token", "unexpected token"},
      {"bad-escape", "invalid escape sequence in string"},
      {"bad-unicode-escape", "invalid unicode escape in string"},
      {"invalid-utf8", "invalid UTF-8"},
      {"control-character", "control character in string"},
      {"invalid-literal", "invalid literal"},
      {"unexpected-end", "unexpected end of input"},
      {"out-of-memory", "out of memory"},
      {"trailing-data", "unexpected data after the document"},
  };

  for(size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
    if(!strcmp(code, table[i][0]))
      return table[i][1];

  return "parse error";
}

/* jnext_push: copies an event, text included, into the queue. text is kept as an offset plus one until it is handed out. */
static ssize_t
jnext_push(void* user, const JsonEvent* ev) {
  JsonParser* json = user;
  struct json_queue* q = &json->queue;
  JsonEvent copy = *ev;
  const char* str = ev->type == JSON_EVENT_ERROR ? jnext_message(ev->code) : ev->str;
  size_t len = ev->type == JSON_EVENT_ERROR ? strlen(str) : ev->len;

  if(q->head + q->len == q->cap) {
    size_t cap = q->cap ? q->cap * 2 : 64;
    JsonEvent* p = realloc(q->ev, cap * sizeof(JsonEvent));

    if(!p)
      return -1;

    q->ev = p;
    q->cap = cap;
  }

  if(str) {
    if(q->text_len + len + 1 > q->text_cap) {
      size_t cap = q->text_cap ? q->text_cap : 256;
      char* p;

      while(cap < q->text_len + len + 1)
        cap *= 2;

      if(!(p = realloc(q->text, cap)))
        return -1;

      q->text = p;
      q->text_cap = cap;
    }

    memcpy(q->text + q->text_len, str, len);
    copy.str = (const char*)(uintptr_t)(q->text_len + 1);
    q->text_len += len;

    /* a message is read as a C string */
    if(ev->type == JSON_EVENT_ERROR)
      q->text[q->text_len++] = '\0';
  }

  copy.len = len;
  q->ev[q->head + q->len++] = copy;
  return 0;
}

/* jnext_pop: writes the oldest queued event to *ev, JSON_PULL_AGAIN when the queue is empty. */
static JsonPull
jnext_pop(JsonParser* json, JsonEvent* ev) {
  struct json_queue* q = &json->queue;

  if(!q->len)
    return JSON_PULL_AGAIN;

  *ev = q->ev[q->head++];
  q->len--;

  if(ev->str)
    ev->str = q->text + ((uintptr_t)ev->str - 1);

  if(!q->len) {
    q->head = 0;
    q->text_len = 0;
  }

  if(ev->type == JSON_EVENT_OBJECT_START || ev->type == JSON_EVENT_ARRAY_START)
    json->depth++;
  else if((ev->type == JSON_EVENT_OBJECT_END || ev->type == JSON_EVENT_ARRAY_END) && json->depth)
    json->depth--;

  location_from_srcpos(json->loc, &ev->at);
  return JSON_PULL_EVENT;
}

static void
jnext_start(JsonParser* json) {
  if(!json->started) {
    json->started = 1;
    jr_state_init_flags(&json->st, json->flags);
  }

  json->st.comment_events = json->comments;
}

JsonPull
json_next(JsonParser* json, JsonEvent* ev) {
  for(;;) {
    ssize_t n;

    if(jnext_pop(json, ev) == JSON_PULL_EVENT)
      return JSON_PULL_EVENT;

    if(json->ended)
      return JSON_PULL_END;

    /* a dry reader and the end of the input look the same: the caller decides with json_next_end() */
    if((n = reader_read(&json->reader, json->block, sizeof(json->block))) <= 0) {
      /* a line comment at the end of the data is complete enough to deliver */
      if(json->started && jr_flush(&json->st, jnext_push, json) == 0 && json->queue.len)
        continue;

      return JSON_PULL_AGAIN;
    }

    jnext_start(json);
    jr_feed(&json->st, (const char*)json->block, (size_t)n, jnext_push, json);
  }
}

JsonPull
json_next_end(JsonParser* json, JsonEvent* ev) {
  if(!json->ended) {
    jnext_start(json);
    json->ended = 1;
    jr_end(&json->st, jnext_push, json);
  }

  return jnext_pop(json, ev) == JSON_PULL_EVENT ? JSON_PULL_EVENT : JSON_PULL_END;
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
