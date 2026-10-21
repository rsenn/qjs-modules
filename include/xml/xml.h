#ifndef XML_H
#define XML_H

#include "stream-utils.h"
#include "location.h"
#include "xml/xevent.h"
#include "xml/xread.h"

#include <stdint.h>
#include <stddef.h>

/**
 * \defgroup xml xml: streaming XML parser
 * @{
 *
 * The pull parser behind XMLParser and read(): a Reader in and one XmlEvent per xml_next().
 * xread does the parsing (XR_COMPAT mode): xml_next() reads a block, feeds it to xr_feed() and
 * queues the events it makes, so a block may leave many events behind.
 */

#define XML_BLOCK_SIZE 16384

typedef struct xml_parser {
  Reader* reader;
  Location* loc; /* borrowed, NULL for none: set to where the last event came from, 0-based */
  xr_state_t st;
  unsigned tolerant : 1;
  unsigned flat : 1;           /* no open-element stack: a start tag is never closed by itself and every closing
                                  tag becomes an XE_ELEMENT_END, matched or not; raw-text elements are not special */
  unsigned no_implied_end : 1; /* tolerant mode normally closes an open <li>, <p>, ... when a sibling
                                  starts (HTML5 optional end tags); set this to turn that off */
  unsigned started : 1;
  unsigned ended : 1;
  unsigned opening : 1; /* the last event started an element that depth does not count yet */
  const char* const* self_closing_tags;
  uint32_t depth; /* open elements, as of the last event returned */
  struct xml_queue {
    XmlEvent* ev;
    size_t head, len, cap;
    char* text;
    size_t text_len, text_cap;
  } queue;
  uint8_t block[XML_BLOCK_SIZE];
} XMLParser;

/* xml_init: sets up p, reading from reader (borrowed: it must outlive p). Set tolerant, flat,
 * no_implied_end and self_closing_tags (default: the HTML void elements) before the first xml_next(). */
void xml_init(XMLParser*, Reader* reader);
void xml_free(XMLParser*);
void xml_set_tolerant(XMLParser*, int tolerant);
void xml_set_implied_end(XMLParser*, int enable);
void xml_set_flat(XMLParser*, int flat);

/* xml_next: the next event of a pull parser, written to *ev .
 *
 *   XMLParser*  p   the parser, set up by xml_init()
 *   XmlEvent*   ev  filled when the result is XML_PULL_EVENT
 *
 *   returns XmlPull  XML_PULL_EVENT, XML_PULL_AGAIN (the Reader has no data right now) or
 *                    XML_PULL_END (clean end of input, and after a terminal XE_ERROR event)
 *
 * in the event
 *   - type is an XmlEventType; markup arrives typed, XE_COMMENT, XE_DECLARATION, XE_INSTRUCTION
 *     and XE_CDATA with their text in str / len, not as elements with the names !-- ! ? ![CDATA[
 *   - name / nlen of element_start, attribute and element_end, value / vlen of an attribute
 *     (value NULL: a bare attribute); text in str / len; all valid until the next call
 *   - at is the line and column of the tag the event belongs to, has_at set for all but text
 *   - an XE_ERROR event has the stable error code in code (mismatched-end-tag) and the message in str
 *
 * never throws. */
XmlPull xml_next(XMLParser*, XmlEvent* ev);

/* xml_next_end is not needed: the Reader answering end of input ends the document, xr_end() runs
 * then and what it completes is delivered before XML_PULL_END. */

/* xml_source: the pull parser as an XmlEventSource, for xevent_pump(). */
XmlEventSource xml_source(XMLParser*);

/**
 * @}
 */
#endif /* defined(XML_H) */
