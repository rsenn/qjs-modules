#ifndef JEVENT_H
#define JEVENT_H

#include <stddef.h>
#include <sys/types.h>
#include "json/jread.h"
#include "srcpos.h"

/* JsonEvent: one JSON event, as a plain value; the twin of XmlEvent (xml/xevent.h).
 *
 * ```c
 * JsonEvent ev = {.type = jr_type_number, .str = "7", .len = 1};
 * jevent_send(&sink, &ev);   // sink.number(sink.opaque, "7", 1)
 * ```
 *
 *   field    meaning                                          for
 *   type     jr_type_t, the id jread and jbuilder use         all
 *   str,len  the text, valid until the next event             error, number, string, key
 *   at       where the event starts; only if `has_at`         optional
 *
 * no engine; a sink must not require `at`. */
typedef struct {
  jr_type_t type;
  const char* str;
  size_t len;
  SourcePos at;
  unsigned has_at : 1;
} JsonEvent;

/* JsonEventSink: the JSON events as function pointers, so a producer can drive any
 * sink without knowing what it is; the twin of XmlEventSink.
 *
 * One member per jr_type_t (`true_`/`false_` because `true`/`false` are macros).
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
 * Each parser event becomes one jevent_send(). An error is sent as a `jr_type_error` event
 * and parsing goes on after the parser's own resync; its position stays in the parser
 * (jr_state_t.err_pos, JsonParser.err_at), since sink members take no position. Comments are
 * dropped. Output is the same whatever the chunk size.
 *
 *   returns ssize_t  JSON_PUMP_END (0) all input consumed; JSON_PUMP_AGAIN (1) the pull parser
 *                    is out of data (it cannot tell the end of its input from a pause); < 0 the first sink failure,
 *                    after which nothing more is sent */
#define JSON_PUMP_END 0
#define JSON_PUMP_AGAIN 1

ssize_t jread_pump(jr_state_t*, const JsonEventSink* sink, const char* chunk, size_t len);
ssize_t jread_pump_finish(jr_state_t*, const JsonEventSink* sink);
struct JsonParser;

ssize_t json_pump(struct JsonParser*, const JsonEventSink* sink);

#endif /* JEVENT_H */
