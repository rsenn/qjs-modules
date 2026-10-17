/* jsink.c: js_jsonsink_register(), js_jsonsink_define(), js_jsonsink_wrap().
 * depends on: jevent.h (the sink), quickjs.h.
 * rule: one JSCFunction for all eleven methods, dispatched by magic; the class decides how a
 * failure is thrown. */

#define _GNU_SOURCE 1

#include "json/jsink.h"
#include <cutils.h>
#include <string.h>

enum { M_ERROR, M_NULL, M_TRUE, M_FALSE, M_NUMBER, M_STRING, M_ARRAY_START, M_ARRAY_END, M_OBJECT_START, M_OBJECT_END, M_KEY };

static const JsonEventType method_types[] = {JSON_EVENT_ERROR, JSON_EVENT_NULL, JSON_EVENT_TRUE, JSON_EVENT_FALSE, JSON_EVENT_NUMBER, JSON_EVENT_STRING, JSON_EVENT_ARRAY_START, JSON_EVENT_ARRAY_END, JSON_EVENT_OBJECT_START, JSON_EVENT_OBJECT_END, JSON_EVENT_KEY};

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

  if(magic == M_ERROR || magic == M_NUMBER || magic == M_STRING || magic == M_KEY) {
    if(argc < 1)
      return JS_ThrowTypeError(ctx, "JsonEventSink: a text is required");

    if(!(str = JS_ToCStringLen(ctx, &len, argv[0])))
      return JS_EXCEPTION;
  }

  switch(magic) {
    case M_ERROR: has_cb = !!sink.error; w = has_cb ? sink.error(sink.opaque, str, len) : 0; break;
    case M_NULL: has_cb = !!sink.null; w = has_cb ? sink.null(sink.opaque) : 0; break;
    case M_TRUE: has_cb = !!sink.true_; w = has_cb ? sink.true_(sink.opaque) : 0; break;
    case M_FALSE: has_cb = !!sink.false_; w = has_cb ? sink.false_(sink.opaque) : 0; break;
    case M_NUMBER: has_cb = !!sink.number; w = has_cb ? sink.number(sink.opaque, str, len) : 0; break;
    case M_STRING: has_cb = !!sink.string; w = has_cb ? sink.string(sink.opaque, str, len) : 0; break;
    case M_ARRAY_START: has_cb = !!sink.array_start; w = has_cb ? sink.array_start(sink.opaque) : 0; break;
    case M_ARRAY_END: has_cb = !!sink.array_end; w = has_cb ? sink.array_end(sink.opaque) : 0; break;
    case M_OBJECT_START: has_cb = !!sink.object_start; w = has_cb ? sink.object_start(sink.opaque) : 0; break;
    case M_OBJECT_END: has_cb = !!sink.object_end; w = has_cb ? sink.object_end(sink.opaque) : 0; break;
    case M_KEY: has_cb = !!sink.key; w = has_cb ? sink.key(sink.opaque, str, len) : 0; break;
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
