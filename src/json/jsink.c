/* jsink.c: js_jsonsink_register(), js_jsonsink_define(), js_jsonsink_wrap().
 * depends on: jevent.h (the sink), quickjs.h.
 * rule: one JSCFunction for all thirteen methods, dispatched by magic; the class decides how a
 * failure is thrown. */

#define _GNU_SOURCE 1

#include "json/jsink.h"
#include <cutils.h>
#include <string.h>

/**
 * \addtogroup jsink
 * @{
 */

enum { M_ERROR, M_NULL, M_TRUE, M_FALSE, M_NUMBER, M_STRING, M_ARRAY_START, M_ARRAY_END, M_OBJECT_START, M_OBJECT_END, M_KEY, M_COMMENT, M_RAW };

static const JsonEventType method_types[] = {JSON_EVENT_ERROR,
                                             JSON_EVENT_NULL,
                                             JSON_EVENT_TRUE,
                                             JSON_EVENT_FALSE,
                                             JSON_EVENT_NUMBER,
                                             JSON_EVENT_STRING,
                                             JSON_EVENT_ARRAY_START,
                                             JSON_EVENT_ARRAY_END,
                                             JSON_EVENT_OBJECT_START,
                                             JSON_EVENT_OBJECT_END,
                                             JSON_EVENT_KEY,
                                             JSON_EVENT_COMMENT,
                                             JSON_EVENT_RAW};

static JSClassID js_jsonsink_class_id = 0;

/* classes whose objects can use the methods */
static struct {
  JSClassID class_id;
  JsonSinkFromOpaque* from_opaque;
  JsonSinkThrow* throw_;
  JsonSinkNote* note;
} sink_classes[16];

static size_t sink_classes_len;

int
js_jsonsink_register(JSClassID class_id, JsonSinkFromOpaque* from_opaque, JsonSinkThrow* throw_, JsonSinkNote* note) {
  for(size_t i = 0; i < sink_classes_len; i++)
    if(sink_classes[i].class_id == class_id)
      return 0;

  if(sink_classes_len == countof(sink_classes))
    return -1;

  sink_classes[sink_classes_len].class_id = class_id;
  sink_classes[sink_classes_len].from_opaque = from_opaque;
  sink_classes[sink_classes_len].throw_ = throw_;
  sink_classes[sink_classes_len++].note = note;
  return 0;
}

static JsonEventSink
js_jsonsink_copy(void* opaque) {
  return *(JsonEventSink*)opaque;
}

static JSValue
js_jsonsink_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JsonEventSink sink;
  JSClassID class_id = JS_GetClassID(this_val);
  size_t k;
  void* opaque = 0;
  const char* str = 0;
  size_t len = 0;
  ssize_t w = -1;
  int has_cb = 1;

  for(k = 0; k < sink_classes_len; k++) {
    if(sink_classes[k].class_id == class_id) {
      if(!(opaque = JS_GetOpaque2(ctx, this_val, class_id)))
        return JS_EXCEPTION;

      sink = sink_classes[k].from_opaque(opaque);
      break;
    }
  }

  if(!opaque)
    return JS_ThrowTypeError(ctx, "JsonEventSink: not a sink object");

  if(magic == M_ERROR || magic == M_NUMBER || magic == M_STRING || magic == M_KEY || magic == M_COMMENT || magic == M_RAW) {
    if(argc < 1)
      return JS_ThrowTypeError(ctx, "JsonEventSink: a text is required");

    if(!(str = JS_ToCStringLen(ctx, &len, argv[0])))
      return JS_EXCEPTION;
  }

  switch(magic) {
    case M_ERROR:
      has_cb = !!sink.error;
      w = has_cb ? sink.error(sink.opaque, str, len) : 0;
      break;
    case M_NULL:
      has_cb = !!sink.null;
      w = has_cb ? sink.null(sink.opaque) : 0;
      break;
    case M_TRUE:
      has_cb = !!sink.true_;
      w = has_cb ? sink.true_(sink.opaque) : 0;
      break;
    case M_FALSE:
      has_cb = !!sink.false_;
      w = has_cb ? sink.false_(sink.opaque) : 0;
      break;
    case M_NUMBER:
      has_cb = !!sink.number;
      w = has_cb ? sink.number(sink.opaque, str, len) : 0;
      break;
    case M_STRING:
      has_cb = !!sink.string;
      w = has_cb ? sink.string(sink.opaque, str, len) : 0;
      break;
    case M_ARRAY_START:
      has_cb = !!sink.array_start;
      w = has_cb ? sink.array_start(sink.opaque) : 0;
      break;
    case M_ARRAY_END:
      has_cb = !!sink.array_end;
      w = has_cb ? sink.array_end(sink.opaque) : 0;
      break;
    case M_OBJECT_START:
      has_cb = !!sink.object_start;
      w = has_cb ? sink.object_start(sink.opaque) : 0;
      break;
    case M_OBJECT_END:
      has_cb = !!sink.object_end;
      w = has_cb ? sink.object_end(sink.opaque) : 0;
      break;
    case M_KEY:
      has_cb = !!sink.key;
      w = has_cb ? sink.key(sink.opaque, str, len) : 0;
      break;
    case M_COMMENT:
      has_cb = !!sink.comment;
      w = has_cb ? sink.comment(sink.opaque, str, len) : 0;
      break;
    case M_RAW:
      has_cb = !!sink.raw;
      w = has_cb ? sink.raw(sink.opaque, str, len) : 0;
      break;
  }

  if(str)
    JS_FreeCString(ctx, str);

  if(!has_cb)
    return JS_ThrowTypeError(ctx, "JsonEventSink: callback not set");

  if(w < 0) {
    if(sink_classes[k].throw_)
      return sink_classes[k].throw_(ctx, w);

    return JS_ThrowInternalError(ctx, "JsonEventSink: event failed (%zd)", w);
  }

  if(sink_classes[k].note)
    sink_classes[k].note(opaque, method_types[magic]);

  return JS_NewInt64(ctx, w);
}

