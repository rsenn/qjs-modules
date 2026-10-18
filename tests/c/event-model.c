/* event-model.c: sources and sinks of the Event model v2, engine-free pieces and the JS-bound ones.
 * built and run by ctest (`c-event-model`); exits non-zero on the first failed check.
 *
 * covered: json_next / xml_next events and positions, typed XML markup, jr_feed / xr_feed handlers,
 * tee, builder queue (take) and jbuild_value_js, walkers as pull sources (jwalk_event,
 * xwalk_event), the demand-driven serials, the JS object as a sink. */

#define _GNU_SOURCE 1

#include "json/jevent.h"
#include "json/jwrite.h"
#include "json/json.h"
#include "json/jwalk.h"
#include "json/jbuilder.h"
#include "json/jserial.h"
#include "json/jsink.h"
#include "xml/xevent.h"
#include "xml/xwrite.h"
#include "xml/xml.h"
#include "xml/xread.h"
#include "xml/xwalk.h"
#include "xml/xserial.h"
#include "xml/xsink.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) \
  do { \
    if(!(cond)) { \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++; \
    } \
  } while(0)

#define CHECK_STR(db, want) \
  do { \
    if(!((db).size == strlen(want) && !memcmp((db).buf, want, (db).size))) { \
      printf("FAIL %s:%d: got [%.*s] want [%s]\n", __FILE__, __LINE__, (int)(db).size, (db).buf, want); \
      failures++; \
    } \
  } while(0)

static JSContext* ctx;

/* ---- json: pull events and positions ---- */

static void
json_events(void) {
  const char* text = "{\"a\":[1,\"x\"]}";
  Reader rd = reader_from_bytes(text, strlen(text));
  JsonParser json;
  JsonEvent ev;
  char got[128] = "";
  int n = 0, cols[16];

  CHECK(json_init(&json, rd, "t.json", ctx));

  while(json_next(&json, &ev) == JSON_PULL_EVENT) {
    static const char* const names[] = {"E", "null", "true", "false", "N", "S", "[", "]", "{", "}", "K", "C", "R"};

    strcat(got, names[ev.type]);

    if(ev.str && ev.len) {
      strncat(got, ev.str, ev.len);
    }

    strcat(got, " ");
    CHECK(ev.has_at && ev.at.line == 1);
    cols[n++] = ev.at.column;
  }

  CHECK(!strcmp(got, "{ Ka [ N1 Sx ] } "));
  CHECK(n == 7);
  CHECK(cols[0] == 1 && cols[1] == 2 && cols[2] == 6 && cols[3] == 7 && cols[4] == 9 && cols[5] == 12 && cols[6] == 13);
  json_clear(&json, JS_GetRuntime(ctx));
}

static void
json_error_event(void) {
  const char* text = "[1, x, 2]";
  Reader rd = reader_from_bytes(text, strlen(text));
  JsonParser json;
  JsonEvent ev;
  int errors = 0, numbers = 0;

  CHECK(json_init(&json, rd, "t.json", ctx));

  while(json_next(&json, &ev) == JSON_PULL_EVENT) {
    if(ev.type == JSON_EVENT_ERROR) {
      errors++;
      CHECK(ev.has_at && ev.at.column >= 5 && ev.len > 0);
    }

    numbers += ev.type == JSON_EVENT_NUMBER;
  }

  CHECK(errors == 1 && numbers == 2);
  json_clear(&json, JS_GetRuntime(ctx));
}

/* ---- json: push handler, tee, builder queue ---- */

