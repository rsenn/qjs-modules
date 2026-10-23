#include "char-utils.h"
#include "stream-utils.h"
#include "utils.h"
#include "vector.h"
#include "property-enumeration.h"
#include "json/json.h"
#include "json/jread.h"
#include "json/jwrite.h"
#include "json/jwalk.h"
#include "json/jopts.h"
#include "json/jevent-js.h"
#include "json/jserial.h"
#include "json/jsink.h"
#include "json/jconnect.h"
#include "json/jbuilder.h"
#include "json/jpush.h"
#include "json/jserializer.h"
#include "quickjs-location.h"

VISIBLE JSClassID js_jsonparser_class_id = 0, js_jsonpushparser_class_id = 0, js_jsonserializer_class_id = 0;

static JSValue
js_json_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

static JSValue js_json_tee(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]);

static const JSCFunctionListEntry js_json_funcs[] = {
    JS_CFUNC_DEF("tee", 0, js_json_tee),
};

/* ---------------------------------------------------------------------- */
/* JsonPushParser                                                        */
/* ---------------------------------------------------------------------- */

static JSValue
js_jsonpushparser_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonPushParser* pp;
  InputBuffer input;
  ssize_t r;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_jsonpushparser_class_id)))
    return JS_EXCEPTION;

  if(!(argc > 0 && js_is_bytes_or_string(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "JsonPushParser.write(): expecting buffer or string");

  if(!(input = js_input_chars(ctx, argv[0])).data)
    return JS_EXCEPTION;

  r = jpush_feed(pp, inputbuffer_data(&input), inputbuffer_length(&input));
  inputbuffer_free(&input, ctx);

  if(r < 0)
    return js_jsonsink_failed(ctx, pp->ext_adapter != NULL, r);

  /* just_erred (not the sticky error) - a resynced parser must not keep throwing on every
   * future write() call just because some earlier call hit a (already-reported) error. */
  return pp->jrs.just_erred ? jpush_throw(ctx, pp, "parse error") : JS_UNDEFINED;
}

static JSValue
js_jsonpushparser_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonPushParser* pp;
  ssize_t r;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_jsonpushparser_class_id)))
    return JS_EXCEPTION;

  if((r = jpush_end(pp)) < 0)
    return js_jsonsink_failed(ctx, pp->ext_adapter != NULL, r);

  if(!pp->jrs.done || pp->jrs.error)
    return jpush_throw(ctx, pp, pp->jrs.done ? "parse error" : "unexpected end of input");

  return JS_UNDEFINED;
}

/* JsonPushParser.connect: sends every event of write() and close() to a sink.
 *
 * ```js
 * const p = new JsonPushParser();
 * p.connect(new JsonWriter(sink));   // or a JsonBuilder, tee(a, b), { key(k) {}, ... }
 * p.write('{"a":1}');
 * ```
 *
 *   object  sink  a JsonEventSink object (handshake) or any object with the sink methods
 *
 *   returns  the parser
 *   throws   TypeError for a non-object, what the sink methods throw, Error with a code when a
 *            native sink refuses an event
 *
 * the callbacks and the builder of the constructor are not called while connected.
 * wired in: JS_CFUNC_DEF("connect") and the `sink` getter. */
static JSValue
js_jsonpushparser_connect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_jsonpushparser_class_id)))
    return JS_EXCEPTION;

  return jpush_connect(pp, ctx, argc > 0 ? argv[0] : JS_UNDEFINED) ? JS_EXCEPTION : JS_DupValue(ctx, this_val);
}

static JSValue
js_jsonpushparser_get_sink(JSContext* ctx, JSValueConst this_val) {
  JsonPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_jsonpushparser_class_id)))
    return JS_EXCEPTION;

  return JS_DupValue(ctx, pp->ext_obj);
}

enum {
  PUSHPARSER_ROOT,
  PUSHPARSER_PATH,
  PUSHPARSER_LOCATION,
  PUSHPARSER_ERROR,
};

