#ifndef XSINK_H
#define XSINK_H

#include <quickjs.h>
#include "xml/xevent.h"

/**
 * \defgroup xsink xsink: XML event sink as a JS object
 * @{
 */

/* XmlEventSink as a JS object: the twin of js_walk_wrap()/js_walk_define() (walk.h).
 *
 * ```js
 * w.elementStart('a');
 * w.attribute('id', '7');   // value omitted, undefined, null or true: a bare attribute
 * w.text('x < y');
 * w.elementEnd('a');        // name optional: closes the open element
 * ```
 *
 *   JS method                  calls              argument
 *   error(str)                 error              text
 *   elementStart(name)         element_start      name
 *   attribute(name[, value])   attribute          name, value
 *   elementEnd([name])         element_end        name or undefined
 *   text(str)                  text               text
 *   comment(str)               comment            text
 *   declaration(str)           declaration        text
 *   instruction(str)           instruction        text
 *   cdata(str)                 cdata              text
 *
 * returns the bytes written, as the member reports.
 * throws TypeError for a missing argument or a NULL member; a negative member result
 * throws what the class' XmlSinkThrow makes of it (Error "XmlEventSink: event failed" when none). */

/* the sink of the object `opaque`, an object's JS_GetOpaque() */
typedef XmlEventSink XmlSinkFromOpaque(void* opaque);

/* the exception for a failed member result `r` (< 0); returns JS_EXCEPTION */
typedef JSValue XmlSinkThrow(JSContext*, ssize_t r);

/* called after each successful call with the XmlEventType, e.g. to remember the last event */
typedef void XmlSinkNote(void* opaque, int type);

/* js_xmlsink_register: lets objects of `class_id` call the XmlEventSink methods.
 * `throw_`/`note` may be NULL.
 *
 * JS thread only: call it while the class is set up.
 * returns 0, or -1 when the table is full (never throws). */
int js_xmlsink_register(JSClassID, XmlSinkFromOpaque*, XmlSinkThrow* throw_, XmlSinkNote* note);

/* js_xmlsink_define: puts the nine methods onto `obj`; works on registered classes and js_xmlsink_wrap() objects.
 *
 * returns 0, or -1 with an exception pending. */
int js_xmlsink_define(JSContext*, JSValueConst obj);

/* js_xmlsink_wrap: makes a JS object whose methods call `sink`.
 *
 * borrowed: sink.opaque is not owned; it must outlive the JS object.
 * returns the object (refcount: new reference), or JS_EXCEPTION. */
JSValue js_xmlsink_wrap(JSContext*, XmlEventSink sink);

/* XmlJsSink: a JS object used as an XmlEventSink, so a C source can drive it; the twin of JsonJsSink.
 *
 * ```js
 * { elementStart(name) {}, attribute(name, value) {}, text(str) {}, elementEnd(name) {} }
 * ```
 *
 * each event calls the method of the same name (as the table above); a bare attribute passes
 * value undefined, elementEnd passes no argument for a NULL name. A missing method drops the
 * event. A method that returns a negative number fails the event with it; one that throws fails it
 * with -1 and leaves the exception pending. */
typedef struct xml_js_sink XmlJsSink;

/* js_xmlsink_from_js: makes *sink call the methods of `obj`.
 *
 * borrowed: obj is dup'ed; free the result with js_xmlsink_from_js_free().
 * returns the adapter (sink->opaque), or NULL with an exception pending. */
XmlJsSink* js_xmlsink_from_js(JSContext*, JSValueConst obj, XmlEventSink* sink);

/* js_xmlsink_from_js_free: releases the adapter and the object reference. never throws. */
void js_xmlsink_from_js_free(JSContext*, XmlJsSink*);

/* js_xmlsink_from_js_free_rt: the same for a finalizer, which has a runtime and no context. */
void js_xmlsink_from_js_free_rt(JSRuntime*, XmlJsSink*);

/* js_xmlsink_failed: the exception for a sink member that returned r < 0.
 *
 * a JS-object sink (adapter) leaves its own exception pending, a native sink gives an Error with
 * code r. returns JS_EXCEPTION. */
JSValue js_xmlsink_failed(JSContext*, int adapter, ssize_t r);

/**
 * @}
 */

#endif
