#include "char-utils.h"
#include "stream-utils.h"
#include "utils.h"
/*#include "virtual-properties.h"*/
#include "vector.h"
#include "property-enumeration.h"
#include <ctype.h>
#include <math.h>
#include "json/json.h"
#include "json/jread.h"
#include "json/jwrite.h"
#include "json/jwalk.h"
#include "json/jopts.h"
#include "json/jevent-js.h"
#include "json/jserial.h"
#include "json/jbuilder.h"
#include "quickjs-location.h"

VISIBLE JSClassID js_jsonparser_class_id = 0, js_jsonpushparser_class_id = 0, js_jsonserializer_class_id = 0;
static JSValue json_parser_proto, json_parser_ctor, json_pushparser_proto, json_pushparser_ctor, json_serializer_proto, json_serializer_ctor;

static JSValue
js_json_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

/* read: JSON.parse() of a string or UTF-8 bytes, a JsonParser feeding a JsonBuilder.
 *
 * ```js
 * read('{"a":[1,"x"]}');        // {a: [1, "x"]}
 * read(bytes, 'config.json');   // errors name config.json
 * read('[1,');                  // SyntaxError, code 'unexpected-end'
 * ```
 *
 *   string|Uint8Array  input  the JSON text
 *   string             name   optional: the input name for error messages
 *
 *   returns  the value, objects have no prototype
 *   throws   TypeError for another input, SyntaxError with loc, length and code for bad JSON
 *
 * exported as `read` by the json module. */
static JSValue
js_json_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  InputBuffer input;
  JsonParser* p;
  JsonBuilder b;
  JsonEventSink sink;
  JsonEvent ev;
  JsonPull st;
  JSValue ret = JS_EXCEPTION;
  const char* name = argc >= 2 ? JS_ToCString(ctx, argv[1]) : 0;

  if(!(argc > 0 && js_is_bytes_or_string(ctx, argv[0]))) {
    JS_ThrowTypeError(ctx, "json.read(): expecting buffer or string");
    goto done;
  }

  if(!(input = js_input_chars(ctx, argv[0])).data)
    goto done;

  if(!(p = json_new(reader_from_bytes(input.data, input.size), 0, ctx))) {
    inputbuffer_free(&input, ctx);
    goto done;
  }

  p->flags = JR_JSON;
  jbuild_init(&b, ctx);
  sink = jbuild_sink(&b);

  /* the input ends when the reader is dry, then the end events follow */
  do {
    st = json_next(p, &ev);

    if(st == JSON_PULL_AGAIN)
      st = json_next_end(p, &ev);

    if(st != JSON_PULL_EVENT)
      break;

    if(ev.type == JSON_EVENT_ERROR) {
      js_throw_syntax_error_at(ctx, name ? name : "<json>", &ev.at, ev.span, ev.code, ev.str ? ev.str : "parse error");
      goto free;
    }

    jevent_send(&sink, &ev);
  } while(1);

  ret = b.has_root ? jbuild_root(&b) : JS_UNDEFINED;
free:
  jbuild_free(&b, JS_GetRuntime(ctx));
  json_free(p, JS_GetRuntime(ctx));
  inputbuffer_free(&input, ctx);
done:
  if(name)
    JS_FreeCString(ctx, name);

  return ret;
}

static void
clear_pending_exception(JSContext* ctx) {
  JSValue exc = JS_GetException(ctx);

  if(!JS_IsNull(exc) && !JS_IsUndefined(exc))
    JS_FreeValue(ctx, exc);
}

static ssize_t js_jsonwriter_ret(JSContext* ctx, ssize_t r);

/* write: JSON.stringify(), through walk (the rules) and jwrite (the text).
 *
 * ```js
 * write({ a: new Date(0), b: undefined }, null, 2);   // '{\n  "a": "1970-01-01T00:00:00.000Z"\n}'
 * write([1n]);                                         // TypeError
 * ```
 *
 *   any              value     what to serialize
 *   function|array   replacer  optional: JSON.stringify's replacer; with two arguments a number
 *                              or string here is taken as `space` (write(value, 2))
 *   number|string    space     optional: indentation
 *
 *   returns  the JSON text, or undefined when value has no JSON form
 *   throws   TypeError for a bigint or a circular structure; what toJSON or the replacer throws
 *
 * exported as `write` by the json module. */
static JSValue
js_json_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValueConst value = argc > 0 ? argv[0] : JS_UNDEFINED, replacer = JS_UNDEFINED, space = JS_UNDEFINED;
  JsonSpace sp;
  JsonWriter wr = {0};
  JwalkSource src;
  JsonEventSink sink;
  JsonEvent ev;
  JsonPull st;
  DynBuf out;
  JSValue ret;

  jsonspace_args(ctx, argc, argv, &replacer, &space);

  if(jsonspace_parse(ctx, space, &sp))
    return JS_EXCEPTION;

  if(jwalk_source_init(&src, ctx, value))
    return JS_EXCEPTION;

  dbuf_init_ctx(ctx, &out);
  wr.writer = writer_from_dynbuf(&out);
  jsonspace_apply(&sp, &wr.opts);
  jwrite_init(&wr, (DynBufReallocFunc*)&js_realloc_rt, JS_GetRuntime(ctx));
  sink = jwrite_sink(&wr);
  st = JS_IsUndefined(replacer) || !jwalk_set_replacer(&src.it, ctx, replacer) ? JSON_PULL_EVENT : JSON_PULL_FAIL;

  while(st == JSON_PULL_EVENT && (st = jwalk_event(&src, &ev)) == JSON_PULL_EVENT)
    if(js_jsonwriter_ret(ctx, jevent_send(&sink, &ev)) < 0)
      st = JSON_PULL_FAIL;

  if(st == JSON_PULL_FAIL)
    ret = JS_EXCEPTION;
  else if(jwalk_root_omitted(&src.it))
    ret = JS_UNDEFINED;
  else
    ret = JS_NewStringLen(ctx, (const char*)out.buf, out.size);

  jwalk_source_close(&src, ctx);
  jwrite_free(&wr);
  dbuf_free(&out);
  return ret;
}

