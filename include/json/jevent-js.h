#ifndef JEVENT_JS_H
#define JEVENT_JS_H

#include <quickjs.h>
#include "json/jevent.h"

/**
 * \addtogroup jevent
 * @{
 */

/* js_jsonevent_value: the JS value of a JSON event.
 *
 *   NULL, TRUE, FALSE         null, true, false
 *   NUMBER                    the number (0 for text no number has)
 *   STRING, KEY, ERROR        the text as a string
 *   containers, COMMENT, RAW  undefined
 *
 * returns the value (refcount: a new reference), never throws except out of memory. */
JSValue js_jsonevent_value(JSContext*, const JsonEvent* ev);

/* js_jsonevent_new: a JsonEvent as a plain object, what JsonParser.next() returns.
 *
 *   { type: JsonEventType, value?: js_jsonevent_value(), loc: { line, column, offset } }
 *
 * value is left out for the events that carry none, loc is the 1-based position of the token
 * start (offset in characters). returns the object (refcount: a new reference), or JS_EXCEPTION. */
JSValue js_jsonevent_new(JSContext*, const JsonEvent* ev);

/** @} */
#endif /* JEVENT_JS_H */
