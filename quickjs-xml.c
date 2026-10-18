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
#include "xml/xread.h"
#include "xml/xbuilder.h"
#include "xml/xml_entities.h"
#include "xml/xml.h"
#include "xml/xwalk.h"
#include "xml/xserial.h"
#include "xml/xconnect.h"
#include "xml/xopts.h"
#include "xml/xevent-js.h"

#include <stdint.h>

char* js_inspect_tostring(JSContext* ctx, JSValueConst value);

/**
 * \defgroup quickjs-xml quickjs-xml: XML parser & printer
 * @{
 */
#ifdef DEBUG_OUTPUT
#define xml_debug(args...) printf(args)
#else
#define xml_debug(args...)
#endif

static JSValue
js_xml_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  InputBuffer input;
  const char* input_name = 0;
  XmlOptions opts = {.self_closing_tags = xml_html_self_closing_tags};

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
      if(xml_options_from_js(ctx, argv[2], &opts)) {
        inputbuffer_free(&input, ctx);
        return JS_EXCEPTION;
      }
    } else {
      opts.flat = JS_ToBool(ctx, argv[2]);

      if(argc >= 4)
        opts.tolerant = JS_ToBool(ctx, argv[3]);
    }
  }

  ret = xml_read_value(ctx, input.data, input.size, input_name ? input_name : "<xml>", opts);

  if(input_name)
    JS_FreeCString(ctx, input_name);

  xml_options_free(ctx, &opts);
  inputbuffer_free(&input, ctx);
  return ret;
}

static JSValue
js_xml_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int32_t max_depth = INT32_MAX;

  if(argc >= 2)
    JS_ToInt32(ctx, &max_depth, argv[1]);

  return xml_write_value(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, max_depth);
}

static JSValue js_xml_tee(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]);

