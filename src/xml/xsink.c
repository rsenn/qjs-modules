/* xsink.c: js_xmlsink_register(), js_xmlsink_define(), js_xmlsink_wrap().
 * depends on: xevent.h (the sink), quickjs.h.
 * rule: one JSCFunction for all nine methods, dispatched by magic; the class decides how a
 * failure is thrown. */

#define _GNU_SOURCE 1

#include "xml/xsink.h"
#include <cutils.h>
#include <string.h>

/**
 * \addtogroup xsink
 * @{
 */

enum { M_ERROR, M_ELEMENT_START, M_ATTRIBUTE, M_ELEMENT_END, M_TEXT, M_COMMENT, M_DECLARATION, M_INSTRUCTION, M_CDATA };

static const XmlEventType method_types[] = {
    XE_ERROR, XE_ELEMENT_START, XE_ATTRIBUTE, XE_ELEMENT_END, XE_TEXT, XE_COMMENT, XE_DECLARATION, XE_INSTRUCTION, XE_CDATA};

static JSClassID js_xmlsink_class_id = 0;

/* classes whose objects can use the methods */
static struct {
  JSClassID class_id;
  XmlSinkFromOpaque* from_opaque;
  XmlSinkThrow* throw_;
  XmlSinkNote* note;
} sink_classes[16];

static size_t sink_classes_len;

int
js_xmlsink_register(JSClassID class_id, XmlSinkFromOpaque* from_opaque, XmlSinkThrow* throw_, XmlSinkNote* note) {
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

static XmlEventSink
js_xmlsink_copy(void* opaque) {
  return *(XmlEventSink*)opaque;
}

static JSValue
js_xmlsink_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  XmlEventSink sink;
  JSClassID class_id = JS_GetClassID(this_val);
  size_t k;
  void* opaque = 0;
  const char *name = 0, *value = 0;
  size_t nlen = 0, vlen = 0;
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
    return JS_ThrowTypeError(ctx, "XmlEventSink: not a sink object");

  if(argc > 0 && !(magic == M_ELEMENT_END && JS_IsUndefined(argv[0]))) {
    if(!(name = JS_ToCStringLen(ctx, &nlen, argv[0])))
      return JS_EXCEPTION;
  } else if(magic != M_ELEMENT_END) {
    return JS_ThrowTypeError(ctx, "XmlEventSink: a name or text is required");
  }

  if(magic == M_ATTRIBUTE && argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]) && !(JS_IsBool(argv[1]) && JS_ToBool(ctx, argv[1])))
    if(!(value = JS_ToCStringLen(ctx, &vlen, argv[1]))) {
      JS_FreeCString(ctx, name);
      return JS_EXCEPTION;
    }

  switch(magic) {
    case M_ERROR:
      has_cb = !!sink.error;
      w = has_cb ? sink.error(sink.opaque, name, nlen) : 0;
      break;
    case M_ELEMENT_START:
      has_cb = !!sink.element_start;
      w = has_cb ? sink.element_start(sink.opaque, name, nlen) : 0;
      break;
    case M_ATTRIBUTE:
      has_cb = !!sink.attribute;
      w = has_cb ? sink.attribute(sink.opaque, name, nlen, value, vlen) : 0;
      break;
    case M_ELEMENT_END:
      has_cb = !!sink.element_end;
      w = has_cb ? sink.element_end(sink.opaque, name, nlen) : 0;
      break;
    case M_TEXT:
      has_cb = !!sink.text;
      w = has_cb ? sink.text(sink.opaque, name, nlen) : 0;
      break;
    case M_COMMENT:
      has_cb = !!sink.comment;
      w = has_cb ? sink.comment(sink.opaque, name, nlen) : 0;
      break;
    case M_DECLARATION:
      has_cb = !!sink.declaration;
      w = has_cb ? sink.declaration(sink.opaque, name, nlen) : 0;
      break;
    case M_INSTRUCTION:
      has_cb = !!sink.instruction;
      w = has_cb ? sink.instruction(sink.opaque, name, nlen) : 0;
      break;
    case M_CDATA:
      has_cb = !!sink.cdata;
      w = has_cb ? sink.cdata(sink.opaque, name, nlen) : 0;
      break;
  }

  if(name)
    JS_FreeCString(ctx, name);
  if(value)
    JS_FreeCString(ctx, value);

  if(!has_cb)
    return JS_ThrowTypeError(ctx, "XmlEventSink: callback not set");

  if(w < 0) {
    if(sink_classes[k].throw_)
      return sink_classes[k].throw_(ctx, w);

    return JS_ThrowInternalError(ctx, "XmlEventSink: event failed (%zd)", w);
  }

  if(sink_classes[k].note)
    sink_classes[k].note(opaque, method_types[magic]);

  return JS_NewInt64(ctx, w);
}