/* JSONL: Bun's `JSONL` namespace, values separated by newlines.
 *
 * ```js
 * JSONL.parse('1\n{"a":2}\n[3');     // [1, {a: 2}]
 * JSONL.parseChunk('1\n2\n[3');      // {values: [1, 2], read: 3, done: false, error: null}
 * ```
 *
 *   string|Uint8Array  input   text, or its UTF-8 bytes
 *   number             start   first unit to read (chars for a string, bytes otherwise)
 *   number             end     unit to stop at
 *
 *   returns parse():      the values read; a trailing incomplete value is ignored
 *           parseChunk(): `read` is the unit after the last value, `done` is true when
 *                         the rest is only whitespace, `error` the SyntaxError of a bad value
 *
 * throws parse(): SyntaxError when the first value is bad; TypeError for a bad input.
 * wired in: JS_CFUNC_MAGIC_DEF("parse"/"parseChunk") on the `JSONL` export.
 */
static BOOL
jsonl_is_ws(uint8_t c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* end of the value starting at b[i], or 0 when the input ends inside it */
static size_t
jsonl_value_end(const uint8_t* b, size_t n, size_t i) {
  int depth = 0;

  if(b[i] == '{' || b[i] == '[' || b[i] == '"') {
    for(BOOL in_str = FALSE; i < n; i++) {
      uint8_t c = b[i];

      if(in_str) {
        if(c == '\\')
          i++;
        else if(c == '"') {
          in_str = FALSE;
          if(!depth)
            return i + 1;
        }
      } else if(c == '"') {
        in_str = TRUE;
      } else if(c == '{' || c == '[') {
        depth++;
      } else if((c == '}' || c == ']') && --depth == 0) {
        return i + 1;
      }
    }

    return 0;
  }

  while(i < n && !jsonl_is_ws(b[i]))
    i++;

  return i;
}

/* byte offset of UTF-16 unit `units` in the UTF-8 buffer `b` */
static size_t
jsonl_unit_to_byte(const uint8_t* b, size_t n, size_t units) {
  size_t i = 0;

  while(i < n && units > 0) {
    uint8_t c = b[i];

    i += c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
    units -= c >= 0xf0 && units >= 2 ? 2 : 1;
  }

  return i > n ? n : i;
}

/* UTF-16 unit index of byte offset `end` in the UTF-8 buffer `b` */
static size_t
jsonl_byte_to_unit(const uint8_t* b, size_t end) {
  size_t units = 0;

  for(size_t i = 0; i < end; i++)
    if((b[i] & 0xc0) != 0x80)
      units += b[i] >= 0xf0 ? 2 : 1;

  return units;
}

static JSValue
js_jsonl_parse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  BOOL is_str = argc > 0 && JS_IsString(argv[0]);
  InputBuffer input = js_input_chars(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
  JSValue values, error = JS_NULL, ret = JS_EXCEPTION;
  int64_t start = 0, end = -1;
  size_t i, n, read, nvalues = 0;
  BOOL done = FALSE;

  if(!input.data) {
    JS_ThrowTypeError(ctx, "%s: expecting string or Uint8Array", magic ? "JSONL.parseChunk()" : "JSONL.parse()");
    return JS_EXCEPTION;
  }

  if(magic) {
    if(argc > 1 && !JS_IsUndefined(argv[1]))
      JS_ToInt64(ctx, &start, argv[1]);
    if(argc > 2 && !JS_IsUndefined(argv[2]))
      JS_ToInt64(ctx, &end, argv[2]);
  }

  if(start < 0)
    start = 0;

  i = is_str ? jsonl_unit_to_byte(input.data, input.size, start) : MIN_NUM((size_t)start, input.size);
  n = end < 0 ? input.size : is_str ? jsonl_unit_to_byte(input.data, input.size, end) : MIN_NUM((size_t)end, input.size);
  read = i;
  values = JS_NewArray(ctx);

  for(;;) {
    size_t e, vs;
    char* text;
    JSValue v;

    while(i < n && jsonl_is_ws(input.data[i]))
      i++;

    if(i >= n) {
      done = TRUE;
      break;
    }

    vs = i;

    if(!(e = jsonl_value_end(input.data, n, vs)))
      break;

    if(!(text = js_malloc(ctx, e - vs + 1)))
      goto fail;

    memcpy(text, input.data + vs, e - vs);
    text[e - vs] = 0;
    v = JS_ParseJSON(ctx, text, e - vs, "<jsonl>");
    js_free(ctx, text);

    if(JS_IsException(v)) {
      /* a bare token cut off by the end of input may be completed by more data */
      if(e == n && input.data[vs] != '{' && input.data[vs] != '[' && input.data[vs] != '"') {
        JS_FreeValue(ctx, JS_GetException(ctx));
        break;
      }

      /* loc is the start of the failing value, length spans it; the engine's own
       * message says what is wrong inside it */
      {
        JSValue engine = JS_GetException(ctx), msg = JS_GetPropertyStr(ctx, engine, "message");
        const char* text = JS_ToCString(ctx, msg);
        SourcePos at;

        srcpos_init(&at, 0);
        srcpos_advance(&at, input.data, vs);
        error = js_syntax_error_at(ctx, "<jsonl>", &at, e - vs, "invalid-json", text ? text : "invalid JSON");

        if(text)
          JS_FreeCString(ctx, text);
        JS_FreeValue(ctx, msg);
        JS_FreeValue(ctx, engine);

        if(JS_IsException(error)) {
          error = JS_GetException(ctx);
        }
      }
      break;
    }

    JS_SetPropertyUint32(ctx, values, nvalues++, v);
    read = i = e;

    while(i < n && (input.data[i] == ' ' || input.data[i] == '\t' || input.data[i] == '\r'))
      i++;

    if(i < n && input.data[i] != '\n') {
      SourcePos at;

      srcpos_init(&at, 0);
      srcpos_advance(&at, input.data, i);
      js_throw_syntax_error_at(ctx, "<jsonl>", &at, 1, "unexpected-token", "JSONL: expected newline after value");
      error = JS_GetException(ctx);
      break;
    }
  }

  if(!magic) {
    if(nvalues == 0 && !JS_IsNull(error)) {
      JS_Throw(ctx, error);
      error = JS_NULL;
      goto fail;
    }

    ret = values;
    values = JS_UNDEFINED;
  } else {
    ret = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, ret, "values", values);
    JS_SetPropertyStr(ctx, ret, "read", JS_NewInt64(ctx, is_str ? (int64_t)jsonl_byte_to_unit(input.data, read) : (int64_t)read));
    JS_SetPropertyStr(ctx, ret, "done", JS_NewBool(ctx, done));
    JS_SetPropertyStr(ctx, ret, "error", error);
    values = error = JS_UNDEFINED;
  }

fail:
  JS_FreeValue(ctx, values);
  JS_FreeValue(ctx, error);
  inputbuffer_free(&input, ctx);
  return ret;
}

