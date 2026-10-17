#ifndef XML_H
#define XML_H

#include "stream-utils.h"
#include "location.h"

#include <stdint.h>
#include <stddef.h>
#include <cutils.h>
#include "xml/xevent.h"
/**
 * \defgroup xml xml: streaming, JSValue-free XML parser
 * @{
 *
 * The parser behind read() (quickjs-xml.c), XMLParser and XMLNodeParser. It reads from a stream-utils Reader
 * (one byte at a time, via peek/consume - never assumes the whole document is in
 * one addressable buffer) instead of a flat in-memory buffer, tracks source
 * location as it goes, and is a *pull* parser: each xml_parse() call advances
 * just far enough to produce exactly one event (or a status), which it returns
 * directly - there's no callback, no JSValue, no tree. It reuses js_xml_parse()'s
 * macro names (yield_push, yield_pop, yield_add, yield_next, yield_return,
 * parse_getc, parse_loc, parse_skip, parse_until, parse_skipspace, parse_is,
 * parse_inside, parse_close) with the same role in the control flow, but each
 * implemented in terms of the Reader/Location/pull-return primitives instead of a
 * Vector<OutputValue> stack of JSValue objects.
 *
 * xml_parse() is restartable in two distinct ways:
 *  - XML_PARSE_AGAIN: the Reader has no data available right now (reader_getc()
 *    returning STREAM_ERROR, used here as an EAGAIN convention, not necessarily a
 *    real I/O error). Call xml_parse() again once the same Reader may have
 *    more to give it.
 *  - An XmlEventType (XE_ELEMENT_START/XE_ATTRIBUTE/XE_ELEMENT_END/XE_TEXT):
 *    one event is ready - read it from event_name/event_value/loc, then call
 *    xml_parse() again to advance to the next one.
 * Either way, resuming picks up exactly where the previous call left off, never
 * from the top - down to mid-token, mid-tag, mid-attribute-value.
 */

typedef struct xml_str {
  const char* data;
  size_t len;
} xml_str_t;

/* xml_parse() returns an XmlEventType (xml/xevent.h) or one of these statuses,
 * which are not events. XE_ERROR (-1) is also a result: a non-tolerant mismatched
 * closing tag - terminal, event_name is the offending tag.
 *
 *   XE_ELEMENT_START  event_name = tag name, event_has_value = 0
 *   XE_ATTRIBUTE      event_name = attribute name; event_has_value = 0 for a valueless
 *                     attribute (<input disabled>), else 1 with event_value = the value
 *   XE_ELEMENT_END    event_name = tag name, event_has_value = 0; once per
 *                     XE_ELEMENT_START, also for elements the input never closed
 *   XE_TEXT           event_name unset, event_has_value = 1, event_value = the text run */
enum {
  XML_PARSE_AGAIN = -2, /* the Reader has no data right now; call xml_parse()
                            again once it might */
  XML_PARSE_OK = 0,     /* clean end of stream - terminal */
};

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
  unsigned flat : 1; /* no open-element stack: a start tag is never closed by itself and every closing
                        tag becomes an XE_ELEMENT_END, matched or not; raw-text elements are not special */
  unsigned no_implied_end : 1; /* tolerant mode normally closes an open <li>, <p>, ... when a sibling
                                  starts (HTML5 optional end tags); set this to turn that off */
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

  DynBuf ev_name;  /* dedicated output buffer for event_name - stable until next event */
  DynBuf ev_value; /* dedicated output buffer for event_value - stable until next event */

  XMLFrame* top; /* open-element stack; NULL at the document root */

  /* Set right before xml_parse() returns an XmlEventType (not for
   * XML_PARSE_OK/AGAIN/ERROR, except event_name is also set for XE_ERROR).
   * Points into ev_name/ev_value above - stable until the next event is produced,
   * unlike the old design where these pointed into name/attr/text scan buffers
   * which could be freed and re-initialized before the next xml_parse() call. */
  xml_str_t event_name;
  xml_str_t event_value;
  unsigned event_has_value : 1;

  /* 1-based line and column of the '<' that began the tag the last element event
   * belongs to (a closing tag's own '<' for the END events it causes). */
  uint32_t tag_line, tag_column;

  /* set with XE_ERROR: stable name of the error and the characters the offending
   * token spans, starting at tag_line/tag_column */
  const char* err_code;
  size_t err_len;

  /* xml_parse()'s own locals, promoted to fields: suspending (XML_PARSE_AGAIN,
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
  size_t text_pos;          /* how much of `text` xml_yield_text()'s inlined splitting
                                loop has already turned into XE_TEXT events */
  XMLFrame* closing_target; /* mid closing-tag handling: which frame yield_return()
                                is popping its way down to */
} XMLParser;

void xml_init(XMLParser*, Reader* reader);
void xml_free(XMLParser*);
void xml_set_tolerant(XMLParser*, int tolerant);
void xml_set_implied_end(XMLParser*, int enable);
void xml_set_flat(XMLParser*, int flat);
void xml_set_self_closing_tags(XMLParser*, const char* const* tags);

/* Advances the parse just far enough to produce one XmlEventType (read it back from
 * event_name/event_value/event_has_value/loc), or a status: XML_PARSE_OK (clean
 * EOF), XML_PARSE_AGAIN (call again once the Reader may have more), or
 * XE_ERROR (non-tolerant mismatched closing tag; terminal). Matches
 * js_xml_parse()'s own leniency about truncated input: real EOF with elements
 * still open is not an error, it just stops (no synthetic XE_ELEMENT_END events
 * are made up for them). */
int xml_parse(XMLParser*);

/**
 * @}
 */
#endif /* defined(XML_H) */
