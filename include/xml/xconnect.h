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

/** @} */
#endif
