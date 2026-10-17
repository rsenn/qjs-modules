#ifndef JSINK_H
#define JSINK_H

#include <quickjs.h>
#include "json/jevent.h"

/* JsonEventSink as a JS object: the twin of js_xmlsink_wrap() (xml/xsink.h) and js_walk_wrap().
 *
 * ```js
 * w.objectStart();
 * w.key('id');
 * w.number(7);          // a number or its text
 * w.true();
 * w.objectEnd();
 * ```
 *
 *   JS method               calls         argument
 *   error(str)              error         text
 *   null() true() false()   null, true_, false_
 *   number(n)               number        text of n
 *   string(str)             string        text
 *   key(str)                key           text
 *   arrayStart() arrayEnd() objectStart() objectEnd()
 *
 * returns the bytes written, as the member reports.
 * throws TypeError for a missing argument or a NULL member; a negative member result
 * throws what the class' JsonSinkThrow makes of it (Error "JsonEventSink: event failed" when none). */

/* the sink of the object `opaque`, an object's JS_GetOpaque() */
typedef JsonEventSink JsonSinkFromOpaque(void* opaque);

/* the exception for a failed member result `r` (< 0); returns JS_EXCEPTION */
typedef JSValue JsonSinkThrow(JSContext*, ssize_t r);

/* called after each successful call with the JsonEventType, e.g. to remember the last event */
typedef void JsonSinkNote(void* opaque, int type);

/* js_jsonsink_register: lets objects of `class_id` call the JsonEventSink methods.
 * `throw_`/`note` may be NULL.
 *
 * JS thread only: call it while the class is set up.
 * returns 0, or -1 when the table is full (never throws). */
int js_jsonsink_register(JSClassID, JsonSinkFromOpaque*, JsonSinkThrow* throw_, JsonSinkNote* note);

/* js_jsonsink_define: puts the eleven methods onto `obj`.
 *
 * returns 0, or -1 with an exception pending. */
int js_jsonsink_define(JSContext*, JSValueConst obj);

/* js_jsonsink_wrap: makes a JS object whose methods call `sink`.
 *
 * borrowed: sink.opaque is not owned; it must outlive the JS object.
 * returns the object (refcount: new reference), or JS_EXCEPTION. */
JSValue js_jsonsink_wrap(JSContext*, JsonEventSink sink);

#endif
