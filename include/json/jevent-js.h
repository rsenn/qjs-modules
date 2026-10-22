#ifndef JEVENT_JS_H
#define JEVENT_JS_H

#include <quickjs.h>
#include "json/jevent.h"
#include "location.h"

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
 *   { type: JsonEventType, value?: js_jsonevent_value(), raw?: string, loc: { line, column, offset } }
 *
 * a COMMENT has its text as value, a NUMBER has its source text as raw (1.50, 12345678901234567890).
 * value is left out for the events that carry none, loc is the 1-based position of the token
 * start (offset in characters). returns the object (refcount: a new reference), or JS_EXCEPTION. */
JSValue js_jsonevent_new(JSContext*, const JsonEvent* ev);

/* json_read_value: JSON.parse() of UTF-8 bytes, a strict parser feeding a JsonBuilder.
 *
 *   const uint8_t*  data  the JSON text
 *   size_t          size  its length in bytes
 *   const char*     name  optional: the input name for error messages
 *
 * objects get Object.prototype and a "__proto__" key stays a key, like JSON.parse. Nothing
 * recurses, so the depth is not limited by the C stack. Blob.prototype.json() is built on it.
 *
 * returns the value, or JS_EXCEPTION with a SyntaxError pending (loc, length and code set),
 * also for input without a value. */
JSValue json_read_value(JSContext*, const uint8_t* data, size_t size, const char* name);

/* json_new_from_js: the pull parser of the JsonParser constructor.
 *
 *   JSValueConst  input    a string, buffer, read function, object with read() or fd
 *   JSValueConst  options  a file name, or { dialect, strict, comments, filename }, or undefined
 *
 * returns the parser (json_free() it), or NULL with an exception pending. */
struct json_parser* json_new_from_js(JSContext*, JSValueConst input, JSValueConst options);

/* js_jsonevent_throw: throws the SyntaxError of an error event, the file name is that of base.
 *
 * returns nothing, an exception is pending (unless out of memory cloned the location). */
void js_jsonevent_throw(JSContext*, const Location* base, const JsonEvent* ev);

/** @} */
#endif /* JEVENT_JS_H */
