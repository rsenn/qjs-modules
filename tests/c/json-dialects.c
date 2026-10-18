/* json-dialects.c: the syntax flags of the pull parser src/json/json.c, against jread.
 * every case runs through both parsers (the pull parser with reads of 1, 3 and 16384 bytes);
 * the events must be equal up to the first error, and the first error must have the same code.
 * built and run by ctest (`c-json-dialects`); exits non-zero on the first failed check. */

#include "json/json.h"
#include "json/jread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

typedef struct {
  char out[1024];
  jr_state_t st;
} Run;

static void
put(Run* r, const char* s) {
  size_t n = strlen(r->out);

  if(n + strlen(s) + 2 < sizeof(r->out)) {
    if(n)
      r->out[n++] = ' ';

    strcpy(r->out + n, s);
  }
}

static void
emit(Run* r, const char* kind, const char* p, size_t n) {
  char buf[512];

  snprintf(buf, sizeof(buf), "%s:%.*s", kind, (int)n, p ? p : "");
  put(r, buf);
}

static ssize_t
cb(void* user, const JsonEvent* ev) {
  JsonEventType type = ev->type;
  Run* r = user;

  switch(type) {
    case JSON_EVENT_NULL: put(r, "null"); break;
    case JSON_EVENT_TRUE: put(r, "true"); break;
    case JSON_EVENT_FALSE: put(r, "false"); break;
    case JSON_EVENT_ARRAY_START: put(r, "["); break;
    case JSON_EVENT_ARRAY_END: put(r, "]"); break;
    case JSON_EVENT_OBJECT_START: put(r, "{"); break;
    case JSON_EVENT_OBJECT_END: put(r, "}"); break;
    case JSON_EVENT_NUMBER: emit(r, "n", ev->str, ev->len); break;
    case JSON_EVENT_STRING: emit(r, "s", ev->str, ev->len); break;
    case JSON_EVENT_KEY: emit(r, "k", ev->str, ev->len); break;
    case JSON_EVENT_COMMENT: break;
    case JSON_EVENT_ERROR: emit(r, "E", jr_error_name(r->st.err_code), strlen(jr_error_name(r->st.err_code))); break;
  }

  return 0;
}

static void
run_push(Run* r, uint8_t flags, const char* s) {
  memset(r, 0, sizeof(*r));
  jr_state_init_flags(&r->st, flags);
  jr_feed(&r->st, s, strlen(s), cb, r);
  jr_end(&r->st, cb, r);
  jr_state_free(&r->st);
}

typedef struct {
  const char* data;
  size_t len, pos, step;
} Feed;

static ssize_t
feed_read(intptr_t fd, void* buf, size_t n, Reader* rd) {
  Feed* f = rd->opaque;
  size_t k = f->len - f->pos;

  (void)fd;

  if(k > f->step)
    k = f->step;
  if(k > n)
    k = n;

  memcpy(buf, f->data + f->pos, k);
  f->pos += k;
  return (ssize_t)k;
}

static void
run_pull(Run* r, JSContext* ctx, uint8_t flags, const char* s, size_t step) {
  Feed feed = {s, strlen(s), 0, step};
  Reader rd = {&feed_read, &feed, NULL, NULL};
  JsonParser json;

  memset(r, 0, sizeof(*r));
  json_init(&json, rd, "t", ctx);
  json.flags = flags;

  for(int guard = 0; guard < 10000; guard++) {
    int t = json_parse(&json);
    char buf[512];

    if(t == JSON_NEED_DATA) {
      /* json_end() answers once per pending event, until JSON_NONE */
      t = json_end(&json);

      if(t == JSON_NONE)
        break;
    }

    if(t == JSON_NONE)
      break;

    switch(t) {
      case JSON_ERROR: emit(r, "E", json.err_code, json.err_code ? strlen(json.err_code) : 0); break;
      case JSON_EVENT_NULL: put(r, "null"); break;
      case JSON_EVENT_TRUE: put(r, "true"); break;
      case JSON_EVENT_FALSE: put(r, "false"); break;
      case JSON_EVENT_ARRAY_START: put(r, "["); break;
      case JSON_EVENT_ARRAY_END: put(r, "]"); break;
      case JSON_EVENT_OBJECT_START: put(r, "{"); break;
      case JSON_EVENT_OBJECT_END: put(r, "}"); break;
      case JSON_EVENT_NUMBER: emit(r, "n", (const char*)json.token.buf, json.token.size); break;
      case JSON_EVENT_STRING: emit(r, "s", (const char*)json.token.buf, json.token.size); break;
      case JSON_EVENT_KEY: emit(r, "k", (const char*)json.token.buf, json.token.size); break;
      default: snprintf(buf, sizeof(buf), "?%d", t); put(r, buf); break;
    }
  }

  json_clear(&json, JS_GetRuntime(ctx));
}