static const JSCFunctionListEntry js_xmlsink_tag[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XmlEventSink", JS_PROP_CONFIGURABLE),
};

int
js_xmlsink_define(JSContext* ctx, JSValueConst obj) {
  static const JSCFunctionListEntry funcs[] = {
      JS_CFUNC_MAGIC_DEF("error", 1, js_xmlsink_method, M_ERROR),
      JS_CFUNC_MAGIC_DEF("elementStart", 1, js_xmlsink_method, M_ELEMENT_START),
      JS_CFUNC_MAGIC_DEF("attribute", 2, js_xmlsink_method, M_ATTRIBUTE),
      JS_CFUNC_MAGIC_DEF("elementEnd", 1, js_xmlsink_method, M_ELEMENT_END),
      JS_CFUNC_MAGIC_DEF("text", 1, js_xmlsink_method, M_TEXT),
      JS_CFUNC_MAGIC_DEF("comment", 1, js_xmlsink_method, M_COMMENT),
      JS_CFUNC_MAGIC_DEF("declaration", 1, js_xmlsink_method, M_DECLARATION),
      JS_CFUNC_MAGIC_DEF("instruction", 1, js_xmlsink_method, M_INSTRUCTION),
      JS_CFUNC_MAGIC_DEF("cdata", 1, js_xmlsink_method, M_CDATA),
  };

  return JS_SetPropertyFunctionList(ctx, obj, funcs, countof(funcs));
}

static void
js_xmlsink_finalizer(JSRuntime* rt, JSValue val) {
  js_free_rt(rt, JS_GetOpaque(val, js_xmlsink_class_id));
}

static JSClassDef js_xmlsink_class = {
    .class_name = "XmlEventSink",
    .finalizer = js_xmlsink_finalizer,
};

JSValue
js_xmlsink_wrap(JSContext* ctx, XmlEventSink sink) {
  JSRuntime* rt = JS_GetRuntime(ctx);
  JSValue proto, obj;
  XmlEventSink* copy;

  if(js_xmlsink_class_id == 0)
    JS_NewClassID(&js_xmlsink_class_id);

  if(!JS_IsRegisteredClass(rt, js_xmlsink_class_id))
    JS_NewClass(rt, js_xmlsink_class_id, &js_xmlsink_class);

  js_xmlsink_register(js_xmlsink_class_id, js_xmlsink_copy, 0, 0);

  proto = JS_GetClassProto(ctx, js_xmlsink_class_id);

  if(JS_IsException(proto))
    return JS_EXCEPTION;

  if(JS_IsNull(proto) || JS_IsUndefined(proto)) {
    proto = JS_NewObject(ctx);

    if(js_xmlsink_define(ctx, proto) < 0 || JS_SetPropertyFunctionList(ctx, proto, js_xmlsink_tag, countof(js_xmlsink_tag)) < 0) {
      JS_FreeValue(ctx, proto);
      return JS_EXCEPTION;
    }

    JS_SetClassProto(ctx, js_xmlsink_class_id, JS_DupValue(ctx, proto));
  }

  if(!(copy = js_malloc(ctx, sizeof(*copy)))) {
    JS_FreeValue(ctx, proto);
    return JS_EXCEPTION;
  }

  *copy = sink;
  obj = JS_NewObjectProtoClass(ctx, proto, js_xmlsink_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    js_free(ctx, copy);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, copy);
  return obj;
}

/* the JS object as a sink: each member calls the method of the same name on `obj` */
struct xml_js_sink {
  JSContext* ctx;
  JSValue obj;
};

static const char* const js_sink_names[] = {"error", "elementStart", "attribute", "elementEnd", "text", "comment", "declaration", "instruction", "cdata"};

/* js_xmlsink_call: obj[name](a, b?) with a, b strings, or undefined for NULL; a missing method drops the event.
 *
 * returns the method's number when negative, else 0; -1 with the exception pending when it throws. */