static JSValue
js_jsonpushparser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonPushParser* pp;
  JSValue ret = JS_UNDEFINED;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_jsonpushparser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PUSHPARSER_ROOT: ret = jbuild_root(&pp->builder); break;
    case PUSHPARSER_PATH: ret = jbuild_path(&pp->builder); break;
    case PUSHPARSER_ERROR: ret = JS_IsUndefined(pp->last_error) ? JS_NULL : JS_DupValue(ctx, pp->last_error); break;
    case PUSHPARSER_LOCATION: ret = pp->loc ? js_location_wrap(ctx, pp->loc) : JS_UNDEFINED; break;
  }

  return ret;
}

static JSValue
js_jsonpushparser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JsonPushParser* pp;
  JSValue obj;

  if(!(pp = js_malloc(ctx, sizeof(JsonPushParser))))
    return JS_EXCEPTION;

  if(jpush_init(pp, ctx, argc > 0 ? argv[0] : JS_UNDEFINED)) {
    js_free(ctx, pp);
    return JS_EXCEPTION;
  }

  if(JS_IsException(obj = js_object_from_target(ctx, new_target, js_jsonpushparser_class_id))) {
    jpush_free(pp, JS_GetRuntime(ctx));
    js_free(ctx, pp);
    return obj;
  }

  JS_SetOpaque(obj, pp);
  return obj;
}

static void
js_jsonpushparser_finalizer(JSRuntime* rt, JSValue val) {
  JsonPushParser* pp;

  if((pp = JS_GetOpaque(val, js_jsonpushparser_class_id))) {
    jpush_free(pp, rt);
    js_free_rt(rt, pp);
  }
}