static const JSCFunctionListEntry js_jsonl_funcs[] = {
    JS_CFUNC_MAGIC_DEF("parse", 1, js_jsonl_parse, 0),
    JS_CFUNC_MAGIC_DEF("parseChunk", 1, js_jsonl_parse, 1),
};

static const JSCFunctionListEntry js_json_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_json_read),
    JS_CFUNC_DEF("write", 2, js_json_write),
};

/* ---------------------------------------------------------------------- */
/* JsonPushParser                                                        */
/* ---------------------------------------------------------------------- */

typedef struct PushParser {
  JSContext* ctx;
  jr_state_t jrs;
  JsonBuilder builder;
  JsonEventSink sink; /* jbuild_sink(&builder) */
  JSValue callback_fn, callbacks_obj, callbacks[JSON_EVENT_KEY + 1 - JSON_EVENT_ERROR];
  unsigned use_builder : 1;
  Location* loc;
  JSValue last_error; /* SyntaxError of the latest error, or JS_UNDEFINED */
} JsonPushParser;

/* json_pushparser_onerror: the callback `onError` of the options object, or undefined;
 * refcount: a new reference. */
static JSValue
json_pushparser_onerror(JsonPushParser* pp) {
  if(JS_IsUndefined(pp->callbacks_obj))
    return JS_UNDEFINED;

  JSValue fn = JS_GetPropertyStr(pp->ctx, pp->callbacks_obj, "onError");

  if(JS_IsFunction(pp->ctx, fn))
    return fn;

  JS_FreeValue(pp->ctx, fn);
  return JS_UNDEFINED;
}

/* json_pushparser_call: calls fn, an exception it throws is dropped. */
static void
json_pushparser_call(JSContext* ctx, JSValueConst fn, JSValueConst this_obj, int argc, JSValueConst argv[]) {
  JSValue ret = JS_Call(ctx, fn, this_obj, argc, argv);

  if(JS_IsException(ret))
    clear_pending_exception(ctx);
  else
    JS_FreeValue(ctx, ret);
}

/* json_pushparser_error: records the error event as last_error and passes it to
 * onError(err), runs for both the builder and the callback mode. */
static void
json_pushparser_error(JsonPushParser* pp, const JsonEvent* ev) {
  JSContext* ctx = pp->ctx;
  const char* msg = ev->code && !strcmp(ev->code, "unexpected-end") ? "unexpected end of input" : "parse error";
  JSValue fn, err = js_syntax_error_at(ctx, "<json>", &ev->at, ev->span, ev->code, msg);

  if(JS_IsException(err)) {
    clear_pending_exception(ctx);
    return;
  }

  JS_FreeValue(ctx, pp->last_error);
  pp->last_error = JS_DupValue(ctx, err);

  if(JS_IsFunction(ctx, (fn = json_pushparser_onerror(pp))))
    json_pushparser_call(ctx, fn, pp->callbacks_obj, 1, &err);

  JS_FreeValue(ctx, fn);
  JS_FreeValue(ctx, err);
}

/* json_pushparser_event_build: the handler of the builder mode. */
static ssize_t
json_pushparser_event_build(void* user, const JsonEvent* ev) {
  JsonPushParser* pp = user;

  if(ev->type == JSON_EVENT_ERROR) {
    json_pushparser_error(pp, ev);
    return 0;
  }

  return jevent_send(&pp->sink, ev);
}

/* json_pushparser_event: the handler of the callback mode, the builder sees every event too. */
static ssize_t
json_pushparser_event(void* user, const JsonEvent* ev) {
  JsonPushParser* pp = user;
  JSContext* ctx = pp->ctx;
  ssize_t r = json_pushparser_event_build(user, ev);

  if(!JS_IsUndefined(pp->callback_fn)) {
    JSValue args[2] = {JS_NewInt32(ctx, ev->type), js_jsonevent_value(ctx, ev)};

    json_pushparser_call(ctx, pp->callback_fn, JS_UNDEFINED, 2, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
  }

  if(ev->type >= JSON_EVENT_ERROR && ev->type <= JSON_EVENT_KEY) {
    JSValue cb = pp->callbacks[ev->type - JSON_EVENT_ERROR];

    if(JS_IsFunction(ctx, cb)) {
      JSValue val = js_jsonevent_value(ctx, ev);

      json_pushparser_call(ctx, cb, pp->callbacks_obj, 1, &val);
      JS_FreeValue(ctx, val);
    }
  }

  return r;
}

/* js_jsonpushparser_throw: throws the latest error (position and code from jread), unless
 * the options object has `onError`, which already received it; returns JS_UNDEFINED then. */
static JSValue
js_jsonpushparser_throw(JSContext* ctx, JsonPushParser* pp, const char* msg) {
  JSValue fn = json_pushparser_onerror(pp);
  BOOL handled = !JS_IsUndefined(fn);

  JS_FreeValue(ctx, fn);

  if(handled)
    return JS_UNDEFINED;

  if(!JS_IsUndefined(pp->last_error))
    return JS_Throw(ctx, JS_DupValue(ctx, pp->last_error));

  return js_throw_syntax_error_at(ctx, "<json>", &pp->jrs.err_pos, pp->jrs.err_len, jr_error_name(pp->jrs.err_code), msg);
}

static JSValue
js_jsonpushparser_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonPushParser* pp;
  InputBuffer input;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_jsonpushparser_class_id)))
    return JS_EXCEPTION;

  if(!(argc > 0 && js_is_bytes_or_string(ctx, argv[0])))
    return JS_ThrowTypeError(ctx, "JsonPushParser.write(): expecting buffer or string");

  input = js_input_chars(ctx, argv[0]);

  if(input.data == 0)
    return JS_EXCEPTION;

  jr_feed(&pp->jrs, (const char*)inputbuffer_data(&input), inputbuffer_length(&input), pp->use_builder ? json_pushparser_event_build : json_pushparser_event, pp);

  if(pp->loc)
    location_count(pp->loc, inputbuffer_data(&input), inputbuffer_length(&input));

  inputbuffer_free(&input, ctx);

  /* just_erred (not the sticky error) - a resynced parser must not keep throwing on every
   * future write() call just because some earlier call hit a (already-reported) error. */
  return pp->jrs.just_erred ? js_jsonpushparser_throw(ctx, pp, "parse error") : JS_UNDEFINED;
}