static void
json_push_tee_build(void) {
  const char* text = "{\"a\":[1,2]} 3 [4]";
  DynBuf db;
  JsonWriter wr = {.writer = writer_from_dynbuf(&db), .opts = {.minify = 1}};
  JsonBuilder b;
  JsonEventSink sinks[2];
  JsonTee tee = {sinks, 2};
  JsonEventSink sink;
  jr_state_t st;
  JSValue roots, v;
  int32_t len;

  dbuf_init(&db);
  jwrite_init(&wr, NULL, NULL);
  jbuild_init(&b, ctx);
  sinks[0] = jbuild_sink(&b);
  sinks[1] = jwrite_sink(&wr);
  sink = jtee_sink(&tee);

  jr_state_init_flags(&st, JR_JSONL);

  for(size_t i = 0; i < strlen(text); i += 3)
    CHECK(jr_feed(&st, text + i, strlen(text) - i < 3 ? strlen(text) - i : 3, jevent_handler, &sink) >= 0);

  CHECK(jr_end(&st, jevent_handler, &sink) >= 0);

  roots = jbuild_take(&b);
  v = JS_GetPropertyStr(ctx, roots, "length");
  JS_ToInt32(ctx, &len, v);
  CHECK(len == 3);
  JS_FreeValue(ctx, v);
  JS_FreeValue(ctx, roots);

  roots = jbuild_take(&b); /* the queue was cleared */
  v = JS_GetPropertyStr(ctx, roots, "length");
  JS_ToInt32(ctx, &len, v);
  CHECK(len == 0);
  JS_FreeValue(ctx, v);
  JS_FreeValue(ctx, roots);

  jbuild_value_js(&b, JS_NewInt32(ctx, 9));
  roots = jbuild_take(&b);
  v = JS_GetPropertyUint32(ctx, roots, 0);
  JS_ToInt32(ctx, &len, v);
  CHECK(len == 9);
  JS_FreeValue(ctx, v);
  JS_FreeValue(ctx, roots);

  jr_state_free(&st);
  jbuild_free(&b, JS_GetRuntime(ctx));
  jwrite_free(&wr);
  dbuf_free(&db);
}

/* ---- json: walker as source, into a writer, through the serial ---- */

static JSValue
eval(const char* code) {
  return JS_Eval(ctx, code, strlen(code), "<test>", JS_EVAL_TYPE_GLOBAL);
}

static void
json_walker(void) {
  const char* code = "({a: [1, 'x\\u00e9', null, true, NaN, undefined, 1e21], b: {c: -0, d: JSON.rawJSON('1e400')}, e: undefined})";
  const char* want = "{\"a\":[1,\"x\xc3\xa9\",null,true,null,null,1e+21],\"b\":{\"c\":0,\"d\":1e400}}";
  JSValue v = eval(code);
  JwalkSource src;
  DynBuf db;
  JsonWriter wr = {.writer = writer_from_dynbuf(&db), .opts = {.minify = 1}};
  JsonEventSink sink;

  dbuf_init(&db);
  jwrite_init(&wr, NULL, NULL);
  sink = jwrite_sink(&wr);
  CHECK(jwalk_source_init(&src, ctx, v) == 0);
  CHECK(jevent_pump(jwalk_source(&src), &sink) == JSON_PULL_END);
  CHECK_STR(db, want);
  jwalk_source_close(&src, ctx);
  jwrite_free(&wr);
  dbuf_free(&db);

  /* the serial gives the same bytes at every read size, and runs the source once */
  for(size_t step = 1; step <= 4096; step *= 7) {
    JsonSerial s;
    char buf[4096];
    DynBuf out;
    ssize_t n;

    dbuf_init(&out);
    CHECK(jwalk_source_init(&src, ctx, v) == 0);
    jserial_init(&s, jwalk_source(&src), (JsonWriteOptions){.minify = 1}, JS_GetRuntime(ctx));

    while((n = jserial_read(&s, buf, step, 0)) > 0)
      dbuf_put(&out, (const uint8_t*)buf, n);

    CHECK(n == 0);
    CHECK_STR(out, want);
    jserial_free(&s);
    jwalk_source_close(&src, ctx);
    dbuf_free(&out);
  }

  JS_FreeValue(ctx, v);
}

static void
json_walker_throws(void) {
  JSValue v = eval("({a: 1n})");
  JwalkSource src;
  JsonEvent ev;
  JsonPull st;

  CHECK(jwalk_source_init(&src, ctx, v) == 0);

  while((st = jwalk_event(&src, &ev)) == JSON_PULL_EVENT) {}

  CHECK(st == JSON_PULL_FAIL && JS_HasException(ctx));
  JS_FreeValue(ctx, JS_GetException(ctx));
  jwalk_source_close(&src, ctx);
  JS_FreeValue(ctx, v);
}

/* ---- json: a JS object as the sink ---- */

