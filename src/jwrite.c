/* jwrite.c: JsonWriter, the jwrite_*() events, jwrite_put*().
 * depends on: stream-utils.h (Writer), vector.h (frame stack).
 * rule: no JSContext or JSValue anywhere; every byte goes through
 * jwrite_out(), so `written` counts all output. */

#include "jwrite.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* jwrite_put() and jwrite_put_string() return the bytes on full success, 0 if the writer ran out of
 * room (a chunked destination signalling "retry later"), or -1 on a real error. Callers
 * must check the return value before mutating any traversal state, so a blocked write can
 * be retried later without having advanced past what was actually delivered.
 *
 * All multi-byte pieces (escapes, numbers, indentation, literals) are written one byte at
 * a time via jwrite_put() rather than as a single bulk write: against a bounded destination
 * (see JsonSerializer's zero-copy .read(buffer)) a bulk write is all-or-nothing, so any
 * atomic unit wider than the caller's buffer could never be delivered at all. Byte-granular
 * writes guarantee forward progress as long as the destination has room for at least 1 byte. */
/* jwrite_put: writes `len` bytes one at a time.
 *
 * returns len, 1 for len 0, 0 when the Writer is blocked, -1 on error. */
ssize_t
jwrite_put(Writer* wr, const void* buf, size_t len) {
  const uint8_t* p = buf;

  if(len == 0)
    return 1;

  for(size_t i = 0; i < len; i++) {
    ssize_t w = writer_putc(wr, p[i]);

    if(w < 0)
      return -1;
    if(w == 0)
      return 0;
  }

  return (ssize_t)len;
}

/* jwrite_put_string: writes `str` as a JSON string in `quote` ('"' or '\'').
 *
 *   jwrite_put_string(wr, "it's", 4, '\'')   // 'it\'s'
 *
 * returns the bytes written (> 0), 0 when blocked, -1 on error. */
ssize_t
jwrite_put_string(Writer* wr, const char* s, size_t len, int quote) {
  ssize_t w, total = 0;

  if((w = writer_putc(wr, quote)) <= 0)
    return w;

  total += w;

  for(size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    char buf[8];

    switch(c) {
      case '\\': w = jwrite_put(wr, "\\\\", 2); break;
      case '\b': w = jwrite_put(wr, "\\b", 2); break;
      case '\f': w = jwrite_put(wr, "\\f", 2); break;
      case '\n': w = jwrite_put(wr, "\\n", 2); break;
      case '\r': w = jwrite_put(wr, "\\r", 2); break;
      case '\t': w = jwrite_put(wr, "\\t", 2); break;
      default:
        if(c == quote) {
          buf[0] = '\\';
          buf[1] = c;
          w = jwrite_put(wr, buf, 2);
        } else if(c < 0x20) {
          int n = snprintf(buf, sizeof(buf), "\\u%04x", c);
          w = jwrite_put(wr, buf, n);
        } else {
          w = writer_putc(wr, c);
        }

        break;
    }

    if(w <= 0)
      return w;

    total += w;
  }

  if((w = writer_putc(wr, quote)) <= 0)
    return w;

  return total + w;
}

static const char* const jwrite_messages[] = {
    0,
    "write failed",
    "out of memory",
    "expected key",
    "key cannot be at root level",
    "key cannot be used inside an array",
    "expected value for previous key",
    "unmatched objectEnd",
    "unmatched arrayEnd",
    "expected arrayEnd, got objectEnd",
    "expected objectEnd, got arrayEnd",
    "expected value for key",
    "input reported an error",
    "invalid number text",
};

/* jwrite_error_message: the text for a JWRITE_E_* code (not the negative).
 *
 * returns NULL for an unknown code. */
const char*
jwrite_error_message(int code) {
  return code > 0 && code < (int)(sizeof(jwrite_messages) / sizeof(*jwrite_messages)) ? jwrite_messages[code] : 0;
}

void
jwrite_init(JsonWriter* wr, DynBufReallocFunc* realloc_func, void* opaque) {
  dbuf_init2(&wr->stack, opaque, realloc_func);
}

/* jwrite_free: frees the writer and the frame stack; not `wr` itself. */
void
jwrite_free(JsonWriter* wr) {
  writer_free(&wr->writer);
  vector_free(&wr->stack);
}

/* the one path for output: counts into `written`.
 *
 * returns len, or -JWRITE_E_WRITE. */
static ssize_t
jwrite_out(JsonWriter* wr, const void* buf, size_t len) {
  if(len == 0)
    return 0;

  if(jwrite_put(&wr->writer, buf, len) <= 0)
    return -JWRITE_E_WRITE;

  wr->written += len;
  return (ssize_t)len;
}

static ssize_t
jwrite_outc(JsonWriter* wr, int c) {
  uint8_t b = c;

  return jwrite_out(wr, &b, 1);
}

/* bare_keys: ES5 IdentifierName, ASCII only; anything else stays quoted. */
static BOOL
jwrite_is_identifier(const char* s, size_t len) {
  if(len == 0 || isdigit((unsigned char)s[0]))
    return FALSE;

  for(size_t i = 0; i < len; i++)
    if(!(isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '$'))
      return FALSE;

  return TRUE;
}

static BOOL
jwrite_pretty(const JsonWriter* wr) {
  return wr->opts.indent > 0 && !wr->opts.minify;
}

/* newline plus one indent per open container; nothing when not pretty. */
static ssize_t
jwrite_indent(JsonWriter* wr) {
  ssize_t r, w = 0;

  if(jwrite_pretty(wr)) {
    size_t n = (size_t)wr->opts.indent * vector_size(&wr->stack, sizeof(JsonWriterFrame));

    if((w = jwrite_outc(wr, '\n')) < 0)
      return w;

    for(size_t i = 0; i < n; i++) {
      if((r = jwrite_outc(wr, ' ')) < 0)
        return r;

      w += r;
    }
  }

  return w;
}

static ssize_t
jwrite_comma_indent(JsonWriter* wr, uint32_t count) {
  ssize_t r, w = 0;

  if(count > 0 && (w = jwrite_outc(wr, ',')) < 0)
    return w;

  if((r = jwrite_indent(wr)) < 0)
    return r;

  return w + r;
}

/* before a value in the current container: checks key/value order, and
 * writes the comma and indent between array items. */
static ssize_t
jwrite_before_value(JsonWriter* wr) {
  JsonWriterFrame* top;
  ssize_t w;

  if(vector_empty(&wr->stack))
    return 0;

  top = vector_back(&wr->stack, sizeof(JsonWriterFrame));

  if(top->is_object) {
    if(!top->expecting_value)
      return -JWRITE_E_EXPECTED_KEY;

    top->expecting_value = FALSE;
    return 0;
  }

  if((w = jwrite_comma_indent(wr, top->count)) < 0)
    return w;

  top->count++;
  return w;
}

ssize_t
jwrite_key(JsonWriter* wr, const char* kstr, size_t klen) {
  JsonWriterFrame* top;
  ssize_t r, w;

  if(vector_empty(&wr->stack))
    return -JWRITE_E_KEY_AT_ROOT;

  top = vector_back(&wr->stack, sizeof(JsonWriterFrame));

  if(!top->is_object)
    return -JWRITE_E_KEY_IN_ARRAY;

  if(top->expecting_value)
    return -JWRITE_E_EXPECTED_VALUE;

  if((w = jwrite_comma_indent(wr, top->count)) < 0)
    return w;

  if(wr->opts.bare_keys && jwrite_is_identifier(kstr, klen)) {
    r = jwrite_out(wr, kstr, klen);
  } else if((r = jwrite_put_string(&wr->writer, kstr, klen, wr->opts.single_quotes ? '\'' : '"')) > 0) {
    wr->written += r;
  } else {
    r = -JWRITE_E_WRITE;
  }

  if(r < 0)
    return r;

  w += r;

  if((r = jwrite_outc(wr, ':')) < 0)
    return r;

  w += r;

  if(jwrite_pretty(wr)) {
    if((r = jwrite_outc(wr, ' ')) < 0)
      return r;

    w += r;
  }

  top->expecting_value = TRUE;
  top->count++;
  return w;
}

static ssize_t
jwrite_start(JsonWriter* wr, int c, BOOL is_object) {
  ssize_t r, w;

  if((w = jwrite_before_value(wr)) < 0)
    return w;

  if((r = jwrite_outc(wr, c)) < 0)
    return r;

  if(!vector_push(&wr->stack, ((JsonWriterFrame){is_object, FALSE, 0})))
    return -JWRITE_E_NOMEM;

  return w + r;
}

/* after a value, an enclosing object expects a key (or its end) again. */
static void
jwrite_after_value(JsonWriter* wr) {
  if(!vector_empty(&wr->stack)) {
    JsonWriterFrame* parent = vector_back(&wr->stack, sizeof(JsonWriterFrame));

    if(parent->is_object)
      parent->expecting_value = FALSE;
  }
}

ssize_t
jwrite_object_start(JsonWriter* wr) {
  return jwrite_start(wr, '{', TRUE);
}

ssize_t
jwrite_array_start(JsonWriter* wr) {
  return jwrite_start(wr, '[', FALSE);
}

static ssize_t
jwrite_end(JsonWriter* wr, int c, BOOL is_object) {
  JsonWriterFrame* top;
  ssize_t r, w = 0;
  uint32_t count;

  if(vector_empty(&wr->stack))
    return is_object ? -JWRITE_E_UNMATCHED_OBJECT : -JWRITE_E_UNMATCHED_ARRAY;

  top = vector_back(&wr->stack, sizeof(JsonWriterFrame));

  if(top->is_object != is_object)
    return is_object ? -JWRITE_E_NOT_ARRAY : -JWRITE_E_NOT_OBJECT;

  if(is_object && top->expecting_value)
    return -JWRITE_E_PENDING_KEY;

  count = top->count;
  vector_pop(&wr->stack, sizeof(JsonWriterFrame));

  if(count > 0 && (w = jwrite_indent(wr)) < 0)
    return w;

  if((r = jwrite_outc(wr, c)) < 0)
    return r;

  jwrite_after_value(wr);
  return w + r;
}

ssize_t
jwrite_object_end(JsonWriter* wr) {
  return jwrite_end(wr, '}', TRUE);
}

ssize_t
jwrite_array_end(JsonWriter* wr) {
  return jwrite_end(wr, ']', FALSE);
}

/* a value written as the text `str`: order check, the text, bookkeeping. */
static ssize_t
jwrite_literal(JsonWriter* wr, const char* str, size_t len) {
  ssize_t r, w;

  if((w = jwrite_before_value(wr)) < 0)
    return w;

  if((r = jwrite_out(wr, str, len)) < 0)
    return r;

  jwrite_after_value(wr);
  return w + r;
}

ssize_t
jwrite_error(JsonWriter* wr, const char* str, size_t len) {
  return -JWRITE_E_SOURCE;
}

ssize_t
jwrite_null(JsonWriter* wr) {
  return jwrite_literal(wr, "null", 4);
}

ssize_t
jwrite_true(JsonWriter* wr) {
  return jwrite_literal(wr, "true", 4);
}

ssize_t
jwrite_false(JsonWriter* wr) {
  return jwrite_literal(wr, "false", 5);
}

/* number text: a digit, '+', '-' or '.' first, then letters, digits and
 * those; keeps ',' quotes and brackets out of a value written verbatim. */
static BOOL
jwrite_is_number(const char* s, size_t len) {
  if(len == 0 || !(isdigit((unsigned char)s[0]) || s[0] == '+' || s[0] == '-' || s[0] == '.'))
    return FALSE;

  for(size_t i = 1; i < len; i++)
    if(!(isalnum((unsigned char)s[i]) || s[i] == '+' || s[i] == '-' || s[i] == '.'))
      return FALSE;

  return TRUE;
}

/* hex_numbers: "-31" -> "-0x1f" in buf; returns the length, or 0 when the
 * text is not a plain integer of at most 18 digits. */
static size_t
jwrite_hex_text(const char* s, size_t len, char buf[32]) {
  size_t i = s[0] == '-';
  unsigned long long n = 0;

  if(len == i || len - i > 18)
    return 0;

  for(size_t j = i; j < len; j++) {
    if(!isdigit((unsigned char)s[j]))
      return 0;

    n = n * 10 + (s[j] - '0');
  }

  return snprintf(buf, 32, "%s0x%llx", i ? "-" : "", n);
}

ssize_t
jwrite_number(JsonWriter* wr, const char* str, size_t len) {
  char buf[32];
  size_t n;

  if(!jwrite_is_number(str, len))
    return -JWRITE_E_NUMBER;

  if(wr->opts.hex_numbers && (n = jwrite_hex_text(str, len, buf)))
    return jwrite_literal(wr, buf, n);

  return jwrite_literal(wr, str, len);
}

ssize_t
jwrite_string(JsonWriter* wr, const char* str, size_t len) {
  ssize_t r, w;

  if((w = jwrite_before_value(wr)) < 0)
    return w;

  if((r = jwrite_put_string(&wr->writer, str, len, wr->opts.single_quotes ? '\'' : '"')) <= 0)
    return -JWRITE_E_WRITE;

  wr->written += r;
  jwrite_after_value(wr);
  return w + r;
}

/* the JsonWriteInterface members: each casts `opaque` back to the writer */
static ssize_t jwrite_if_error(void* wr, const char* str, size_t len) { return jwrite_error(wr, str, len); }
static ssize_t jwrite_if_null(void* wr) { return jwrite_null(wr); }
static ssize_t jwrite_if_true(void* wr) { return jwrite_true(wr); }
static ssize_t jwrite_if_false(void* wr) { return jwrite_false(wr); }
static ssize_t jwrite_if_number(void* wr, const char* str, size_t len) { return jwrite_number(wr, str, len); }
static ssize_t jwrite_if_string(void* wr, const char* str, size_t len) { return jwrite_string(wr, str, len); }
static ssize_t jwrite_if_array_start(void* wr) { return jwrite_array_start(wr); }
static ssize_t jwrite_if_array_end(void* wr) { return jwrite_array_end(wr); }
static ssize_t jwrite_if_object_start(void* wr) { return jwrite_object_start(wr); }
static ssize_t jwrite_if_object_end(void* wr) { return jwrite_object_end(wr); }
static ssize_t jwrite_if_key(void* wr, const char* str, size_t len) { return jwrite_key(wr, str, len); }

JsonWriteInterface
jwrite_interface(JsonWriter* wr) {
  return (JsonWriteInterface){
      wr,
      jwrite_if_error,
      jwrite_if_null,
      jwrite_if_true,
      jwrite_if_false,
      jwrite_if_number,
      jwrite_if_string,
      jwrite_if_array_start,
      jwrite_if_array_end,
      jwrite_if_object_start,
      jwrite_if_object_end,
      jwrite_if_key,
  };
}
