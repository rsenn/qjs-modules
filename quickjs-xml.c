#include "defines.h"
#include <cutils.h>
#include "property-enumeration.h"
#include <quickjs.h>
#include "utils.h"
#include "vector.h"
#include "buffer-utils.h"
#include "stream-utils.h"
#include "location.h"
#include "debug.h"
#include "virtual-properties.h"
#include "quickjs-location.h"
#include "xml/xwrite.h"
#include "xml/xsink.h"
#include "xml/xtreewrite.h"
#include "xml/xread.h"
#include "xml/xbuilder.h"
#include "xml/xml_entities.h"
#include "xml/xml.h"
#include "xml/xwalk.h"
#include "xml/xserial.h"

#include <stdint.h>

char* js_inspect_tostring(JSContext* ctx, JSValueConst value);

/**
 * \defgroup quickjs-xml quickjs-xml: XML parser & printer
 * @{
 */
static const char* const default_self_closing_tags[] = {
    "area",
    "base",
    "br",
    "col",
    "embed",
    "hr",
    "img",
    "input",
    "link",
    "meta",
    "param",
    "source",
    "track",
    "wbr",
    0,
};

typedef struct {
  BOOL flat, tolerant, location;
  const char* const* self_closing_tags;
} ParseOptions;

#ifdef DEBUG_OUTPUT
#define xml_debug(args...) printf(args)
#else
#define xml_debug(args...)
#endif

static JSValue
make_tuple(JSContext* ctx, JSValue a, JSValue b) {
  JSValue ret = JS_NewArray(ctx);

  JS_SetPropertyUint32(ctx, ret, 0, a);
  JS_SetPropertyUint32(ctx, ret, 1, b);

  return ret;
}

/* the element tree or flat list of `buf`, driven by the event parser of src/xml/xml.c.
 *
 *   tree  [{tagName, attributes, children}], children only on elements that stay open
 *   flat  [{tagName, attributes}, 'text', {tagName: '/name'}, ...]
 *
 * returns the array (or [array, WeakMap] with location) or throws SyntaxError. */
/* js_xml_parse: the element tree or flat list of `buf`, driven by the event parser of
 * src/xml/xml.c and built by XmlBuilder (src/xml/xbuilder.c).
 *
 *   tree  [{tagName, attributes, children}], children only on elements that stay open
 *   flat  [{tagName, attributes}, 'text', {tagName: '/name'}, ...]
 *
 * returns the array (or [array, WeakMap] with location) or throws SyntaxError. */
static JSValue
js_xml_parse(JSContext* ctx, const uint8_t* buf, size_t len, const char* input_name, ParseOptions opts) {
  Reader reader = reader_from_bytes(buf, len);
  XMLParser xp;
  XmlBuilder xb;
  JSValue ret;
  VirtualProperties vprop = {0};
  XmlEvent ev;
  XmlPull st;
  BOOL failed = FALSE;

  xml_init(&xp, &reader);
  xml_set_tolerant(&xp, opts.tolerant);
  xml_set_flat(&xp, opts.flat);
  xp.self_closing_tags = opts.self_closing_tags;
  xbuild_init(&xb, ctx, XB_PLAIN | (opts.flat ? XB_FLAT : 0));

  if(opts.location)
    vprop = virtual_properties_map(ctx, js_object_new(ctx, "WeakMap", 0, 0));

  while((st = xml_next(&xp, &ev)) != XML_PULL_END && st != XML_PULL_FAIL) {
    BOOL located;

    if(st == XML_PULL_AGAIN)
      continue;

    if(ev.type == XE_ERROR) {
      char msg[128];

      snprintf(msg, sizeof(msg), "mismatch </%.*s>", (int)xp.event_name.len, xp.event_name.data);
      js_throw_syntax_error_at(ctx, input_name, &ev.at, ev.span, ev.code, msg);
      failed = TRUE;
      break;
    }

    xbuild_handler(&xb, &ev);

    /* an element or markup start has a position, so has the end of a flat list's element */
    located = ev.type == XE_ELEMENT_START || ev.type == XE_COMMENT || ev.type == XE_DECLARATION || ev.type == XE_INSTRUCTION || ev.type == XE_CDATA || (ev.type == XE_ELEMENT_END && opts.flat);

    if(located && opts.location) {
      JSValue last = xbuild_last(&xb);

      if(!JS_IsUndefined(last))
        virtual_set(&vprop, ctx, last, make_tuple(ctx, JS_NewUint32(ctx, xp.tag_line), JS_NewUint32(ctx, xp.tag_column)));

      JS_FreeValue(ctx, last);
    }
  }

  /* input that ends inside open elements: they keep (empty) children */
  if(!failed)
    xbuild_finish(&xb);

  ret = xbuild_root(&xb);
  xbuild_free(&xb, JS_GetRuntime(ctx));
  xml_free(&xp);
  reader_free(&reader);

  if(failed) {
    JS_FreeValue(ctx, ret);

    if(opts.location)
      JS_FreeValue(ctx, vprop.this_obj);

    return JS_EXCEPTION;
  }

  return opts.location ? make_tuple(ctx, ret, vprop.this_obj) : ret;
}

static JSValue
js_xml_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  InputBuffer input;
  const char* input_name = 0;
  ParseOptions opts = {
      .flat = FALSE,
      .tolerant = FALSE,
      .location = FALSE,
      .self_closing_tags = default_self_closing_tags,
  };

  if(!(argc > 0 && js_is_bytes_or_string(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "xml.read(): expecting buffer or string");

  input = js_input_chars(ctx, argv[0]);

  if(input.size == 0) {
    SourcePos start;

    inputbuffer_free(&input, ctx);
    srcpos_init(&start, 0);
    return js_throw_syntax_error_at(ctx, "<xml>", &start, 0, "unexpected-end", "xml.read(): empty input");
  }

  if(argc >= 2)
    input_name = JS_ToCString(ctx, argv[1]);

  if(argc >= 3) {
    if(JS_IsObject(argv[2])) {
      JSValue tags = JS_UNDEFINED;

      if(js_has_propertystr(ctx, argv[2], "flat"))
        opts.flat = js_get_propertystr_bool(ctx, argv[2], "flat");

      if(js_has_propertystr(ctx, argv[2], "tolerant"))
        opts.tolerant = js_get_propertystr_bool(ctx, argv[2], "tolerant");

      if(js_has_propertystr(ctx, argv[2], "location"))
        opts.location = js_get_propertystr_bool(ctx, argv[2], "location");

      if(js_has_propertystr(ctx, argv[2], "selfClosingTags"))
        tags = JS_GetPropertyStr(ctx, argv[2], "selfClosingTags");

      if(JS_IsArray(ctx, tags)) {
        size_t ac;
        opts.self_closing_tags = (const char* const*)js_array_to_argv(ctx, &ac, tags);
      }

      JS_FreeValue(ctx, tags);

    } else {
      opts.flat = JS_ToBool(ctx, argv[2]);

      if(argc >= 4)
        opts.tolerant = JS_ToBool(ctx, argv[3]);
    }
  }

  ret = js_xml_parse(ctx, input.data, input.size, input_name ? input_name : "<xml>", opts);

  if(input_name)
    JS_FreeCString(ctx, input_name);

  inputbuffer_free(&input, ctx);
  return ret;
}

static JSValue
js_xml_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  DynBuf output = {0};
  JSValueConst obj = argc > 0 ? argv[0] : JS_UNDEFINED;
  JSValue ret, last, children = JS_UNDEFINED, arr = JS_UNDEFINED;
  int32_t max_depth = INT32_MAX;
  size_t len;
  BOOL flat = TRUE;

  dbuf_init_ctx(ctx, &output);

  if(argc >= 2)
    JS_ToInt32(ctx, &max_depth, argv[1]);

  if(!JS_IsArray(ctx, obj)) {
    arr = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, arr, 0, JS_DupValue(ctx, obj));
    obj = arr;
  }

  len = js_array_length(ctx, obj);
  last = JS_GetPropertyUint32(ctx, obj, len - 1);

  if(JS_IsObject(last)) {
    children = JS_GetPropertyStr(ctx, last, "children");

    if(JS_IsArray(ctx, children))
      flat = FALSE;
  }

  xml_debug("js_xml_write len=%"PRIu64", children=%s, flat=%d\n", (uint64_t)len, JS_ToCString(ctx, children), flat);

  if(xwrite_tree(ctx, obj, max_depth, flat, writer_from_dynbuf(&output)) < 0)
    ret = JS_EXCEPTION;
  else
    ret = JS_NewStringLen(ctx, (const char*)output.buf, output.size);

  dbuf_free(&output);

  if(!JS_IsUndefined(arr))
    JS_FreeValue(ctx, arr);

  return ret;
}

