/* jopts.c: jsonspace_parse(), jsonspace_args(), jwrite_options_from_js().
 * depends on: jwrite.h (the options), utils.h (property getters).
 * rule: JS option values become a JsonWriteOptions here, the writer never sees a JSValue. */

#define _GNU_SOURCE 1
#include "json/jopts.h"
#include "utils.h"
#include "json/jread.h"
#include <math.h>
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

/* js_jsonwriter_ret: turns a jwrite_*() result into a JS outcome.
 *
 * returns the bytes written, or -1 with a TypeError pending (an Error for
 * a failed write, out of memory for a full stack). */
ssize_t
js_jsonwriter_ret(JSContext* ctx, ssize_t r) {
  if(r >= 0)
    return r;

  switch(-r) {
    case JWRITE_E_WRITE: {
      JSValue err = JS_NewError(ctx);

      JS_DefinePropertyValueStr(ctx, err, "message", JS_NewString(ctx, "JsonWriter: write failed"), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
      JS_DefinePropertyValueStr(ctx, err, "code", JS_NewString(ctx, "write-failed"), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
      JS_Throw(ctx, err);
      break;
    }
    case JWRITE_E_NOMEM: JS_ThrowOutOfMemory(ctx); break;
    default: JS_ThrowTypeError(ctx, "JsonWriter: %s", jwrite_error_message(-r)); break;
  }

  return -1;
}

/* jwrite_js_text: the JS string conversion of val, written as a string or a raw number. */
static ssize_t
jwrite_js_text(JSContext* ctx, JsonWriter* wr, JSValueConst val, int quoted) {
  size_t len;
  const char* str = JS_ToCStringLen(ctx, &len, val);
  ssize_t r;

  if(!str) {
    JS_FreeValue(ctx, JS_GetException(ctx));
    return js_jsonwriter_ret(ctx, jwrite_null(wr));
  }

  r = quoted ? jwrite_string(wr, str, len) : jwrite_number(wr, str, len);
  JS_FreeCString(ctx, str);
  return js_jsonwriter_ret(ctx, r);
}

ssize_t
jwrite_js_value(JSContext* ctx, JsonWriter* wr, JSValueConst val) {
  if(JS_IsNull(val) || JS_IsUndefined(val) || JS_IsSymbol(val) || JS_IsFunction(ctx, val))
    return js_jsonwriter_ret(ctx, jwrite_null(wr));

  if(JS_IsBool(val))
    return js_jsonwriter_ret(ctx, JS_ToBool(ctx, val) ? jwrite_true(wr) : jwrite_false(wr));

  double d = 0;

  if(JS_IsNumber(val))
    JS_ToFloat64(ctx, &d, val);

  return isnan(d) || isinf(d) ? js_jsonwriter_ret(ctx, jwrite_null(wr)) : jwrite_js_text(ctx, wr, val, !js_is_numeric(ctx, val));
}

/* jread_options_from_js: the syntax flags of the options object, -1 if it asks for none.
 *
 * reads dialect (a name of jr_flags_from_name()) and strict (true means json).
 *
 * returns 0, or -1 with a TypeError pending for an unknown dialect. */
int
jread_options_from_js(JSContext* ctx, JSValueConst obj, const char* who, int* flags) {
  JSValue name = JS_GetPropertyStr(ctx, obj, "dialect");

  *flags = -1;

  if(!JS_IsUndefined(name)) {
    size_t len;
    const char* str = JS_ToCStringLen(ctx, &len, name);

    if(!str || jr_flags_from_name(str, len, flags)) {
      JS_ThrowTypeError(ctx, "%s: unknown dialect %s", who, str ? str : "");

      if(str)
        JS_FreeCString(ctx, str);

      JS_FreeValue(ctx, name);
      return -1;
    }

    JS_FreeCString(ctx, str);
  } else if(js_get_propertystr_bool(ctx, obj, "strict")) {
    *flags = JR_JSON;
  }

  JS_FreeValue(ctx, name);
  return 0;
}

/** @} */