/* the text of `out` up to and including the first error */
static void
upto_error(char* out) {
  char* e = strstr(out, "E:");

  if(e) {
    char* sp = strchr(e, ' ');

    if(sp)
      *sp = '\0';
  }
}

static void
check(JSContext* ctx, uint8_t flags, const char* s) {
  static const size_t steps[] = {1, 3, 16384};
  Run a;

  run_push(&a, flags, s);
  upto_error(a.out);

  for(size_t k = 0; k < 3; k++) {
    Run b;

    run_pull(&b, ctx, flags, s, steps[k]);
    upto_error(b.out);

    if(strcmp(a.out, b.out)) {
      printf("FAIL flags=%u step=%zu input=%s\n  jread %s\n  json  %s\n", (unsigned)flags, steps[k], s, a.out, b.out);
      failures++;
      return;
    }
  }
}

int
main(int argc, char** argv) {
  JSRuntime* rt = JS_NewRuntime();
  JSContext* ctx = JS_NewContext(rt);

  if(argc > 2) { /* ad-hoc: json-dialects FLAGS INPUT */
    Run r;

    run_pull(&r, ctx, (unsigned)atoi(argv[1]), argv[2], 16384);
    printf("%s\n", r.out);
    return 0;
  }

  static const struct {
    uint8_t flags;
    const char* s;
  } cases[] = {
      {JR_JSON, "{\"a\":[1,-2.5e+3,true,null,\"x\\n\"]}"},
      {JR_JSON, "  \"s\"  "},
      {JR_JSON, "1e5"},
      {JR_JSON, "0"},
      {JR_JSON, "-0.5"},
      {JR_JSON, "[1,]"},
      {JR_JSON, "{\"a\":1,}"},
      {JR_JSON, "{\"a\":}"},
      {JR_JSON, "[,1]"},
      {JR_JSON, "[1,,2]"},
      {JR_JSON, "// c\n1"},
      {JR_JSON, "[01]"},
      {JR_JSON, "[1.]"},
      {JR_JSON, "[.5]"},
      {JR_JSON, "[+1]"},
      {JR_JSON, "[1e]"},
      {JR_JSON, "[-]"},
      {JR_JSON, "[0x10]"},
      {JR_JSON, "[NaN]"},
      {JR_JSON, "['a']"},
      {JR_JSON, "{a:1}"},
      {JR_JSON, "1 2"},
      {JR_JSON, "{} x"},
      {JR_JSON, "[1,2]x"},
      {JR_JSON, "[1}"},
      {JR_JSON, "]"},
      {JR_JSON, "[1"},
      {JR_JSON, "\"abc"},
      {JR_JSON, "\"a\nb\""},
      {JR_JSON, ""},
      {JR_JSON, "  "},
      {JR_JSON, "-"},
      {JR_JSONC, "// c\n{\"a\":1, /* x */ \"b\":[1,2,],}"},
      {JR_JSONC, "[1,,2]"},
      {JR_JSONC, "[1] /* open"},
      {JR_JSONC, "1 // tail"},
      {JR_JSONC, "['a']"},
      {JR_JSON5, "{a:1, $b_:'x', \"c\":[+1, .5, 5., 0x1F, -0xff, Infinity, -Infinity, +Infinity, NaN, 1e3,],}"},
      {JR_JSON5, "[-NaN, -.5, +.5e-2, 1.e3]"},
      {JR_JSON5, "'it\\'s \"q\"'"},
      {JR_JSON5, "\"a\\\nb\""},
      {JR_JSON5, "\"a\\\r\nb\""},
      {JR_JSON5, "\"a\\\xe2\x80\xa8" "b\""},
      {JR_JSON5, "'\\x41\\u0042\\v\\z\\0'"},
      {JR_JSON5, "'\\01'"},
      {JR_JSON5, "\"tab\there\""},
      {JR_JSON5, "\"a\nb\""},
      {JR_JSON5, "{ключ: 1}"},
      {JR_JSON5, "{a /* c */ : 1}"},
      {JR_JSON5, "{null: 1, true: 2}"},
      {JR_JSON5, "[Infinit]"},
      {JR_JSON5, "[0x]"},
      {JR_JSON5, "[1e]"},
      {JR_JSON5, "[.]"},
      {JR_JSON5, "[01]"},
      {JR_JSON5, "1 2"},
      {JR_JSON5, "[\"\\1\"]"},
      {JR_JSONL, "{\"a\":1}\n{\"b\":2}\n"},
      {JR_JSONL, "1\n2\n3"},
      {JR_JSONL, "1 2 3"},
      {JR_JSONL, ""},
      {JR_JSONL, "[1,]"},
      {JR_LEGACY, "[1,,2,]"},
      {JR_LEGACY, "// c\n[01, 1.]"},
      {JR_LEGACY, "1 2"},
      {JR_LEGACY, ""},
  };

  for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    check(ctx, cases[i].flags, cases[i].s);

  if(failures)
    printf("%d failed\n", failures);
  else
    printf("ok\n");

  return failures != 0;
}