static const JSCFunctionListEntry js_jsonsink_tag[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonEventSink", JS_PROP_CONFIGURABLE),
};

int
js_jsonsink_define(JSContext* ctx, JSValueConst obj) {
  static const JSCFunctionListEntry funcs[] = {
      JS_CFUNC_MAGIC_DEF("error", 1, js_jsonsink_method, M_ERROR),
      JS_CFUNC_MAGIC_DEF("null", 0, js_jsonsink_method, M_NULL),
      JS_CFUNC_MAGIC_DEF("true", 0, js_jsonsink_method, M_TRUE),
      JS_CFUNC_MAGIC_DEF("false", 0, js_jsonsink_method, M_FALSE),
      JS_CFUNC_MAGIC_DEF("number", 1, js_jsonsink_method, M_NUMBER),
      JS_CFUNC_MAGIC_DEF("string", 1, js_jsonsink_method, M_STRING),
      JS_CFUNC_MAGIC_DEF("arrayStart", 0, js_jsonsink_method, M_ARRAY_START),
      JS_CFUNC_MAGIC_DEF("arrayEnd", 0, js_jsonsink_method, M_ARRAY_END),
      JS_CFUNC_MAGIC_DEF("objectStart", 0, js_jsonsink_method, M_OBJECT_START),
      JS_CFUNC_MAGIC_DEF("objectEnd", 0, js_jsonsink_method, M_OBJECT_END),
      JS_CFUNC_MAGIC_DEF("key", 1, js_jsonsink_method, M_KEY),
      JS_CFUNC_MAGIC_DEF("comment", 1, js_jsonsink_method, M_COMMENT),
      JS_CFUNC_MAGIC_DEF("raw", 1, js_jsonsink_method, M_RAW),
  };

  return JS_SetPropertyFunctionList(ctx, obj, funcs, countof(funcs));
}

static void
js_jsonsink_finalizer(JSRuntime* rt, JSValue val) {
  js_free_rt(rt, JS_GetOpaque(val, js_jsonsink_class_id));
}

static JSClassDef js_jsonsink_class = {
    .class_name = "JsonEventSink",
    .finalizer = js_jsonsink_finalizer,
};

JSValue
js_jsonsink_wrap(JSContext* ctx, JsonEventSink sink) {
  JSRuntime* rt = JS_GetRuntime(ctx);
  JSValue proto, obj;
  JsonEventSink* copy;

  if(js_jsonsink_class_id == 0)
    JS_NewClassID(&js_jsonsink_class_id);

  if(!JS_IsRegisteredClass(rt, js_jsonsink_class_id))
    JS_NewClass(rt, js_jsonsink_class_id, &js_jsonsink_class);

  js_jsonsink_register(js_jsonsink_class_id, js_jsonsink_copy, 0, 0);

  proto = JS_GetClassProto(ctx, js_jsonsink_class_id);

  if(JS_IsException(proto))
    return JS_EXCEPTION;

  if(JS_IsNull(proto) || JS_IsUndefined(proto)) {
    proto = JS_NewObject(ctx);

    if(js_jsonsink_define(ctx, proto) < 0 || JS_SetPropertyFunctionList(ctx, proto, js_jsonsink_tag, countof(js_jsonsink_tag)) < 0) {
      JS_FreeValue(ctx, proto);
      return JS_EXCEPTION;
    }

    JS_SetClassProto(ctx, js_jsonsink_class_id, JS_DupValue(ctx, proto));
  }

  if(!(copy = js_malloc(ctx, sizeof(*copy)))) {
    JS_FreeValue(ctx, proto);
    return JS_EXCEPTION;
  }

  *copy = sink;
  obj = JS_NewObjectProtoClass(ctx, proto, js_jsonsink_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    js_free(ctx, copy);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, copy);
  return obj;
}

