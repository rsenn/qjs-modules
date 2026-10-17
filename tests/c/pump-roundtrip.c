/* pump-roundtrip.c: parser to writer through the sinks, at several chunk sizes.
 * built and run by ctest (`c-pump-roundtrip`); exits non-zero on the first failed check. */

#define _GNU_SOURCE 1

#include "json/jevent.h"
#include "json/jwrite.h"
#include "json/json.h"
#include "xml/xevent.h"
#include "xml/xwrite.h"
#include "xml/xml.h"
#include "xml/xread.h"
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

static const char json_text[] = "{\"a\":[1,\"x\\ny\",true,false,null,-2.5e3],\"b\":{},\"c\":[]}";
static const char xml_text[] = "<a id=\"7\" b><b/>t &amp; u<c x='1'></c></a>";
static const char xml_want[] = "<a id=\"7\" b><b />t &amp; u<c x=\"1\" /></a>";

static int
equals(DynBuf* db, const char* s) {
  return db->size == strlen(s) && !memcmp(db->buf, s, db->size);
}

static void
json_push(size_t step) {
  DynBuf db;
  JsonWriter wr = {.writer = writer_from_dynbuf(&db), .opts = {.minify = 1}};
  JsonEventSink sink;
  jr_state_t st;
  size_t n = strlen(json_text);

  dbuf_init(&db);
  jwrite_init(&wr, NULL, NULL);
  sink = jwrite_sink(&wr);
  jr_state_init(&st);

  for(size_t i = 0; i < n; i += step)
    CHECK(jread_pump(&st, &sink, json_text + i, n - i < step ? n - i : step) == 0);

  CHECK(jread_pump_finish(&st, &sink) == 0);
  CHECK(equals(&db, json_text));
  jr_state_free(&st);
  jwrite_free(&wr);
}

static void
json_pull(JSContext* ctx) {
  DynBuf db;
  JsonWriter wr = {.writer = writer_from_dynbuf(&db), .opts = {.minify = 1}};
  JsonEventSink sink;
  JsonParser json;
  ssize_t r;

  dbuf_init(&db);
  jwrite_init(&wr, NULL, NULL);
  sink = jwrite_sink(&wr);

  CHECK(json_init(&json, reader_from_bytes(json_text, strlen(json_text)), "t.json", ctx));
  r = json_pump(&json, &sink);
  CHECK(r == JSON_PUMP_AGAIN); /* the parser cannot tell the end of the input from a pause */
  CHECK(equals(&db, json_text));
  json_clear(&json, JS_GetRuntime(ctx)); /* json_free() would free the struct itself */
  jwrite_free(&wr);
}

/* an error reaches the sink's `error` member once; the parser resyncs and goes on */
static ssize_t
count_error(void* opaque, const char* str, size_t len) {
  ++*(int*)opaque;
  return 0;
}

static void
json_error(void) {
  int errors = 0;
  JsonEventSink sink = {.opaque = &errors, .error = count_error};
  jr_state_t st;
  const char* text = "[1, x, 2]";

  jr_state_init(&st);
  CHECK(jread_pump(&st, &sink, text, strlen(text)) == 0);
  CHECK(errors == 1);
  CHECK(st.err_pos.column == 5 && st.err_code == JR_E_UNEXPECTED_TOKEN);
  jr_state_free(&st);
}

static void
xml_push(size_t step) {
  DynBuf db;
  XmlWriter wr = {.writer = writer_from_dynbuf(&db), .opts = {.layout = XW_COMPACT}};
  XmlEventSink sink;
  xr_state_t st;
  size_t n = strlen(xml_text);

  dbuf_init(&db);
  xwrite_init(&wr, NULL, NULL);
  sink = xwrite_sink(&wr);
  xr_state_init(&st);

  for(size_t i = 0; i < n; i += step)
    CHECK(xread_pump(&st, &sink, xml_text + i, n - i < step ? n - i : step) == 0);

  CHECK(xread_pump_finish(&st, &sink) == 0);
  CHECK(equals(&db, xml_want));
  xr_state_free(&st);
  xwrite_free(&wr);
}

static void
xml_pull(void) {
  DynBuf db;
  XmlWriter wr = {.writer = writer_from_dynbuf(&db), .opts = {.layout = XW_COMPACT}};
  XmlEventSink sink;
  Reader rd = reader_from_bytes(xml_text, strlen(xml_text));
  XMLParser xp;

  dbuf_init(&db);
  xwrite_init(&wr, NULL, NULL);
  sink = xwrite_sink(&wr);
  xml_init(&xp, &rd);

  CHECK(xml_pump(&xp, &sink) == XML_PUMP_END);
  CHECK(equals(&db, xml_want));
  xml_free(&xp);
  xwrite_free(&wr);
}

/* a sink that fails on the second element stops the pump with that failure */
static ssize_t
fail_element_start(void* opaque, const char* name, size_t len) {
  return ++*(int*)opaque == 2 ? -42 : 0;
}

static void
xml_sink_failure(void) {
  int calls = 0;
  XmlEventSink sink = {.opaque = &calls, .element_start = fail_element_start};
  Reader rd = reader_from_bytes("<a><b/><c/></a>", 15);
  XMLParser xp;

  xml_init(&xp, &rd);
  CHECK(xml_pump(&xp, &sink) == -42);
  CHECK(calls == 2);
  xml_free(&xp);
}

int
main(void) {
  JSRuntime* rt = JS_NewRuntime();
  JSContext* ctx = JS_NewContext(rt);

  json_error();

  for(size_t step = 1; step <= 9; step += 3) {
    json_push(step);
    xml_push(step);
  }

  json_push(1000);
  xml_push(1000);
  json_pull(ctx);
  xml_pull();
  xml_sink_failure();

  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  if(failures)
    return 1;

  puts("ok");
  return 0;
}
