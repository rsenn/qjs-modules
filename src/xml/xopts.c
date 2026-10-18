/* xopts.c: xml_options_from_js(), xml_options_free().
 * depends on: utils.h (property getters, string arrays).
 * rule: one place reads the XML options, read() and XMLParser share it. */

#define _GNU_SOURCE 1

#include "xml/xopts.h"
#include "utils.h"

/**
 * \addtogroup xml
 * @{
 */

int
xml_options_from_js(JSContext* ctx, JSValueConst options, XmlOptions* o) {
  JSValue v;

  if(!JS_IsObject(options))
    return 0;

  if(js_has_propertystr(ctx, options, "flat"))
    o->flat = js_get_propertystr_bool(ctx, options, "flat");

  if(js_has_propertystr(ctx, options, "tolerant"))
    o->tolerant = js_get_propertystr_bool(ctx, options, "tolerant");

  if(js_has_propertystr(ctx, options, "location"))
    o->location = js_get_propertystr_bool(ctx, options, "location");

  v = JS_GetPropertyStr(ctx, options, "selfClosingTags");

  if(JS_IsArray(ctx, v)) {
    js_strv_free(ctx, o->owned);
    o->self_closing_tags = (const char* const*)(o->owned = js_array_to_argv(ctx, NULL, v));
  }

  JS_FreeValue(ctx, v);
  v = JS_GetPropertyStr(ctx, options, "filename");

  if(JS_IsString(v)) {
    js_free(ctx, o->filename);
    o->filename = js_tostring(ctx, v);
  }

  JS_FreeValue(ctx, v);
  return 0;
}

void
xml_options_free(JSContext* ctx, XmlOptions* o) {
  js_strv_free(ctx, o->owned);
  js_free(ctx, o->filename);
  o->owned = NULL;
  o->filename = NULL;
}

/** @} */