static void
json_js_sink(void) {
  JSValue obj = eval("({log: [], objectStart() { this.log.push('{'); }, key(t) { this.log.push('k' + t); },"
                     " number(t) { this.log.push('n' + t); }, objectEnd() { this.log.push('}'); }})");
  const char* text = "{\"a\":1e400}";
  JsonEventSink sink;
  JsonJsSink* js = js_jsonsink_from_js(ctx, obj, &sink);
  jr_state_t st;
  JSValue log, joined;
  const char* s;

  CHECK(js != NULL);
  jr_state_init(&st);
  CHECK(jr_feed(&st, text, strlen(text), jevent_handler, &sink) >= 0);
  jr_end(&st, jevent_handler, &sink);
  log = JS_GetPropertyStr(ctx, obj, "log");
  joined = JS_Invoke(ctx, log, JS_NewAtom(ctx, "join"), 0, NULL);
  s = JS_ToCString(ctx, joined);
  CHECK(s && !strcmp(s, "{,ka,n1e400,}"));
  JS_FreeCString(ctx, s);
  JS_FreeValue(ctx, joined);
  JS_FreeValue(ctx, log);
  jr_state_free(&st);
  js_jsonsink_from_js_free(ctx, js);
  JS_FreeValue(ctx, obj);
}

/* ---- xml: typed markup, handlers, walkers, serial ---- */

static void
xml_typed(void) {
  const char* text = "<?xml version=\"1.0\"?><!DOCTYPE html><!-- c --><a id=\"7\"><![CDATA[x<y]]>t</a>";
  Reader rd = reader_from_bytes(text, strlen(text));
  XMLParser xp;
  XmlEvent ev;
  char got[256] = "";
  XmlPull st;

  xml_init(&xp, &rd);

  while((st = xml_next(&xp, &ev)) == XML_PULL_EVENT) {
    static const char* const names[] = {"", "<", "@", ">", "T", "C", "D", "P", "X"};

    strcat(got, names[ev.type]);
    strncat(got, ev.str ? ev.str : "", ev.str ? ev.len : 0);

    if(ev.type == XE_ATTRIBUTE && ev.value) {
      strcat(got, "=");
      strncat(got, ev.value, ev.vlen);
    }

    strcat(got, "|");
  }

  CHECK(st == XML_PULL_END);
  CHECK(!strcmp(got, "Pxml version=\"1.0\"|DDOCTYPE html|C c |<a|@id=7|Xx<y|Tt|>a|"));
  xml_free(&xp);
}

static void
xml_pull_serial(void) {
  const char* text = "<?xml version=\"1.0\"?><a id=\"7\"><!-- c -->x &amp; y<b/></a>";
  const char* want = "<?xml version=\"1.0\"?><a id=\"7\"><!-- c -->x &amp; y<b /></a>";

  for(size_t step = 1; step <= 4096; step *= 7) {
    Reader rd = reader_from_bytes(text, strlen(text));
    XMLParser xp;
    XmlSerial s;
    char buf[4096];
    DynBuf out;
    ssize_t n;

    dbuf_init(&out);
    xml_init(&xp, &rd);
    xserial_init(&s, xml_source(&xp), (XmlWriteOptions){.layout = XW_COMPACT}, JS_GetRuntime(ctx));

    while((n = xserial_read(&s, buf, step, 0)) > 0)
      dbuf_put(&out, (const uint8_t*)buf, n);

    CHECK(n == 0);
    CHECK_STR(out, want);
    xserial_free(&s);
    xml_free(&xp);
    dbuf_free(&out);
  }
}

static void
xml_push_handler(void) {
  const char* text = "<a id=\"7\">t<b/></a>";
  DynBuf db;
  XmlWriter wr = {.writer = writer_from_dynbuf(&db), .opts = {.layout = XW_COMPACT}};
  XmlEventSink sink;
  xr_state_t st;

  dbuf_init(&db);
  xwrite_init(&wr, NULL, NULL);
  sink = xwrite_sink(&wr);
  xr_state_init(&st);

  for(size_t i = 0; i < strlen(text); i += 2)
    CHECK(xr_feed(&st, text + i, strlen(text) - i < 2 ? strlen(text) - i : 2, xevent_handler, &sink) >= 0);

  CHECK(xr_end(&st, xevent_handler, &sink) >= 0);
  CHECK_STR(db, "<a id=\"7\">t<b /></a>");
  xr_state_free(&st);
  xwrite_free(&wr);
  dbuf_free(&db);
}

