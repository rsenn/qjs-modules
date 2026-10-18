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

/**
 * @}
 */

#endif