static const JSCFunctionListEntry js_xml_funcs[] = {
    JS_CFUNC_DEF("tee", 0, js_xml_tee),
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
    js_xmlwrite_throw(ctx, xs->ser.error);

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
  JSValue proto, obj, root;
  JSValueConst tree = argc > 0 ? argv[0] : JS_UNDEFINED;
  XMLTreeSerializer* xs;
  BOOL flat;
  int flags;

  if(!(xs = js_mallocz(ctx, sizeof(XMLTreeSerializer))))
    return JS_EXCEPTION;

  root = xml_tree_list(ctx, tree, &flat);
  flags = flat ? XWALK_FLAT | XWALK_BALANCED : 0;

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
  struct xr_state xrs;
  XmlJsCallbacks cb;
  JSValue last_error;     /* SyntaxError of the latest error, or JS_UNDEFINED */
  XmlBuilder builder;
  XmlEventSink sink; /* xbuild_sink(&builder) */
  BOOL use_builder; /* build {tagName, attributes, children} tree
                       instead of forwarding events */
  BOOL connected;   /* connect(): every event goes to ext */
  XmlEventSink ext;
  XmlJsSink* ext_adapter;
  JSValue ext_obj;  /* the object behind ext, kept alive */
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

  if(JS_IsFunction(ctx, pp->cb.on_parse_error)) {
    JSValue ret = JS_Call(ctx, pp->cb.on_parse_error, pp->cb.this_obj, 1, &err);

    JS_FreeValue(ctx, ret); /* an exception from the callback is dropped */
    JS_FreeValue(ctx, JS_GetException(ctx));
  }

  JS_FreeValue(ctx, err);
  return JS_IsFunction(ctx, pp->cb.on_parse_error);
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

/* xml_pushparser_event: the handler of the callback mode, an error goes to onParseError and
 * to the error callback. */
static ssize_t
xml_pushparser_event(void* user, const XmlEvent* ev) {
  XMLPushParser* pp = user;

  if(ev->type == XE_ERROR)
    xml_pushparser_report(pp, &ev->at, ev->span, ev->code, "XMLPushParser: malformed input");

  return js_xmlcallbacks_event(&pp->cb, ev);
}

/* xml_pushparser_event_ext: the handler after connect(), every event goes to the sink. */
static ssize_t
xml_pushparser_event_ext(void* user, const XmlEvent* ev) {
  XMLPushParser* pp = user;

  if(ev->type == XE_ERROR)
    xml_pushparser_report(pp, &ev->at, ev->span, ev->code, "XMLPushParser: malformed input");

  return xevent_send(&pp->ext, ev);
}

/* xml_pushparser_handler: the handler for the mode the parser is in. */
static XmlEventHandler*
xml_pushparser_handler(const XMLPushParser* pp) {
  return pp->connected ? xml_pushparser_event_ext : pp->use_builder ? xml_pushparser_event_build : xml_pushparser_event;
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

  ssize_t r = xr_feed(&pp->xrs, (const char*)inputbuffer_data(&input), inputbuffer_length(&input), xml_pushparser_handler(pp), pp);

  inputbuffer_free(&input, ctx);

  if(r < 0)
    return js_xmlsink_failed(ctx, pp->ext_adapter != NULL, r);

  /* xr_feed() sets xrs.error and goes silently inert (every further xr_feed()/
   * xr_end() call becomes a no-op) rather than reporting anything itself - most
   * notably on any non-whitespace text between tags, which its grammar doesn't
   * accept at all (see BUGS: xread-no-text-content). With custom callbacks, the
   * caller's own error() callback already ran (xread_callback() dispatches
   * XE_ERROR to it) and owns handling this, so only throw here in the
   * builder fallback, which otherwise has no way to report it at all - .root
   * would just silently stop growing. */
  if(pp->xrs.error && pp->use_builder && !JS_IsFunction(ctx, pp->cb.on_parse_error))
    return js_throw_syntax_error_at(ctx, "<xml>", &pp->xrs.err_pos, pp->xrs.err_len, xr_error_name(pp->xrs.err_code), "XMLPushParser: malformed input");

  return JS_UNDEFINED;
}

static JSValue
js_xml_pushparser_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XMLPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  ssize_t r = xr_end(&pp->xrs, xml_pushparser_handler(pp), pp);

  if(r < 0)
    return js_xmlsink_failed(ctx, pp->ext_adapter != NULL, r);

  /* xr_end()'s own !at_root check (which sets xrs.error) only catches ending
   * mid-tag, e.g. "<a><b" with no closing '>' - src/xml/xread.c's l_tag_end sets
   * at_root=1 after *every* '>', nested or not, so it does NOT mean every opened
   * element was also closed. When building a tree, check the frame stack
   * directly instead, to also catch e.g. "<a><b/>" (well-formed lexically, but
   * <a> itself was never closed). Only in the builder fallback - see write()'s
   * comment above for why custom-callback mode doesn't also throw here. */
  if(pp->xrs.error && pp->use_builder && !JS_IsFunction(ctx, pp->cb.on_parse_error))
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
   * elementStart() etc. method can be defined directly on the options object. */
  js_xmlcallbacks_init(ctx, &pp->cb, options);
  pp->last_error = JS_UNDEFINED;
  pp->ext_obj = JS_UNDEFINED;

  /* no real callback given at all: build a tree (retrievable via .root) instead of
   * forwarding events that would just be dropped on the floor. */
  pp->use_builder = !js_xmlcallbacks_any(&pp->cb);

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
    js_xmlcallbacks_free_rt(rt, &pp->cb);
    JS_FreeValueRT(rt, pp->last_error);
    JS_FreeValueRT(rt, pp->ext_obj);
    js_xmlsink_from_js_free_rt(rt, pp->ext_adapter);
    js_free_rt(rt, pp);
  }
}

/* XMLPushParser.connect: sends every event of write() and close() to a sink.
 *
 * ```js
 * const p = new XMLPushParser();
 * p.connect(new XmlBuilder());      // or an XMLWriter, tee(a, b), { elementStart(n) {}, ... }
 * p.write('<a><b/></a>');
 * ```
 *
 *   object  sink  an XmlEventSink object (handshake) or any object with the sink methods
 *
 *   returns  the parser
 *   throws   TypeError for a non-object, what the sink methods throw, Error with a code when a
 *            native sink refuses an event
 *
 * the callbacks and the builder of the constructor are not called while connected.
 * wired in: JS_CFUNC_DEF("connect") and the `sink` getter. */
static JSValue
js_xml_pushparser_connect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XMLPushParser* pp;
  XmlEventSink sink;
  XmlJsSink* adapter;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  if(js_xmlsink_resolve(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &sink, &adapter))
    return JS_EXCEPTION;

  JS_FreeValue(ctx, pp->ext_obj);
  js_xmlsink_from_js_free(ctx, pp->ext_adapter);
  pp->ext = sink;
  pp->ext_adapter = adapter;
  pp->ext_obj = JS_DupValue(ctx, argv[0]);
  pp->connected = TRUE;
  return JS_DupValue(ctx, this_val);
}

