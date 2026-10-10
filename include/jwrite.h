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
 * jwrite_number(&wr, "7", 1);
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
  JWRITE_E_SOURCE,            /* jwrite_error(): the input reported an error */
  JWRITE_E_NUMBER,            /* number text empty, or with a byte no number has */
};

/* the text for a failed event's return value: jwrite_error_message(-r) */
const char* jwrite_error_message(int code);

/* jwrite_init: `realloc_func`/`opaque` allocate the frame stack; NULL, NULL
 * uses libc realloc. `wr->writer` and `wr->opts` are set by the caller. */
void jwrite_init(JsonWriter*, DynBufReallocFunc* realloc_func, void* opaque);
void jwrite_free(JsonWriter*);

/* The events, one per jr_type_t of jread.h and in its order. Valueless
 * events take the writer only; the others take `str`/`len`.
 *
 *   jr_type_t               jwrite call
 *   jr_type_error           jwrite_error(wr, str, len)   message; writes nothing
 *   jr_type_null            jwrite_null(wr)
 *   jr_type_true            jwrite_true(wr)
 *   jr_type_false           jwrite_false(wr)
 *   jr_type_number          jwrite_number(wr, str, len)  the number as text
 *   jr_type_string          jwrite_string(wr, str, len)  decoded UTF-8, escaped here
 *   jr_type_array_start     jwrite_array_start(wr)
 *   jr_type_array_end       jwrite_array_end(wr)
 *   jr_type_object_start    jwrite_object_start(wr)
 *   jr_type_object_end      jwrite_object_end(wr)
 *   jr_type_key             jwrite_key(wr, str, len)     decoded UTF-8, escaped here
 *
 * jwrite_number() writes `str` as given: "1e400" and "12345678901234567890"
 * stay as they are. With hex_numbers set, a plain integer (-31, 255) becomes
 * -0x1f, 0xff. It returns -JWRITE_E_NUMBER for empty text, a first byte
 * other than a digit, '+', '-' or '.', or a later byte other than a letter,
 * a digit, '+', '-' or '.'.
 *
 * jwrite_error() returns -JWRITE_E_SOURCE without writing, so a stream of
 * jr events stops at the error with the writer's state intact. */
ssize_t jwrite_error(JsonWriter*, const char* str, size_t len);
ssize_t jwrite_null(JsonWriter*);
ssize_t jwrite_true(JsonWriter*);
ssize_t jwrite_false(JsonWriter*);
ssize_t jwrite_number(JsonWriter*, const char* str, size_t len);
ssize_t jwrite_string(JsonWriter*, const char* str, size_t len);
ssize_t jwrite_array_start(JsonWriter*);
ssize_t jwrite_array_end(JsonWriter*);
ssize_t jwrite_object_start(JsonWriter*);
ssize_t jwrite_object_end(JsonWriter*);
ssize_t jwrite_key(JsonWriter*, const char* str, size_t len);

/* JsonWriteInterface: the jwrite events as function pointers, so a
 * producer can drive any sink without knowing it is a JsonWriter.
 *
 * Like WalkInterface, minus the engine: no JSContext, no JSValue. One member
 * per jwrite event, named after the jr_type_t it mirrors (`true_`/`false_`
 * because `true`/`false` are macros).
 *
 * ```c
 * JsonWriteInterface w = jwrite_interface(&wr);
 * w.object_start(w.opaque);
 * w.key(w.opaque, "id", 2);
 * w.number(w.opaque, "7", 1);
 * w.object_end(w.opaque);
 * ```
 *
 * Every member returns what its jwrite call returns: the bytes written
 * (>= 0), or -JWRITE_E_*. `opaque` is passed as the first argument. */
typedef struct {
  void* opaque;
  ssize_t (*error)(void*, const char* str, size_t len);
  ssize_t (*null)(void*);
  ssize_t (*true_)(void*);
  ssize_t (*false_)(void*);
  ssize_t (*number)(void*, const char* str, size_t len);
  ssize_t (*string)(void*, const char* str, size_t len);
  ssize_t (*array_start)(void*);
  ssize_t (*array_end)(void*);
  ssize_t (*object_start)(void*);
  ssize_t (*object_end)(void*);
  ssize_t (*key)(void*, const char* str, size_t len);
} JsonWriteInterface;

/* jwrite_interface: the interface of a JsonWriter; `opaque` is `wr`.
 * never throws; `wr` must outlive the returned struct. */
JsonWriteInterface jwrite_interface(JsonWriter* wr);

/* Writer-level helpers, no JsonWriter needed. Both return the bytes written
 * (> 0), 0 when the Writer is blocked, or -1 on a real error; a zero-length
 * jwrite_put() returns 1. */
ssize_t jwrite_put(Writer*, const void* buf, size_t len);
ssize_t jwrite_put_string(Writer*, const char* str, size_t len, int quote);

#endif
