/* jread-dialects.c: the syntax flags of src/json/jread.c (JR_JSON, JR_JSONC, JR_JSON5, JR_JSONL).
 * each case runs whole and in pieces of 1, 2, 3 and 7 bytes; the token stream must not change.
 * built and run by ctest (`c-jread-dialects`); exits non-zero on the first failed check. */

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

static ssize_t
cb(void* user, const JsonEvent* ev) {
  JsonEventType type = ev->type;
  Run* r = user;
  char buf[256];

  switch(type) {
    case JSON_EVENT_NULL: put(r, "null"); break;
    case JSON_EVENT_TRUE: put(r, "true"); break;
    case JSON_EVENT_FALSE: put(r, "false"); break;
    case JSON_EVENT_ARRAY_START: put(r, "["); break;
    case JSON_EVENT_ARRAY_END: put(r, "]"); break;
    case JSON_EVENT_OBJECT_START: put(r, "{"); break;
    case JSON_EVENT_OBJECT_END: put(r, "}"); break;
    case JSON_EVENT_NUMBER: snprintf(buf, sizeof(buf), "n:%.*s", (int)ev->len, ev->str ? ev->str : ""); put(r, buf); break;
    case JSON_EVENT_STRING: snprintf(buf, sizeof(buf), "s:%.*s", (int)ev->len, ev->str ? ev->str : ""); put(r, buf); break;
    case JSON_EVENT_KEY: snprintf(buf, sizeof(buf), "k:%.*s", (int)ev->len, ev->str ? ev->str : ""); put(r, buf); break;
    case JSON_EVENT_COMMENT: break;
    case JSON_EVENT_ERROR: snprintf(buf, sizeof(buf), "E:%s@%u:%u", jr_error_name(r->st.err_code), (unsigned)r->st.err_pos.line, (unsigned)r->st.err_pos.column); put(r, buf); break;
  }

  return 0;
}

static void
run(Run* r, uint8_t flags, const char* s, size_t n, size_t step) {
  size_t i;

  memset(r, 0, sizeof(*r));
  jr_state_init_flags(&r->st, flags);

  if(!step)
    step = n ? n : 1;

  for(i = 0; i < n; i += step)
    jr_feed(&r->st, s + i, n - i < step ? n - i : step, cb, r);

  jr_end(&r->st, cb, r);
  jr_state_free(&r->st);
}

/* the token stream of `s` under `flags`, at every chunk size, equals `expect` */
static void
check(uint8_t flags, const char* s, const char* expect) {
  static const size_t steps[] = {0, 1, 2, 3, 7};

  for(size_t k = 0; k < sizeof(steps) / sizeof(steps[0]); k++) {
    Run r;

    run(&r, flags, s, strlen(s), steps[k]);

    if(strcmp(r.out, expect)) {
      printf("FAIL flags=%u step=%zu input=%s\n  got    %s\n  expect %s\n", (unsigned)flags, steps[k], s, r.out, expect);
      failures++;
      return;
    }
  }
}