static JSValue
js_xml_pushparser_get_sink(JSContext* ctx, JSValueConst this_val) {
  XMLPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_xml_pushparser_class_id)))
    return JS_EXCEPTION;

  return JS_DupValue(ctx, pp->ext_obj);
}

static const JSCFunctionListEntry js_xml_pushparser_proto_funcs[] = {
    JS_CFUNC_DEF("connect", 1, js_xml_pushparser_connect),
    JS_CGETSET_DEF("sink", js_xml_pushparser_get_sink, 0),
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
   XMLParser: pull parser over include/xml/xml.h, next() and the iterator
   give typed events, pump() runs it into a sink in C.
   ---------------------------------------------------------------------- */

typedef struct {
  Reader reader;
  XMLParser xp;
  Location* loc;
  char** self_closing_owned; /* the selfClosingTags option */
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
 * after every next() or pump() call, the two can't just be the same object since xp.loc
 * isn't heap-allocated the way location_free() requires. */
static void
xml_sync_location(XmlParser* p) {
  p->loc->line = p->xp.loc.line;
  p->loc->column = p->xp.loc.column;
  p->loc->char_offset = p->xp.loc.char_offset;
  p->loc->byte_offset = p->xp.loc.byte_offset;
}

/* XMLParser.pump: runs the parser into a sink, in C.
 *
 * ```js
 * const b = new XmlBuilder();
 * new XMLParser('<a><b/></a>').pump(b);   // 0
 * b.root;                                 // [{tagName: 'a', attributes: {}, children: [...]}]
 * ```
 *
 *   object  sink  an XmlEventSink object (handshake) or any object with the sink methods
 *
 *   returns  0 when the input ended, 1 when the reader has no data now
 *   throws   the SyntaxError of a mismatched closing tag, what the sink throws, Error with a
 *            code when a native sink refuses an event
 *
 * markup arrives typed (comment, declaration, instruction, cdata), see xml_next().
 * wired in: JS_CFUNC_DEF("pump"). */
static JSValue
js_xml_parser_pump(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XmlParser* p;
  XmlEventSink sink;
  XmlJsSink* adapter;
  XmlEvent ev;
  XmlPull st;
  JSValue ret = JS_EXCEPTION;
  ssize_t r;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_parser_class_id)))
    return JS_EXCEPTION;

  if(!p->reader.read)
    return JS_ThrowTypeError(ctx, "XMLParser: no input to parse");

  if(js_xmlsink_resolve(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &sink, &adapter))
    return JS_EXCEPTION;

  while((st = xml_next(&p->xp, &ev)) == XML_PULL_EVENT) {
    if(ev.type == XE_ERROR) {
      JSValue err = js_xmlevent_error(ctx, &p->xp, p->loc);

      if(!JS_IsException(err))
        JS_Throw(ctx, err);

      goto done;
    }

    if((r = xevent_send(&sink, &ev)) < 0) {
      js_xmlsink_failed(ctx, adapter != NULL, r);
      goto done;
    }
  }

  ret = JS_NewInt32(ctx, st);
done:
  xml_sync_location(p);
  js_xmlsink_from_js_free(ctx, adapter);
  return ret;
}

/* XMLParser.next: the next event as { type, name?, value?, loc }, also the iterator protocol.
 *
 * ```js
 * for(const { type, name, value, loc } of new XMLParser('<a x="1">t</a>')) {}
 * // ELEMENT_START a, ATTRIBUTE x 1, TEXT t, ELEMENT_END a
 * ```
 *
 * markup arrives typed (COMMENT, DECLARATION, INSTRUCTION, CDATA with the text in value). The iteration ends when the document is
 * complete or the reader has no data now. throws the SyntaxError of a mismatched closing tag.
 * wired in: JS_ITERATOR_NEXT_DEF("next"). */
static JSValue
js_xml_parser_iterator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  XmlParser* p;
  XmlEvent ev;
  XmlPull st;
  JSValue ret;

  *pdone = TRUE;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_parser_class_id)))
    return JS_EXCEPTION;

  if(!p->reader.read)
    return JS_ThrowTypeError(ctx, "XMLParser: no input to parse");

  st = xml_next(&p->xp, &ev);
  xml_sync_location(p);

  if(st != XML_PULL_EVENT)
    return JS_UNDEFINED;

  if(ev.type == XE_ERROR) {
    JSValue err = js_xmlevent_error(ctx, &p->xp, p->loc);

    return JS_IsException(err) ? err : JS_Throw(ctx, err);
  }

  *pdone = FALSE;
  ret = js_xmlevent_new(ctx, &ev);
  return ret;
}

