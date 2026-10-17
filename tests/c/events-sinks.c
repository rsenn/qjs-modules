/* events-sinks.c: jevent_send()/xevent_send() through the writer sinks.
 * built and run by ctest (`c-events-sinks`); exits non-zero on the first failed check. */

#define _GNU_SOURCE 1

#include "json/jevent.h"
#include "json/jwrite.h"
#include "xml/xevent.h"
#include "xml/xwrite.h"
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

static int
equals(DynBuf* db, const char* s) {
  return db->size == strlen(s) && !memcmp(db->buf, s, db->size);
}

int
main(void) {
  DynBuf db;
  JsonEvent je[] = {
      {.type = jr_type_object_start},
      {.type = jr_type_key, .str = "id", .len = 2},
      {.type = jr_type_number, .str = "7", .len = 1},
      {.type = jr_type_key, .str = "t", .len = 1},
      {.type = jr_type_array_start},
      {.type = jr_type_true},
      {.type = jr_type_null},
      {.type = jr_type_string, .str = "x", .len = 1},
      {.type = jr_type_array_end},
      {.type = jr_type_object_end},
  };
  XmlEvent xe[] = {
      {.type = XE_ELEMENT_START, .name = "a", .nlen = 1},
      {.type = XE_ATTRIBUTE, .name = "id", .nlen = 2, .value = "7", .vlen = 1},
      {.type = XE_ATTRIBUTE, .name = "b", .nlen = 1},
      {.type = XE_TEXT, .name = "x < y", .nlen = 5},
      {.type = XE_COMMENT, .name = "c", .nlen = 1},
      {.type = XE_ELEMENT_END, .name = "a", .nlen = 1},
  };

  /* JSON: the events of {"id":7,"t":[true,null,"x"]} reach a JsonWriter through its sink */
  dbuf_init(&db);
  {
    JsonWriter wr = {.writer = writer_from_dynbuf(&db), .opts = {.minify = 1}};
    JsonEventSink sink;

    jwrite_init(&wr, NULL, NULL);
    sink = jwrite_sink(&wr);

    for(size_t i = 0; i < sizeof(je) / sizeof(je[0]); i++)
      CHECK(jevent_send(&sink, &je[i]) >= 0);

    CHECK(equals(&db, "{\"id\":7,\"t\":[true,null,\"x\"]}"));

    /* a NULL member drops the event */
    sink.number = NULL;
    CHECK(jevent_send(&sink, &(JsonEvent){.type = jr_type_number, .str = "1", .len = 1}) == 0);
    jwrite_free(&wr); /* also frees the dynbuf behind the writer */
  }

  /* XML: <a id="7" b>x &lt; y<!--c--></a> */
  dbuf_init(&db);
  {
    XmlWriter wr = {.writer = writer_from_dynbuf(&db), .opts = {.layout = XW_COMPACT}};
    XmlEventSink sink;

    xwrite_init(&wr, NULL, NULL);
    sink = xwrite_sink(&wr);

    for(size_t i = 0; i < sizeof(xe) / sizeof(xe[0]); i++)
      CHECK(xevent_send(&sink, &xe[i]) >= 0);

    CHECK(equals(&db, "<a id=\"7\" b>x &lt; y<!--c--></a>"));
    CHECK(xwrite_finish(&wr) == 0);

    /* a mismatching end is an error code of the sink, an error event writes nothing */
    CHECK(xevent_send(&sink, &(XmlEvent){.type = XE_ELEMENT_END, .name = "b", .nlen = 1}) < 0);
    CHECK(xevent_send(&sink, &(XmlEvent){.type = XE_ERROR, .name = "e", .nlen = 1}) == 0);
    sink.text = NULL;
    CHECK(xevent_send(&sink, &(XmlEvent){.type = XE_TEXT, .name = "t", .nlen = 1}) == 0);
    xwrite_free(&wr);
  }

  /* xwrite_error_code() names, xwrite_set_indent() layouts */
  CHECK(strcmp(xwrite_error_code(XWRITE_E_MISMATCH), "mismatched-end-tag") == 0);
  CHECK(strcmp(xwrite_error_code(XWRITE_E_NOT_IN_TAG), "attribute-outside-tag") == 0);
  CHECK(strcmp(xwrite_error_code(999), "") == 0);
  {
    XmlWriter wr = {0};

    xwrite_set_indent(&wr, -1);
    CHECK(wr.opts.layout == XW_COMPACT && wr.opts.indent == 0);
    xwrite_set_indent(&wr, 4);
    CHECK(wr.opts.layout == XW_PRETTY && wr.opts.indent == 4);
    xwrite_set_indent(&wr, 5000);
    CHECK(wr.opts.indent == 1023);
  }

  if(failures)
    return 1;

  puts("ok");
  return 0;
}
