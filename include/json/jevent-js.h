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

/** @} */
#endif /* JEVENT_JS_H */