static const JSCFunctionListEntry js_jsonpushparser_proto_funcs[] = {
    JS_CFUNC_DEF("write", 1, js_jsonpushparser_write),
    JS_CFUNC_DEF("close", 0, js_jsonpushparser_close),
    JS_CFUNC_DEF("connect", 1, js_jsonpushparser_connect),
    JS_CGETSET_DEF("sink", js_jsonpushparser_get_sink, 0),
    JS_CGETSET_MAGIC_DEF("root", js_jsonpushparser_get, 0, PUSHPARSER_ROOT),
    JS_CGETSET_MAGIC_DEF("path", js_jsonpushparser_get, 0, PUSHPARSER_PATH),
    JS_CGETSET_MAGIC_DEF("location", js_jsonpushparser_get, 0, PUSHPARSER_LOCATION),
    JS_CGETSET_MAGIC_DEF("error", js_jsonpushparser_get, 0, PUSHPARSER_ERROR),
    JS_PROP_INT32_DEF("TYPE_ERROR", JSON_EVENT_ERROR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_NULL", JSON_EVENT_NULL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_TRUE", JSON_EVENT_TRUE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_FALSE", JSON_EVENT_FALSE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_NUMBER", JSON_EVENT_NUMBER, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_STRING", JSON_EVENT_STRING, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_OBJECT", JSON_EVENT_OBJECT_START, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_OBJECT_START", JSON_EVENT_OBJECT_START, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_OBJECT_END", JSON_EVENT_OBJECT_END, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ARRAY", JSON_EVENT_ARRAY_START, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ARRAY_START", JSON_EVENT_ARRAY_START, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ARRAY_END", JSON_EVENT_ARRAY_END, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_KEY", JSON_EVENT_KEY, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonPushParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_jsonpushparser_class = {
    .class_name = "JsonPushParser",
    .finalizer = js_jsonpushparser_finalizer,
};

/* ---------------------------------------------------------------------- */
/* JsonSerializer: pull (.read(n)) serializer traversing via              */
/* property_recursion_*(), producing only as much text as requested.      */
/* ---------------------------------------------------------------------- */

enum {
  SERIALIZER_LOCATION,
  SERIALIZER_ROOT,
  SERIALIZER_INDENT,
  SERIALIZER_PATH,
  SERIALIZER_FINISHED,
  SERIALIZER_STARTED,
  SERIALIZER_ERROR,
  SERIALIZER_BLOCKED,
};

static JSValue
js_jsonserializer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonSerializer* js;
  JSValue ret = JS_UNDEFINED;

  if(!(js = JS_GetOpaque2(ctx, this_val, js_jsonserializer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case SERIALIZER_LOCATION: ret = js_location_wrap(ctx, js->loc); break;
    case SERIALIZER_ROOT: ret = JS_DupValue(ctx, js->root); break;
    case SERIALIZER_INDENT: ret = JS_NewInt32(ctx, js->space.indent); break;
    case SERIALIZER_PATH: ret = property_recursion_path(&js->src.it.walk.stack, ctx); break;
    case SERIALIZER_FINISHED: ret = JS_NewBool(ctx, js->ser.status != JSON_PULL_EVENT); break;
    case SERIALIZER_STARTED: ret = JS_NewBool(ctx, js->started); break;
    case SERIALIZER_ERROR: ret = JS_NewBool(ctx, js->error); break;
    case SERIALIZER_BLOCKED: ret = JS_FALSE; break;
  }

  return ret;
}

static JSValue
js_jsonserializer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JsonSerializer* js;
  JSValue ret = JS_UNDEFINED;

  if(!(js = JS_GetOpaque2(ctx, this_val, js_jsonserializer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case SERIALIZER_INDENT:
      if(!jsonspace_parse(ctx, value, &js->space))
        jsonspace_apply(&js->space, &js->ser.wr.opts);

      break;
  }

  return ret;
}

static JSValue
js_jsonserializer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JsonSerializer* js;
  JSValue obj;

  if(!(js = jserializer_new(ctx, argc, argv)))
    return JS_EXCEPTION;

  if(JS_IsException(obj = js_object_from_target(ctx, new_target, js_jsonserializer_class_id))) {
    jserializer_free(js, JS_GetRuntime(ctx));
    return obj;
  }

  JS_SetOpaque(obj, js);
  return obj;
}

static JSValue
js_jsonserializer_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonSerializer* js;

  if(!(js = JS_GetOpaque2(ctx, this_val, js_jsonserializer_class_id)))
    return JS_EXCEPTION;

  return jserializer_read(ctx, js, argc, argv);
}

static JSValue
js_jsonserializer_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  JSValue args[] = {argc > 0 ? argv[0] : JS_NewUint32(ctx, 1024)};
  JSValue ret = js_jsonserializer_read(ctx, this_val, countof(args), args);

  *pdone = FALSE;

  if(JS_IsException(ret)) {
    *pdone = TRUE;
    ret = JS_GetException(ctx);
  }

  return ret;
}

static void
js_jsonserializer_finalizer(JSRuntime* rt, JSValue val) {
  JsonSerializer* js;

  if((js = JS_GetOpaque(val, js_jsonserializer_class_id)))
    jserializer_free(js, rt);
}

static const JSCFunctionListEntry js_jsonserializer_proto_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_jsonserializer_read),
    JS_ITERATOR_NEXT_DEF("next", 0, js_jsonserializer_next, 0),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_json_iterator),
    JS_CGETSET_MAGIC_DEF("location", js_jsonserializer_get, 0, SERIALIZER_LOCATION),
    JS_CGETSET_MAGIC_DEF("root", js_jsonserializer_get, 0, SERIALIZER_ROOT),
    JS_CGETSET_MAGIC_DEF("indent", js_jsonserializer_get, js_jsonserializer_set, SERIALIZER_INDENT),
    JS_CGETSET_MAGIC_DEF("path", js_jsonserializer_get, 0, SERIALIZER_PATH),
    JS_CGETSET_MAGIC_DEF("finished", js_jsonserializer_get, 0, SERIALIZER_FINISHED),
    JS_CGETSET_MAGIC_DEF("started", js_jsonserializer_get, 0, SERIALIZER_STARTED),
    JS_CGETSET_MAGIC_DEF("error", js_jsonserializer_get, 0, SERIALIZER_ERROR),
    JS_CGETSET_MAGIC_DEF("blocked", js_jsonserializer_get, 0, SERIALIZER_BLOCKED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonSerializer", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_jsonserializer_class = {
    .class_name = "JsonSerializer",
    .finalizer = js_jsonserializer_finalizer,
};