static JSValue
js_jsonpushparser_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonPushParser* pp;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_jsonpushparser_class_id)))
    return JS_EXCEPTION;

  jr_end(&pp->jrs, pp->use_builder ? json_pushparser_event_build : json_pushparser_event, pp);

  if(!pp->jrs.done || pp->jrs.error)
    return js_jsonpushparser_throw(ctx, pp, pp->jrs.done ? "parse error" : "unexpected end of input");

  return JS_UNDEFINED;
}

enum {
  PUSHPARSER_ROOT,
  PUSHPARSER_PATH,
  PUSHPARSER_LOCATION,
  PUSHPARSER_ERROR,
};

static JSValue
js_jsonpushparser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonPushParser* pp;
  JSValue ret = JS_UNDEFINED;

  if(!(pp = JS_GetOpaque2(ctx, this_val, js_jsonpushparser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PUSHPARSER_ROOT: ret = jbuild_root(&pp->builder); break;
    case PUSHPARSER_PATH: ret = jbuild_path(&pp->builder); break;
    case PUSHPARSER_ERROR: ret = JS_IsUndefined(pp->last_error) ? JS_NULL : JS_DupValue(ctx, pp->last_error); break;
    case PUSHPARSER_LOCATION: ret = pp->loc ? js_location_wrap(ctx, pp->loc) : JS_UNDEFINED; break;
  }

  return ret;
}

static JSValue
js_jsonpushparser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  JsonPushParser* pp;

  if(!(pp = js_mallocz(ctx, sizeof(JsonPushParser))))
    return JS_EXCEPTION;

  pp->ctx = ctx;
  jr_state_init(&pp->jrs);
  jbuild_init(&pp->builder, ctx);
  pp->sink = jbuild_sink(&pp->builder);

  if(!(pp->loc = location_new(ctx))) {
    jr_state_free(&pp->jrs);
    jbuild_free(&pp->builder, JS_GetRuntime(ctx));
    js_free(ctx, pp);
    return JS_EXCEPTION;
  }

  location_zero(pp->loc);

  pp->callback_fn = JS_UNDEFINED;
  pp->callbacks_obj = JS_UNDEFINED;
  pp->last_error = JS_UNDEFINED;

  for(int i = 0; i < JSON_EVENT_KEY + 1 - JSON_EVENT_ERROR; i++)
    pp->callbacks[i] = JS_UNDEFINED;

  if(argc > 0) {
    if(JS_IsFunction(ctx, argv[0])) {
      pp->callback_fn = JS_DupValue(ctx, argv[0]);
    } else if(JS_IsObject(argv[0])) {
      pp->callbacks_obj = JS_DupValue(ctx, argv[0]);

      for(int t = JSON_EVENT_ERROR; t <= JSON_EVENT_KEY; t++) {
        const char* prop_name = NULL;

        switch((JsonEventType)t) {
          case JSON_EVENT_ERROR: prop_name = "error"; break;
          case JSON_EVENT_NULL:
          case JSON_EVENT_TRUE:
          case JSON_EVENT_FALSE:
          case JSON_EVENT_NUMBER:
          case JSON_EVENT_STRING: prop_name = "value"; break;
          case JSON_EVENT_OBJECT_START: prop_name = "objectStart"; break;
          case JSON_EVENT_OBJECT_END: prop_name = "objectEnd"; break;
          case JSON_EVENT_ARRAY_START: prop_name = "arrayStart"; break;
          case JSON_EVENT_ARRAY_END: prop_name = "arrayEnd"; break;
          case JSON_EVENT_KEY: prop_name = "key"; break;
          case JSON_EVENT_COMMENT:
          case JSON_EVENT_RAW: break;
        }

        if(prop_name) {
          pp->callbacks[t - JSON_EVENT_ERROR] = JS_GetPropertyStr(ctx, argv[0], prop_name);
        }
      }
    }
  }

  BOOL all_callbacks_present = TRUE;

  for(int t = JSON_EVENT_NULL; t <= JSON_EVENT_KEY; t++) {
    if(!JS_IsFunction(ctx, pp->callbacks[t - JSON_EVENT_ERROR])) {
      all_callbacks_present = FALSE;
      break;
    }
  }

  if(!JS_IsUndefined(pp->callback_fn) || all_callbacks_present)
    pp->use_builder = FALSE;
  else
    pp->use_builder = TRUE;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, json_pushparser_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_jsonpushparser_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    jr_state_free(&pp->jrs);
    jbuild_free(&pp->builder, JS_GetRuntime(ctx));
    JS_FreeValue(ctx, pp->callback_fn);
    JS_FreeValue(ctx, pp->callbacks_obj);
    JS_FreeValue(ctx, pp->last_error);

    for(int i = 0; i < JSON_EVENT_KEY + 1 - JSON_EVENT_ERROR; i++)
      JS_FreeValue(ctx, pp->callbacks[i]);

    js_free(ctx, pp);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, pp);
  return obj;
}

static void
js_jsonpushparser_finalizer(JSRuntime* rt, JSValue val) {
  JsonPushParser* pp;

  if((pp = JS_GetOpaque(val, js_jsonpushparser_class_id))) {
    jr_state_free(&pp->jrs);
    jbuild_free(&pp->builder, rt);

    if(pp->loc)
      location_free(pp->loc, rt);

    JS_FreeValueRT(rt, pp->callback_fn);
    JS_FreeValueRT(rt, pp->callbacks_obj);
    JS_FreeValueRT(rt, pp->last_error);

    for(int i = 0; i < JSON_EVENT_KEY + 1 - JSON_EVENT_ERROR; i++)
      JS_FreeValueRT(rt, pp->callbacks[i]);

    js_free_rt(rt, pp);
  }
}