static const JSCFunctionListEntry js_xml_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_xml_read),
    JS_CFUNC_DEF("write", 2, js_xml_write),
};

/* XMLTreeSerializer: a tree walked as XmlEvent (XwalkSource) pulled through an XmlSerial, so
 * read() steps the walk only as far as the bytes asked for.
 *
 * ```js
 * const s = new XMLTreeSerializer([{tagName: 'a', attributes: {id: '7'}, children: ['x']}]);
 * s.read(4);        // '<a i'
 * s.read(100);      // 'd="7">x</a>'
 * s.finished;       // true
 * ```
 *
 *   read(n)        up to n bytes as a string, cut on a character boundary
 *   read(buffer)   fills the ArrayBuffer or typed array, returns the byte count
 *   root           the tree given to the constructor
 *   finished       true once the walk has ended
 *
 * throws a bad tree like XMLWriter does (mismatched end, bad element), RangeError for a negative n.
 * wired in: JS_CFUNC_DEF("read") and two getters on the XMLTreeSerializer prototype.
 * rule: an event is taken once, so a getter in the tree runs once. */
static JSClassID js_xmlserializer_class_id;
static JSValue xmlserializer_proto, xmlserializer_ctor;

static JSValue xmlwriter_throw(JSContext* ctx, ssize_t r);

typedef struct {
  XwalkSource src;
  XmlSerial ser;
  JSValue root;
  unsigned failed : 1;
} XMLTreeSerializer;

/* xmlserializer_read: up to n bytes into buf, on a character boundary.
 *
 * a failure throws once, the walk is closed then and later reads find the end.
 *
 * returns the bytes copied (0 at the end), or -1 with an exception pending. */
static ssize_t
xmlserializer_read(JSContext* ctx, XMLTreeSerializer* xs, void* buf, size_t n) {
  ssize_t r;

  if(xs->failed)
    return 0;

  if((r = xserial_read(&xs->ser, buf, n, 1)) >= 0)
    return r;

  xs->failed = TRUE;
  xwalk_source_close(&xs->src, ctx);

  if(xs->ser.error)
    xmlwriter_throw(ctx, xs->ser.error);

  return -1;
}

static JSValue
js_xmlserializer_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XMLTreeSerializer* xs;
  ssize_t r;

  if(!(xs = JS_GetOpaque2(ctx, this_val, js_xmlserializer_class_id)))
    return JS_EXCEPTION;

  if(argc > 0 && (js_is_arraybuffer(ctx, argv[0]) || js_is_sharedarraybuffer(ctx, argv[0]) || js_is_typedarray(ctx, argv[0]))) {
    InputBuffer buf = js_input_args(ctx, argc, argv);

    r = xmlserializer_read(ctx, xs, buf.data, inputbuffer_length(&buf));
    inputbuffer_free(&buf, ctx);
    return r < 0 ? JS_EXCEPTION : JS_NewInt64(ctx, r);
  }

  int64_t n;

  if(JS_ToInt64(ctx, &n, argc > 0 ? argv[0] : JS_UNDEFINED))
    return JS_EXCEPTION;

  if(n < 0)
    return JS_ThrowRangeError(ctx, "size must not be negative");

  DynBuf out;
  uint8_t chunk[4096];

  dbuf_init_ctx(ctx, &out);

  while(n > 0 && (r = xmlserializer_read(ctx, xs, chunk, MIN_NUM((size_t)n, sizeof(chunk)))) > 0) {
    dbuf_put(&out, chunk, r);
    n -= r;
  }

  JSValue ret = r < 0 ? JS_EXCEPTION : JS_NewStringLen(ctx, (const char*)out.buf, out.size);

  dbuf_free(&out);
  return ret;
}

static JSValue
js_xmlserializer_get_root(JSContext* ctx, JSValueConst this_val) {
  XMLTreeSerializer* xs;

  if(!(xs = JS_GetOpaque2(ctx, this_val, js_xmlserializer_class_id)))
    return JS_EXCEPTION;

  return JS_DupValue(ctx, xs->root);
}

static JSValue
js_xmlserializer_get_finished(JSContext* ctx, JSValueConst this_val) {
  XMLTreeSerializer* xs;

  if(!(xs = JS_GetOpaque2(ctx, this_val, js_xmlserializer_class_id)))
    return JS_EXCEPTION;

  return JS_NewBool(ctx, xs->ser.status != XML_PULL_EVENT);
}

static JSValue
js_xmlserializer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj, root = JS_UNDEFINED;
  JSValueConst tree = argc > 0 ? argv[0] : JS_UNDEFINED;
  XMLTreeSerializer* xs;

  if(!(xs = js_mallocz(ctx, sizeof(XMLTreeSerializer))))
    return JS_EXCEPTION;

  /* a single node is a list of one */
  if(JS_IsArray(ctx, tree)) {
    root = JS_DupValue(ctx, tree);
  } else {
    root = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, root, 0, JS_DupValue(ctx, tree));
  }

  /* as xml.write(): a list whose last node has no children array is a flat list */
  size_t len = js_array_length(ctx, root);
  JSValue last = len ? JS_GetPropertyUint32(ctx, root, len - 1) : JS_UNDEFINED;
  JSValue kids = JS_IsObject(last) ? JS_GetPropertyStr(ctx, last, "children") : JS_UNDEFINED;
  int flags = JS_IsArray(ctx, kids) ? 0 : XWALK_FLAT | XWALK_BALANCED;

  JS_FreeValue(ctx, kids);
  JS_FreeValue(ctx, last);

  if(xwalk_source_init(&xs->src, ctx, root, flags, NULL)) {
    JS_FreeValue(ctx, root);
    js_free(ctx, xs);
    return JS_EXCEPTION;
  }

  xserial_init(&xs->ser, xwalk_source(&xs->src), (XmlWriteOptions){0}, JS_GetRuntime(ctx));
  xs->root = JS_DupValue(ctx, tree);
  JS_FreeValue(ctx, root);

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  obj = JS_IsException(proto) ? JS_EXCEPTION : JS_NewObjectProtoClass(ctx, proto, js_xmlserializer_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    xwalk_source_close(&xs->src, ctx);
    xserial_free(&xs->ser);
    JS_FreeValue(ctx, xs->root);
    js_free(ctx, xs);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, xs);
  return obj;
}

static void
js_xmlserializer_finalizer(JSRuntime* rt, JSValue val) {
  XMLTreeSerializer* xs;

  if((xs = JS_GetOpaque(val, js_xmlserializer_class_id))) {
    xwalk_source_close_rt(&xs->src, rt);
    xserial_free(&xs->ser);
    JS_FreeValueRT(rt, xs->root);
    js_free_rt(rt, xs);
  }
}

static JSClassDef js_xmlserializer_class = {
    .class_name = "XMLTreeSerializer",
    .finalizer = js_xmlserializer_finalizer,
};