/* ---------------------------------------------------------------------- */
/* JsonWriter: push-based incremental JSON writer                         */
/* ---------------------------------------------------------------------- */

/* the JsonWriter of the JS object, and its sink for the handshake */
typedef struct {
  JsonWriter wr; /* first: the opaque pointer is also a JsonWriter* */
  JsonEventSink sink;
} JsonWriterObj;

static JSClassID js_jsonwriter_class_id = 0;

/* js_jsonwriter_throw: the exception of a failed sink member, see js_jsonwriter_ret(). */
static JSValue
js_jsonwriter_throw(JSContext* ctx, ssize_t r) {
  js_jsonwriter_ret(ctx, r);
  return JS_EXCEPTION;
}

static JsonEventSink
js_jsonwriter_sink(void* opaque) {
  return jwrite_sink(opaque);
}

/* JsonWriter.value: writes a JS primitive, the one method besides the JsonEventSink ones.
 *
 *   JS value                        written as
 *   null, undefined, symbol, fn     null
 *   boolean                         true / false
 *   string                          string
 *   NaN, Infinity                   null
 *   integer, hexNumbers set         0xff / -0x1f
 *   number, bigint                  the JS text (1.5, 1e21, 123n -> 123)
 *   other object                    its toString, as a string
 */
static JSValue
js_jsonwriter_value(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonWriter* wr;
  JSValueConst val = argc > 0 ? argv[0] : JS_UNDEFINED;
  ssize_t r;

  if(!(wr = JS_GetOpaque2(ctx, this_val, js_jsonwriter_class_id)))
    return JS_EXCEPTION;

  r = jwrite_js_value(ctx, wr, val);

  return r < 0 ? JS_EXCEPTION : JS_UNDEFINED;
}

enum {
  WRITER_WRITTEN,
  WRITER_DEPTH,
  WRITER_COUNT,
  WRITER_STACK,
};

static JSValue
js_jsonwriter_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonWriter* wr;
  JSValue ret = JS_UNDEFINED;

  if(!(wr = JS_GetOpaque2(ctx, this_val, js_jsonwriter_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITER_WRITTEN: ret = JS_NewInt64(ctx, wr->written); break;
    case WRITER_DEPTH: ret = JS_NewUint32(ctx, vector_size(&wr->stack, sizeof(JsonWriterFrame))); break;
    case WRITER_COUNT: {
      if(!vector_empty(&wr->stack))
        ret = JS_NewUint32(ctx, (vector_end_t(&wr->stack, JsonWriterFrame) - 1)->count);

      break;
    }
    case WRITER_STACK: ret = jwrite_js_stack(ctx, wr); break;
  }

  return ret;
}

static JSValue
js_jsonwriter_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj;
  JsonWriter* wr;
  int i = 0;

  if(!(wr = js_mallocz(ctx, sizeof(JsonWriterObj))))
    return JS_EXCEPTION;

  if(i < argc && writer_from_js(ctx, argv[i], &wr->writer))
    i++;

  jwrite_init(wr, (DynBufReallocFunc*)&vector_js_realloc, ctx);
  ((JsonWriterObj*)wr)->sink = jwrite_sink(wr);

  JSValue options = i < argc ? argv[i] : (argc > 0 ? argv[0] : JS_UNDEFINED);

  jwrite_options_from_js(ctx, options, &wr->opts);

  if(JS_IsException(obj = js_object_from_target(ctx, new_target, js_jsonwriter_class_id))) {
    jwrite_free(wr);
    js_free(ctx, wr);
    return obj;
  }

  JS_SetOpaque(obj, wr);
  return obj;
}

static void
js_jsonwriter_finalizer(JSRuntime* rt, JSValue val) {
  JsonWriter* wr;

  if((wr = JS_GetOpaque(val, js_jsonwriter_class_id))) {
    jwrite_free(wr);
    js_free_rt(rt, wr);
  }
}