static JSValue
js_xml_parser_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

enum {
  XML_PARSER_DEPTH,
  XML_PARSER_LOCATION,
  XML_PARSER_TOLERANT,
};

static JSValue
js_xml_parser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  XmlParser* p;
  JSValue ret = JS_UNDEFINED;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_parser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case XML_PARSER_DEPTH: ret = JS_NewUint32(ctx, xml_depth(p)); break;
    case XML_PARSER_LOCATION: ret = js_location_wrap(ctx, p->loc); break;
    case XML_PARSER_TOLERANT: ret = JS_NewBool(ctx, p->xp.tolerant); break;
  }

  return ret;
}

static JSValue
js_xml_parser_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  XmlParser* p;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_xml_parser_class_id)))
    return JS_EXCEPTION;

  if(magic == XML_PARSER_TOLERANT)
    xml_set_tolerant(&p->xp, JS_ToBool(ctx, value));

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

  /* p->xp.reader is a borrowed pointer (include/xml/xml.h doesn't own/copy the Reader
   * it's given), valid as long as p->reader is - i.e. for XmlParser's whole
   * lifetime, since both live in this one allocation and are freed together in
   * the finalizer below. */
  xml_init(&p->xp, &p->reader);

  if(JS_IsObject(options)) {
    XmlOptions xo = {0};

    if(xml_options_from_js(ctx, options, &xo)) {
      xml_free(&p->xp);
      reader_free(&p->reader);
      location_free(p->loc, JS_GetRuntime(ctx));
      js_free(ctx, p);
      return JS_EXCEPTION;
    }

    xml_set_tolerant(&p->xp, xo.tolerant);
    xml_set_flat(&p->xp, xo.flat);
    p->xp.self_closing_tags = xo.self_closing_tags;
    p->self_closing_owned = xo.owned;
    js_free(ctx, xo.filename);
  }

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, xml_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_xml_parser_class_id);
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
js_xml_parser_finalizer(JSRuntime* rt, JSValue val) {
  XmlParser* p;

  if((p = JS_GetOpaque(val, js_xml_parser_class_id))) {
    xml_free(&p->xp);
    reader_free(&p->reader);
    js_strv_free_rt(rt, p->self_closing_owned);

    if(p->loc)
      location_free(p->loc, rt);

    js_free_rt(rt, p);
  }
}

