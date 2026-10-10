#ifndef JWRITE_H
#define JWRITE_H

#include <stddef.h>
#include <stdint.h>
#include "stream-utils.h"
#include "vector.h"

/* JsonWriteOptions: how a JsonWriter formats its output. */
typedef struct {
  unsigned indent : 10;       /* spaces per level, 0 = none */
  unsigned bare_keys : 1;     /* keys unquoted: kind: ... (JSON5) */
  unsigned single_quotes : 1; /* strings in '...' instead of "..." (JSON5) */
  unsigned hex_numbers : 1;   /* numbers as 0x1f, not 31 (JSON5) */
  unsigned minify : 1;        /* no whitespace at all, whatever indent says */
} JsonWriteOptions;

typedef struct {
  unsigned is_object : 1, expecting_value : 1;
  uint32_t count;
} JsonWriterFrame;

/* JsonWriter: push-based incremental JSON writer.
 *
 * The caller drives it with events, in document order:
 *
 * ```c
 * JsonWriter wr = {.writer = w, .opts = {.indent = 2}};
 * jwrite_init(&wr, NULL, NULL);
 * jwrite_object_start(&wr);
 * jwrite_key(&wr, "id", 2);
 * jwrite_int64(&wr, 7);
 * jwrite_object_end(&wr);
 * jwrite_free(&wr);          // {"id": 7}, indented
 * ```
 *
 * Commas, indentation and key/value ordering are handled here. Every
 * event returns the bytes written (>= 0), or -JWRITE_E_* on failure; none
 * of them touches a JSContext. */
typedef struct {
  Writer writer;
  size_t written; /* bytes put through the writer so far */
  JsonWriteOptions opts;
  Vector stack; /* JsonWriterFrame, one per open container */
} JsonWriter;

enum {
  JWRITE_E_WRITE = 1,         /* the Writer refused the bytes */
  JWRITE_E_NOMEM,             /* the frame stack could not grow */
  JWRITE_E_EXPECTED_KEY,      /* value inside an object, no key before it */
  JWRITE_E_KEY_AT_ROOT,       /* key outside any container */
  JWRITE_E_KEY_IN_ARRAY,      /* key inside an array */
  JWRITE_E_EXPECTED_VALUE,    /* key written twice in a row */
  JWRITE_E_UNMATCHED_OBJECT,  /* object_end with nothing open */
  JWRITE_E_UNMATCHED_ARRAY,   /* array_end with nothing open */
  JWRITE_E_NOT_ARRAY,         /* object_end inside an array */
  JWRITE_E_NOT_OBJECT,        /* array_end inside an object */
  JWRITE_E_PENDING_KEY,       /* object_end right after a key */
};

/* the text for a failed event's return value: jwrite_error_message(-r) */
const char* jwrite_error_message(int code);

/* jwrite_init: `realloc_func`/`opaque` allocate the frame stack; NULL, NULL
 * uses libc realloc. `wr->writer` and `wr->opts` are set by the caller. */
void jwrite_init(JsonWriter*, DynBufReallocFunc* realloc_func, void* opaque);
void jwrite_free(JsonWriter*);

ssize_t jwrite_object_start(JsonWriter*);
ssize_t jwrite_array_start(JsonWriter*);
ssize_t jwrite_object_end(JsonWriter*);
ssize_t jwrite_array_end(JsonWriter*);
ssize_t jwrite_key(JsonWriter*, const char* str, size_t len);

ssize_t jwrite_null(JsonWriter*);
ssize_t jwrite_bool(JsonWriter*, int b);
ssize_t jwrite_int64(JsonWriter*, int64_t n);
ssize_t jwrite_double(JsonWriter*, double d);
ssize_t jwrite_string(JsonWriter*, const char* str, size_t len);
ssize_t jwrite_raw(JsonWriter*, const char* str, size_t len);

/* Writer-level helpers, no JsonWriter needed. Both return the bytes written
 * (> 0), 0 when the Writer is blocked, or -1 on a real error; a zero-length
 * jwrite_put() returns 1. */
ssize_t jwrite_put(Writer*, const void* buf, size_t len);
ssize_t jwrite_put_string(Writer*, const char* str, size_t len, int quote);

#endif