static const JSCFunctionListEntry js_jsonpushparser_proto_funcs[] = {
    JS_CFUNC_DEF("write", 1, js_jsonpushparser_write),
    JS_CFUNC_DEF("close", 0, js_jsonpushparser_close),
    JS_CGETSET_MAGIC_DEF("root", js_jsonpushparser_get, 0, PUSHPARSER_ROOT),
    JS_CGETSET_MAGIC_DEF("path", js_jsonpushparser_get, 0, PUSHPARSER_PATH),
    JS_CGETSET_MAGIC_DEF("location", js_jsonpushparser_get, 0, PUSHPARSER_LOCATION),
    JS_CGETSET_MAGIC_DEF("error", js_jsonpushparser_get, 0, PUSHPARSER_ERROR),
    JS_PROP_INT32_DEF("TYPE_ERROR", JSON_EVENT_ERROR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_NULL", JSON_EVENT_NULL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_TRUE", JSON_EVENT_TRUE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_FALSE", JSON_EVENT_FALSE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_NUMBER", JSON_EVENT_NUMBER, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_STRING", JSON_EVENT_STRING, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_OBJECT", JSON_EVENT_OBJECT_START, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_OBJECT_START", JSON_EVENT_OBJECT_START, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_OBJECT_END", JSON_EVENT_OBJECT_END, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ARRAY", JSON_EVENT_ARRAY_START, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ARRAY_START", JSON_EVENT_ARRAY_START, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ARRAY_END", JSON_EVENT_ARRAY_END, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_KEY", JSON_EVENT_KEY, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonPushParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_jsonpushparser_class = {
    .class_name = "JsonPushParser",
    .finalizer = js_jsonpushparser_finalizer,
};

/* ---------------------------------------------------------------------- */
/* JsonSerializer: pull (.read(n)) serializer traversing via              */
/* property_recursion_*(), producing only as much text as requested.      */
/* ---------------------------------------------------------------------- */

/* JsonSerializer: a JwalkSource (JSON.stringify's rules) pulled through a JsonSerial;
 * read() takes events from the walk until the bytes asked for are buffered.
 * rule: an event is taken once, so toJSON and the replacer run exactly once per property. */
typedef struct {
  JwalkSource src;
  JsonSerial ser;
  JsonSpace space;
  unsigned started : 1, error : 1, failed : 1;
  JSValue root;
  Location* loc;
} JsonSerializer;

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

static JSValue
js_jsonserializer_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonSerializer* js;
  int64_t n = -1;
  ssize_t r;

  if(!(js = JS_GetOpaque2(ctx, this_val, js_jsonserializer_class_id)))
    return JS_EXCEPTION;

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

enum {
  SERIALIZER_LOCATION,
  SERIALIZER_ROOT,
  SERIALIZER_INDENT,
  SERIALIZER_PATH,
  SERIALIZER_FINISHED,
  SERIALIZER_STARTED,
  SERIALIZER_ERROR,
  SERIALIZER_BLOCKED,
};

static JSValue
js_jsonserializer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonSerializer* js;
  JSValue ret = JS_UNDEFINED;

  if(!(js = JS_GetOpaque2(ctx, this_val, js_jsonserializer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case SERIALIZER_LOCATION: ret = js_location_wrap(ctx, js->loc); break;
    case SERIALIZER_ROOT: ret = JS_DupValue(ctx, js->root); break;
    case SERIALIZER_INDENT: ret = JS_NewInt32(ctx, js->space.indent); break;
    case SERIALIZER_PATH: ret = property_recursion_path(&js->src.it.walk.stack, ctx); break;
    case SERIALIZER_FINISHED: ret = JS_NewBool(ctx, js->ser.status != JSON_PULL_EVENT); break;
    case SERIALIZER_STARTED: ret = JS_NewBool(ctx, js->started); break;
    case SERIALIZER_ERROR: ret = JS_NewBool(ctx, js->error); break;
    case SERIALIZER_BLOCKED: ret = JS_FALSE; break;
  }

  return ret;
}

static JSValue
js_jsonserializer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JsonSerializer* js;
  JSValue ret = JS_UNDEFINED;

  if(!(js = JS_GetOpaque2(ctx, this_val, js_jsonserializer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case SERIALIZER_INDENT:
      if(!jsonspace_parse(ctx, value, &js->space))
        jsonspace_apply(&js->space, &js->ser.wr.opts);

      break;
  }

  return ret;
}

static JSValue
js_jsonserializer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  JsonSerializer* js;
  JSValueConst root = argc > 0 ? argv[0] : JS_UNDEFINED, replacer = JS_UNDEFINED, space = JS_UNDEFINED;

  jsonspace_args(ctx, argc, argv, &replacer, &space);

  JsonWriteOptions opts = {0};

  if(!(js = js_mallocz(ctx, sizeof(JsonSerializer))))
    return JS_EXCEPTION;

  if(jsonspace_parse(ctx, space, &js->space)) {
    js_free(ctx, js);
    return JS_EXCEPTION;
  }

  if(!(js->loc = location_new(ctx)) || jwalk_source_init(&js->src, ctx, root)) {
    if(js->loc)
      location_free(js->loc, JS_GetRuntime(ctx));

    js_free(ctx, js);
    return JS_EXCEPTION;
  }

  location_zero(js->loc);
  jsonspace_apply(&js->space, &opts);
  jserial_init(&js->ser, jwalk_source(&js->src), opts, JS_GetRuntime(ctx));
  js->root = JS_DupValue(ctx, root);

  if(!JS_IsUndefined(replacer) && jwalk_set_replacer(&js->src.it, ctx, replacer))
    goto fail;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, json_serializer_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_jsonserializer_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, js);
  return obj;

fail:
  jwalk_source_close(&js->src, ctx);
  jserial_free(&js->ser);
  JS_FreeValue(ctx, js->root);
  location_free(js->loc, JS_GetRuntime(ctx));
  js_free(ctx, js);
  return JS_EXCEPTION;
}

static JSValue
js_jsonserializer_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  JSValue args[] = {argc > 0 ? argv[0] : JS_NewUint32(ctx, 1024)};
  JSValue ret = js_jsonserializer_read(ctx, this_val, countof(args), args);

  *pdone = FALSE;

  if(JS_IsException(ret)) {
    *pdone = TRUE;
    ret = JS_GetException(ctx);
  }

  return ret;
}

static void
js_jsonserializer_finalizer(JSRuntime* rt, JSValue val) {
  JsonSerializer* js;

  if((js = JS_GetOpaque(val, js_jsonserializer_class_id))) {
    jwalk_source_close_rt(&js->src, rt);
    jserial_free(&js->ser);
    JS_FreeValueRT(rt, js->root);

    if(js->loc)
      location_free(js->loc, rt);

    js_free_rt(rt, js);
  }
}

static const JSCFunctionListEntry js_jsonserializer_proto_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_jsonserializer_read),
    JS_ITERATOR_NEXT_DEF("next", 0, js_jsonserializer_next, 0),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_json_iterator),
    JS_CGETSET_MAGIC_DEF("location", js_jsonserializer_get, 0, SERIALIZER_LOCATION),
    JS_CGETSET_MAGIC_DEF("root", js_jsonserializer_get, 0, SERIALIZER_ROOT),
    JS_CGETSET_MAGIC_DEF("indent", js_jsonserializer_get, js_jsonserializer_set, SERIALIZER_INDENT),
    JS_CGETSET_MAGIC_DEF("path", js_jsonserializer_get, 0, SERIALIZER_PATH),
    JS_CGETSET_MAGIC_DEF("finished", js_jsonserializer_get, 0, SERIALIZER_FINISHED),
    JS_CGETSET_MAGIC_DEF("started", js_jsonserializer_get, 0, SERIALIZER_STARTED),
    JS_CGETSET_MAGIC_DEF("error", js_jsonserializer_get, 0, SERIALIZER_ERROR),
    JS_CGETSET_MAGIC_DEF("blocked", js_jsonserializer_get, 0, SERIALIZER_BLOCKED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonSerializer", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_jsonserializer_class = {
    .class_name = "JsonSerializer",
    .finalizer = js_jsonserializer_finalizer,
};