/* the JS object as a sink: each member calls the method of the same name on `obj` */
struct json_js_sink {
  JSContext* ctx;
  JSValue obj;
};

static const char* const js_sink_names[] = {
    "error", "null", "true", "false", "number", "string", "arrayStart", "arrayEnd", "objectStart", "objectEnd", "key", "comment", "raw"};

/* js_jsonsink_call: obj[name](text?) or obj[name](); a missing method drops the event.
 *
 * returns the method's number when negative, else 0; -1 with the exception pending when it throws. */
static ssize_t
js_jsonsink_call(void* opaque, int magic, const char* str, size_t len) {
  JsonJsSink* s = opaque;
  JSContext* ctx = s->ctx;
  JSValue fn = JS_GetPropertyStr(ctx, s->obj, js_sink_names[magic]), arg, ret;
  int32_t r = 0;

  if(JS_IsException(fn))
    return -1;

  if(!JS_IsFunction(ctx, fn)) {
    JS_FreeValue(ctx, fn);
    return 0;
  }

  arg = str ? JS_NewStringLen(ctx, str, len) : JS_UNDEFINED;
  ret = JS_Call(ctx, fn, s->obj, str ? 1 : 0, str ? &arg : NULL);
  JS_FreeValue(ctx, arg);
  JS_FreeValue(ctx, fn);

  if(JS_IsException(ret))
    return -1;

  if(JS_IsNumber(ret))
    JS_ToInt32(ctx, &r, ret);

  JS_FreeValue(ctx, ret);
  return r < 0 ? r : 0;
}

#define JS_SINK_PLAIN(name, magic) \
  static ssize_t name(void* o) { \
    return js_jsonsink_call(o, magic, 0, 0); \
  }
#define JS_SINK_TEXT(name, magic) \
  static ssize_t name(void* o, const char* str, size_t len) { \
    return js_jsonsink_call(o, magic, str ? str : "", len); \
  }

JS_SINK_TEXT(js_sink_error, M_ERROR)
JS_SINK_PLAIN(js_sink_null, M_NULL)
JS_SINK_PLAIN(js_sink_true, M_TRUE)
JS_SINK_PLAIN(js_sink_false, M_FALSE)
JS_SINK_TEXT(js_sink_number, M_NUMBER)
JS_SINK_TEXT(js_sink_string, M_STRING)
JS_SINK_PLAIN(js_sink_array_start, M_ARRAY_START)
JS_SINK_PLAIN(js_sink_array_end, M_ARRAY_END)
JS_SINK_PLAIN(js_sink_object_start, M_OBJECT_START)
JS_SINK_PLAIN(js_sink_object_end, M_OBJECT_END)
JS_SINK_TEXT(js_sink_key, M_KEY)
JS_SINK_TEXT(js_sink_comment, M_COMMENT)
JS_SINK_TEXT(js_sink_raw, M_RAW)

JsonJsSink*
js_jsonsink_from_js(JSContext* ctx, JSValueConst obj, JsonEventSink* sink) {
  JsonJsSink* s;

  if(!JS_IsObject(obj)) {
    JS_ThrowTypeError(ctx, "JsonEventSink: an object is required");
    return NULL;
  }

  if(!(s = js_malloc(ctx, sizeof(*s))))
    return NULL;

  s->ctx = ctx;
  s->obj = JS_DupValue(ctx, obj);
  *sink = (JsonEventSink){
      s,
      js_sink_error,
      js_sink_null,
      js_sink_true,
      js_sink_false,
      js_sink_number,
      js_sink_string,
      js_sink_array_start,
      js_sink_array_end,
      js_sink_object_start,
      js_sink_object_end,
      js_sink_key,
      js_sink_comment,
      js_sink_raw,
  };
  return s;
}

void
js_jsonsink_from_js_free(JSContext* ctx, JsonJsSink* s) {
  if(s) {
    JS_FreeValue(ctx, s->obj);
    js_free(ctx, s);
  }
}

void
js_jsonsink_from_js_free_rt(JSRuntime* rt, JsonJsSink* s) {
  if(s) {
    JS_FreeValueRT(rt, s->obj);
    js_free_rt(rt, s);
  }
}

/* js_jsonsink_failed: the exception for a sink member that returned r < 0.
 *
 * a JS-object sink (adapter) leaves its own exception pending, a native sink gives a code.
 *
 * returns JS_EXCEPTION. */
JSValue
js_jsonsink_failed(JSContext* ctx, int adapter, ssize_t r) {
  JSValue err;

  if(adapter)
    return JS_EXCEPTION;

  err = JS_NewError(ctx);
  JS_DefinePropertyValueStr(ctx, err, "message", JS_NewString(ctx, "JsonEventSink: event failed"), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
  JS_DefinePropertyValueStr(ctx, err, "code", JS_NewInt64(ctx, (int64_t)r), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
  return JS_Throw(ctx, err);
}

/**
 * @}
 */
