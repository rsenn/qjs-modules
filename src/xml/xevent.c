/* xevent.c: xevent_send(), and the build-time check that XmlEventType extends xml_event_t.
 * depends on: xevent.h, xml.h (ids only).
 * rule: no state; one call per event, the sink decides what it means. */

#define _GNU_SOURCE 1

#include "xml/xevent.h"
#include "xml/xml.h"

_Static_assert((int)XE_ERROR == (int)XML_PARSE_ERROR, "XE_ERROR is XML_PARSE_ERROR");
_Static_assert((int)XE_ELEMENT_START == (int)XML_ELEMENT_START, "XE_ELEMENT_START is XML_ELEMENT_START");
_Static_assert((int)XE_ATTRIBUTE == (int)XML_ATTRIBUTE, "XE_ATTRIBUTE is XML_ATTRIBUTE");
_Static_assert((int)XE_ELEMENT_END == (int)XML_ELEMENT_END, "XE_ELEMENT_END is XML_ELEMENT_END");
_Static_assert((int)XE_TEXT == (int)XML_TEXT, "XE_TEXT is XML_TEXT");

ssize_t
xevent_send(const XmlEventSink* sink, const XmlEvent* ev) {
  switch(ev->type) {
    case XE_ERROR: return sink->error ? sink->error(sink->opaque, ev->name, ev->nlen) : 0;
    case XE_ELEMENT_START: return sink->element_start ? sink->element_start(sink->opaque, ev->name, ev->nlen) : 0;
    case XE_ATTRIBUTE: return sink->attribute ? sink->attribute(sink->opaque, ev->name, ev->nlen, ev->value, ev->vlen) : 0;
    case XE_ELEMENT_END: return sink->element_end ? sink->element_end(sink->opaque, ev->name, ev->nlen) : 0;
    case XE_TEXT: return sink->text ? sink->text(sink->opaque, ev->name, ev->nlen) : 0;
    case XE_COMMENT: return sink->comment ? sink->comment(sink->opaque, ev->name, ev->nlen) : 0;
    case XE_DECLARATION: return sink->declaration ? sink->declaration(sink->opaque, ev->name, ev->nlen) : 0;
    case XE_INSTRUCTION: return sink->instruction ? sink->instruction(sink->opaque, ev->name, ev->nlen) : 0;
    case XE_CDATA: return sink->cdata ? sink->cdata(sink->opaque, ev->name, ev->nlen) : 0;
  }

  return 0;
}
