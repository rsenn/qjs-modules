#ifndef XEVENT_H
#define XEVENT_H

#include <stddef.h>
#include <sys/types.h>
#include "srcpos.h"

/**
 * \defgroup xevent xevent: XML events and event sinks
 * @{
 */

/* XmlEventType: the kind of an XML event; the twin of JsonEventType (json/jevent.h).
 *
 * the one vocabulary of every xml/ header: xread (callbacks), xml (xml_parse()
 * results), xbuilder, xwrite and the sinks. XML_PARSE_AGAIN/OK (xml/xml.h) are
 * statuses of the pull parser, not events.
 *
 *   id  type              name       value      stands for
 *   -1  XE_ERROR          -          text       a parse error (XE_ERROR)
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
 * attribute). An XE_ERROR event also has `code` and `span`. `at` is where the event starts, only if `has_at`; a sink must not
 * require it. no engine. */
typedef struct {
  XmlEventType type;
  union {
    const char* name; /* element_start, attribute, element_end */
    const char* str;  /* error, text, comment, declaration, instruction, cdata */
  };
  const char* value;
  union {
    size_t nlen;
    size_t len;
  };
  size_t vlen;
  const char* code; /* error only: the stable kebab-case name, e.g. mismatched-end-tag; a static string */
  size_t span;      /* error only: bytes from `at` through the offending byte, 0 at the end of input */
  SourcePos at;
  unsigned has_at : 1;
  unsigned self_closing : 1; /* element_end only: from <a/> or a void tag, xml_next() sets it */
} XmlEvent;

/* XmlEventSink: the XML events as function pointers, so a producer can drive any sink
 * without knowing what it is, the twin of JsonEventSink.
 *
 * One member per XmlEventType, opaque is passed as the first argument. A member
 * returns what the sink's own call returns: the bytes written (>= 0), or a negative
 * error code of the sink.
 *
 *   XmlEventSink w = xwrite_sink(&wr);
 *   w.element_start(w.opaque, "a", 1);
 *   w.attribute(w.opaque, "id", 2, "7", 1);
 *   w.text(w.opaque, "x", 1);
 *   w.element_end(w.opaque, "a", 1);
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

/* xevent_send: calls the sink member that matches ev->type.
 *
 *   const XmlEventSink*  sink  where the event goes
 *   const XmlEvent*      ev    the event; for error, text, comment, declaration, instruction
 *                              and cdata the text is name and nlen
 *
 *   returns ssize_t  what the member returns, 0 when the member is NULL or the type unknown */
ssize_t xevent_send(const XmlEventSink* sink, const XmlEvent* ev);

/* XmlPull: what a pull source (xml_next(), xwalk_event()) answers; the twin of JsonPull.
 *
 *   XML_PULL_FAIL   -1  an exception is pending; only sources that run JS (the walkers)
 *   XML_PULL_END     0  no more events (also after a terminal XE_ERROR)
 *   XML_PULL_AGAIN   1  the Reader has no data now; call again once it may
 *   XML_PULL_EVENT   2  *ev is filled
 *
 * END and AGAIN equal XML_PUMP_END and XML_PUMP_AGAIN. */
typedef enum {
  XML_PULL_FAIL = -1,
  XML_PULL_END = 0,
  XML_PULL_AGAIN = 1,
  XML_PULL_EVENT = 2,
} XmlPull;

/* XmlEventHandler: where a push source delivers each event; the first negative result stops it.
 * xevent_handler() has this shape with `user` a const XmlEventSink*. */
typedef ssize_t XmlEventHandler(void* user, const XmlEvent* ev);

ssize_t xevent_handler(void* sink, const XmlEvent* ev);

/* xevent_markup: types the magic element name of a comment, declaration or CDATA section.
 *
 *   XmlEvent*    ev    receives type, str and len; other fields are left alone
 *   const char*  name  the tag name, name[0] is '!'
 *   size_t       len   length of name
 *
 *   name                 type            text
 *   !--text--            XE_COMMENT      text
 *   ![CDATA[text]]       XE_CDATA        text
 *   !DOCTYPE html        XE_DECLARATION  DOCTYPE html
 *
 * str points into name; never throws. */
void xevent_markup(XmlEvent* ev, const char* name, size_t len);

/* XmlTee: a sink that sends every event to several sinks; the twin of JsonTee.
 * the first negative result stops that event and is returned, otherwise the first sink's. */
typedef struct {
  const XmlEventSink* sinks;
  size_t len;
} XmlTee;

XmlEventSink xtee_sink(XmlTee* tee);

/* XmlEventSource: any pull source behind one function pointer; the twin of JsonEventSource. */
typedef struct {
  void* opaque;
  XmlPull (*next)(void* opaque, XmlEvent* ev);
} XmlEventSource;

/* xevent_pump: sends the events of `src` to `sink` until the source stops or the sink fails.
 *
 * returns ssize_t  the XmlPull that stopped it (END, AGAIN or FAIL), or the first negative
 *                  result of the sink. An XE_ERROR event is sent like any other. */
ssize_t xevent_pump(XmlEventSource src, const XmlEventSink* sink);

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
 * Each parser event becomes one xevent_send(); text is sent as `str`/`len`. A parse error
 * is sent as an XE_ERROR event; its position stays in the parser (xr_state_t.err_pos,
 * XMLParser.tag_line/tag_column), since sink members take no position. xml_pump() types the markup (xml_next()); the push
 * parser xread produces no comment, declaration, instruction or CDATA events yet.
 *
 *   returns ssize_t  XML_PUMP_END (0) all input consumed; XML_PUMP_AGAIN (1) the pull parser
 *                    needs more data; < 0 the first sink failure, after which nothing more is sent.
 *                    An unrecoverable parse error is an XE_ERROR event, then XML_PUMP_END;
 *                    XML_PUMP_ERROR is no longer returned. */
#define XML_PUMP_END 0
#define XML_PUMP_AGAIN 1
#define XML_PUMP_ERROR 2

struct xr_state;
struct xml_parser;

ssize_t xread_pump(struct xr_state*, const XmlEventSink* sink, const char* chunk, size_t len);
ssize_t xread_pump_finish(struct xr_state*, const XmlEventSink* sink);
ssize_t xml_pump(struct xml_parser*, const XmlEventSink* sink);

/**
 * @}
 */

#endif /* XEVENT_H */