static JSValue
js_jsonwriter_handshake(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonWriterObj* o;

  if(!(o = JS_GetOpaque2(ctx, this_val, js_jsonwriter_class_id)))
    return JS_EXCEPTION;

  return JS_NewInt64(ctx, (int64_t)(intptr_t)&o->sink);
}

static JSClassDef js_jsonwriter_class = {
    .class_name = "JsonWriter",
    .finalizer = js_jsonwriter_finalizer,
};

static const JSCFunctionListEntry js_jsonwriter_funcs[] = {
    JS_CFUNC_DEF("value", 1, js_jsonwriter_value),
    JS_CGETSET_MAGIC_DEF("written", js_jsonwriter_get, 0, WRITER_WRITTEN),
    JS_CGETSET_MAGIC_DEF("depth", js_jsonwriter_get, 0, WRITER_DEPTH),
    JS_CGETSET_MAGIC_DEF("stack", js_jsonwriter_get, 0, WRITER_STACK),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonWriter", JS_PROP_CONFIGURABLE),
};

/* JsonBuilder: a JsonEventSink that builds JS values.
 *
 * ```js
 * const b = new JsonBuilder();
 * parser.pump(b);
 * b.root;                         // {a: [1, "x"]}
 * new JsonBuilder({ plain: true }) // objects get Object.prototype, else none
 * ```
 *
 *   root   the value built so far (the last top-level value)
 *   path   the keys and indexes from the root to the open container
 *   take() the top-level values completed since the last call, as an array (JSONL)
 *
 * the sink methods are those of JsonWriter without value(), see jsink.h.
 * wired in: JS_CGETSET_DEF("root") and friends, js_jsonsink_register for the methods. */
typedef struct {
  JsonBuilder b; /* first: the opaque pointer is also a JsonBuilder* */
  JsonEventSink sink;
} JsonBuilderObj;

static JSClassID js_jsonbuilder_class_id = 0;

static JsonEventSink
js_jsonbuilder_sink(void* opaque) {
  return jbuild_sink(opaque);
}

static JSValue
js_jsonbuilder_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj;
  JsonBuilderObj* o;

  if(!(o = js_mallocz(ctx, sizeof(JsonBuilderObj))))
    return JS_EXCEPTION;

  jbuild_init(&o->b, ctx);
  o->b.plain_objects = argc > 0 && JS_IsObject(argv[0]) && js_get_propertystr_bool(ctx, argv[0], "plain");
  o->sink = jbuild_sink(&o->b);

  if(JS_IsException(obj = js_object_from_target(ctx, new_target, js_jsonbuilder_class_id))) {
    jbuild_free(&o->b, JS_GetRuntime(ctx));
    js_free(ctx, o);
    return obj;
  }

  JS_SetOpaque(obj, o);
  return obj;
}

static void
js_jsonbuilder_finalizer(JSRuntime* rt, JSValue val) {
  JsonBuilderObj* o;

  if((o = JS_GetOpaque(val, js_jsonbuilder_class_id))) {
    jbuild_free(&o->b, rt);
    js_free_rt(rt, o);
  }
}

static JSValue
js_jsonbuilder_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonBuilderObj* o;

  if(!(o = JS_GetOpaque2(ctx, this_val, js_jsonbuilder_class_id)))
    return JS_EXCEPTION;

  return magic ? jbuild_path(&o->b) : jbuild_root(&o->b);
}

static JSValue
js_jsonbuilder_take(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonBuilderObj* o;

  if(!(o = JS_GetOpaque2(ctx, this_val, js_jsonbuilder_class_id)))
    return JS_EXCEPTION;

  return jbuild_take(&o->b);
}

static JSValue
js_jsonbuilder_handshake(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonBuilderObj* o;

  if(!(o = JS_GetOpaque2(ctx, this_val, js_jsonbuilder_class_id)))
    return JS_EXCEPTION;

  return JS_NewInt64(ctx, (int64_t)(intptr_t)&o->sink);
}