/* ---------------------------------------------------------------------- */
/* JsonWriter: push-based incremental JSON writer                         */
/* ---------------------------------------------------------------------- */

static JSClassID js_jsonwriter_class_id = 0;
static JSValue jsonwriter_proto, jsonwriter_ctor;

/* js_jsonwriter_ret: turns a jwrite_*() result into a JS outcome.
 *
 * returns the bytes written, or -1 with a TypeError pending (an Error for
 * a failed write, out of memory for a full stack). */
static ssize_t
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

static ssize_t
js_jsonwriter_key(void* opaque, JSContext* ctx, JSValueConst key) {
  size_t len;
  const char* str;
  ssize_t r;

  if(!(str = JS_ToCStringLen(ctx, &len, key)))
    return -1;

  r = jwrite_key(opaque, str, len);
  JS_FreeCString(ctx, str);
  return js_jsonwriter_ret(ctx, r);
}

static ssize_t
js_jsonwriter_object_start(void* opaque, JSContext* ctx) {
  return js_jsonwriter_ret(ctx, jwrite_object_start(opaque));
}

static ssize_t
js_jsonwriter_array_start(void* opaque, JSContext* ctx) {
  return js_jsonwriter_ret(ctx, jwrite_array_start(opaque));
}

static ssize_t
js_jsonwriter_object_end(void* opaque, JSContext* ctx) {
  return js_jsonwriter_ret(ctx, jwrite_object_end(opaque));
}

static ssize_t
js_jsonwriter_array_end(void* opaque, JSContext* ctx) {
  return js_jsonwriter_ret(ctx, jwrite_array_end(opaque));
}

/* js_jsonwriter_text: the JS string conversion of `val`, written as a raw
 * value; null when the conversion throws (the exception is discarded). */
static ssize_t
js_jsonwriter_text(JSContext* ctx, JsonWriter* wr, JSValueConst val, BOOL quoted) {
  size_t len;
  const char* str = JS_ToCStringLen(ctx, &len, val);
  ssize_t r;

  if(!str) {
    clear_pending_exception(ctx);
    return js_jsonwriter_ret(ctx, jwrite_null(wr));
  }

  r = quoted ? jwrite_string(wr, str, len) : jwrite_number(wr, str, len);
  JS_FreeCString(ctx, str);
  return js_jsonwriter_ret(ctx, r);
}

/* js_jsonwriter_value: writes a JS primitive.
 *
 *   JS value                        written as
 *   null, undefined, symbol, fn     null
 *   boolean                         true / false
 *   string                          string
 *   NaN, Infinity                   null
 *   integer, hexNumbers set         0xff / -0x1f
 *   number, bigint                  the JS text (1.5, 1e21, 123n -> 123)
 *   other object                    its toString, as a string
 */
static ssize_t
js_jsonwriter_value(void* opaque, JSContext* ctx, JSValueConst val) {
  JsonWriter* wr = opaque;

  if(JS_IsNull(val) || JS_IsUndefined(val) || JS_IsSymbol(val) || JS_IsFunction(ctx, val))
    return js_jsonwriter_ret(ctx, jwrite_null(wr));

  if(JS_IsBool(val))
    return js_jsonwriter_ret(ctx, JS_ToBool(ctx, val) ? jwrite_true(wr) : jwrite_false(wr));

  if(JS_IsNumber(val)) {
    double d;

    JS_ToFloat64(ctx, &d, val);

    if(isnan(d) || isinf(d))
      return js_jsonwriter_ret(ctx, jwrite_null(wr));
  }

  return js_jsonwriter_text(ctx, wr, val, !js_is_numeric(ctx, val));
}

static WalkInterface
js_jsonwriter_walk(void* opaque) {
  return (WalkInterface){
      opaque,
      js_jsonwriter_key,
      js_jsonwriter_object_start,
      js_jsonwriter_array_start,
      js_jsonwriter_object_end,
      js_jsonwriter_array_end,
      js_jsonwriter_value,
  };
}

enum {
  WRITER_WRITTEN,
  WRITER_DEPTH,
  WRITER_COUNT,
  WRITER_STACK,
};

static JSValue
js_jsonwriter_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonWriter* wr;
  JSValue ret = JS_UNDEFINED;

  if(!(wr = JS_GetOpaque2(ctx, this_val, js_jsonwriter_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITER_WRITTEN: ret = JS_NewInt64(ctx, wr->written); break;
    case WRITER_DEPTH: ret = JS_NewUint32(ctx, vector_size(&wr->stack, sizeof(JsonWriterFrame))); break;
    case WRITER_COUNT: {
      if(!vector_empty(&wr->stack))
        ret = JS_NewUint32(ctx, (vector_end_t(&wr->stack, JsonWriterFrame) - 1)->count);

      break;
    }
    case WRITER_STACK: {
      JsonWriterFrame* frame;
      uint32_t i = 0;

      ret = JS_NewArray(ctx);

      vector_foreach_t(&wr->stack, frame) {
        JSValue item = JS_NewObjectProto(ctx, JS_NULL);
        JS_SetPropertyStr(ctx, item, "isObject", JS_NewBool(ctx, frame->is_object));
        JS_SetPropertyStr(ctx, item, "expectingValue", JS_NewBool(ctx, frame->expecting_value));
        JS_SetPropertyStr(ctx, item, "count", JS_NewUint32(ctx, frame->count));
        JS_SetPropertyUint32(ctx, ret, i++, item);
      }

      break;
    }
  }

  return ret;
}

static JSValue
js_jsonwriter_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  JsonWriter* wr;
  int i = 0;

  if(!(wr = js_mallocz(ctx, sizeof(JsonWriter))))
    return JS_EXCEPTION;

  if(i < argc && writer_from_js(ctx, argv[i], &wr->writer))
    i++;

  jwrite_init(wr, (DynBufReallocFunc*)&vector_js_realloc, ctx);

  JSValue options = i < argc ? argv[i] : (argc > 0 ? argv[0] : JS_UNDEFINED);

  jwrite_options_from_js(ctx, options, &wr->opts);

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_jsonwriter_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, wr);
  return obj;