static const JSCFunctionListEntry js_xmlserializer_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_xmlserializer_read),
    JS_CGETSET_DEF("root", js_xmlserializer_get_root, 0),
    JS_CGETSET_DEF("finished", js_xmlserializer_get_finished, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLTreeSerializer", JS_PROP_CONFIGURABLE),
};

typedef struct PushParser {
  JSContext* ctx;
  JSValue this_obj;
  struct xr_state xrs;
  JSValue attribute, element_start, element_end, error, text;
  JSValue on_parse_error; /* onParseError(err): receives the SyntaxError instead of a throw */
  JSValue last_error;     /* SyntaxError of the latest error, or JS_UNDEFINED */
  XmlBuilder builder;
  XmlEventSink sink; /* xbuild_sink(&builder) */
  BOOL use_builder; /* build {tagName, attributes, children} tree
                       instead of forwarding events */
} XMLPushParser;

/* xml_pushparser_report: builds the SyntaxError for a failure at `pos`, keeps it as
 * `last_error` and passes it to `onParseError(err)`; throws nothing.
 *
 * returns TRUE when onParseError exists (the error is handled). */
static BOOL
xml_pushparser_report(XMLPushParser* pp, const SourcePos* pos, size_t length, const char* code, const char* msg) {
  JSContext* ctx = pp->ctx;
  JSValue err = js_syntax_error_at(ctx, "<xml>", pos, length, code, msg);

  if(JS_IsException(err)) {
    JS_FreeValue(ctx, JS_GetException(ctx));
    return FALSE;
  }

  JS_FreeValue(ctx, pp->last_error);
  pp->last_error = JS_DupValue(ctx, err);

  if(JS_IsFunction(ctx, pp->on_parse_error)) {
    JSValue ret = JS_Call(ctx, pp->on_parse_error, pp->this_obj, 1, &err);

    JS_FreeValue(ctx, ret); /* an exception from the callback is dropped */
    JS_FreeValue(ctx, JS_GetException(ctx));
  }

  JS_FreeValue(ctx, err);
  return JS_IsFunction(ctx, pp->on_parse_error);
}

/* xml_pushparser_event_build: the handler of the builder mode, an error goes to onParseError. */
static ssize_t
xml_pushparser_event_build(void* user, const XmlEvent* ev) {
  XMLPushParser* pp = user;

  if(ev->type == XE_ERROR) {
    xml_pushparser_report(pp, &ev->at, ev->span, ev->code, "XMLPushParser: malformed input");
    return 0;
  }

  return xevent_send(&pp->sink, ev);
}

/* xml_pushparser_event: the handler of the callback mode, calls the matching option function.
 *
 *   event          callback
 *   element_start  elementStart(name)
 *   element_end    elementEnd(name)
 *   attribute      attribute(name, value), true for a bare attribute
 *   text           text(undefined, value)
 *   error          error("Error!", offending text), and onParseError(err)
 */
static ssize_t
xml_pushparser_event(void* user, const XmlEvent* ev) {
  XMLPushParser* pp = user;
  JSContext* ctx = pp->ctx;
  JSValue args[2] = {JS_UNDEFINED, JS_UNDEFINED};
  JSValue* cb = 0;

  if(ev->type == XE_ERROR)
    xml_pushparser_report(pp, &ev->at, ev->span, ev->code, "XMLPushParser: malformed input");

  switch(ev->type) {
    case XE_ELEMENT_START:
    case XE_ELEMENT_END:
      cb = ev->type == XE_ELEMENT_START ? &pp->element_start : &pp->element_end;
      args[0] = ev->name ? JS_NewStringLen(ctx, ev->name, ev->nlen) : JS_UNDEFINED;
      break;
    case XE_ATTRIBUTE:
      cb = &pp->attribute;
      args[0] = JS_NewStringLen(ctx, ev->name, ev->nlen);
      args[1] = ev->value ? JS_NewStringLen(ctx, ev->value, ev->vlen) : JS_TRUE;
      break;
    case XE_TEXT:
      cb = &pp->text;
      args[1] = JS_NewStringLen(ctx, ev->str, ev->len);
      break;
    case XE_ERROR:
      cb = &pp->error;
      args[0] = JS_NewString(ctx, "Error!");
      args[1] = ev->str ? JS_NewStringLen(ctx, ev->str, ev->len) : JS_UNDEFINED;
      break;
    default: break;
  }

  /* the options object need not define every callback, a missing one is skipped */
  if(cb && JS_IsFunction(ctx, *cb))
    JS_FreeValue(ctx, JS_Call(ctx, *cb, pp->this_obj, countof(args), args));

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  return 0;
}

static JSClassID js_xml_pushparser_class_id;
static JSValue xml_pushparser_proto, xml_pushparser_ctor;

static JSValue
js_xml_pushparser_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XMLPushParser* pp;
  InputBuffer input;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  if(!(argc > 0 && js_is_bytes_or_string(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "XMLPushParser.write(): expecting buffer or string");

  input = js_input_chars(ctx, argv[0]);

  if(input.data == 0)
    return JS_EXCEPTION;

  xr_feed(&pp->xrs, (const char*)inputbuffer_data(&input), inputbuffer_length(&input), pp->use_builder ? xml_pushparser_event_build : xml_pushparser_event, pp);
  inputbuffer_free(&input, ctx);

  /* xr_read() sets xrs.error and goes silently inert (every further xr_read()/
   * xr_finish() call becomes a no-op) rather than reporting anything itself - most
   * notably on any non-whitespace text between tags, which its grammar doesn't
   * accept at all (see BUGS: xread-no-text-content). With custom callbacks, the
   * caller's own error() callback already ran (xread_callback() dispatches
   * XE_ERROR to it) and owns handling this, so only throw here in the
   * builder fallback, which otherwise has no way to report it at all - .root
   * would just silently stop growing. */
  if(pp->xrs.error && pp->use_builder && !JS_IsFunction(ctx, pp->on_parse_error))
    return js_throw_syntax_error_at(ctx, "<xml>", &pp->xrs.err_pos, pp->xrs.err_len, xr_error_name(pp->xrs.err_code), "XMLPushParser: malformed input");

  return JS_UNDEFINED;
}

static JSValue
js_xml_pushparser_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XMLPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  xr_end(&pp->xrs, pp->use_builder ? xml_pushparser_event_build : xml_pushparser_event, pp);

  /* xr_finish()'s own !at_root check (which sets xrs.error) only catches ending
   * mid-tag, e.g. "<a><b" with no closing '>' - src/xml/xread.c's l_tag_end sets
   * at_root=1 after *every* '>', nested or not, so it does NOT mean every opened
   * element was also closed. When building a tree, check the frame stack
   * directly instead, to also catch e.g. "<a><b/>" (well-formed lexically, but
   * <a> itself was never closed). Only in the builder fallback - see write()'s
   * comment above for why custom-callback mode doesn't also throw here. */
  if(pp->xrs.error && pp->use_builder && !JS_IsFunction(ctx, pp->on_parse_error))
    return js_throw_syntax_error_at(ctx, "<xml>", &pp->xrs.err_pos, pp->xrs.err_len, xr_error_name(pp->xrs.err_code), "XMLPushParser: malformed or truncated input");

  if(pp->xrs.error) /* the error callback already ran; nothing left open to report */
    return JS_UNDEFINED;

  if(pp->use_builder && xbuild_depth(&pp->builder)) {
    if(xml_pushparser_report(pp, &pp->xrs.pos, 0, "unexpected-end", "XMLPushParser: unexpected end of input (unclosed element)"))
      return JS_UNDEFINED;

    return js_throw_syntax_error_at(ctx, "<xml>", &pp->xrs.pos, 0, "unexpected-end", "XMLPushParser: unexpected end of input (unclosed element)");
  }

  return JS_UNDEFINED;
}