static JSClassDef js_jsonbuilder_class = {
    .class_name = "JsonBuilder",
    .finalizer = js_jsonbuilder_finalizer,
};

static const JSCFunctionListEntry js_jsonbuilder_funcs[] = {
    JS_CGETSET_MAGIC_DEF("root", js_jsonbuilder_get, 0, 0),
    JS_CGETSET_MAGIC_DEF("path", js_jsonbuilder_get, 0, 1),
    JS_CFUNC_DEF("take", 0, js_jsonbuilder_take),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonBuilder", JS_PROP_CONFIGURABLE),
};

/* tee: a sink that sends every event to several sinks.
 *
 * ```js
 * const t = tee(new JsonBuilder(), new JsonWriter(out));
 * parser.pump(t);                 // one parse, a value and a copy of the text
 * ```
 *
 *   object...  sinks  JsonEventSink objects or objects with the sink methods
 *
 *   returns  a JsonTee sink object
 *
 * the sinks are called in order, the first failure stops the event.
 * wired in: JS_CFUNC_DEF("tee") in the module exports. */

static JSClassID js_jsontee_class_id = 0;

static JsonEventSink
js_jsontee_sink(void* opaque) {
  JsonJsTee* t = opaque;

  return t->sink;
}

static JSValue
js_json_tee(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonJsTee* t = js_jsontee_new(ctx, argc, argv);
  JSValue obj;

  if(!t)
    return JS_EXCEPTION;

  if(JS_IsException(obj = JS_NewObjectClass(ctx, js_jsontee_class_id))) {
    js_jsontee_free_rt(JS_GetRuntime(ctx), t);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, t);
  return obj;
}

static void
js_jsontee_finalizer(JSRuntime* rt, JSValue val) {
  JsonJsTee* t;

  if((t = JS_GetOpaque(val, js_jsontee_class_id)))
    js_jsontee_free_rt(rt, t);
}

static JSValue
js_jsontee_handshake(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonJsTee* t;

  if(!(t = JS_GetOpaque2(ctx, this_val, js_jsontee_class_id)))
    return JS_EXCEPTION;

  return JS_NewInt64(ctx, (int64_t)(intptr_t)&t->sink);
}

static JSClassDef js_jsontee_class = {
    .class_name = "JsonTee",
    .finalizer = js_jsontee_finalizer,
};

/* JsonParser: a pull parser over a string, a buffer, a read function, an object with read() or a fd.
 *
 * ```js
 * new JsonParser(text, 'a.json')                       // the file name for errors
 * new JsonParser(text, { dialect: 'json5', filename: 'a.json5', comments: true })
 * new JsonParser(text, { strict: true })               // RFC 8259 only, same as dialect 'json'
 * ```
 *
 *   dialect   'json', 'jsonc' (comments, trailing commas), 'json5', 'jsonl' (many documents), 'legacy'
 *   strict    true: dialect 'json' when no dialect is given
 *   comments  true: comments arrive as events (COMMENT) instead of being skipped
 *   filename  the name that errors report
 *
 * without a dialect the parser is lenient ('legacy'): it skips bad input and goes on.
 * throws TypeError for an unknown dialect. wired in: the JsonParser export. */
static JSValue
js_jsonparser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JsonParser* p;
  JSValue obj;

  if(!(p = json_new_from_js(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, argc > 1 ? argv[1] : JS_UNDEFINED)))
    return JS_EXCEPTION;

  if(JS_IsException(obj = js_object_from_target(ctx, new_target, js_jsonparser_class_id))) {
    json_free(p, JS_GetRuntime(ctx));
    return obj;
  }

  JS_SetOpaque(obj, p);
  return obj;
}