fail:
  jwrite_free(wr);
  js_free(ctx, wr);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_jsonwriter_finalizer(JSRuntime* rt, JSValue val) {
  JsonWriter* wr;

  if((wr = JS_GetOpaque(val, js_jsonwriter_class_id))) {
    jwrite_free(wr);
    js_free_rt(rt, wr);
  }
}

static JSClassDef js_jsonwriter_class = {
    .class_name = "JsonWriter",
    .finalizer = js_jsonwriter_finalizer,
};

static const JSCFunctionListEntry js_jsonwriter_funcs[] = {
    JS_CGETSET_MAGIC_DEF("written", js_jsonwriter_get, 0, WRITER_WRITTEN),
    JS_CGETSET_MAGIC_DEF("depth", js_jsonwriter_get, 0, WRITER_DEPTH),
    JS_CGETSET_MAGIC_DEF("stack", js_jsonwriter_get, 0, WRITER_STACK),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonWriter", JS_PROP_CONFIGURABLE),
};

static JSValue
js_jsonparser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  JsonParser* p;
  JSValueConst input = argc > 0 ? argv[0] : JS_UNDEFINED;
  Reader reader;
  const char* filename = 0;

  if(JS_IsFunction(ctx, input)) {
    reader = reader_from_jsfunction(ctx, input);
  } else if(JS_IsObject(input)) {
    JSValue read_fn = JS_GetPropertyStr(ctx, input, "read");

    if(JS_IsException(read_fn))
      return JS_EXCEPTION;

    if(JS_IsFunction(ctx, read_fn))
      reader = reader_from_jsmethod(ctx, read_fn, input);
    else
      reader = reader_from_jsbuf(ctx, input);

    JS_FreeValue(ctx, read_fn);
  } else if(JS_IsNumber(input)) {
    reader = reader_from_fd(js_toint64(ctx, input), FALSE);
  } else {
    reader = reader_from_jsbuf(ctx, input);
  }

  if(argc > 1)
    filename = JS_ToCString(ctx, argv[1]);

  p = json_new(reader, filename, ctx);

  if(filename)
    JS_FreeCString(ctx, filename);

  if(!p) {
    reader_free(&reader);
    return JS_EXCEPTION;
  }

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, json_parser_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_jsonparser_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj)) {
    json_free(p, JS_GetRuntime(ctx));
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, p);

  return obj;
}

enum {
  PARSER_PARSE,
};

/* js_jsonparser_next: the next token type of p as parse() returns it.
 *
 *   json_next() answers   parse() returns
 *   an event              its JsonEventType
 *   AGAIN                 JSON_NEED_DATA
 *   END                   JSON_NONE
 *   an error event        throws the SyntaxError (loc: p->loc file, the event position)
 *
 * returns the type, or JSON_ERROR with an exception pending. */
static int
js_jsonparser_next(JSContext* ctx, JsonParser* p) {
  JsonEvent ev;
  Location* at;

  switch(json_next(p, &ev)) {
    case JSON_PULL_END: return JSON_NONE;
    case JSON_PULL_AGAIN: return JSON_NEED_DATA;
    default: break;
  }

  if(ev.type != JSON_EVENT_ERROR)
    return ev.type;

  if((at = location_clone(p->loc, ctx))) {
    location_from_srcpos(at, &ev.at);
    js_throw_syntax_error(ctx, at, ev.span, ev.code ? ev.code : "unexpected-token", "%s", ev.str ? ev.str : "parse error");
    location_free(at, JS_GetRuntime(ctx));
  }

  return JSON_ERROR;
}

static JSValue
js_jsonparser_parse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JsonParser* p;
  BOOL skip = argc > 0 && JS_ToBool(ctx, argv[0]);
  int type;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_jsonparser_class_id)))
    return JS_EXCEPTION;

  if((type = js_jsonparser_next(ctx, p)) == JSON_ERROR)
    return JS_EXCEPTION;

  /* skipping: a token that starts a container is consumed up to its end */
  if(skip && (type == JSON_EVENT_OBJECT_START || type == JSON_EVENT_ARRAY_START)) {
    int depth = 1;

    while(depth > 0) {
      if((type = js_jsonparser_next(ctx, p)) == JSON_ERROR)
        return JS_EXCEPTION;

      if(type == JSON_EVENT_OBJECT_START || type == JSON_EVENT_ARRAY_START)
        depth++;
      else if(type == JSON_EVENT_OBJECT_END || type == JSON_EVENT_ARRAY_END)
        depth--;
    }
  }

  return JS_NewInt32(ctx, type);
}

enum {
  PARSER_POS,
  PARSER_TOKEN,
  PARSER_STATE,
  PARSER_DEPTH,
  PARSER_LOCATION,
  PARSER_COMMENTS,
};

static JSValue
js_jsonparser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonParser* p;
  JSValue ret = JS_UNDEFINED;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_jsonparser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PARSER_POS: {
      ret = JS_NewUint32(ctx, p->pos);
      break;
    }
    case PARSER_COMMENTS: {
      ret = JS_NewBool(ctx, p->comments);
      break;
    }
    case PARSER_TOKEN: {
      ret = dbuf_tostring(&p->token, ctx);
      break;
    }
    case PARSER_STATE: {
      ret = JS_NewInt32(ctx, p->state);
      break;
    }
    case PARSER_DEPTH: {
      ret = JS_NewUint32(ctx, p->stack.len);
      break;
    }
    case PARSER_LOCATION: {
      ret = js_location_wrap(ctx, p->loc);
      break;
    }
  }

  return ret;
}

