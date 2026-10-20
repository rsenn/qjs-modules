/* xparser.c: XmlParser, the pull parser of the JS class without the JS methods.
 * depends on: xml.h (the parser), xopts.h (the options), xconnect.h (sinks), xevent-js.h (events, errors).
 * rule: the Location of the JS side is the one the parser updates, there is no copy. */

#define _GNU_SOURCE 1

#include "xml/xparser.h"
#include "xml/xconnect.h"
#include "xml/xevent-js.h"
#include "xml/xsink.h"
#include "quickjs-location.h"
#include "utils.h"
#include <string.h>

/**
 * \addtogroup xml
 * @{
 */

XmlParser*
xparser_new(JSContext* ctx, int argc, JSValueConst argv[]) {
  XmlParser* p;
  JSValueConst options = argc > 1 ? argv[1] : JS_UNDEFINED;
  JSValue filename;
  char* file;

  if(!(p = js_mallocz(ctx, sizeof(XmlParser))))
    return NULL;

  if(argc > 0)
    reader_from_js(ctx, argv[0], &p->reader);

  if(!(p->loc = location_new(ctx))) {
    reader_free(&p->reader);
    js_free(ctx, p);
    return NULL;
  }

  filename = JS_IsString(options) ? JS_DupValue(ctx, options) : JS_IsObject(options) ? JS_GetPropertyStr(ctx, options, "filename") : JS_UNDEFINED;

  if((file = js_tostring_free(ctx, filename))) {
    location_set_filename(p->loc, file, ctx);
    js_free(ctx, file);
  }

  xml_init(&p->xp, &p->reader);
  p->xp.loc = p->loc;

  if(JS_IsObject(options)) {
    XmlOptions xo = {0};

    if(xml_options_from_js(ctx, options, &xo)) {
      xparser_free(p, JS_GetRuntime(ctx));
      return NULL;
    }

    xml_set_tolerant(&p->xp, xo.tolerant);
    xml_set_flat(&p->xp, xo.flat);
    p->xp.self_closing_tags = xo.self_closing_tags;
    p->self_closing_owned = xo.owned;
    js_free(ctx, xo.filename);
  }

  return p;
}

void
xparser_free(XmlParser* p, JSRuntime* rt) {
  xml_free(&p->xp);
  reader_free(&p->reader);
  js_strv_free_rt(rt, p->self_closing_owned);

  if(p->loc)
    location_free(p->loc, rt);

  js_free_rt(rt, p);
}

JSValue
xparser_pump(JSContext* ctx, XmlParser* p, JSValueConst sink_arg) {
  XmlEventSink sink;
  XmlJsSink* adapter;
  XmlEvent ev;
  XmlPull st;
  JSValue ret = JS_EXCEPTION;
  ssize_t r;

  if(!p->reader.read)
    return JS_ThrowTypeError(ctx, "XMLParser: no input to parse");

  if(js_xmlsink_resolve(ctx, sink_arg, &sink, &adapter))
    return ret;

  while((st = xml_next(&p->xp, &ev)) == XML_PULL_EVENT) {
    if(ev.type == XE_ERROR) {
      JSValue err = js_xmlevent_error(ctx, &ev, p->loc);

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
  js_xmlsink_from_js_free(ctx, adapter);
  return ret;
}

JSValue
xparser_next(JSContext* ctx, XmlParser* p, BOOL* done) {
  XmlEvent ev;

  *done = TRUE;

  if(!p->reader.read)
    return JS_ThrowTypeError(ctx, "XMLParser: no input to parse");

  if(xml_next(&p->xp, &ev) != XML_PULL_EVENT)
    return JS_UNDEFINED;

  if(ev.type == XE_ERROR) {
    JSValue err = js_xmlevent_error(ctx, &ev, p->loc);

    return JS_IsException(err) ? err : JS_Throw(ctx, err);
  }

  *done = FALSE;
  return js_xmlevent_new(ctx, &ev);
}

/**
 * @}
 */
