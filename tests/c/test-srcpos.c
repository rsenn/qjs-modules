/* test-srcpos.c: include/srcpos.h; exits non-zero on the first failed check.
 * built and run by ctest (`srcpos`), no engine needed. */

#include "srcpos.h"
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

static SourcePos
count(const char* s, int utf16) {
  SourcePos p;

  srcpos_init(&p, utf16);
  srcpos_advance(&p, s, strlen(s));
  return p;
}

static int
same(const SourcePos* a, const SourcePos* b) {
  return a->byte_offset == b->byte_offset && a->char_offset == b->char_offset && a->line == b->line && a->column == b->column && a->cr == b->cr;
}

int
main(void) {
  SourcePos p;

  /* start */
  srcpos_init(&p, 0);
  CHECK(p.line == 1 && p.column == 1 && p.byte_offset == 0 && p.char_offset == 0);

  /* ASCII, one line */
  p = count("abc", 0);
  CHECK(p.line == 1 && p.column == 4 && p.byte_offset == 3 && p.char_offset == 3);

  /* the position of the bad token in {"a":\n  [1, x]} is 2:7, so 6 characters on line 2 precede it */
  p = count("{\"a\":\n  [1, ", 0);
  CHECK(p.line == 2 && p.column == 7);

  /* line breaks: \n, \r\n as one, lone \r */
  p = count("a\nb", 0);
  CHECK(p.line == 2 && p.column == 2);
  p = count("a\r\nb", 0);
  CHECK(p.line == 2 && p.column == 2 && p.char_offset == 4);
  p = count("a\rb", 0);
  CHECK(p.line == 2 && p.column == 2);
  p = count("a\r\rb", 0);
  CHECK(p.line == 3 && p.column == 2);
  p = count("a\n\r", 0);
  CHECK(p.line == 3 && p.column == 1);
  p = count("\r\n\r\n", 0);
  CHECK(p.line == 3 && p.column == 1 && p.byte_offset == 4);
  p = count("a\tb", 0);
  CHECK(p.column == 4);

  /* UTF-8: 2-byte (é), 3-byte (€), 4-byte (😀) */
  p = count("\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80", 0);
  CHECK(p.byte_offset == 9 && p.char_offset == 3 && p.column == 4);
  p = count("\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80", 1);
  CHECK(p.byte_offset == 9 && p.char_offset == 4 && p.column == 5);

  /* a stray continuation byte counts for nothing */
  p = count("a\x80" "b", 0);
  CHECK(p.byte_offset == 3 && p.char_offset == 2 && p.column == 3);

  /* BOM is one character */
  p = count("\xef\xbb\xbf{", 0);
  CHECK(p.char_offset == 2 && p.column == 3 && p.byte_offset == 4);

  /* chunk invariance: every split point and every piece size gives the whole-input result */
  {
    static const char* const inputs[] = {
        "{\"a\":\r\n  [1, \xc3\xa9\xe2\x82\xac, \xf0\x9f\x98\x80]\r}\n\r\n\t x",
        "\r\n\r\n\r",
        "\xf0\x9f\x98\x80\xf0\x9f\x98\x80\n\xf0\x9f\x98\x80",
        "",
    };

    for(int u = 0; u < 2; u++) {
      for(size_t k = 0; k < sizeof(inputs) / sizeof(*inputs); k++) {
        size_t n = strlen(inputs[k]);
        SourcePos whole = count(inputs[k], u);

        for(size_t piece = 1; piece <= n + 1; piece++) {
          SourcePos q;

          srcpos_init(&q, u);

          for(size_t at = 0; at < n; at += piece)
            srcpos_advance(&q, inputs[k] + at, at + piece <= n ? piece : n - at);

          CHECK(same(&q, &whole));
        }

        for(size_t split = 0; split <= n; split++) {
          SourcePos q;

          srcpos_init(&q, u);
          srcpos_advance(&q, inputs[k], split);
          srcpos_advance(&q, inputs[k] + split, n - split);
          CHECK(same(&q, &whole));
        }
      }
    }
  }

  /* zero-length advance changes nothing, including the pending \r */
  srcpos_init(&p, 0);
  srcpos_advance(&p, "\r", 1);
  srcpos_advance(&p, "", 0);
  srcpos_advance(&p, "\n", 1);
  CHECK(p.line == 2 && p.column == 1);

  if(failures)
    printf("%d failed\n", failures);
  else
    printf("srcpos: all checks passed\n");

  return failures != 0;
}