static const JSCFunctionListEntry js_xml_parser_proto_funcs[] = {
    JS_CFUNC_DEF("pump", 1, js_xml_parser_pump),
    JS_ITERATOR_NEXT_DEF("next", 0, js_xml_parser_iterator_next, 0),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_xml_parser_iterator),
    JS_CGETSET_MAGIC_FLAGS_DEF("depth", js_xml_parser_get, 0, XML_PARSER_DEPTH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("location", js_xml_parser_get, 0, XML_PARSER_LOCATION, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("tolerant", js_xml_parser_get, js_xml_parser_set, XML_PARSER_TOLERANT),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_xml_parser_class = {
    .class_name = "XMLParser",
    .finalizer = js_xml_parser_finalizer,
};

/* XmlBuilder: an XmlEventSink that builds the element tree or flat list of xml.read().
 *
 * ```js
 * const b = new XmlBuilder();            // { flat: true } for the flat list
 * parser.pump(b);
 * b.finish();                            // ends what the input left open
 * b.root;                                // [{tagName: 'a', attributes: {}, children: [...]}]
 * ```
 *
 *   root    the top-level array (a new reference to the same array each time)
 *   path    the tag names from the root down to the open element
 *   finish() ends every element still open, as the end of the input
 *
 * options { flat, plain }: plain objects get Object.prototype, else none. the sink methods are those
 * of XMLWriter, see xsink.h.
 * wired in: JS_CGETSET_DEF("root") and friends, js_xmlsink_register for the methods. */
typedef struct {
  XmlBuilder b; /* first: the opaque pointer is also an XmlBuilder* */
  XmlEventSink sink;
} XmlBuilderObj;

static JSClassID js_xmlbuilder_class_id = 0;
static JSValue xmlbuilder_proto, xmlbuilder_ctor;

static XmlEventSink
js_xmlbuilder_sink(void* opaque) {
  return xbuild_sink(opaque);
}

static JSValue
js_xmlbuilder_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  XmlBuilderObj* o;
  JSValueConst options = argc > 0 ? argv[0] : JS_UNDEFINED;
  unsigned flags = 0;

  if(JS_IsObject(options)) {
    if(js_get_propertystr_bool(ctx, options, "flat"))
      flags |= XB_FLAT;

    if(js_get_propertystr_bool(ctx, options, "plain"))
      flags |= XB_PLAIN;
  }

  if(!(o = js_mallocz(ctx, sizeof(XmlBuilderObj))))
    return JS_EXCEPTION;

  xbuild_init(&o->b, ctx, flags);
  o->sink = xbuild_sink(&o->b);

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  obj = JS_IsException(proto) ? JS_EXCEPTION : JS_NewObjectProtoClass(ctx, proto, js_xmlbuilder_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    xbuild_free(&o->b, JS_GetRuntime(ctx));
    js_free(ctx, o);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, o);
  return obj;
}

static void
js_xmlbuilder_finalizer(JSRuntime* rt, JSValue val) {
  XmlBuilderObj* o;

  if((o = JS_GetOpaque(val, js_xmlbuilder_class_id))) {
    xbuild_free(&o->b, rt);
    js_free_rt(rt, o);
  }
}

static JSValue
js_xmlbuilder_get(JSContext* ctx, JSValueConst this_val, int magic) {
  XmlBuilderObj* o;

  if(!(o = JS_GetOpaque2(ctx, this_val, js_xmlbuilder_class_id)))
    return JS_EXCEPTION;

  return magic ? xbuild_path(&o->b) : xbuild_root(&o->b);
}

static JSValue
js_xmlbuilder_finish(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XmlBuilderObj* o;

  if(!(o = JS_GetOpaque2(ctx, this_val, js_xmlbuilder_class_id)))
    return JS_EXCEPTION;

  xbuild_finish(&o->b);
  return JS_DupValue(ctx, this_val);
}

static JSValue
js_xmlbuilder_handshake(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XmlBuilderObj* o;

  if(!(o = JS_GetOpaque2(ctx, this_val, js_xmlbuilder_class_id)))
    return JS_EXCEPTION;

  return JS_NewInt64(ctx, (int64_t)(intptr_t)&o->sink);
}

static JSClassDef js_xmlbuilder_class = {
    .class_name = "XmlBuilder",
    .finalizer = js_xmlbuilder_finalizer,
};

static const JSCFunctionListEntry js_xmlbuilder_funcs[] = {
    JS_CGETSET_MAGIC_DEF("root", js_xmlbuilder_get, 0, 0),
    JS_CGETSET_MAGIC_DEF("path", js_xmlbuilder_get, 0, 1),
    JS_CFUNC_DEF("finish", 0, js_xmlbuilder_finish),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XmlBuilder", JS_PROP_CONFIGURABLE),
};

/* tee: a sink that sends every event to several sinks.
 *
 * ```js
 * const t = tee(new XmlBuilder(), new XMLWriter(out));
 * parser.pump(t);                 // one parse, a tree and a copy of the text
 * ```
 *
 *   object...  sinks  XmlEventSink objects or objects with the sink methods
 *
 *   returns  a XmlTee sink object
 *
 * the sinks are called in order, the first failure stops the event.
 * wired in: JS_CFUNC_DEF("tee") in the module exports. */

static JSClassID js_xmltee_class_id = 0;

static XmlEventSink
js_xmltee_sink(void* opaque) {
  XmlJsTee* t = opaque;

  return t->sink;
}

static JSValue
js_xml_tee(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XmlJsTee* t = js_xmltee_new(ctx, argc, argv);
  JSValue obj;

  if(!t)
    return JS_EXCEPTION;

  if(JS_IsException(obj = JS_NewObjectClass(ctx, js_xmltee_class_id))) {
    js_xmltee_free_rt(JS_GetRuntime(ctx), t);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, t);
  return obj;
}

static void
js_xmltee_finalizer(JSRuntime* rt, JSValue val) {
  XmlJsTee* t;

  if((t = JS_GetOpaque(val, js_xmltee_class_id)))
    js_xmltee_free_rt(rt, t);
}

static JSValue
js_xmltee_handshake(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XmlJsTee* t;

  if(!(t = JS_GetOpaque2(ctx, this_val, js_xmltee_class_id)))
    return JS_EXCEPTION;

  return JS_NewInt64(ctx, (int64_t)(intptr_t)&t->sink);
}

static JSClassDef js_xmltee_class = {
    .class_name = "XmlTee",
    .finalizer = js_xmltee_finalizer,
};

typedef struct {
  XmlWriter xw;   /* first: the opaque pointer is also an XmlWriter* */
  XmlEventSink sink;
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

/* XMLWriter is an XmlEventSink object: elementStart, attribute, elementEnd, text, comment,
 * declaration, instruction, cdata and error come from js_xmlsink_define() (xml/xsink.h) and
 * write through xwrite.h; failures are thrown by js_xmlwrite_throw(). */
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
  wr->sink = xwrite_sink(&wr->xw);

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

static JSValue
js_xmlwriter_handshake(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  XMLWriter* wr;

  if(!(wr = JS_GetOpaque2(ctx, this_val, js_xmlwriter_class_id)))
    return JS_EXCEPTION;

  return JS_NewInt64(ctx, (int64_t)(intptr_t)&wr->sink);
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

  /* the type of an event of next(), the XmlEventType ids of include/xml/xevent.h */
  JS_DefinePropertyValueStr(ctx, xml_ctor, "ELEMENT_START", JS_NewInt32(ctx, XE_ELEMENT_START), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_ctor, "ATTRIBUTE", JS_NewInt32(ctx, XE_ATTRIBUTE), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_ctor, "ELEMENT_END", JS_NewInt32(ctx, XE_ELEMENT_END), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_ctor, "TEXT", JS_NewInt32(ctx, XE_TEXT), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_ctor, "COMMENT", JS_NewInt32(ctx, XE_COMMENT), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_ctor, "DECLARATION", JS_NewInt32(ctx, XE_DECLARATION), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_ctor, "INSTRUCTION", JS_NewInt32(ctx, XE_INSTRUCTION), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, xml_ctor, "CDATA", JS_NewInt32(ctx, XE_CDATA), JS_PROP_ENUMERABLE);

  JS_SetModuleExport(ctx, m, "XMLParser", xml_ctor);

  JS_NewClassID(&js_xmlwriter_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xmlwriter_class_id, &js_xmlwriter_class);

  xmlwriter_ctor = JS_NewCFunction2(ctx, js_xmlwriter_constructor, "XMLWriter", 1, JS_CFUNC_constructor, 0);
  xmlwriter_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, xmlwriter_proto, js_xmlwriter_funcs, countof(js_xmlwriter_funcs));
  js_xmlsink_register(js_xmlwriter_class_id, js_xmlwriter_sink, js_xmlwrite_throw, js_xmlwriter_note);
  js_xmlsink_define(ctx, xmlwriter_proto);
  js_xmlsink_define_handshake(ctx, xmlwriter_proto, js_xmlwriter_handshake);

  JS_NewClassID(&js_xmlbuilder_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xmlbuilder_class_id, &js_xmlbuilder_class);
  xmlbuilder_ctor = JS_NewCFunction2(ctx, js_xmlbuilder_constructor, "XmlBuilder", 0, JS_CFUNC_constructor, 0);
  xmlbuilder_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, xmlbuilder_proto, js_xmlbuilder_funcs, countof(js_xmlbuilder_funcs));
  js_xmlsink_register(js_xmlbuilder_class_id, js_xmlbuilder_sink, NULL, NULL);
  js_xmlsink_define(ctx, xmlbuilder_proto);
  js_xmlsink_define_handshake(ctx, xmlbuilder_proto, js_xmlbuilder_handshake);
  JS_SetClassProto(ctx, js_xmlbuilder_class_id, xmlbuilder_proto);
  JS_SetConstructor(ctx, xmlbuilder_ctor, xmlbuilder_proto);
  JS_SetModuleExport(ctx, m, "XmlBuilder", xmlbuilder_ctor);

  JS_NewClassID(&js_xmltee_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_xmltee_class_id, &js_xmltee_class);
  {
    JSValue tee_proto = JS_NewObject(ctx);

    js_xmlsink_register(js_xmltee_class_id, js_xmltee_sink, NULL, NULL);
    js_xmlsink_define(ctx, tee_proto);
    js_xmlsink_define_handshake(ctx, tee_proto, js_xmltee_handshake);
    JS_SetClassProto(ctx, js_xmltee_class_id, tee_proto);
  }
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
    JS_AddModuleExport(ctx, m, "XmlBuilder");
    JS_AddModuleExport(ctx, m, "XMLPushParser");
    JS_AddModuleExport(ctx, m, "XMLParser");
    JS_AddModuleExport(ctx, m, "XMLWriter");
  }

  return m;
}