/* JsonParser.pump: runs the parser into a sink, in C.
 *
 * ```js
 * const b = new JsonBuilder();
 * new JsonParser('{"a":[1]}').pump(b, true);   // 0
 * b.root;                                      // {a: [1]}
 * ```
 *
 *   object   sink  a JsonEventSink object (handshake) or any object with the sink methods
 *   boolean  end   optional: the input is complete, so a dry reader ends the document
 *
 *   returns  0 when the input ended, 1 when the reader has no data now (call again once it has,
 *            or with end set when it will not)
 *   throws   the SyntaxError of the first error event, what the sink throws, Error with a code
 *            when a native sink refuses an event
 *
 * wired in: JS_CFUNC_DEF("pump"). */
static JSValue
js_jsonparser_pump(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonParser* p;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_jsonparser_class_id)))
    return JS_EXCEPTION;

  return js_jsonpull_pump(ctx, p, argc > 0 ? argv[0] : JS_UNDEFINED, argc > 1 && JS_ToBool(ctx, argv[1]));
}

enum {
  PARSER_DEPTH,
  PARSER_LOCATION,
  PARSER_COMMENTS,
};

static JSValue
js_jsonparser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonParser* p;
  JSValue ret = JS_UNDEFINED;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_jsonparser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PARSER_COMMENTS: {
      ret = JS_NewBool(ctx, p->comments);
      break;
    }
    case PARSER_DEPTH: {
      ret = JS_NewUint32(ctx, p->depth);
      break;
    }
    case PARSER_LOCATION: {
      ret = js_location_wrap(ctx, p->loc);
      break;
    }
  }

  return ret;
}

static JSValue
js_jsonparser_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JsonParser* p;
  JSValue ret = JS_UNDEFINED;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_jsonparser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PARSER_COMMENTS: {
      p->comments = JS_ToBool(ctx, value);
      break;
    }
  }

  return ret;
}

/* JsonParser.next: the next event as { type, value?, loc }, also the iterator protocol.
 *
 * ```js
 * for(const { type, value, loc } of new JsonParser('{"a":1}')) {}   // OBJECT, KEY a, NUMBER 1, OBJECT_END
 * ```
 *
 * the iteration ends when the document is complete or the reader has no data now.
 * throws the SyntaxError of an error event. wired in: JS_ITERATOR_NEXT_DEF("next"). */
static JSValue
js_jsonparser_iterator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  JsonParser* p;
  JsonEvent ev;

  *pdone = TRUE;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_jsonparser_class_id)))
    return JS_EXCEPTION;

  if(json_next(p, &ev) != JSON_PULL_EVENT)
    return JS_UNDEFINED;

  if(ev.type == JSON_EVENT_ERROR) {
    js_jsonevent_throw(ctx, p->loc, &ev);
    return JS_EXCEPTION;
  }

  *pdone = FALSE;
  return js_jsonevent_new(ctx, &ev);
}

static void
js_jsonparser_finalizer(JSRuntime* rt, JSValue obj) {
  JsonParser* p;

  if((p = JS_GetOpaque(obj, js_jsonparser_class_id)))
    json_free(p, rt);
}

