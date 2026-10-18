/* jopts.c: jsonspace_parse(), jsonspace_args(), jwrite_options_from_js().
 * depends on: jwrite.h (the options), utils.h (property getters).
 * rule: JS option values become a JsonWriteOptions here, the writer never sees a JSValue. */

#define _GNU_SOURCE 1
#include "json/jopts.h"
#include "utils.h"
#include <string.h>

/**
 * \addtogroup jwrite
 * @{
 */

int
jsonspace_parse(JSContext* ctx, JSValueConst space, JsonSpace* out) {
  *out = (JsonSpace){0};

  if(JS_IsNumber(space)) {
    double d;

    if(JS_ToFloat64(ctx, &d, space))
      return -1;

    out->indent = !(d >= 1) ? 0 : d > 10 ? 10 : (int32_t)d;
  } else if(JS_IsString(space)) {
    size_t n;
    const char* s = JS_ToCStringLen(ctx, &n, space);

    if(!s)
      return -1;

    if(n > 10) {
      n = 10;

      while(n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
        n--;
    }

    out->len = n;
    memcpy(out->str, s, n);
    out->indent = n;
    JS_FreeCString(ctx, s);
  }

  return 0;
}

void
jsonspace_apply(const JsonSpace* sp, JsonWriteOptions* opts) {
  opts->indent = sp->len ? 0 : sp->indent;
  opts->indent_len = sp->len;
  memcpy(opts->indent_str, sp->str, sp->len);
  opts->minify = !(sp->indent || sp->len);
}

void
jsonspace_args(JSContext* ctx, int argc, JSValueConst argv[], JSValueConst* replacer, JSValueConst* space) {
  *replacer = *space = JS_UNDEFINED;

  if(argc > 2) {
    if(JS_IsFunction(ctx, argv[1]) || JS_IsArray(ctx, argv[1]))
      *replacer = argv[1];

    *space = argv[2];
  } else if(argc == 2) {
    if(JS_IsFunction(ctx, argv[1]) || JS_IsArray(ctx, argv[1]))
      *replacer = argv[1];
    else
      *space = argv[1];
  }
}

void
jwrite_options_from_js(JSContext* ctx, JSValueConst obj, JsonWriteOptions* opts) {
  memset(opts, 0, sizeof(*opts));

  if(JS_IsNumber(obj)) {
    opts->indent = js_touint32(ctx, obj);
    return;
  }

  if(!JS_IsObject(obj))
    return;

  opts->bare_keys = js_get_propertystr_bool(ctx, obj, "bareKeys");
  opts->single_quotes = js_get_propertystr_bool(ctx, obj, "singleQuotes");
  opts->hex_numbers = js_get_propertystr_bool(ctx, obj, "hexNumbers");

  if(!(opts->minify = js_get_propertystr_bool(ctx, obj, "minify")))
    opts->indent = js_get_propertystr_uint32(ctx, obj, "indent");
}

/** @} */