static void
xml_walk_to_string(const char* code, int flags, const char* want) {
  JSValue v = eval(code);
  XwalkSource src;
  DynBuf db;
  XmlWriter wr = {.writer = writer_from_dynbuf(&db), .opts = {.layout = XW_COMPACT}};
  XmlEventSink sink;

  dbuf_init(&db);
  xwrite_init(&wr, NULL, NULL);
  sink = xwrite_sink(&wr);
  CHECK(xwalk_source_init(&src, ctx, v, flags, NULL) == 0);
  CHECK(xevent_pump(xwalk_source(&src), &sink) == XML_PULL_END);
  CHECK(xwrite_finish(&wr) == 0);
  CHECK_STR(db, want);
  xwalk_source_close(&src, ctx);
  xwrite_free(&wr);
  dbuf_free(&db);
  JS_FreeValue(ctx, v);
}

static void
xml_walker(void) {
  xml_walk_to_string("[{tagName: 'a', attributes: {id: '7', b: true}, children: ['x < y', {tagName: 'b'}, {tagName: '!-- c --'}]}]",
                     0,
                     "<a id=\"7\" b>x &lt; y<b /><!-- c --></a>");
  xml_walk_to_string("[{tagName: '?xml', attributes: {version: '1.0'}}, {tagName: 'a', children: []}]", 0, "<?xml version=\"1.0\"?><a />");
  /* flat list: a start waits for its marker, void elements are ended by BALANCED */
  xml_walk_to_string("[{tagName: 'p'}, 'hi', {tagName: 'br'}, 'x', {tagName: '/p'}, {tagName: 'i'}, {tagName: '/i'}]",
                     XWALK_FLAT | XWALK_BALANCED,
                     "<p>hi<br>x</br></p><i />");
  /* max_depth cuts the children, the element at the cut still gets its end */
  {
    JSValue v = eval("[{tagName: 'a', children: [{tagName: 'b', children: ['t']}]}]");
    XwalkSource src;
    XmlEvent ev;
    int ends = 0;

    CHECK(xwalk_source_init(&src, ctx, v, 0, NULL) == 0);
    src.max_depth = 2;

    while(xwalk_event(&src, &ev) == XML_PULL_EVENT)
      ends += ev.type == XE_ELEMENT_END;

    CHECK(ends == 2);
    xwalk_source_close(&src, ctx);
    JS_FreeValue(ctx, v);
  }
}

static void
xml_js_sink(void) {
  JSValue obj = eval("({log: [], elementStart(n) { this.log.push('<' + n); }, attribute(n, v) { this.log.push('@' + n + '=' + v); },"
                     " text(t) { this.log.push('T' + t); }, elementEnd(n) { this.log.push('>' + n); }})");
  Reader rd = reader_from_bytes("<a b c='1'>t</a>", 16);
  XMLParser xp;
  XmlEventSink sink;
  XmlJsSink* js = js_xmlsink_from_js(ctx, obj, &sink);
  JSValue log, joined;
  const char* s;

  CHECK(js != NULL);
  xml_init(&xp, &rd);
  CHECK(xevent_pump(xml_source(&xp), &sink) == XML_PULL_END);
  log = JS_GetPropertyStr(ctx, obj, "log");
  joined = JS_Invoke(ctx, log, JS_NewAtom(ctx, "join"), 0, NULL);
  s = JS_ToCString(ctx, joined);
  CHECK(s && !strcmp(s, "<a,@b=undefined,@c=1,Tt,>a"));
  JS_FreeCString(ctx, s);
  JS_FreeValue(ctx, joined);
  JS_FreeValue(ctx, log);
  xml_free(&xp);
  js_xmlsink_from_js_free(ctx, js);
  JS_FreeValue(ctx, obj);
}

/* ---- error events carry code and span, whichever way the source delivers them ---- */

typedef struct {
  int errors;
  char code[32];
  size_t span;
  uint32_t line, column;
} ErrorInfo;

static ssize_t
json_take_error(void* user, const JsonEvent* ev) {
  ErrorInfo* e = user;

  if(ev->type == JSON_EVENT_ERROR) {
    e->errors++;
    snprintf(e->code, sizeof(e->code), "%s", ev->code ? ev->code : "(null)");
    e->span = ev->span;
    e->line = ev->at.line;
    e->column = ev->at.column;
  }

  return 0;
}

