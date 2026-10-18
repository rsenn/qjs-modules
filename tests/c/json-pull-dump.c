/* json-pull-dump.c: the events of the pull parser json_next() with their positions, up to the first
 * error, for a table of inputs, dialects and read sizes, as text.
 *
 *   json-pull-dump                 writes the text
 *   json-pull-dump FIXTURE         compares it with FIXTURE (ctest: c-json-pull-dump) */

#include "json/json.h"
#include "json/jread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
dump(FILE* out, JSContext* ctx, unsigned flags, int comments, const char* s, size_t step) {
  Feed feed = {s, strlen(s), 0, step};
  Reader rd = {&feed_read, &feed, NULL, NULL};
  JsonParser* json = json_new(rd, "t", ctx);
  JsonEvent ev;
  int finishing = 0;

  json->flags = flags;
  json->comments = comments;
  fprintf(out, "flags=%u comments=%d step=%zu input=", flags, comments, step);

  for(const char* p = s; *p; p++)
    fprintf(out, *p == '\n' ? "\\n" : "%c", *p);

  fputc('\n', out);

  for(int guard = 0; guard < 10000; guard++) {
    JsonPull st = finishing ? json_next_end(json, &ev) : json_next(json, &ev);

    if(st == JSON_PULL_AGAIN && !finishing) {
      finishing = 1;
      continue;
    }

    if(st != JSON_PULL_EVENT)
      break;

    fprintf(out, "  %d@%u:%u", ev.type, (unsigned)ev.at.line, (unsigned)ev.at.column);

    if(ev.str && (ev.type == JSON_EVENT_KEY || ev.type == JSON_EVENT_STRING || ev.type == JSON_EVENT_NUMBER || ev.type == JSON_EVENT_COMMENT || ev.type == JSON_EVENT_ERROR))
      fprintf(out, " [%.*s]", (int)ev.len, ev.str);

    if(ev.type == JSON_EVENT_ERROR) {
      fprintf(out, " code=%s span=%zu\n", ev.code ? ev.code : "-", ev.span);
      break;
    }

    fputc('\n', out);
  }

  json_free(json, JS_GetRuntime(ctx));
}

int
main(int argc, char** argv) {
  JSRuntime* rt = JS_NewRuntime();
  JSContext* ctx = JS_NewContext(rt);
  static const size_t steps[] = {1, 3, 16384};
  FILE* out = argc > 1 ? tmpfile() : stdout;
  int bad = 0;

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
      {JR_JSON, "{\n  \"a\": [1,\n    2],\n  \"b\": \"x\"\n}\n"},
      {JR_JSON, "[\"\xc3\xa9\xe2\x82\xac\", \"\\u00e9\\ud83d\\ude00\"]"},
      {JR_JSON, "[1,\n2,\n  x]"},
      {JR_JSON, "{\"a\":{\"b\":{\"c\":[[],{}]}}}"},
      {JR_JSON, "[1.5e-7,-0,0e0,12345678901234567890]"},
      {JR_JSONC, "// a\n[1, /* b */ 2] // c"},
      {JR_JSONC, "/* multi\n   line */ {\"a\" : 1}"},
      {JR_JSONL, "{\"a\":1}\n\n{\"b\":\n2}\n"},
      {JR_LEGACY, "{\"a\":1,\n\"b\":[1,2,,3]}\n{\"c\":4}"},
      {JR_LEGACY, "[1, 2\n"},
      {JR_LEGACY, "\"unterminated\n"},
  };

  for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    for(int c = 0; c < 2; c++)
      for(size_t k = 0; k < 3; k++)
        if(c == 0 || cases[i].flags == JR_JSONC || cases[i].flags == JR_LEGACY || cases[i].flags == JR_JSON5)
          dump(out, ctx, cases[i].flags, c, cases[i].s, steps[k]);

  if(argc > 1) {
    FILE* want = fopen(argv[1], "r");
    int a, b;
    long line = 1;

    if(!want) {
      printf("cannot open %s\n", argv[1]);
      return 1;
    }

    rewind(out);

    while((a = fgetc(out)) == (b = fgetc(want)) && a != EOF)
      if(a == '\n')
        line++;

    if(a != b) {
      printf("FAIL: line %ld differs from %s\n", line, argv[1]);
      bad = 1;
    } else {
      printf("ok\n");
    }
  }

  return bad;
}
