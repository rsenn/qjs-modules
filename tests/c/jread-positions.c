/* jread-positions.c: error code, position and length of src/json/jread.c; chunk invariance.
 * built and run by ctest (`c-jread-positions`); exits non-zero on the first failed check. */

#include "json/jread.h"
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

typedef struct {
  int errors;
  jr_state_t st;
  jr_error_t code;
  uint32_t line, column;
  size_t len, offset;
} Result;

static void
cb(JsonEventType type, const jr_str_t* data, void* user) {
  Result* r = user;

  if(type == JSON_EVENT_ERROR && !r->errors++) {
    r->code = r->st.err_code;
    r->line = r->st.err_pos.line;
    r->column = r->st.err_pos.column;
    r->offset = r->st.err_pos.byte_offset;
    r->len = r->st.err_len;
  }
}

/* feeds `s` in pieces of `step` bytes (0: whole), then finishes. */
static Result
run(const char* s, size_t step) {
  Result r;
  size_t n = strlen(s), i;

  memset(&r, 0, sizeof(r));
  jr_state_init(&r.st);

  if(!step)
    step = n ? n : 1;

  for(i = 0; i < n; i += step)
    jr_read(cb, s + i, n - i < step ? n - i : step, &r, &r.st);

  jr_finish(cb, &r, &r.st);
  jr_state_free(&r.st);
  return r;
}

static void
expect(const char* s, jr_error_t code, uint32_t line, uint32_t column, size_t len) {
  for(size_t step = 0; step <= 4; step++) {
    Result r = run(s, step);

    if(r.errors < 1 || r.code != code || r.line != line || r.column != column || r.len != len) {
      printf("FAIL %s step %zu: code %s %u:%u len %zu\n", s, step, jr_error_name(r.code), r.line, r.column, r.len);
      failures++;
    }
  }
}

int
main(void) {
  expect("{\"a\":\n  [1, x]}", JR_E_UNEXPECTED_TOKEN, 2, 7, 1);
  expect("[\"a\\qb\"]", JR_E_BAD_ESCAPE, 1, 2, 4);
  expect("[\"a\\u12g4\"]", JR_E_BAD_UNICODE_ESCAPE, 1, 2, 7);
  expect("[\"a\tb\"]", JR_E_CONTROL_CHARACTER, 1, 2, 3);
  expect("[nulx]", JR_E_INVALID_LITERAL, 1, 2, 4);
  expect("[\"a\xff\"]", JR_E_INVALID_UTF8, 1, 2, 3);
  expect("[1,\r\n2,\r\n x]", JR_E_UNEXPECTED_TOKEN, 3, 2, 1);
  expect("[1, 2", JR_E_UNEXPECTED_END, 1, 6, 0);
  expect("{\"k\":", JR_E_UNEXPECTED_END, 1, 6, 0);

  CHECK(strcmp(jr_error_name(JR_E_BAD_ESCAPE), "bad-escape") == 0);

  if(failures)
    return 1;

  puts("ok");
  return 0;
}