static ssize_t
xml_take_error(void* user, const XmlEvent* ev) {
  ErrorInfo* e = user;

  if(ev->type == XE_ERROR) {
    e->errors++;
    snprintf(e->code, sizeof(e->code), "%s", ev->code ? ev->code : "(null)");
    e->span = ev->span;
    e->line = ev->at.line;
    e->column = ev->at.column;
  }

  return 0;
}

static void
json_error_info(void) {
  const char* texts[] = {"[1, x, 2]", "[1, 2"};
  const char* codes[] = {"unexpected-token", "unexpected-end"};
  size_t spans[] = {1, 0};

  for(int t = 0; t < 2; t++) {
    const char* text = texts[t];
    size_t n = strlen(text);
    ErrorInfo pull = {0};
    Reader rd = reader_from_bytes(text, n);
    JsonParser json;
    JsonEvent ev;

    /* pull */
    CHECK(json_init(&json, rd, "t.json", ctx));
    json.flags = JR_JSON; /* strict: only then does the end of the input break a document */

    while(json_next(&json, &ev) == JSON_PULL_EVENT)
      json_take_error(&pull, &ev);

    while(json_next_end(&json, &ev) == JSON_PULL_EVENT) /* the end of the input */
      json_take_error(&pull, &ev);

    CHECK(pull.errors == 1 && !strcmp(pull.code, codes[t]) && pull.span == spans[t]);
    json_clear(&json, JS_GetRuntime(ctx));

    /* push, at several chunk sizes: the same code, span and position as the pull parser */
    for(size_t step = 1; step <= n; step += (step < 3 ? 1 : n)) {
      ErrorInfo push = {0};
      jr_state_t st;

      jr_state_init_flags(&st, JR_JSON);

      for(size_t i = 0; i < n; i += step)
        jr_feed(&st, text + i, n - i < step ? n - i : step, json_take_error, &push);

      jr_end(&st, json_take_error, &push);
      CHECK(push.errors >= 1 && !strcmp(push.code, codes[t]));
      CHECK(push.span == spans[t] && push.line == pull.line && push.column == pull.column);
      jr_state_free(&st);
    }
  }
}

static void
xml_error_info(void) {
  /* pull: a mismatched end tag, "</b>" is 4 bytes */
  {
    const char* text = "<a></b>";
    Reader rd = reader_from_bytes(text, strlen(text));
    XMLParser xp;
    XmlEvent ev;
    ErrorInfo e = {0};

    xml_init(&xp, &rd);

    while(xml_next(&xp, &ev) == XML_PULL_EVENT)
      xml_take_error(&e, &ev);

    CHECK(e.errors == 1 && !strcmp(e.code, "mismatched-end-tag") && e.span == 4);
    xml_free(&xp);
  }

  /* push: input that ends inside a tag, at chunk sizes 1 and whole */
  for(size_t step = 1; step <= 2; step++) {
    const char* text = "<a><b";
    size_t n = strlen(text);
    ErrorInfo e = {0};
    xr_state_t st;

    xr_state_init(&st);

    for(size_t i = 0; i < n; i += (step == 1 ? 1 : n))
      xr_feed(&st, text + i, step == 1 ? 1 : n - i, xml_take_error, &e);

    xr_end(&st, xml_take_error, &e);
    CHECK(e.errors == 1 && !strcmp(e.code, "unexpected-end") && e.span == 0);
    xr_state_free(&st);
  }
}

static void
dialects(void) {
  int f;

  CHECK(jr_flags_from_name("json5", 5, &f) == 0 && f == JR_JSON5);
  CHECK(jr_flags_from_name("jsonl", 5, &f) == 0 && f == JR_JSONL);
  CHECK(jr_flags_from_name("jsonx", 5, &f) == -1);
}

int
main(void) {
  JSRuntime* rt = JS_NewRuntime();

  ctx = JS_NewContext(rt);

  json_events();
  json_error_event();
  json_push_tee_build();
  json_walker();
  json_walker_throws();
  json_js_sink();
  xml_typed();
  xml_pull_serial();
  xml_push_handler();
  xml_walker();
  xml_js_sink();
  dialects();
  json_error_info();
  xml_error_info();

  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  if(failures)
    printf("%d failed\n", failures);

  return failures != 0;
}