static JSValue
js_xml_pushparser_get_root(JSContext* ctx, JSValueConst this_val) {
  XMLPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  return xbuild_root(&pp->builder);
}

static JSValue
js_xml_pushparser_get_error(JSContext* ctx, JSValueConst this_val) {
  XMLPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  return JS_IsUndefined(pp->last_error) ? JS_NULL : JS_DupValue(ctx, pp->last_error);
}

/* Array of tagName strings from the document root down to the currently-open
 * element, root-first - mirrors JsonPushParser's .path. Only reflects reality
 * when the builder is actually being fed (pp->use_builder); with custom
 * callbacks given instead, xbuild_start()/xbuild_end() are never called, so this
 * stays empty. */
static JSValue
js_xml_pushparser_get_path(JSContext* ctx, JSValueConst this_val) {
  XMLPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  return xbuild_path(&pp->builder);
}

static JSValue
js_xml_pushparser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  XMLPushParser* pp;
  JSValueConst options = argc > 0 ? argv[0] : JS_UNDEFINED;

  if(!(pp = js_mallocz(ctx, sizeof(XMLPushParser))))
    return JS_EXCEPTION;

  pp->ctx = ctx;

  /* options is also this_obj: the object callbacks are invoked with, so an
   * elementStart() etc. method can be defined directly on the options object passed
   * to the constructor and use `this` to reach the others (or the parser itself, via
   * a self-reference the caller stashes on it) without a separate thisArg. */
  pp->this_obj = JS_DupValue(ctx, options);
  pp->attribute = JS_IsObject(options) ? JS_GetPropertyStr(ctx, options, "attribute") : JS_UNDEFINED;
  pp->element_start = JS_IsObject(options) ? JS_GetPropertyStr(ctx, options, "elementStart") : JS_UNDEFINED;
  pp->element_end = JS_IsObject(options) ? JS_GetPropertyStr(ctx, options, "elementEnd") : JS_UNDEFINED;
  pp->error = JS_IsObject(options) ? JS_GetPropertyStr(ctx, options, "error") : JS_UNDEFINED;
  pp->text = JS_IsObject(options) ? JS_GetPropertyStr(ctx, options, "text") : JS_UNDEFINED;
  pp->on_parse_error = JS_IsObject(options) ? JS_GetPropertyStr(ctx, options, "onParseError") : JS_UNDEFINED;
  pp->last_error = JS_UNDEFINED;

  /* no real callback given at all: build a tree (retrievable via .root) instead of
   * forwarding events that would just be dropped on the floor. */
  pp->use_builder = !JS_IsFunction(ctx, pp->attribute) && !JS_IsFunction(ctx, pp->element_start) && !JS_IsFunction(ctx, pp->element_end) &&
                    !JS_IsFunction(ctx, pp->error) && !JS_IsFunction(ctx, pp->text);

  xr_state_init(&pp->xrs);

  xbuild_init(&pp->builder, ctx, 0);
  pp->sink = xbuild_sink(&pp->builder);

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, xml_pushparser_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_xml_pushparser_class_id);
  JS_FreeValue(ctx, proto);

  JS_SetOpaque(obj, pp);
  return obj;
}

static void
js_xml_pushparser_finalizer(JSRuntime* rt, JSValue val) {
  XMLPushParser* pp;

  if((pp = JS_GetOpaque(val, js_xml_pushparser_class_id))) {
    xr_state_free(&pp->xrs);
    xbuild_free(&pp->builder, rt);
    JS_FreeValueRT(rt, pp->this_obj);
    JS_FreeValueRT(rt, pp->attribute);
    JS_FreeValueRT(rt, pp->element_start);
    JS_FreeValueRT(rt, pp->element_end);
    JS_FreeValueRT(rt, pp->error);
    JS_FreeValueRT(rt, pp->text);
    JS_FreeValueRT(rt, pp->on_parse_error);
    JS_FreeValueRT(rt, pp->last_error);
    js_free_rt(rt, pp);
  }
}

static const JSCFunctionListEntry js_xml_pushparser_proto_funcs[] = {
    JS_CFUNC_DEF("write", 1, js_xml_pushparser_write),
    JS_CFUNC_DEF("close", 0, js_xml_pushparser_close),
    JS_CGETSET_DEF("root", js_xml_pushparser_get_root, 0),
    JS_CGETSET_DEF("path", js_xml_pushparser_get_path, 0),
    JS_CGETSET_DEF("error", js_xml_pushparser_get_error, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLPushParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_xml_pushparser_class = {
    .class_name = "XMLPushParser",
    .finalizer = js_xml_pushparser_finalizer,
};

/* ----------------------------------------------------------------------
   XMLParser: pull (.parse()) parser over include/xml/xml.h's xml_parse(),
   modeled on JsonParser (quickjs-json.c) - a constructor taking a reader-
   like input, .parse() advancing one token at a time, and getters for the
   current token/position - except .parse() returns the raw XmlEventType
   token id (an int) rather than a string, and, like XMLPushParser above,
   every element/attribute/text event is also fed into an XmlBuilder as it
   happens, so .root optionally gives the tree built so far without the
   caller having to do that bookkeeping itself.
   ---------------------------------------------------------------------- */

/* xml_error_new: the SyntaxError for the XE_ERROR `xp` just returned, at the
 * start of the offending tag; `base` supplies the input name.
 *
 * returns the error object (refcount: a new reference), or JS_EXCEPTION. */
static JSValue
xml_error_new(JSContext* ctx, const XMLParser* xp, const Location* base) {
  Location* at;
  JSValue ret;

  if(!(at = location_clone(base, ctx)))
    return JS_EXCEPTION;

  at->line = xp->tag_line - 1;
  at->column = xp->tag_column - 1;
  at->char_offset = at->byte_offset = -1;
  ret = js_syntax_error_new(ctx, at, xp->err_len, xp->err_code, "mismatch </%.*s>", (int)xp->event_name.len, xp->event_name.data);
  location_free(at, JS_GetRuntime(ctx));
  return ret;
}

typedef struct {
  Reader reader;
  XMLParser xp;
  Location* loc;
  XmlBuilder builder;
  XmlEventSink sink; /* xbuild_sink(&builder) */
  BOOL use_builder;
  JSValue element_start, element_end, attribute, text, error;
  JSValue this_obj;
  JSValue last_error; /* SyntaxError after parse() returned PARSE_ERROR, else undefined */
} XmlParser;

static JSClassID js_xml_parser_class_id;
static JSValue xml_proto, xml_ctor;

static uint32_t
xml_depth(XmlParser* p) {
  uint32_t n = 0;
  XMLFrame* f;

  for(f = p->xp.top; f; f = f->parent)
    n++;

  return n;
}

/* p->loc (a ref-counted, js_malloc'd Location, wrapped as-is by .location) tracks
 * xp.loc (a plain embedded value struct xml_parse() advances - see
 * include/xml/xml.h) by copying the fields js_location_wrap()'s consumers care about
 * after every .parse() call; the two can't just be the same object since xp.loc
 * isn't heap-allocated the way location_free() requires. */
static void
xml_sync_location(XmlParser* p) {
  p->loc->line = p->xp.loc.line;
  p->loc->column = p->xp.loc.column;
  p->loc->char_offset = p->xp.loc.char_offset;
  p->loc->byte_offset = p->xp.loc.byte_offset;
}

static void
xml_build(XmlParser* p, XmlEventType ev) {
  XmlEvent e = {0};

  switch(ev) {
    case XE_ELEMENT_START: e = (XmlEvent){.type = XE_ELEMENT_START, .name = p->xp.event_name.data, .nlen = p->xp.event_name.len}; break;
    case XE_ELEMENT_END: xbuild_end(&p->builder, p->xp.event_name.data, p->xp.event_name.len, 0); return;
    case XE_ATTRIBUTE:
      /* a valueless attribute is stored as "" here, `true` in XMLPushParser */
      e = (XmlEvent){.type = XE_ATTRIBUTE,
                     .name = p->xp.event_name.data,
                     .nlen = p->xp.event_name.len,
                     .value = p->xp.event_has_value ? p->xp.event_value.data : "",
                     .vlen = p->xp.event_has_value ? p->xp.event_value.len : 0};
      break;
    case XE_TEXT:
      if(!p->xp.event_has_value)
        return;
      e = (XmlEvent){.type = XE_TEXT, .name = p->xp.event_value.data, .nlen = p->xp.event_value.len};
      break;
    default: return;
  }

  xevent_send(&p->sink, &e);
}

static void
xml_callback(XmlParser* p, XmlEventType ev, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  switch(ev) {
    case XE_ELEMENT_START: {
      JSValue name = JS_NewStringLen(ctx, p->xp.event_name.data, p->xp.event_name.len);
      ret = (JS_Call(ctx, p->element_start, p->this_obj, 1, &name));
      JS_FreeValue(ctx, name);
      break;
    }
    case XE_ATTRIBUTE: {
      JSValue argv[] = {
          JS_NewStringLen(ctx, p->xp.event_name.data, p->xp.event_name.len),
          p->xp.event_has_value ? JS_NewStringLen(ctx, p->xp.event_value.data, p->xp.event_value.len) : JS_UNDEFINED,
      };
      ret = (JS_Call(ctx, p->attribute, p->this_obj, countof(argv), argv));
      JS_FreeValue(ctx, argv[0]);
      JS_FreeValue(ctx, argv[1]);
      break;
    }
    case XE_ELEMENT_END: {
      JSValue name = JS_NewStringLen(ctx, p->xp.event_name.data, p->xp.event_name.len);
      ret = (JS_Call(ctx, p->element_end, p->this_obj, 1, &name));
      JS_FreeValue(ctx, name);
      break;
    }
    case XE_TEXT: {
      JSValue value = p->xp.event_has_value ? JS_NewStringLen(ctx, p->xp.event_value.data, p->xp.event_value.len) : JS_UNDEFINED;
      ret = (JS_Call(ctx, p->text, p->this_obj, 1, &value));
      JS_FreeValue(ctx, value);
      break;
    }

    default: break;
  }

  if(JS_IsException(ret))
    JS_GetException(ctx);
}

static JSValue
js_xml_parser_parse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XmlParser* p;
  int ev;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_parser_class_id)))
    return JS_EXCEPTION;

  if(!p->reader.read)
    return JS_ThrowTypeError(ctx, "XMLParser: no input to parse");

  ev = xml_parse(&p->xp);

  if(ev == XE_ERROR) {
    JSValue err = xml_error_new(ctx, &p->xp, p->loc);

    if(JS_IsException(err))
      return err;

    JS_FreeValue(ctx, p->last_error);
    p->last_error = err;
  }

  if(p->use_builder)
    xml_build(p, ev);
  else
    xml_callback(p, ev, ctx);

  xml_sync_location(p);

  return JS_NewInt32(ctx, ev);
}