int
main(int argc, char** argv) {
  if(argc > 2) { /* ad-hoc: jread-dialects FLAGS INPUT */
    Run r;

    run(&r, (unsigned)atoi(argv[1]), argv[2], strlen(argv[2]), argc > 3 ? (size_t)atoi(argv[3]) : 0);
    printf("%s\n", r.out);
    return 0;
  }

  /* JR_JSON: RFC 8259; error positions are 1-based line:column of the token */
  check(JR_JSON, "{\"a\":[1,-2.5e+3,true,null,\"x\\n\"]}", "{ k:a [ n:1 n:-2.5e+3 true null s:x\n ] }");
  check(JR_JSON, "  \"s\"  ", "s:s");
  check(JR_JSON, "1e5", "n:1e5");
  check(JR_JSON, "0", "n:0");
  check(JR_JSON, "-0.5", "n:-0.5");
  check(JR_JSON, "[1,]", "[ n:1 E:unexpected-token@1:4 ]");
  check(JR_JSON, "{\"a\":1,}", "{ k:a n:1 E:unexpected-token@1:8 }");
  check(JR_JSON, "[,1]", "[ E:unexpected-token@1:2 ]");
  check(JR_JSON, "[1,,2]", "[ n:1 E:unexpected-token@1:4 ]");
  check(JR_JSON, "// c\n1", "E:unexpected-token@1:1");
  check(JR_JSON, "[01]", "[ E:unexpected-token@1:2 ]");
  check(JR_JSON, "[1.]", "[ E:unexpected-token@1:2 ]");
  check(JR_JSON, "[1.,2]", "[ E:unexpected-token@1:2 n:2 ]");
  check(JR_JSON, "[.5]", "[ E:unexpected-token@1:2 ]");
  check(JR_JSON, "[+1]", "[ E:unexpected-token@1:2 ]");
  check(JR_JSON, "[1e]", "[ E:unexpected-token@1:2 ]");
  check(JR_JSON, "[-]", "[ E:unexpected-token@1:2 ]");
  check(JR_JSON, "[0x10]", "[ E:unexpected-token@1:2 ]");
  check(JR_JSON, "[NaN]", "[ E:unexpected-token@1:2 ]");
  check(JR_JSON, "['a']", "[ E:unexpected-token@1:2 ]");
  check(JR_JSON, "{a:1}", "{ E:unexpected-token@1:2 }");
  check(JR_JSON, "1 2", "n:1 E:trailing-data@1:3");
  check(JR_JSON, "{} x", "{ } E:trailing-data@1:4");
  check(JR_JSON, "[1,2]x", "[ n:1 n:2 ] E:trailing-data@1:6");
  check(JR_JSON, "", "E:unexpected-end@1:1");
  check(JR_JSON, "  ", "E:unexpected-end@1:3");
  check(JR_JSON, "-", "E:unexpected-end@1:2");

  /* JR_JSONC: comments and trailing commas, nothing else */
  check(JR_JSONC, "// c\n{\"a\":1, /* x */ \"b\":[1,2,],}", "{ k:a n:1 k:b [ n:1 n:2 ] }");
  check(JR_JSONC, "[1,,2]", "[ n:1 E:unexpected-token@1:4 ]");
  check(JR_JSONC, "[1] /* open", "[ n:1 ] E:unexpected-end@1:12");
  check(JR_JSONC, "1 // tail", "n:1");
  check(JR_JSONC, "['a']", "[ E:unexpected-token@1:2 ]");

  /* JR_JSON5 */
  check(JR_JSON5, "{a:1, $b_:'x', \"c\":[+1, .5, 5., 0x1F, -0xff, Infinity, -Infinity, +Infinity, NaN, 1e3,],}",
        "{ k:a n:1 k:$b_ s:x k:c [ n:+1 n:.5 n:5. n:0x1F n:-0xff n:Infinity n:-Infinity n:+Infinity n:NaN n:1e3 ] }");
  check(JR_JSON5, "[-NaN, -.5, +.5e-2, 1.e3]", "[ n:-NaN n:-.5 n:+.5e-2 n:1.e3 ]");
  check(JR_JSON5, "'it\\'s \"q\"'", "s:it's \"q\"");
  check(JR_JSON5, "\"a\\\nb\"", "s:ab");
  check(JR_JSON5, "\"a\\\r\nb\"", "s:ab");
  check(JR_JSON5, "'\\x41\\u0042\\v\\z'", "s:AB\vz");
  check(JR_JSON5, "\"tab\there\"", "s:tab\there");
  check(JR_JSON5, "\"a\nb\"", "E:control-character@1:1");
  check(JR_JSON5, "{ключ: 1}", "{ k:ключ n:1 }");
  check(JR_JSON5, "{a /* c */ : 1}", "{ k:a n:1 }");
  check(JR_JSON5, "{a\t:1}", "{ k:a n:1 }");
  check(JR_JSON5, "\v[\f1]", "[ n:1 ]");
  check(JR_JSON5, "[Infinit]", "[ E:invalid-literal@1:2 ]");
  check(JR_JSON5, "[0x]", "[ E:unexpected-token@1:2 ]");
  check(JR_JSON5, "[1e]", "[ E:unexpected-token@1:2 ]");
  check(JR_JSON5, "[.]", "[ E:unexpected-token@1:2 ]");
  check(JR_JSON5, "[01]", "[ E:unexpected-token@1:2 ]");
  check(JR_JSON5, "1 2", "n:1 E:trailing-data@1:3");
  check(JR_JSON5, "[\"\\1\"]", "[ E:bad-escape@1:2 ]");

  /* JR_JSONL: strict values, many documents */
  check(JR_JSONL, "{\"a\":1}\n{\"b\":2}\n", "{ k:a n:1 } { k:b n:2 }");
  check(JR_JSONL, "1\n2\n3", "n:1 n:2 n:3");
  check(JR_JSONL, "1 2 3", "n:1 n:2 n:3");
  check(JR_JSONL, "", "");
  check(JR_JSONL, "[1,]", "[ n:1 E:unexpected-token@1:4 ]");

  /* the default stays the historic lax reader */
  check(JR_LEGACY, "[1,,2,]", "[ n:1 n:2 ]");
  check(JR_LEGACY, "// c\n[01, 1.]", "[ n:01 n:1. ]");
  check(JR_LEGACY, "1 2", "n:1 n:2");
  check(JR_LEGACY, "", "");

  if(failures)
    printf("%d failed\n", failures);
  else
    printf("ok\n");

  return failures != 0;
}
