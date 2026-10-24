/* vtparse-events.c: checks include/vtparse.h, exits non-zero on a failure.
 * built and run by ctest (c-vtparse-events), no engine needed. */

#include "vtparse.h"
#include <stdio.h>
#include <string.h>

static int failures;
static char log_buf[1024];

#define CHECK(cond) \
  do { \
    if(!(cond)) { \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++; \
    } \
  } while(0)

static void
put(const char* s) {
  strncat(log_buf, s, sizeof(log_buf) - strlen(log_buf) - 1);
}

static void
on_text(void* opaque, const char* s, size_t len) {
  char tmp[256];

  snprintf(tmp, sizeof(tmp), "T(%.*s)", (int)len, s);
  put(tmp);
}

static void
on_control(void* opaque, int c) {
  char tmp[16];

  snprintf(tmp, sizeof(tmp), "C(%d)", c);
  put(tmp);
}

static void
on_esc(void* opaque, int intermediate, int final) {
  char tmp[16];

  snprintf(tmp, sizeof(tmp), "E(%c%c)", intermediate ? intermediate : '_', final);
  put(tmp);
}

static void
on_csi(void* opaque, const VtParser* vp, int final) {
  char tmp[32];
  int i;

  snprintf(tmp, sizeof(tmp), "S(%c", vp->prefix ? vp->prefix : '_');
  put(tmp);

  for(i = 0; i < vp->nparams; i++) {
    snprintf(tmp, sizeof(tmp), "%s%d", i ? "," : " ", vp->params[i]);
    put(tmp);
  }

  snprintf(tmp, sizeof(tmp), " %c)", final);
  put(tmp);
}

static const VtHandler handler = {on_text, on_control, on_esc, on_csi};

/* parses each part with one parser, compares the event log with want */
static int
parse(const char* want, const char* parts[]) {
  VtParser vp;

  log_buf[0] = '\0';
  vt_init(&vp, &handler, 0);

  for(; *parts; parts++)
    vt_feed(&vp, *parts, strlen(*parts));

  if(strcmp(log_buf, want)) {
    printf("  want %s\n  got  %s\n", want, log_buf);
    return 0;
  }

  return 1;
}

#define PARSE(want, ...) parse(want, (const char*[]){__VA_ARGS__, 0})

int
main(void) {
  VtParser vp;

  CHECK(PARSE("T(abc)", "abc"));
  CHECK(PARSE("T(a)C(13)C(10)T(b)", "a\r\nb"));
  CHECK(PARSE("T(x)S(_ 3,5 H)T(y)", "x\x1b[3;5Hy"));
  CHECK(PARSE("S(_ m)", "\x1b[m"));
  CHECK(PARSE("S(_ -1,5 H)", "\x1b[;5H"));
  CHECK(PARSE("S(? 25 l)", "\x1b[?25l"));
  CHECK(PARSE("S(_ 1,31 m)T(r)S(_ 0 m)", "\x1b[1;31mr\x1b[0m"));

  /* a sequence split across writes */
  CHECK(PARSE("T(a)S(_ 12 C)T(b)", "a\x1b", "[1", "2Cb"));

  /* ESC, OSC and DCS strings are consumed */
  CHECK(PARSE("E(_7)E(_8)",
              "\x1b"
              "7\x1b"
              "8"));
  CHECK(PARSE("E((B)", "\x1b(B"));
  CHECK(PARSE("T(a)T(b)",
              "a\x1b]0;title\x07"
              "b"));
  CHECK(PARSE("T(a)T(b)", "a\x1b]0;ti", "tle\x1b\\b"));
  CHECK(PARSE("T(c)", "\x1bPq#0\x1b\\c"));

  /* UTF-8 passes as text, DEL is dropped */
  CHECK(PARSE("T(\xc3\xa4\xe2\x82\xac)", "\xc3\xa4\xe2\x82\xac"));
  CHECK(PARSE("T(a)T(b)",
              "a\x7f"
              "b"));

  /* a control inside CSI runs, the sequence goes on */
  CHECK(PARSE("C(8)S(_ 2 A)", "\x1b[2\bA"));

  /* CAN aborts, ESC restarts */
  CHECK(PARSE("T(z)", "\x1b[12\x18z"));
  CHECK(PARSE("S(_ 4 D)", "\x1b[3\x1b[4D"));

  /* a misplaced private marker drops the sequence */
  CHECK(PARSE("T(q)", "\x1b[1?hq"));

  vt_init(&vp, &handler, 0);
  log_buf[0] = '\0';
  vt_feed(&vp, "\x1b[5;;7H", 8);
  CHECK(vt_param(&vp, 0, 1) == 5);
  CHECK(vt_param(&vp, 1, 1) == 1);
  CHECK(vt_param(&vp, 2, 1) == 7);
  CHECK(vt_param(&vp, 3, 9) == 9);

  if(failures)
    printf("%d failed\n", failures);

  return !!failures;
}