enum {
  XML_PARSER_EVENT_NAME,
  XML_PARSER_EVENT_VALUE,
  XML_PARSER_HAS_VALUE,
  XML_PARSER_DEPTH,
  XML_PARSER_LOCATION,
  XML_PARSER_TOLERANT,
  XML_PARSER_ROOT,
  XML_PARSER_ERROR,
};

static JSValue
js_xml_parser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  XmlParser* p;
  JSValue ret = JS_UNDEFINED;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_parser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case XML_PARSER_EVENT_NAME: {
      if(p->xp.event_name.data && p->xp.event_name.len > 0)
        ret = JS_NewStringLen(ctx, p->xp.event_name.data, p->xp.event_name.len);
      break;
    }
    case XML_PARSER_EVENT_VALUE: {
      if(p->xp.event_has_value)
        ret = JS_NewStringLen(ctx, p->xp.event_value.data, p->xp.event_value.len);
      break;
    }
    case XML_PARSER_HAS_VALUE: {
      ret = JS_NewBool(ctx, p->xp.event_has_value);
      break;
    }
    case XML_PARSER_DEPTH: {
      ret = JS_NewUint32(ctx, xml_depth(p));
      break;
    }
    case XML_PARSER_LOCATION: {
      ret = js_location_wrap(ctx, p->loc);
      break;
    }
    case XML_PARSER_ERROR: {
      ret = JS_IsUndefined(p->last_error) ? JS_NULL : JS_DupValue(ctx, p->last_error);
      break;
    }
    case XML_PARSER_TOLERANT: {
      ret = JS_NewBool(ctx, p->xp.tolerant);
      break;
    }
    case XML_PARSER_ROOT: {
      ret = xbuild_root(&p->builder);
      break;
    }
  }

  return ret;
}

static JSValue
js_xml_parser_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  XmlParser* p;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_parser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case XML_PARSER_TOLERANT: {
      xml_set_tolerant(&p->xp, JS_ToBool(ctx, value));
      break;
    }
  }

  return JS_UNDEFINED;
}

static JSValue
js_xml_parser_callback(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  XmlParser* p;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_parser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case XE_ELEMENT_START: {
      JS_FreeValue(ctx, p->element_start);
      p->element_start = JS_DupValue(ctx, value);
      break;
    }
    case XE_ELEMENT_END: {
      JS_FreeValue(ctx, p->element_end);
      p->element_end = JS_DupValue(ctx, value);
      break;
    }
    case XE_ATTRIBUTE: {
      JS_FreeValue(ctx, p->attribute);
      p->attribute = JS_DupValue(ctx, value);
      break;
    }
    case XE_TEXT: {
      JS_FreeValue(ctx, p->text);
      p->text = JS_DupValue(ctx, value);
      break;
    }
  }

  /* Assigning a real callback post-construction is an explicit request to switch out of
   * builder mode - matches the "no real callback given at all" default computed in
   * js_xml_parser_constructor(), which only defaults to use_builder=TRUE because none of
   * these were set yet. Without this, use_builder stays whatever it was at construction
   * time forever, and callbacks assigned afterward (the only way to assign them without
   * passing an options object to the constructor) are silently never invoked. */
  if(JS_IsFunction(ctx, value))
    p->use_builder = FALSE;

  return JS_UNDEFINED;
}

static JSValue
js_xml_parser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  XmlParser* p;
  int i = 0;

  if(!(p = js_mallocz(ctx, sizeof(XmlParser))))
    return JS_EXCEPTION;

  if(reader_from_js(ctx, argv[i], &p->reader)) {
    i++;
  }

  JSValue options = i == argc ? argv[0] : argv[1];

  if(!(p->loc = location_new(ctx))) {
    reader_free(&p->reader);
    js_free(ctx, p);
    return JS_EXCEPTION;
  }

  JSValue filename = argc > 1 && JS_IsString(argv[1]) ? JS_DupValue(ctx, argv[1]) : JS_GetPropertyStr(ctx, options, "filename");

  char* file = js_tostring_free(ctx, filename);
  if(file) {
    location_set_filename(p->loc, file, ctx);
    js_free(ctx, file);
  }

  p->attribute = JS_GetPropertyStr(ctx, options, "attribute");
  p->element_start = JS_GetPropertyStr(ctx, options, "elementStart");
  p->element_end = JS_GetPropertyStr(ctx, options, "elementEnd");
  p->error = JS_GetPropertyStr(ctx, options, "error");
  p->text = JS_GetPropertyStr(ctx, options, "text");
  p->last_error = JS_UNDEFINED;

  p->this_obj = JS_DupValue(ctx, options);

  if(js_has_propertystr(ctx, options, "builder"))
    p->use_builder = js_tobool_free(ctx, JS_GetPropertyStr(ctx, options, "builder"));
  else
    /* no real callback given at all: build a tree (retrievable via .root) instead of
     * forwarding events that would just be dropped on the floor. */
    p->use_builder = !JS_IsFunction(ctx, p->attribute) && !JS_IsFunction(ctx, p->element_start) && !JS_IsFunction(ctx, p->element_end) &&
                     !JS_IsFunction(ctx, p->error) && !JS_IsFunction(ctx, p->text);

  /* p->xp.reader is a borrowed pointer (include/xml/xml.h doesn't own/copy the Reader
   * it's given), valid as long as p->reader is - i.e. for XmlParser's whole
   * lifetime, since both live in this one allocation and are freed together in
   * the finalizer below. */
  xml_init(&p->xp, &p->reader);
  xbuild_init(&p->builder, ctx, 0);
  p->sink = xbuild_sink(&p->builder);

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, xml_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_xml_parser_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    xml_free(&p->xp);
    reader_free(&p->reader);
    location_free(p->loc, JS_GetRuntime(ctx));
    xbuild_free(&p->builder, JS_GetRuntime(ctx));
    js_free(ctx, p);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, p);
  return obj;
}

