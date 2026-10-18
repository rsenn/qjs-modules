#ifndef XCONNECT_H
#define XCONNECT_H

#include <quickjs.h>
#include "xml/xsink.h"

/**
 * \addtogroup xsink
 * @{
 */

/* js_xmlsink_resolve: the XmlEventSink behind a JS object, for pump() and connect().
 *
 *   obj has [Symbol.for('qjs.xmlsink')]()   the sink of that native object, found by address
 *                                           (any module's class, the class ids differ per module)
 *   any other object                       the methods of obj, one JS call per event
 *
 * ```js
 * parser.pump(writer);          // the handshake, no JS call per event
 * parser.pump({ key(k) {} });   // the adapter
 * ```
 *
 * borrowed: the handshake sink is valid while obj lives, keep a reference to it. With the adapter
 * *adapter is set, free it with js_xmlsink_from_js_free(), else it is NULL.
 *
 * returns 0, or -1 with an exception pending. */
int js_xmlsink_resolve(JSContext*, JSValueConst obj, XmlEventSink* sink, XmlJsSink** adapter);

/* js_xmlsink_define_handshake: puts fn on proto as [Symbol.for('qjs.xmlsink')].
 *
 * fn takes no argument and returns the address of a XmlEventSink that lives as long as this:
 *
 *   return JS_NewInt64(ctx, (int64_t)(intptr_t)&obj->sink);
 *
 * returns 0, or -1 with an exception pending. */
int js_xmlsink_define_handshake(JSContext*, JSValueConst proto, JSCFunction* fn);

/* XmlJsCallbacks: the callbacks of an options object, as the handler of an event source.
 *
 *   event          callback
 *   element_start  elementStart(name)
 *   element_end    elementEnd(name)
 *   attribute      attribute(name, value), true for a bare attribute
 *   text           text(undefined, value)
 *   error          error("Error!", offending text)
 *
 * the options object is this inside every call, a callback it does not define is skipped. */
typedef struct {
  JSContext* ctx;
  JSValue this_obj;
  JSValue attribute, element_start, element_end, error, text;
  JSValue on_parse_error; /* onParseError(err): receives the SyntaxError instead of a throw */
} XmlJsCallbacks;

/* js_xmlcallbacks_init: reads the callbacks from options (any value, a non-object has none).
 *
 * refcount: takes references, release them with js_xmlcallbacks_free_rt(). never throws. */
void js_xmlcallbacks_init(JSContext*, XmlJsCallbacks*, JSValueConst options);
void js_xmlcallbacks_free_rt(JSRuntime*, XmlJsCallbacks*);

/* js_xmlcallbacks_any: whether one of the five event callbacks is a function. */
int js_xmlcallbacks_any(const XmlJsCallbacks*);

/* js_xmlcallbacks_event: the XmlEventHandler of the callbacks, user is an XmlJsCallbacks*.
 *
 * an exception thrown by a callback is dropped. returns 0. */
ssize_t js_xmlcallbacks_event(void* user, const XmlEvent* ev);

/* XmlJsTee: a tee of sinks that keeps the JS objects behind them alive.
 *
 * sink fans every event out to the sinks in order, handshake gives away &sink. */
typedef struct {
  XmlEventSink* sinks;
  XmlJsSink** adapters;
  JSValue* objs;
  size_t len;
  XmlTee tee; /* tee points at sinks */
  XmlEventSink sink;
} XmlJsTee;

/* js_xmltee_new: resolves each argument with js_xmlsink_resolve() and tees them.
 *
 * returns the tee (free it with js_xmltee_free_rt()), or NULL with an exception pending. */
XmlJsTee* js_xmltee_new(JSContext*, int argc, JSValueConst argv[]);
void js_xmltee_free_rt(JSRuntime*, XmlJsTee*);

/** @} */
#endif
