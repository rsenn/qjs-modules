#ifndef XEVENT_H
#define XEVENT_H

#include <stddef.h>
#include <sys/types.h>
#include "srcpos.h"

/* XmlEventType: the kind of an XML event; the twin of jr_type_t (json/jread.h).
 *
 * the first five values equal xml_event_t's (xml/xml.h), so what xml_parser_run()
 * returns can be used as an XmlEventType; checked at build time in xevent.c.
 * XML_PARSE_AGAIN/OK are statuses of the pull parser, not events.
 *
 *   id  type              name       value      stands for
 *   -1  XE_ERROR          -          text       a parse error (XML_PARSE_ERROR)
 *    1  XE_ELEMENT_START  tag name   -          <a
 *    2  XE_ATTRIBUTE      attr name  text/none  ` b="c"`; no value is a bare attribute
 *    3  XE_ELEMENT_END    tag name   -          </a> or the end of <a />
 *    4  XE_TEXT           -          text       a run of character data
 *    5  XE_COMMENT        -          text       <!--text-->
 *    6  XE_DECLARATION    -          text       <!text>  (<!DOCTYPE ...>)
 *    7  XE_INSTRUCTION    -          text       <?text?>
 *    8  XE_CDATA          -          text       <![CDATA[text]]> */
typedef enum {
  XE_ERROR = -1,
  XE_ELEMENT_START = 1,
  XE_ATTRIBUTE,
  XE_ELEMENT_END,
  XE_TEXT,
  XE_COMMENT,
  XE_DECLARATION,
  XE_INSTRUCTION,
  XE_CDATA,
} XmlEventType;

/* XmlEvent: one XML event, as a plain value; the twin of JsonEvent (json/jevent.h).
 *
 * ```c
 * XmlEvent ev = {.type = XE_ATTRIBUTE, .name = "id", .nlen = 2, .value = "7", .vlen = 1};
 * xevent_send(&sink, &ev);   // sink.attribute(sink.opaque, "id", 2, "7", 1)
 * ```
 *
 * `name`/`value` are valid until the next event; `value` NULL means none (a bare
 * attribute). `at` is where the event starts, only if `has_at`; a sink must not
 * require it. no engine. */
typedef struct {
  XmlEventType type;
  const char *name, *value;
  size_t nlen, vlen;
  SourcePos at;
  unsigned has_at : 1;
} XmlEvent;

/* XmlEventSink: the XML events as function pointers, so a producer can drive any sink
 * without knowing what it is; the twin of JsonEventSink.
 *
 * One member per XmlEventType; `opaque` is passed as the first argument. A member
 * returns what the sink's own call returns: the bytes written (>= 0), or a negative
 * error code of the sink.
 *
 * ```c
 * XmlEventSink w = xwrite_sink(&wr);
 * w.element_start(w.opaque, "a", 1);
 * w.attribute(w.opaque, "id", 2, "7", 1);
 * w.text(w.opaque, "x", 1);
 * w.element_end(w.opaque, "a", 1);
 * ```
 *
 * a member may be NULL only for xevent_send(), which then drops the event. */
typedef struct {
  void* opaque;
  ssize_t (*error)(void*, const char* str, size_t len);
  ssize_t (*element_start)(void*, const char* name, size_t len);
  ssize_t (*attribute)(void*, const char* name, size_t nlen, const char* value, size_t vlen);
  ssize_t (*element_end)(void*, const char* name, size_t len);
  ssize_t (*text)(void*, const char* str, size_t len);
  ssize_t (*comment)(void*, const char* str, size_t len);
  ssize_t (*declaration)(void*, const char* str, size_t len);
  ssize_t (*instruction)(void*, const char* str, size_t len);
  ssize_t (*cdata)(void*, const char* str, size_t len);
} XmlEventSink;

/* xevent_send: calls the sink member that matches `ev->type`.
 *
 *   const XmlEventSink*  sink  where the event goes
 *   const XmlEvent*      ev    the event; for error, text, comment, declaration, instruction
 *                              and cdata the text is `name`/`nlen`
 *
 *   returns ssize_t  what the member returns; 0 when the member is NULL or the type unknown */
ssize_t xevent_send(const XmlEventSink* sink, const XmlEvent* ev);

/* the pumps: an XML parser drives a sink.
 *
 * ```c
 * xr_state_t st; xr_state_init(&st);
 * XmlEventSink sink = xwrite_sink(&wr);
 * xread_pump(&st, &sink, chunk, n);      // push parser: once per chunk
 * xread_pump_finish(&st, &sink);         // then once at the end
 * xml_pump(parser, &sink);               // pull parser: runs until it needs data or ends
 * ```
 *
 * Each parser event becomes one xevent_send(); text is sent as `name`/`nlen`. A parse error
 * is sent as an XE_ERROR event; its position stays in the parser (xr_state_t.err_pos,
 * XMLParser.tag_line/tag_column), since sink members take no position. The comment, declaration, instruction and CDATA
 * events are not produced yet: those still arrive as elements with magic names.
 *
 *   returns ssize_t  XML_PUMP_END (0) all input consumed; XML_PUMP_AGAIN (1) the pull parser
 *                    needs more data; XML_PUMP_ERROR (2) stopped at an unrecoverable parse
 *                    error; < 0 the first sink failure, after which nothing more is sent */
#define XML_PUMP_END 0
#define XML_PUMP_AGAIN 1
#define XML_PUMP_ERROR 2

struct xr_state;
struct xml_parser;

ssize_t xread_pump(struct xr_state*, const XmlEventSink* sink, const char* chunk, size_t len);
ssize_t xread_pump_finish(struct xr_state*, const XmlEventSink* sink);
ssize_t xml_pump(struct xml_parser*, const XmlEventSink* sink);

#endif /* XEVENT_H */
