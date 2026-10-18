/* jtee.c: jtee_sink().
 * depends on: jevent.h.
 * rule: one member per event, each forwards to every sink in order. */

#include "json/jevent.h"

/**
 * \addtogroup jevent
 * @{
 */

#define JTEE_EACH(call) \
  ssize_t first = 0, r; \
  for(size_t i = 0; i < t->len; i++) { \
    const JsonEventSink* s = &t->sinks[i]; \
    r = (call); \
    if(r < 0) \
      return r; \
    if(i == 0) \
      first = r; \
  } \
  return first;

#define JTEE_PLAIN(name, member) \
  static ssize_t name(void* o) { \
    JsonTee* t = o; \
    JTEE_EACH(s->member ? s->member(s->opaque) : 0) \
  }

#define JTEE_TEXT(name, member) \
  static ssize_t name(void* o, const char* str, size_t len) { \
    JsonTee* t = o; \
    JTEE_EACH(s->member ? s->member(s->opaque, str, len) : 0) \
  }

JTEE_TEXT(jtee_error, error)
JTEE_PLAIN(jtee_null, null)
JTEE_PLAIN(jtee_true, true_)
JTEE_PLAIN(jtee_false, false_)
JTEE_TEXT(jtee_number, number)
JTEE_TEXT(jtee_string, string)
JTEE_PLAIN(jtee_array_start, array_start)
JTEE_PLAIN(jtee_array_end, array_end)
JTEE_PLAIN(jtee_object_start, object_start)
JTEE_PLAIN(jtee_object_end, object_end)
JTEE_TEXT(jtee_key, key)
JTEE_TEXT(jtee_comment, comment)
JTEE_TEXT(jtee_raw, raw)

JsonEventSink
jtee_sink(JsonTee* tee) {
  return (JsonEventSink){
      tee,
      jtee_error,
      jtee_null,
      jtee_true,
      jtee_false,
      jtee_number,
      jtee_string,
      jtee_array_start,
      jtee_array_end,
      jtee_object_start,
      jtee_object_end,
      jtee_key,
      jtee_comment,
      jtee_raw,
  };
}

/**
 * @}
 */