static ssize_t
js_xmlsink_call(void* opaque, int magic, const char* a, size_t alen, const char* b, size_t blen, int argc) {
  XmlJsSink* s = opaque;
  JSContext* ctx = s->ctx;
  JSValue fn = JS_GetPropertyStr(ctx, s->obj, js_sink_names[magic]), args[2], ret;
  int32_t r = 0;

  if(JS_IsException(fn))
    return -1;

  if(!JS_IsFunction(ctx, fn)) {
    JS_FreeValue(ctx, fn);
    return 0;
  }

  args[0] = a ? JS_NewStringLen(ctx, a, alen) : JS_UNDEFINED;
  args[1] = b ? JS_NewStringLen(ctx, b, blen) : JS_UNDEFINED;
  ret = JS_Call(ctx, fn, s->obj, argc, args);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, fn);

  if(JS_IsException(ret))
    return -1;

  if(JS_IsNumber(ret))
    JS_ToInt32(ctx, &r, ret);

  JS_FreeValue(ctx, ret);
  return r < 0 ? r : 0;
}

#define JS_SINK_TEXT(name, magic) \
  static ssize_t name(void* o, const char* str, size_t len) { \
    return js_xmlsink_call(o, magic, str, len, 0, 0, 1); \
  }

JS_SINK_TEXT(js_sink_error, M_ERROR)
JS_SINK_TEXT(js_sink_element_start, M_ELEMENT_START)
JS_SINK_TEXT(js_sink_text, M_TEXT)
JS_SINK_TEXT(js_sink_comment, M_COMMENT)
JS_SINK_TEXT(js_sink_declaration, M_DECLARATION)
JS_SINK_TEXT(js_sink_instruction, M_INSTRUCTION)
JS_SINK_TEXT(js_sink_cdata, M_CDATA)

static ssize_t
js_sink_element_end(void* o, const char* str, size_t len) {
  return js_xmlsink_call(o, M_ELEMENT_END, str, len, 0, 0, str ? 1 : 0);
}

static ssize_t
js_sink_attribute(void* o, const char* name, size_t nlen, const char* value, size_t vlen) {
  return js_xmlsink_call(o, M_ATTRIBUTE, name, nlen, value, vlen, value ? 2 : 1);
}

XmlJsSink*
js_xmlsink_from_js(JSContext* ctx, JSValueConst obj, XmlEventSink* sink) {
  XmlJsSink* s;

  if(!JS_IsObject(obj)) {
    JS_ThrowTypeError(ctx, "XmlEventSink: an object is required");
    return NULL;
  }

  if(!(s = js_malloc(ctx, sizeof(*s))))
    return NULL;

  s->ctx = ctx;
  s->obj = JS_DupValue(ctx, obj);
  *sink = (XmlEventSink){
      s,
      js_sink_error,
      js_sink_element_start,
      js_sink_attribute,
      js_sink_element_end,
      js_sink_text,
      js_sink_comment,
      js_sink_declaration,
      js_sink_instruction,
      js_sink_cdata,
  };
  return s;
}

void
js_xmlsink_from_js_free(JSContext* ctx, XmlJsSink* s) {
  if(s) {
    JS_FreeValue(ctx, s->obj);
    js_free(ctx, s);
  }
}

void
js_xmlsink_from_js_free_rt(JSRuntime* rt, XmlJsSink* s) {
  if(s) {
    JS_FreeValueRT(rt, s->obj);
    js_free_rt(rt, s);
  }
}

/* js_xmlsink_failed: the exception for a sink member that returned r < 0.
 *
 * a JS-object sink (adapter) leaves its own exception pending, a native sink gives a code.
 *
 * returns JS_EXCEPTION. */
JSValue
js_xmlsink_failed(JSContext* ctx, int adapter, ssize_t r) {
  JSValue err;

  if(adapter)
    return JS_EXCEPTION;

  err = JS_NewError(ctx);
  JS_DefinePropertyValueStr(ctx, err, "message", JS_NewString(ctx, "XmlEventSink: event failed"), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
  JS_DefinePropertyValueStr(ctx, err, "code", JS_NewInt64(ctx, (int64_t)r), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
  return JS_Throw(ctx, err);
}

/**
 * @}
 */
