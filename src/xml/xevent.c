/* xevent.c: xevent_send().
 * depends on: xevent.h.
 * rule: no state; one call per event, the sink decides what it means. */

#define _GNU_SOURCE 1

#include "xml/xevent.h"
#include <string.h>

/**
 * \addtogroup xevent
 * @{
 */

ssize_t
xevent_send(const XmlEventSink* sink, const XmlEvent* ev) {
  switch(ev->type) {
    case XE_ERROR: return sink->error ? sink->error(sink->opaque, ev->str, ev->len) : 0;
    case XE_ELEMENT_START: return sink->element_start ? sink->element_start(sink->opaque, ev->str, ev->len) : 0;
    case XE_ATTRIBUTE: return sink->attribute ? sink->attribute(sink->opaque, ev->name, ev->nlen, ev->value, ev->vlen) : 0;
    case XE_ELEMENT_END: return sink->element_end ? sink->element_end(sink->opaque, ev->str, ev->len) : 0;
    case XE_TEXT: return sink->text ? sink->text(sink->opaque, ev->str, ev->len) : 0;
    case XE_COMMENT: return sink->comment ? sink->comment(sink->opaque, ev->str, ev->len) : 0;
    case XE_DECLARATION: return sink->declaration ? sink->declaration(sink->opaque, ev->str, ev->len) : 0;
    case XE_INSTRUCTION: return sink->instruction ? sink->instruction(sink->opaque, ev->str, ev->len) : 0;
    case XE_CDATA: return sink->cdata ? sink->cdata(sink->opaque, ev->str, ev->len) : 0;
  }

  return 0;
}

/* xevent_markup: what a start event with a name beginning with ! is.
 *
 *   name                 event            text
 *   !--text--            XE_COMMENT       text
 *   ![CDATA[text]]       XE_CDATA         text
 *   !DOCTYPE html        XE_DECLARATION   DOCTYPE html */
void
xevent_markup(XmlEvent* ev, const char* name, size_t len) {
  if(len >= 5 && !memcmp(name, "!--", 3) && !memcmp(name + len - 2, "--", 2)) {
    ev->type = XE_COMMENT;
    ev->str = name + 3;
    ev->len = len - 5;
  } else if(len >= 10 && !memcmp(name, "![CDATA[", 8) && !memcmp(name + len - 2, "]]", 2)) {
    ev->type = XE_CDATA;
    ev->str = name + 8;
    ev->len = len - 10;
  } else {
    ev->type = XE_DECLARATION;
    ev->str = name + 1;
    ev->len = len - 1;
  }
}

ssize_t
xevent_handler(void* sink, const XmlEvent* ev) {
  return xevent_send(sink, ev);
}

ssize_t
xevent_pump(XmlEventSource src, const XmlEventSink* sink) {
  XmlEvent ev;
  XmlPull st;
  ssize_t r;

  while((st = src.next(src.opaque, &ev)) == XML_PULL_EVENT)
    if((r = xevent_send(sink, &ev)) < 0)
      return r;

  return st;
}

/**
 * @}
 */