static void
js_xml_parser_finalizer(JSRuntime* rt, JSValue val) {
  XmlParser* p;

  if((p = JS_GetOpaque(val, js_xml_parser_class_id))) {
    xml_free(&p->xp);
    reader_free(&p->reader);
    xbuild_free(&p->builder, rt);

    if(p->loc)
      location_free(p->loc, rt);

    JS_FreeValueRT(rt, p->this_obj);
    JS_FreeValueRT(rt, p->last_error);
    js_free_rt(rt, p);
  }
}

static const JSCFunctionListEntry js_xml_parser_proto_funcs[] = {
    JS_CFUNC_DEF("parse", 0, js_xml_parser_parse),
    JS_CGETSET_MAGIC_FLAGS_DEF("eventName", js_xml_parser_get, 0, XML_PARSER_EVENT_NAME, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("eventValue", js_xml_parser_get, 0, XML_PARSER_EVENT_VALUE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("hasValue", js_xml_parser_get, 0, XML_PARSER_HAS_VALUE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("depth", js_xml_parser_get, 0, XML_PARSER_DEPTH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("location", js_xml_parser_get, 0, XML_PARSER_LOCATION, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("tolerant", js_xml_parser_get, js_xml_parser_set, XML_PARSER_TOLERANT),
    JS_CGETSET_MAGIC_FLAGS_DEF("root", js_xml_parser_get, 0, XML_PARSER_ROOT, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("error", js_xml_parser_get, 0, XML_PARSER_ERROR, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("onelementstart", 0, js_xml_parser_callback, XE_ELEMENT_START, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("onelementend", 0, js_xml_parser_callback, XE_ELEMENT_END, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("onattribute", 0, js_xml_parser_callback, XE_ATTRIBUTE, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("ontext", 0, js_xml_parser_callback, XE_TEXT, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_xml_parser_class = {
    .class_name = "XMLParser",
    .finalizer = js_xml_parser_finalizer,
};

/* XMLNodeParser: pull (.parse()) parser that yields one "Flat List" node
   per call. Returns JS strings for text, and JS objects for start/end
   tags (with end tags having a tagName prefixed with '/').
   ---------------------------------------------------------------------- */

static JSClassID js_xml_nodeparser_class_id;
static JSValue xml_nodeparser_proto, xml_nodeparser_ctor;

typedef struct {
  Reader reader;
  XMLParser xp;
  Location* loc;

  /* Flat state machine */
  BOOL has_pending_node;
  JSValue pending_node;
  JSValue pending_attrs;
  int buffered_ev;
  BOOL has_buffered_ev;

  uint32_t depth;
  JSValue last_error; /* SyntaxError after parse() returned PARSE_ERROR, else undefined */
} XMLNodeParser;

static int
xml_nodeparser_record_error(JSContext* ctx, XMLNodeParser* p) {
  JSValue err = xml_error_new(ctx, &p->xp, p->loc);

  if(JS_IsException(err))
    return -1;

  JS_FreeValue(ctx, p->last_error);
  p->last_error = err;
  return 0;
}

static void
xml_nodeparser_sync_location(XMLNodeParser* p) {
  p->loc->line = p->xp.loc.line;
  p->loc->column = p->xp.loc.column;
  p->loc->char_offset = p->xp.loc.char_offset;
  p->loc->byte_offset = p->xp.loc.byte_offset;
}

static JSValue
js_xml_nodeparser_parse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XMLNodeParser* p;
  int ev;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_nodeparser_class_id)))
    return JS_EXCEPTION;

  /* If we don't have a pending start tag, fetch the next event */
  if(!p->has_pending_node) {
    if(p->has_buffered_ev) {
      ev = p->buffered_ev;
      p->has_buffered_ev = FALSE;
      /* event_name/event_value still point into p->xp.ev_name/ev_value,
       * which haven't been touched since the event was buffered. */
    } else {
      ev = xml_parse(&p->xp);
      xml_nodeparser_sync_location(p);

      if(ev == XE_ERROR && xml_nodeparser_record_error(ctx, p))
        return JS_EXCEPTION;
    }

    if(ev <= XML_PARSE_OK)
      return JS_NewInt32(ctx, ev);

    if(ev == XE_ELEMENT_START) {
      JSValue node = JS_NewObjectProto(ctx, JS_NULL);
      JS_SetPropertyStr(ctx, node, "tagName", JS_NewStringLen(ctx, p->xp.event_name.data, p->xp.event_name.len));
      JSValue attrs = JS_NewObjectProto(ctx, JS_NULL);
      JS_SetPropertyStr(ctx, node, "attributes", attrs);

      p->pending_node = node;
      p->pending_attrs = attrs;
      p->has_pending_node = TRUE;
      p->depth++;
    } else if(ev == XE_ELEMENT_END) {
      if(p->depth > 0)
        p->depth--;

      JSValue node = JS_NewObjectProto(ctx, JS_NULL);
      char* closeStr = js_malloc(ctx, p->xp.event_name.len + 2);
      closeStr[0] = '/';
      memcpy(closeStr + 1, p->xp.event_name.data, p->xp.event_name.len);
      closeStr[p->xp.event_name.len + 1] = '\0';
      JS_SetPropertyStr(ctx, node, "tagName", JS_NewString(ctx, closeStr));
      js_free(ctx, closeStr);
      return node;

    } else if(ev == XE_TEXT) {
      if(p->xp.event_has_value)
        return JS_NewStringLen(ctx, p->xp.event_value.data, p->xp.event_value.len);

      return JS_NewString(ctx, "");

    } else {
      return JS_NewInt32(ctx, ev);
    }
  }

  /* Consume Attributes for the pending start tag */
  while(1) {
    int next_ev;

    if(p->has_buffered_ev) {
      next_ev = p->buffered_ev;
      p->has_buffered_ev = FALSE;
    } else {
      next_ev = xml_parse(&p->xp);
      xml_nodeparser_sync_location(p);

      if(next_ev == XE_ERROR && xml_nodeparser_record_error(ctx, p))
        return JS_EXCEPTION;
    }

    /* Stream paused waiting for data; preserve state and return */
    if(next_ev == XML_PARSE_AGAIN)
      return JS_NewInt32(ctx, XML_PARSE_AGAIN);

    if(next_ev == XE_ERROR || next_ev == XML_PARSE_OK) {
      JS_FreeValue(ctx, p->pending_node);
      p->has_pending_node = FALSE;
      return JS_NewInt32(ctx, next_ev);
    }

    if(next_ev == XE_ATTRIBUTE) {
      JSValue val = p->xp.event_has_value ? JS_NewStringLen(ctx, p->xp.event_value.data, p->xp.event_value.len) : JS_TRUE;
      JSAtom prop = JS_NewAtomLen(ctx, p->xp.event_name.data, p->xp.event_name.len);
      JS_SetProperty(ctx, p->pending_attrs, prop, val);
      JS_FreeAtom(ctx, prop);
    } else {
      /* Attributes finished. Buffer the non-attribute event for the NEXT .parse() call.
       * event_name/event_value remain valid since they point into p->xp.ev_name/ev_value. */
      p->buffered_ev = next_ev;
      p->has_buffered_ev = TRUE;

      JSValue ret = p->pending_node;
      p->has_pending_node = FALSE;
      return ret;
    }
  }
}

enum {
  XML_NODEPARSER_DEPTH,
  XML_NODEPARSER_LOCATION,
  XML_NODEPARSER_ERROR,
};

static JSValue
js_xml_nodeparser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  XMLNodeParser* p;
  JSValue ret = JS_UNDEFINED;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_nodeparser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case XML_NODEPARSER_DEPTH: ret = JS_NewUint32(ctx, p->depth); break;
    case XML_NODEPARSER_LOCATION: ret = js_location_wrap(ctx, p->loc); break;
    case XML_NODEPARSER_ERROR: ret = JS_IsUndefined(p->last_error) ? JS_NULL : JS_DupValue(ctx, p->last_error); break;
  }

  return ret;
}

static JSValue
js_xml_nodeparser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  XMLNodeParser* p;
  int i = 0;

  if(!(p = js_mallocz(ctx, sizeof(XMLNodeParser))))
    return JS_EXCEPTION;

  if(reader_from_js(ctx, argv[i], &p->reader)) {
    i++;
  }

  if(!(p->loc = location_new(ctx))) {
    reader_free(&p->reader);
    js_free(ctx, p);
    return JS_EXCEPTION;
  }

  JSValue options = i == argc ? JS_UNDEFINED : argv[i];

  if(JS_IsObject(options)) {
    JSValue filename = JS_GetPropertyStr(ctx, options, "filename");
    char* file;

    if((file = js_tostring_free(ctx, filename))) {
      location_set_filename(p->loc, file, ctx);
      js_free(ctx, file);
    }
  }

  p->has_pending_node = FALSE;
  p->has_buffered_ev = FALSE;
  p->pending_node = JS_UNDEFINED;
  p->pending_attrs = JS_UNDEFINED;
  p->last_error = JS_UNDEFINED;
  p->depth = 0;

  xml_init(&p->xp, &p->reader);

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, xml_nodeparser_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_xml_nodeparser_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    xml_free(&p->xp);
    reader_free(&p->reader);
    location_free(p->loc, JS_GetRuntime(ctx));
    js_free(ctx, p);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, p);
  return obj;
}

static void
js_xml_nodeparser_finalizer(JSRuntime* rt, JSValue val) {
  XMLNodeParser* p;

  if((p = JS_GetOpaque(val, js_xml_nodeparser_class_id))) {
    xml_free(&p->xp);
    reader_free(&p->reader);

    if(p->loc)
      location_free(p->loc, rt);

    if(p->has_pending_node)
      JS_FreeValueRT(rt, p->pending_node);

    JS_FreeValueRT(rt, p->last_error);
    js_free_rt(rt, p);
  }
}

static const JSCFunctionListEntry js_xml_nodeparser_proto_funcs[] = {
    JS_CFUNC_DEF("parse", 0, js_xml_nodeparser_parse),
    JS_CGETSET_MAGIC_FLAGS_DEF("depth", js_xml_nodeparser_get, 0, XML_NODEPARSER_DEPTH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("location", js_xml_nodeparser_get, 0, XML_NODEPARSER_LOCATION, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("error", js_xml_nodeparser_get, 0, XML_NODEPARSER_ERROR, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLNodeParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_xml_nodeparser_class = {
    .class_name = "XMLNodeParser",
    .finalizer = js_xml_nodeparser_finalizer,
};

typedef struct {
  XmlWriter xw;
  DynBuf buf;     /* backs `xw.writer` when no output was given to the constructor */
  int32_t indent; /* as given; negative: compact */
  int event;      /* XML_* of the last call, 0 before the first */
} XMLWriter;

static JSClassID js_xmlwriter_class_id;
static JSValue xmlwriter_proto, xmlwriter_ctor;

/* xmlwriter_set_indent: remembers `indent` as given (the getter returns it) and sets the layout. */
static void
xmlwriter_set_indent(XMLWriter* wr, int32_t indent) {
  wr->indent = indent;
  xwrite_set_indent(&wr->xw, indent);
}

/* xmlwriter_throw: maps a failed xwrite event (-r) to a JS exception.
 *
 *   code                         throws
 *   XWRITE_E_MISMATCH            TypeError, code mismatched-end-tag
 *   XWRITE_E_UNMATCHED           TypeError, code unmatched-end-tag
 *   XWRITE_E_NOT_IN_TAG          TypeError, code attribute-outside-tag
 *   XWRITE_E_WRITE               Error, code write-failed
 *   XWRITE_E_NOMEM               out of memory
 *
 * returns JS_EXCEPTION. */
static JSValue
xmlwriter_throw(JSContext* ctx, ssize_t r) {
  const char* text = xwrite_error_message(-r);
  char msg[96];
  JSValue err;

  if(-r == XWRITE_E_NOMEM)
    return JS_ThrowOutOfMemory(ctx);

  snprintf(msg, sizeof(msg), "XMLWriter: %s", text ? text : "write failed");

  if(-r == XWRITE_E_WRITE) {
    err = JS_NewError(ctx);
    JS_DefinePropertyValueStr(ctx, err, "message", JS_NewString(ctx, msg), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
  } else {
    JS_ThrowTypeError(ctx, "%s", msg);
    err = JS_GetException(ctx);
  }

  JS_DefinePropertyValueStr(ctx, err, "code", JS_NewString(ctx, xwrite_error_code(-r)), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
  return JS_Throw(ctx, err);
}

/* XMLWriter is an XmlEventSink object: elementStart, attribute, elementEnd, text, comment,
 * declaration, instruction, cdata and error come from js_xmlsink_define() (xml/xsink.h) and
 * write through xwrite.h; failures are thrown by xmlwriter_throw(). */
static XmlEventSink
js_xmlwriter_sink(void* opaque) {
  return xwrite_sink(&((XMLWriter*)opaque)->xw);
}

static void
js_xmlwriter_note(void* opaque, int type) {
  ((XMLWriter*)opaque)->event = type;
}

enum {
  WRITER_STATE,
  WRITER_WRITTEN,
  WRITER_INDENT,
};

static JSValue
js_xmlwriter_get(JSContext* ctx, JSValueConst this_val, int magic) {
  XMLWriter* wr;
  JSValue ret = JS_UNDEFINED;

  if(!(wr = JS_GetOpaque2(ctx, this_val, js_xmlwriter_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITER_STATE: {
      ret = JS_NewInt32(ctx, wr->event);
      break;
    }
    case WRITER_WRITTEN: {
      ret = JS_NewInt64(ctx, wr->xw.written);
      break;
    }
    case WRITER_INDENT: {
      ret = JS_NewInt32(ctx, wr->indent);
      break;
    }
  }

  return ret;
}

static JSValue
js_xmlwriter_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  XMLWriter* wr;
  JSValue ret = JS_UNDEFINED;

  if(!(wr = JS_GetOpaque2(ctx, this_val, js_xmlwriter_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITER_INDENT: {
      xmlwriter_set_indent(wr, js_toint32(ctx, value));
      break;
    }
  }

  return ret;
}

static JSValue
js_xmlwriter_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  XMLWriter* wr;
  int i = 0;

  if(!(wr = js_mallocz(ctx, sizeof(XMLWriter))))
    return JS_EXCEPTION;

  if(argc > 0 && writer_from_js(ctx, argv[i], &wr->xw.writer)) {
    i++;
  } else {
    /* No output given (or argv[i] isn't a recognized writer target): fall back to an
     * internal buffer, as documented - leaving `wr->writer` zero-initialized instead
     * would dispatch every write through a null function pointer and crash on the
     * first elementStart()/attribute()/text() call. */
    dbuf_init_ctx(ctx, &wr->buf);
    wr->xw.writer = writer_from_dynbuf(&wr->buf);
  }

  JSValue options = i < argc ? argv[i] : argc > 0 ? argv[0] : JS_UNDEFINED;

  if(JS_IsNumber(options))
    xmlwriter_set_indent(wr, js_toint32(ctx, options));
  else if(JS_IsObject(options) && js_has_propertystr(ctx, options, "indent"))
    xmlwriter_set_indent(wr, js_toint32_free(ctx, JS_GetPropertyStr(ctx, options, "indent")));
  else
    xmlwriter_set_indent(wr, 2);

  xwrite_init(&wr->xw, NULL, NULL);

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_xmlwriter_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, wr);
  return obj;

fail:
  xwrite_free(&wr->xw);
  js_free(ctx, wr);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_xmlwriter_finalizer(JSRuntime* rt, JSValue val) {
  XMLWriter* wr;

  if((wr = JS_GetOpaque(val, js_xmlwriter_class_id))) {
    xwrite_free(&wr->xw); /* also frees the writer */
    js_free_rt(rt, wr);
  }
}

static JSClassDef js_xmlwriter_class = {
    .class_name = "XMLWriter",
    .finalizer = js_xmlwriter_finalizer,
};

static const JSCFunctionListEntry js_xmlwriter_funcs[] = {
    JS_CGETSET_MAGIC_DEF("state", js_xmlwriter_get, 0, WRITER_STATE),
    JS_CGETSET_MAGIC_DEF("written", js_xmlwriter_get, 0, WRITER_WRITTEN),
    JS_CGETSET_MAGIC_DEF("indent", js_xmlwriter_get, js_xmlwriter_set, WRITER_INDENT),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLWriter", JS_PROP_CONFIGURABLE),
};

static int
js_xml_init(JSContext* ctx, JSModuleDef* m) {
  if(js_location_class_id == 0)
    js_location_init(ctx, 0);

  JS_SetModuleExportList(ctx, m, js_xml_funcs, countof(js_xml_funcs));

  JSValue defaultObj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, defaultObj, "read", JS_NewCFunction(ctx, js_xml_read, "read", 1));
  JS_SetPropertyStr(ctx, defaultObj, "write", JS_NewCFunction(ctx, js_xml_write, "write", 2));
  JS_SetModuleExport(ctx, m, "default", defaultObj);

  JS_NewClassID(&js_xmlserializer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xmlserializer_class_id, &js_xmlserializer_class);

  xmlserializer_ctor = JS_NewCFunction2(ctx, js_xmlserializer_constructor, "XMLTreeSerializer", 1, JS_CFUNC_constructor, 0);
  xmlserializer_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, xmlserializer_proto, js_xmlserializer_funcs, countof(js_xmlserializer_funcs));
  JS_SetClassProto(ctx, js_xmlserializer_class_id, xmlserializer_proto);
  JS_SetConstructor(ctx, xmlserializer_ctor, xmlserializer_proto);

  JS_SetModuleExport(ctx, m, "XMLTreeSerializer", xmlserializer_ctor);

  JS_NewClassID(&js_xml_pushparser_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xml_pushparser_class_id, &js_xml_pushparser_class);

  xml_pushparser_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, xml_pushparser_proto, js_xml_pushparser_proto_funcs, countof(js_xml_pushparser_proto_funcs));

  xml_pushparser_ctor = JS_NewCFunction2(ctx, js_xml_pushparser_constructor, "XMLPushParser", 1, JS_CFUNC_constructor, 0);
  JS_SetClassProto(ctx, js_xml_pushparser_class_id, xml_pushparser_proto);
  JS_SetConstructor(ctx, xml_pushparser_ctor, xml_pushparser_proto);

  JS_SetModuleExport(ctx, m, "XMLPushParser", xml_pushparser_ctor);

  JS_NewClassID(&js_xml_parser_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xml_parser_class_id, &js_xml_parser_class);

  xml_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, xml_proto, js_xml_parser_proto_funcs, countof(js_xml_parser_proto_funcs));

  xml_ctor = JS_NewCFunction2(ctx, js_xml_parser_constructor, "XMLParser", 1, JS_CFUNC_constructor, 0);
  JS_SetClassProto(ctx, js_xml_parser_class_id, xml_proto);
  JS_SetConstructor(ctx, xml_ctor, xml_proto);

  /* .parse()'s return value: named constants for the raw XmlEventType token ids
   * (include/xml/xml.h), so callers don't have to hardcode -2..4. */
  JS_DefinePropertyValueStr(ctx, xml_ctor, "PARSE_AGAIN", JS_NewInt32(ctx, XML_PARSE_AGAIN), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_ctor, "PARSE_ERROR", JS_NewInt32(ctx, XE_ERROR), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_ctor, "PARSE_OK", JS_NewInt32(ctx, XML_PARSE_OK), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_ctor, "ELEMENT_START", JS_NewInt32(ctx, XE_ELEMENT_START), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_ctor, "ATTRIBUTE", JS_NewInt32(ctx, XE_ATTRIBUTE), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_ctor, "ELEMENT_END", JS_NewInt32(ctx, XE_ELEMENT_END), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_ctor, "TEXT", JS_NewInt32(ctx, XE_TEXT), JS_PROP_ENUMERABLE);

  JS_SetModuleExport(ctx, m, "XMLParser", xml_ctor);

  JS_NewClassID(&js_xml_nodeparser_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xml_nodeparser_class_id, &js_xml_nodeparser_class);

  xml_nodeparser_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, xml_nodeparser_proto, js_xml_nodeparser_proto_funcs, countof(js_xml_nodeparser_proto_funcs));

  xml_nodeparser_ctor = JS_NewCFunction2(ctx, js_xml_nodeparser_constructor, "XMLNodeParser", 1, JS_CFUNC_constructor, 0);
  JS_SetClassProto(ctx, js_xml_nodeparser_class_id, xml_nodeparser_proto);
  JS_SetConstructor(ctx, xml_nodeparser_ctor, xml_nodeparser_proto);

  JS_SetModuleExport(ctx, m, "XMLNodeParser", xml_nodeparser_ctor);

  JS_NewClassID(&js_xmlwriter_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xmlwriter_class_id, &js_xmlwriter_class);

  xmlwriter_ctor = JS_NewCFunction2(ctx, js_xmlwriter_constructor, "XMLWriter", 1, JS_CFUNC_constructor, 0);
  xmlwriter_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, xmlwriter_proto, js_xmlwriter_funcs, countof(js_xmlwriter_funcs));
  js_xmlsink_register(js_xmlwriter_class_id, js_xmlwriter_sink, xmlwriter_throw, js_xmlwriter_note);
  js_xmlsink_define(ctx, xmlwriter_proto);
  JS_SetClassProto(ctx, js_xmlwriter_class_id, xmlwriter_proto);
  JS_SetConstructor(ctx, xmlwriter_ctor, xmlwriter_proto);

  JS_SetModuleExport(ctx, m, "XMLWriter", xmlwriter_ctor);

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_xml
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_xml_init))) {
    JS_AddModuleExportList(ctx, m, js_xml_funcs, countof(js_xml_funcs));
    JS_AddModuleExport(ctx, m, "default");
    JS_AddModuleExport(ctx, m, "XMLTreeSerializer");
    JS_AddModuleExport(ctx, m, "XMLPushParser");
    JS_AddModuleExport(ctx, m, "XMLParser");
    JS_AddModuleExport(ctx, m, "XMLNodeParser");
    JS_AddModuleExport(ctx, m, "XMLWriter");
  }

  return m;
}
