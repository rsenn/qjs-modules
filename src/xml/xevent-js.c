/* xevent-js.c: js_xmlevent_new(), an XmlEvent as a JS object.
 * depends on: xevent.h (the event).
 * rule: the event text is only read, the object owns its copy. */

#define _GNU_SOURCE 1

#include "xml/xevent-js.h"
#include "xml/xml.h"
#include "xml/xbuilder.h"
#include "xml/xopts.h"
#include "xml/xwalk.h"
#include "xml/xwrite.h"
#include "quickjs-location.h"
#include "virtual-properties.h"
#include "stream-utils.h"
#include "utils.h"

/**
 * \addtogroup xevent
 * @{
 */

JSValue
js_xmlevent_new(JSContext* ctx, const XmlEvent* ev) {
  JSValue obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, obj, "type", JS_NewInt32(ctx, ev->type));

  switch(ev->type) {
    case XE_ELEMENT_START:
    case XE_ELEMENT_END: JS_SetPropertyStr(ctx, obj, "name", ev->name ? JS_NewStringLen(ctx, ev->name, ev->nlen) : JS_UNDEFINED); break;
    case XE_ATTRIBUTE:
      JS_SetPropertyStr(ctx, obj, "name", JS_NewStringLen(ctx, ev->name, ev->nlen));

      if(ev->value)
        JS_SetPropertyStr(ctx, obj, "value", JS_NewStringLen(ctx, ev->value, ev->vlen));

      break;
    case XE_ERROR: break;
    default: JS_SetPropertyStr(ctx, obj, "value", JS_NewStringLen(ctx, ev->str ? ev->str : "", ev->len)); break;
  }

  if(ev->has_at) {
    JSValue loc = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, loc, "line", JS_NewUint32(ctx, ev->at.line));
    JS_SetPropertyStr(ctx, loc, "column", JS_NewUint32(ctx, ev->at.column));
    JS_SetPropertyStr(ctx, obj, "loc", loc);
  }

  return obj;
}

/* js_xmlwrite_throw: maps a failed xwrite event (-r) to a JS exception.
 *
 *   code                         throws
 *   XWRITE_E_MISMATCH            TypeError, code mismatched-end-tag
 *   XWRITE_E_UNMATCHED           TypeError, code unmatched-end-tag
 *   XWRITE_E_NOT_IN_TAG          TypeError, code attribute-outside-tag
 *   XWRITE_E_WRITE               Error, code write-failed
 *   XWRITE_E_NOMEM               out of memory
 *
 * returns JS_EXCEPTION. */
JSValue
js_xmlwrite_throw(JSContext* ctx, ssize_t r) {
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

/* js_xmlevent_error: the SyntaxError for the XE_ERROR `xp` just returned, at the
 * start of the offending tag; `base` supplies the input name.
 *
 * returns the error object (refcount: a new reference), or JS_EXCEPTION. */
JSValue
js_xmlevent_error(JSContext* ctx, const XMLParser* xp, const Location* base) {
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

static JSValue
make_tuple(JSContext* ctx, JSValue a, JSValue b) {
  JSValue ret = JS_NewArray(ctx);

  JS_SetPropertyUint32(ctx, ret, 0, a);
  JS_SetPropertyUint32(ctx, ret, 1, b);

  return ret;
}

/* xml_read_value: the element tree or flat list of `buf`, driven by the event parser of
 * src/xml/xml.c and built by XmlBuilder (src/xml/xbuilder.c).
 *
 *   tree  [{tagName, attributes, children}], children only on elements that stay open
 *   flat  [{tagName, attributes}, 'text', {tagName: '/name'}, ...]
 *
 * returns the array (or [array, WeakMap] with location) or throws SyntaxError. */
JSValue
xml_read_value(JSContext* ctx, const uint8_t* buf, size_t len, const char* input_name, XmlOptions opts) {
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

/* xml_write_tree: the XML text of a tree or flat list, a walk pumped into an XMLWriter layout.
 *
 * returns 0 with the text in *out, or -1 with an exception pending. */
static int
xml_write_tree(JSContext* ctx, JSValueConst tree, int32_t max_depth, BOOL flat, DynBuf* out) {
  XwalkSource src;
  XmlWriter wr = {.writer = writer_from_dynbuf(out)};
  XmlEventSink sink;
  XmlEvent ev;
  XmlPull st;
  ssize_t r;
  int ret = -1;

  if(xwalk_source_init(&src, ctx, tree, flat ? XWALK_FLAT | XWALK_BALANCED : 0, NULL))
    return -1;

  src.max_depth = max_depth;
  xwrite_init(&wr, (DynBufReallocFunc*)&js_realloc_rt, JS_GetRuntime(ctx));
  xwrite_set_indent(&wr, 2);
  sink = xwrite_sink(&wr);

  while((st = xwalk_event(&src, &ev)) == XML_PULL_EVENT)
    if((r = xevent_send(&sink, &ev)) < 0) {
      js_xmlwrite_throw(ctx, r);
      st = XML_PULL_FAIL;
      break;
    }

  if(st != XML_PULL_FAIL)
    ret = 0;

  wr.writer.finalizer = NULL; /* out is the caller's, xwrite_free() must not close it */
  xwrite_free(&wr);
  xwalk_source_close(&src, ctx);
  return ret;
}

JSValue
xml_tree_list(JSContext* ctx, JSValueConst tree, BOOL* flat) {
  JSValue list, last, kids = JS_UNDEFINED;
  size_t len;

  /* a single node is a list of one */
  if(JS_IsArray(ctx, tree)) {
    list = JS_DupValue(ctx, tree);
  } else {
    list = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, list, 0, JS_DupValue(ctx, tree));
  }

  /* the last node has a children array: a tree, else a flat list */
  len = js_array_length(ctx, list);
  last = len ? JS_GetPropertyUint32(ctx, list, len - 1) : JS_UNDEFINED;

  if(JS_IsObject(last))
    kids = JS_GetPropertyStr(ctx, last, "children");

  *flat = !JS_IsArray(ctx, kids);
  JS_FreeValue(ctx, kids);
  JS_FreeValue(ctx, last);
  return list;
}

JSValue
xml_write_value(JSContext* ctx, JSValueConst obj, int32_t max_depth) {
  DynBuf output;
  BOOL flat;
  JSValue ret, list = xml_tree_list(ctx, obj, &flat);

  dbuf_init_ctx(ctx, &output);

  if(xml_write_tree(ctx, list, max_depth, flat, &output) < 0)
    ret = JS_EXCEPTION;
  else
    ret = JS_NewStringLen(ctx, (const char*)output.buf, output.size);

  dbuf_free(&output);
  JS_FreeValue(ctx, list);
  return ret;
}

/** @} */
