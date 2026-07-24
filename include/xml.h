#ifndef XML_H
#define XML_H

#include <stdint.h>
#include <stddef.h>
#include <cutils.h>
#include "stream-utils.h"
#include "location.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \defgroup xml xml: streaming, JSValue-free XML parser
 * @{
 *
 * A port of js_xml_parse() (quickjs-xml.c) that reads from a stream-utils Reader
 * (one byte at a time, via peek/consume - never assumes the whole document is in
 * one addressable buffer) instead of a flat in-memory buffer, tracks source
 * location as it goes, and is a *pull* parser: each xml_parser_run() call advances
 * just far enough to produce exactly one event (or a status), which it returns
 * directly - there's no callback, no JSValue, no tree. It reuses js_xml_parse()'s
 * macro names (yield_push, yield_pop, yield_add, yield_next, yield_return,
 * parse_getc, parse_loc, parse_skip, parse_until, parse_skipspace, parse_is,
 * parse_inside, parse_close) with the same role in the control flow, but each
 * implemented in terms of the Reader/Location/pull-return primitives instead of a
 * Vector<OutputValue> stack of JSValue objects.
 *
 * xml_parser_run() is restartable in two distinct ways:
 *  - XML_PARSE_AGAIN: the Reader has no data available right now (reader_getc()
 *    returning STREAM_ERROR, used here as an EAGAIN convention, not necessarily a
 *    real I/O error). Call xml_parser_run() again once the same Reader may have
 *    more to give it.
 *  - An xml_event_t (XML_ELEMENT_START/XML_ATTRIBUTE/XML_ELEMENT_END/XML_TEXT):
 *    one event is ready - read it from event_name/event_value/loc, then call
 *    xml_parser_run() again to advance to the next one.
 * Either way, resuming picks up exactly where the previous call left off, never
 * from the top - down to mid-token, mid-tag, mid-attribute-value.
 */

typedef struct xml_str {
  const char* data;
  size_t len;
} xml_str_t;

typedef enum xml_event {
  XML_PARSE_AGAIN = -2, /* the Reader has no data right now; call xml_parser_run()
                            again once it might */
  XML_PARSE_ERROR = -1, /* non-tolerant mismatched closing tag - terminal: don't call
                            xml_parser_run() again. event_name is the offending tag */
  XML_PARSE_OK = 0,     /* clean end of stream - terminal */
  XML_ELEMENT_START,    /* event_name = tag name, event_has_value = 0 */
  XML_ATTRIBUTE,        /* event_name = attribute name; event_has_value = 0 for a
                            boolean/valueless attribute (e.g. <input disabled>),
                            else 1 with event_value = the attribute value */
  XML_ELEMENT_END,      /* event_name = tag name, event_has_value = 0. Delivered
                            once per XML_ELEMENT_START, even for elements the input
                            never explicitly closed (tolerant recovery implicitly
                            closes them when an ancestor's closing tag is found) */
  XML_TEXT,              /* event_name unset, event_has_value = 1, event_value = the
                            text run */
} xml_event_t;

/* One entry per still-open element, oldest-first via ->parent; owns its own name
 * copy since - unlike js_xml_parse()'s OutputValue, whose .name/.namelen are raw
 * pointers into the caller's single in-memory buffer - nothing here guarantees a
 * tag name read many reader_getc() calls ago is still sitting in one place. */
typedef struct xml_frame {
  struct xml_frame* parent;
  char* name;
  size_t namelen;
} XMLFrame;

typedef struct xml_parser {
  Reader* reader;
  Location loc;

  unsigned tolerant : 1;
  const char* const* self_closing_tags;

  DynBuf peek;   /* lookahead FIFO: bytes read from `reader` but not yet consumed -
                     backs the ptr[1]/ptr[2]/... lookahead js_xml_parse gets for free
                     from its flat buffer (comment/script-close detection) */
  DynBuf* accum; /* NULL, or which of the three buffers below parse_getc() appends
                     the byte it's about to consume to */
  DynBuf name;   /* current tag name - persists across attribute scanning, since it's
                     needed again for yield_push()/the closing tag lookup */
  DynBuf attr;   /* current attribute name */
  DynBuf text;   /* current attribute value, or text/script-content run */

  XMLFrame* top; /* open-element stack; NULL at the document root */

  /* Set right before xml_parser_run() returns an xml_event_t (not for
   * XML_PARSE_OK/AGAIN/ERROR, except event_name is also set for XML_PARSE_ERROR).
   * Points into name/attr/text above - valid only until the next xml_parser_run()
   * call, same as a borrowed pointer handed to a callback would have been. */
  xml_str_t event_name;
  xml_str_t event_value;
  unsigned event_has_value : 1;

  /* xml_parser_run()'s own locals, promoted to fields: suspending (XML_PARSE_AGAIN,
   * or having just returned an event) can happen from deep inside a loop, and a
   * later call resumes by jumping (via computed goto) straight back into that exact
   * spot in a *fresh* call frame, so nothing that needs to survive that can be a
   * plain C local. */
  void* resume;
  unsigned primed : 1;
  unsigned done : 1;
  unsigned closing : 1;
  unsigned self_closing : 1;
  int c;
  int quote;
  size_t text_pos;       /* how much of `text` xml_yield_text()'s inlined splitting
                             loop has already turned into XML_TEXT events */
  XMLFrame* closing_target; /* mid closing-tag handling: which frame yield_return()
                                is popping its way down to */
} XMLParser;

void xml_parser_init(XMLParser*, Reader* reader);
void xml_parser_free(XMLParser*);
void xml_parser_set_tolerant(XMLParser*, int tolerant);
void xml_parser_set_self_closing_tags(XMLParser*, const char* const* tags);

/* Advances the parse just far enough to produce one xml_event_t (read it back from
 * event_name/event_value/event_has_value/loc), or a status: XML_PARSE_OK (clean
 * EOF), XML_PARSE_AGAIN (call again once the Reader may have more), or
 * XML_PARSE_ERROR (non-tolerant mismatched closing tag; terminal). Matches
 * js_xml_parse()'s own leniency about truncated input: real EOF with elements
 * still open is not an error, it just stops (no synthetic XML_ELEMENT_END events
 * are made up for them). */
xml_event_t xml_parser_run(XMLParser*);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* defined(XML_H) */
