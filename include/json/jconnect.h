#ifndef JCONNECT_H
#define JCONNECT_H

#include <quickjs.h>
#include "json/jsink.h"

/**
 * \addtogroup jsink
 * @{
 */

/* js_jsonsink_resolve: the JsonEventSink behind a JS object, for pump() and connect().
 *
 *   obj has [Symbol.for('qjs.jsonsink')]()   the sink of that native object, found by address
 *                                           (any module's class, the class ids differ per module)
 *   any other object                       the methods of obj, one JS call per event
 *
 * ```js
 * parser.pump(writer);          // the handshake, no JS call per event
 * parser.pump({ key(k) {} });   // the adapter
 * ```
 *
 * borrowed: the handshake sink is valid while obj lives, keep a reference to it. With the adapter
 * *adapter is set, free it with js_jsonsink_from_js_free(), else it is NULL.
 *
 * returns 0, or -1 with an exception pending. */
int js_jsonsink_resolve(JSContext*, JSValueConst obj, JsonEventSink* sink, JsonJsSink** adapter);

/* js_jsonsink_define_handshake: puts fn on proto as [Symbol.for('qjs.jsonsink')].
 *
 * fn takes no argument and returns the address of a JsonEventSink that lives as long as this:
 *
 *   return JS_NewInt64(ctx, (int64_t)(intptr_t)&obj->sink);
 *
 * returns 0, or -1 with an exception pending. */
int js_jsonsink_define_handshake(JSContext*, JSValueConst proto, JSCFunction* fn);

/** @} */
#endif
