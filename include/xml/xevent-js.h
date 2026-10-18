#ifndef XEVENT_JS_H
#define XEVENT_JS_H

#include <quickjs.h>
#include "xml/xevent.h"

/**
 * \addtogroup xevent
 * @{
 */

/* js_xmlevent_new: an XmlEvent as a plain object, what XMLParser.next() returns.
 *
 *   { type: XmlEventType, name?, value?, loc: { line, column } }
 *
 *   element_start, element_end   name
 *   attribute                    name, value (left out for a bare attribute)
 *   text, comment, declaration,
 *   instruction, cdata           value
 *
 * loc is the 1-based line and column of the tag the event belongs to. returns the object
 * (refcount: a new reference), or JS_EXCEPTION. */
JSValue js_xmlevent_new(JSContext*, const XmlEvent* ev);

/** @} */
#endif
