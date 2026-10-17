#ifndef JEVENT_H
#define JEVENT_H

#include <stddef.h>
#include <sys/types.h>
#include "srcpos.h"

/* JsonEventType: the kind of a JSON event; the one vocabulary of every json/ header.
 *
 *   JSON_EVENT_*   value  carries
 *   ERROR          0      message
 *   NULL, TRUE, FALSE     nothing
 *   NUMBER, STRING        text
 *   KEY                   text
 *   ARRAY_START/END,
 *   OBJECT_START/END      nothing
 *   COMMENT               text; only the pull parser, with `comments` set
 *
 * jread (callbacks), json (json_parse() results), jbuilder, jwrite and the sinks all use it. */
typedef enum json_event_type {
  JSON_EVENT_ERROR,
  JSON_EVENT_NULL,
  JSON_EVENT_TRUE,
  JSON_EVENT_FALSE,
  JSON_EVENT_NUMBER,
  JSON_EVENT_STRING,
  JSON_EVENT_ARRAY_START,
  JSON_EVENT_ARRAY_END,
  JSON_EVENT_OBJECT_START,
  JSON_EVENT_OBJECT_END,
  JSON_EVENT_KEY,
  JSON_EVENT_COMMENT,
} JsonEventType;

struct jr_state;

/* JsonEvent: one JSON event, as a plain value; the twin of XmlEvent (xml/xevent.h).
 *
 * ```c
 * JsonEvent ev = {.type = JSON_EVENT_NUMBER, .str = "7", .len = 1};
 * jevent_send(&sink, &ev);   // sink.number(sink.opaque, "7", 1)
 * ```
 *
 *   field    meaning                                          for
 *   type     JsonEventType, the id every json/ header uses        all
 *   str,len  the text, valid until the next event             error, number, string, key
 *   at       where the event starts; only if `has_at`         optional
 *
 * no engine; a sink must not require `at`. */
typedef struct {
  JsonEventType type;
  const char* str;
  size_t len;
  SourcePos at;
  unsigned has_at : 1;
} JsonEvent;

/* JsonEventSink: the JSON events as function pointers, so a producer can drive any
 * sink without knowing what it is; the twin of XmlEventSink.
 *
 * One member per JsonEventType (`true_`/`false_` because `true`/`false` are macros).
 * `opaque` is passed as the first argument. A member returns what the sink's own call
 * returns: the bytes written (>= 0), or a negative error code of the sink.
 *
 * ```c
 * JsonEventSink w = jwrite_sink(&wr);
 * w.object_start(w.opaque);
 * w.key(w.opaque, "id", 2);
 * w.number(w.opaque, "7", 1);
 * w.object_end(w.opaque);
 * ```
 *
 * a member may be NULL only for jevent_send(), which then drops the event. */
typedef struct {
  void* opaque;
  ssize_t (*error)(void*, const char* str, size_t len);
  ssize_t (*null)(void*);
  ssize_t (*true_)(void*);
  ssize_t (*false_)(void*);
  ssize_t (*number)(void*, const char* str, size_t len);
  ssize_t (*string)(void*, const char* str, size_t len);
  ssize_t (*array_start)(void*);
  ssize_t (*array_end)(void*);
  ssize_t (*object_start)(void*);
  ssize_t (*object_end)(void*);
  ssize_t (*key)(void*, const char* str, size_t len);
} JsonEventSink;

/* jevent_send: calls the sink member that matches `ev->type`.
 *
 *   const JsonEventSink*  sink  where the event goes
 *   const JsonEvent*      ev    the event; `str`/`len` are read for error, number, string, key
 *
 *   returns ssize_t  what the member returns; 0 when the member is NULL or the type unknown */
ssize_t jevent_send(const JsonEventSink* sink, const JsonEvent* ev);

/* the pumps: a JSON parser drives a sink.
 *
 * ```c
 * jr_state_t st; jr_state_init(&st);
 * JsonEventSink sink = jwrite_sink(&wr);
 * jread_pump(&st, &sink, chunk, n);      // push parser: once per chunk
 * jread_pump_finish(&st, &sink);         // then once at the end
 * json_pump(parser, &sink);              // pull parser: runs until it needs data or ends
 * ```
 *
 * Each parser event becomes one jevent_send(). An error is sent as a `JSON_EVENT_ERROR` event
 * and parsing goes on after the parser's own resync; its position stays in the parser
 * (jr_state_t.err_pos, JsonParser.err_at), since sink members take no position. Comments are
 * dropped. Output is the same whatever the chunk size.
 *
 *   returns ssize_t  JSON_PUMP_END (0) all input consumed; JSON_PUMP_AGAIN (1) the pull parser
 *                    is out of data (it cannot tell the end of its input from a pause); < 0 the first sink failure,
 *                    after which nothing more is sent */
#define JSON_PUMP_END 0
#define JSON_PUMP_AGAIN 1

ssize_t jread_pump(struct jr_state*, const JsonEventSink* sink, const char* chunk, size_t len);
ssize_t jread_pump_finish(struct jr_state*, const JsonEventSink* sink);
struct json_parser;

ssize_t json_pump(struct json_parser*, const JsonEventSink* sink);

#endif /* JEVENT_H */
