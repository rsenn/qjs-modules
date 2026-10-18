/* jevent.c: jevent_send().
 * depends on: jevent.h.
 * rule: no state; one call per event, the sink decides what it means. */

#include "json/jevent.h"

/**
 * \addtogroup jevent
 * @{
 */

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
    case JSON_EVENT_COMMENT: break; /* the pull parser's comment: sinks have no member for it */
  }

  return 0;
}

/**
 * @}
 */
