/* jevent.c: jevent_send(), jevent_number().
 * depends on: jevent.h.
 * rule: no state; one call per event, the sink decides what it means. */

#define _GNU_SOURCE 1

#include "json/jevent.h"
#include "char-utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * \addtogroup jevent
 * @{
 */

double
jevent_number(const char* str, size_t len) {
  char stack[64], *buf = len < sizeof(stack) ? stack : malloc(len + 1);
  const char* p = buf;
  double sign = 1, num = 0;

  if(!buf)
    return 0;

  memcpy(buf, str, len);
  buf[len] = '\0';

  if(*p == '+' || *p == '-')
    sign = *p++ == '-' ? -1 : 1;

  if(!strcmp(p, "Infinity")) {
    num = INFINITY;
  } else if(!strcmp(p, "NaN")) {
    num = NAN;
  } else if(p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    for(p += 2; *p; p++)
      num = num * 16 + (*p <= '9' ? *p - '0' : (*p | 0x20) - 'a' + 10);
  } else {
    scan_double(p, &num);
  }

  if(buf != stack)
    free(buf);

  return sign * num;
}

ssize_t
jevent_send(const JsonEventSink* sink, const JsonEvent* ev) {
  switch(ev->type) {
    case JSON_EVENT_ERROR: return sink->error ? sink->error(sink->opaque, ev->str, ev->len) : 0;
    case JSON_EVENT_NULL: return sink->null ? sink->null(sink->opaque) : 0;
    case JSON_EVENT_TRUE: return sink->true_ ? sink->true_(sink->opaque) : 0;
    case JSON_EVENT_FALSE: return sink->false_ ? sink->false_(sink->opaque) : 0;
    case JSON_EVENT_NUMBER: return sink->number ? sink->number(sink->opaque, ev->str, ev->len) : 0;
    case JSON_EVENT_STRING: return sink->string ? sink->string(sink->opaque, ev->str, ev->len) : 0;
    case JSON_EVENT_ARRAY_START: return sink->array_start ? sink->array_start(sink->opaque) : 0;
    case JSON_EVENT_ARRAY_END: return sink->array_end ? sink->array_end(sink->opaque) : 0;
    case JSON_EVENT_OBJECT_START: return sink->object_start ? sink->object_start(sink->opaque) : 0;
    case JSON_EVENT_OBJECT_END: return sink->object_end ? sink->object_end(sink->opaque) : 0;
    case JSON_EVENT_KEY: return sink->key ? sink->key(sink->opaque, ev->str, ev->len) : 0;
    case JSON_EVENT_COMMENT: return sink->comment ? sink->comment(sink->opaque, ev->str, ev->len) : 0;
    case JSON_EVENT_RAW: return sink->raw ? sink->raw(sink->opaque, ev->str, ev->len) : 0;
  }

  return 0;
}

ssize_t
jevent_handler(void* sink, const JsonEvent* ev) {
  return jevent_send(sink, ev);
}

ssize_t
jevent_pump(JsonEventSource src, const JsonEventSink* sink) {
  JsonEvent ev;
  JsonPull st;
  ssize_t r;

  while((st = src.next(src.opaque, &ev)) == JSON_PULL_EVENT)
    if((r = jevent_send(sink, &ev)) < 0)
      return r;

  return st;
}

/**
 * @}
 */
