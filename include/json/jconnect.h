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

/* JsonJsCallbacks: the callbacks of a JsonPushParser argument, as the handler of an event source.
 *
 *   argument          calls
 *   a function        fn(type, value) for every event
 *   an object         by event: error(err), value(v) for null, true, false, number and string,
 *                     arrayStart(), arrayEnd(), objectStart(), objectEnd(), key(k)
 *
 * the object is this inside its callbacks, a callback it does not define is skipped. */
enum { JSON_CALLBACKS = 11 }; /* JSON_EVENT_ERROR to JSON_EVENT_KEY */

typedef struct {
  JSContext* ctx;
  JSValue fn, obj, by_type[JSON_CALLBACKS];
} JsonJsCallbacks;

/* js_jsoncallbacks_init: reads the callbacks from arg (any value, others have none).
 *
 * refcount: takes references, release them with js_jsoncallbacks_free_rt(). never throws. */
void js_jsoncallbacks_init(JSContext*, JsonJsCallbacks*, JSValueConst arg);
void js_jsoncallbacks_free_rt(JSRuntime*, JsonJsCallbacks*);

/* js_jsoncallbacks_complete: whether fn is set or every value event has its callback, so a
 * builder would only repeat what the callbacks get. */
int js_jsoncallbacks_complete(const JsonJsCallbacks*);

/* js_jsoncallbacks_event: the JsonEventHandler of the callbacks, user is a JsonJsCallbacks*.
 *
 * an exception thrown by a callback is dropped. returns 0. */
ssize_t js_jsoncallbacks_event(void* user, const JsonEvent* ev);

/* JsonJsTee: a tee of sinks that keeps the JS objects behind them alive.
 *
 * sink fans every event out to the sinks in order, handshake gives away &sink. */
typedef struct {
  JsonEventSink* sinks;
  JsonJsSink** adapters;
  JSValue* objs;
  size_t len;
  JsonTee tee; /* tee points at sinks */
  JsonEventSink sink;
} JsonJsTee;

/* js_jsontee_new: resolves each argument with js_jsonsink_resolve() and tees them.
 *
 * returns the tee (free it with js_jsontee_free_rt()), or NULL with an exception pending. */
JsonJsTee* js_jsontee_new(JSContext*, int argc, JSValueConst argv[]);
void js_jsontee_free_rt(JSRuntime*, JsonJsTee*);

/** @} */
#endif
