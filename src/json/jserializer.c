/* jserializer.c: JsonSerializer, the pull serializer of the JS class without the JS glue.
 * depends on: jserial.h (events to text), jwalk.h (the JSON.stringify rules), jopts.h (space).
 * rule: an event is taken once, so toJSON and the replacer run exactly once per property. */

#include "char-utils.h"
#include "buffer-utils.h"
#include "json/jserializer.h"
#include "utils.h"

/**
 * \addtogroup json
 * @{
 */

/* json_serializer_read: copies up to n serialized bytes (n characters if chars == 2) to buf.
 *
 * a failure throws once (the walk is closed then), reads after it find the end.
 *
 * returns the bytes copied (0 at the end), or -1 with an exception pending. */
static ssize_t
json_serializer_read(JSContext* ctx, JsonSerializer* js, void* buf, size_t n, int chars) {
  ssize_t r;

  js->error = FALSE;
  js->started = TRUE;

  if(js->failed)
    return 0;

  if((r = jserial_read(&js->ser, buf, n, chars)) >= 0)
    return r;

  js->error = js->failed = TRUE;
  jwalk_source_close(&js->src, ctx);

  if(js->ser.error)
    js_jsonwriter_ret(ctx, js->ser.error);

  return -1;
}

JsonSerializer*
jserializer_new(JSContext* ctx, int argc, JSValueConst argv[]) {
  JsonSerializer* js;
  JSValueConst root = argc > 0 ? argv[0] : JS_UNDEFINED, replacer = JS_UNDEFINED, space = JS_UNDEFINED;
  JsonWriteOptions opts = {0};

  jsonspace_args(ctx, argc, argv, &replacer, &space);

  if(!(js = js_mallocz(ctx, sizeof(JsonSerializer))))
    return NULL;

  if(jsonspace_parse(ctx, space, &js->space)) {
    js_free(ctx, js);
    return NULL;
  }

  if(!(js->loc = location_new(ctx)) || jwalk_source_init(&js->src, ctx, root)) {
    if(js->loc)
      location_free(js->loc, JS_GetRuntime(ctx));

    js_free(ctx, js);
    return NULL;
  }

  location_zero(js->loc);
  jsonspace_apply(&js->space, &opts);
  jserial_init(&js->ser, jwalk_source(&js->src), opts, JS_GetRuntime(ctx));
  js->root = JS_DupValue(ctx, root);

  if(!JS_IsUndefined(replacer) && jwalk_set_replacer(&js->src.it, ctx, replacer)) {
    jserializer_free(js, JS_GetRuntime(ctx));
    return NULL;
  }

  return js;
}

void
jserializer_free(JsonSerializer* js, JSRuntime* rt) {
  jwalk_source_close_rt(&js->src, rt);
  jserial_free(&js->ser);
  JS_FreeValueRT(rt, js->root);

  if(js->loc)
    location_free(js->loc, rt);

  js_free_rt(rt, js);
}

JSValue
jserializer_read(JSContext* ctx, JsonSerializer* js, int argc, JSValueConst argv[]) {
  int64_t n = -1;
  ssize_t r;

  if(argc > 0 && (js_is_arraybuffer(ctx, argv[0]) || js_is_sharedarraybuffer(ctx, argv[0]) || js_is_typedarray(ctx, argv[0]))) {
    InputBuffer buf = js_input_args(ctx, argc, argv);

    r = json_serializer_read(ctx, js, buf.data, inputbuffer_length(&buf), 0);

    if(r > 0)
      location_count(js->loc, buf.data, r);

    inputbuffer_free(&buf, ctx);
    return r < 0 ? JS_EXCEPTION : JS_NewInt64(ctx, r);
  }

  if(argc > 0 && JS_ToInt64(ctx, &n, argv[0]))
    return JS_EXCEPTION;

  if(n == -1)
    n = INT32_MAX;
  else if(n < 0)
    return JS_ThrowRangeError(ctx, "size must not be negative");

  /* up to 4 bytes a character, in chunks so that read(-1) does not need one huge buffer */
  DynBuf out;
  uint8_t chunk[4096];

  dbuf_init_ctx(ctx, &out);

  while(n > 0 && (r = json_serializer_read(ctx, js, chunk, MIN_NUM((size_t)n, sizeof(chunk) / 4), 2)) > 0) {
    dbuf_put(&out, chunk, r);
    n -= utf8_strlen(chunk, r);
  }

  JSValue ret = r < 0 ? JS_EXCEPTION : out.size ? JS_NewStringLen(ctx, (const char*)out.buf, out.size) : JS_UNDEFINED;

  if(out.size)
    location_count(js->loc, out.buf, out.size);

  dbuf_free(&out);
  return ret;
}

/**
 * @}
 */