static JSValue
js_jsonparser_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JsonParser* p;
  JSValue ret = JS_UNDEFINED;

  if(!(p = JS_GetOpaque2(ctx, this_val, js_jsonparser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PARSER_COMMENTS: {
      p->comments = JS_ToBool(ctx, value);
      break;
    }
  }

  return ret;
}

static JSValue
js_jsonparser_iterator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  JSValue ret = js_jsonparser_parse(ctx, this_val, argc, argv);

  *pdone = FALSE;

  if(JS_IsException(ret)) {
    *pdone = TRUE;
    ret = JS_GetException(ctx);
  } else if(js_toint32(ctx, ret) == JSON_NEED_DATA) {
    *pdone = TRUE;
    ret = JS_UNDEFINED;
  }

  return ret;
}

static void
js_jsonparser_finalizer(JSRuntime* rt, JSValue obj) {
  JsonParser* p;

  if((p = JS_GetOpaque(obj, js_jsonparser_class_id)))
    json_free(p, rt);
}

static const JSCFunctionListEntry js_jsonparser_proto_funcs[] = {
    JS_CFUNC_DEF("parse", 0, js_jsonparser_parse),
    JS_CGETSET_MAGIC_FLAGS_DEF("pos", js_jsonparser_get, 0, PARSER_POS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("token", js_jsonparser_get, 0, PARSER_TOKEN, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("state", js_jsonparser_get, 0, PARSER_STATE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("depth", js_jsonparser_get, 0, PARSER_DEPTH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("location", js_jsonparser_get, 0, PARSER_LOCATION, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("comments", js_jsonparser_get, js_jsonparser_set, PARSER_COMMENTS),
    JS_ITERATOR_NEXT_DEF("next", 0, js_jsonparser_iterator_next, 0),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_json_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonParser", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_jsonparser_static_props[] = {
    JS_PROP_INT32_DEF("NEED_DATA", JSON_NEED_DATA, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("NONE", JSON_NONE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("OBJECT", JSON_EVENT_OBJECT_START, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("OBJECT_END", JSON_EVENT_OBJECT_END, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ARRAY", JSON_EVENT_ARRAY_START, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ARRAY_END", JSON_EVENT_ARRAY_END, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("KEY", JSON_EVENT_KEY, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("STRING", JSON_EVENT_STRING, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TRUE", JSON_EVENT_TRUE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("FALSE", JSON_EVENT_FALSE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("NULL", JSON_EVENT_NULL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("NUMBER", JSON_EVENT_NUMBER, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("COMMENT", JSON_EVENT_COMMENT, JS_PROP_ENUMERABLE),
};

static JSClassDef js_jsonparser_class = {
    .class_name = "JsonParser",
    .finalizer = js_jsonparser_finalizer,
};

static int
js_json_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_jsonparser_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_jsonparser_class_id, &js_jsonparser_class);

  json_parser_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, json_parser_proto, js_jsonparser_proto_funcs, countof(js_jsonparser_proto_funcs));
  JS_SetPropertyFunctionList(ctx, json_parser_proto, js_jsonparser_static_props, countof(js_jsonparser_static_props));

  json_parser_ctor = JS_NewCFunction2(ctx, js_jsonparser_constructor, "JsonParser", 1, JS_CFUNC_constructor, 0);
  JS_SetPropertyFunctionList(ctx, json_parser_ctor, js_jsonparser_static_props, countof(js_jsonparser_static_props));

  JS_SetClassProto(ctx, js_jsonparser_class_id, json_parser_proto);
  JS_SetConstructor(ctx, json_parser_ctor, json_parser_proto);

  if(js_location_class_id == 0)
    js_location_init(ctx, 0);

  JS_NewClassID(&js_jsonpushparser_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_jsonpushparser_class_id, &js_jsonpushparser_class);

  json_pushparser_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, json_pushparser_proto, js_jsonpushparser_proto_funcs, countof(js_jsonpushparser_proto_funcs));

  json_pushparser_ctor = JS_NewCFunction2(ctx, js_jsonpushparser_constructor, "JsonPushParser", 0, JS_CFUNC_constructor, 0);
  JS_SetClassProto(ctx, js_jsonpushparser_class_id, json_pushparser_proto);
  JS_SetConstructor(ctx, json_pushparser_ctor, json_pushparser_proto);

  JS_NewClassID(&js_jsonserializer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_jsonserializer_class_id, &js_jsonserializer_class);

  json_serializer_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, json_serializer_proto, js_jsonserializer_proto_funcs, countof(js_jsonserializer_proto_funcs));

  json_serializer_ctor = JS_NewCFunction2(ctx, js_jsonserializer_constructor, "JsonSerializer", 1, JS_CFUNC_constructor, 0);
  JS_SetClassProto(ctx, js_jsonserializer_class_id, json_serializer_proto);
  JS_SetConstructor(ctx, json_serializer_ctor, json_serializer_proto);

  JS_NewClassID(&js_jsonwriter_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_jsonwriter_class_id, &js_jsonwriter_class);

  jsonwriter_ctor = JS_NewCFunction2(ctx, js_jsonwriter_constructor, "JsonWriter", 1, JS_CFUNC_constructor, 0);
  jsonwriter_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, jsonwriter_proto, js_jsonwriter_funcs, countof(js_jsonwriter_funcs));

  js_walk_register(js_jsonwriter_class_id, js_jsonwriter_walk);
  js_walk_define(ctx, jsonwriter_proto);

  JS_SetClassProto(ctx, js_jsonwriter_class_id, jsonwriter_proto);
  JS_SetConstructor(ctx, jsonwriter_ctor, jsonwriter_proto);

  if(m) {
    JSValue jsonl = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, jsonl, js_jsonl_funcs, countof(js_jsonl_funcs));
    JS_SetModuleExport(ctx, m, "JSONL", jsonl);
    JS_SetModuleExport(ctx, m, "JsonParser", json_parser_ctor);
    JS_SetModuleExport(ctx, m, "JsonPushParser", json_pushparser_ctor);
    JS_SetModuleExport(ctx, m, "JsonSerializer", json_serializer_ctor);
    JS_SetModuleExport(ctx, m, "JsonWriter", jsonwriter_ctor);
  }

  JS_SetModuleExportList(ctx, m, js_json_funcs, countof(js_json_funcs));
  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_json
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_json_init))) {
    JS_AddModuleExport(ctx, m, "JSONL");
    JS_AddModuleExport(ctx, m, "JsonParser");
    JS_AddModuleExport(ctx, m, "JsonPushParser");
    JS_AddModuleExport(ctx, m, "JsonSerializer");
    JS_AddModuleExport(ctx, m, "JsonWriter");
    JS_AddModuleExportList(ctx, m, js_json_funcs, countof(js_json_funcs));
  }

  return m;
}
