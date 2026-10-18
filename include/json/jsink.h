#ifndef JSINK_H
#define JSINK_H

#include <quickjs.h>
#include "json/jevent.h"

/**
 * \defgroup jsink jsink: JSON event sink as a JS object
 * @{
 */

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
 *   comment(str)            comment       text
 *   raw(str)                raw           text that is already JSON
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

/* JsonJsSink: a JS object used as a JsonEventSink, so a C source can drive it.
 *
 * ```js
 * { number(text) {}, string(text) {}, key(text) {}, objectStart() {}, objectEnd() {} }
 * ```
 *
 * each event calls the method of the same name (as the table above) with the text as a string,
 * numbers as their text. A method the object lacks drops that event. A method that returns a
 * negative number fails the event with it; one that throws fails it with -1 and leaves the
 * exception pending, so the pump stops and the caller rethrows. */
typedef struct json_js_sink JsonJsSink;

/* js_jsonsink_from_js: makes *sink call the methods of `obj`.
 *
 * borrowed: obj is dup'ed; free the result with js_jsonsink_from_js_free().
 * returns the adapter (sink->opaque), or NULL with an exception pending. */
JsonJsSink* js_jsonsink_from_js(JSContext*, JSValueConst obj, JsonEventSink* sink);

/* js_jsonsink_from_js_free: releases the adapter and the object reference. never throws. */
void js_jsonsink_from_js_free(JSContext*, JsonJsSink*);

/* js_jsonsink_from_js_free_rt: the same for a finalizer, which has a runtime and no context. */
void js_jsonsink_from_js_free_rt(JSRuntime*, JsonJsSink*);

/**
 * @}
 */

/* js_jsonsink_failed: the exception for a sink member that returned r < 0.
 *
 * a JS-object sink (adapter) leaves its own exception pending, a native sink gives an Error with
 * code r. returns JS_EXCEPTION. */
JSValue js_jsonsink_failed(JSContext*, int adapter, ssize_t r);

#endif