static const JSCFunctionListEntry js_jsonparser_proto_funcs[] = {
    JS_CFUNC_DEF("pump", 1, js_jsonparser_pump),
    JS_CGETSET_MAGIC_FLAGS_DEF("depth", js_jsonparser_get, 0, PARSER_DEPTH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("location", js_jsonparser_get, 0, PARSER_LOCATION, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("comments", js_jsonparser_get, js_jsonparser_set, PARSER_COMMENTS),
    JS_ITERATOR_NEXT_DEF("next", 0, js_jsonparser_iterator_next, 0),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_json_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonParser", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_jsonparser_static_props[] = {
    JS_PROP_INT32_DEF("OBJECT", JSON_EVENT_OBJECT_START, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("OBJECT_END", JSON_EVENT_OBJECT_END, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ARRAY", JSON_EVENT_ARRAY_START, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ARRAY_END", JSON_EVENT_ARRAY_END, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("KEY", JSON_EVENT_KEY, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("STRING", JSON_EVENT_STRING, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TRUE", JSON_EVENT_TRUE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("FALSE", JSON_EVENT_FALSE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("NULL", JSON_EVENT_NULL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("NUMBER", JSON_EVENT_NUMBER, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("COMMENT", JSON_EVENT_COMMENT, JS_PROP_ENUMERABLE),
};

static JSClassDef js_jsonparser_class = {
    .class_name = "JsonParser",
    .finalizer = js_jsonparser_finalizer,
};

static int
js_json_init(JSContext* ctx, JSModuleDef* m) {
  JSValue parser, push, serializer, writer, builder, proto;

  if(js_location_class_id == 0)
    js_location_init(ctx, 0);

  parser = js_class_define(
      ctx, &js_jsonparser_class_id, &js_jsonparser_class, js_jsonparser_constructor, 1, js_jsonparser_proto_funcs, countof(js_jsonparser_proto_funcs), &proto);
  JS_SetPropertyFunctionList(ctx, proto, js_jsonparser_static_props, countof(js_jsonparser_static_props));
  JS_SetPropertyFunctionList(ctx, parser, js_jsonparser_static_props, countof(js_jsonparser_static_props));

  push = js_class_define(ctx,
                         &js_jsonpushparser_class_id,
                         &js_jsonpushparser_class,
                         js_jsonpushparser_constructor,
                         0,
                         js_jsonpushparser_proto_funcs,
                         countof(js_jsonpushparser_proto_funcs),
                         0);
  serializer = js_class_define(ctx,
                               &js_jsonserializer_class_id,
                               &js_jsonserializer_class,
                               js_jsonserializer_constructor,
                               1,
                               js_jsonserializer_proto_funcs,
                               countof(js_jsonserializer_proto_funcs),
                               0);

  writer = js_class_define(
      ctx, &js_jsonwriter_class_id, &js_jsonwriter_class, js_jsonwriter_constructor, 1, js_jsonwriter_funcs, countof(js_jsonwriter_funcs), &proto);
  js_jsonsink_register(js_jsonwriter_class_id, js_jsonwriter_sink, js_jsonwriter_throw, NULL);
  js_jsonsink_define(ctx, proto);
  js_jsonsink_define_handshake(ctx, proto, js_jsonwriter_handshake);

  builder = js_class_define(
      ctx, &js_jsonbuilder_class_id, &js_jsonbuilder_class, js_jsonbuilder_constructor, 0, js_jsonbuilder_funcs, countof(js_jsonbuilder_funcs), &proto);
  js_jsonsink_register(js_jsonbuilder_class_id, js_jsonbuilder_sink, NULL, NULL);
  js_jsonsink_define(ctx, proto);
  js_jsonsink_define_handshake(ctx, proto, js_jsonbuilder_handshake);

  js_class_define(ctx, &js_jsontee_class_id, &js_jsontee_class, NULL, 0, NULL, 0, &proto);
  js_jsonsink_register(js_jsontee_class_id, js_jsontee_sink, NULL, NULL);
  js_jsonsink_define(ctx, proto);
  js_jsonsink_define_handshake(ctx, proto, js_jsontee_handshake);

  JS_SetModuleExport(ctx, m, "JsonParser", parser);
  JS_SetModuleExport(ctx, m, "JsonPushParser", push);
  JS_SetModuleExport(ctx, m, "JsonSerializer", serializer);
  JS_SetModuleExport(ctx, m, "JsonWriter", writer);
  JS_SetModuleExport(ctx, m, "JsonBuilder", builder);
  JS_SetModuleExportList(ctx, m, js_json_funcs, countof(js_json_funcs));
  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_json
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_json_init))) {
    JS_AddModuleExport(ctx, m, "JsonParser");
    JS_AddModuleExport(ctx, m, "JsonPushParser");
    JS_AddModuleExport(ctx, m, "JsonSerializer");
    JS_AddModuleExport(ctx, m, "JsonWriter");
    JS_AddModuleExport(ctx, m, "JsonBuilder");
    JS_AddModuleExportList(ctx, m, js_json_funcs, countof(js_json_funcs));
  }

  return m;
}
