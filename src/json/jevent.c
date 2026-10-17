/* jevent.c: jevent_send().
 * depends on: jevent.h.
 * rule: no state; one call per event, the sink decides what it means. */

#include "json/jevent.h"

ssize_t
jevent_send(const JsonEventSink* sink, const JsonEvent* ev) {
  switch(ev->type) {
    case jr_type_error: return sink->error ? sink->error(sink->opaque, ev->str, ev->len) : 0;
    case jr_type_null: return sink->null ? sink->null(sink->opaque) : 0;
    case jr_type_true: return sink->true_ ? sink->true_(sink->opaque) : 0;
    case jr_type_false: return sink->false_ ? sink->false_(sink->opaque) : 0;
    case jr_type_number: return sink->number ? sink->number(sink->opaque, ev->str, ev->len) : 0;
    case jr_type_string: return sink->string ? sink->string(sink->opaque, ev->str, ev->len) : 0;
    case jr_type_array_start: return sink->array_start ? sink->array_start(sink->opaque) : 0;
    case jr_type_array_end: return sink->array_end ? sink->array_end(sink->opaque) : 0;
    case jr_type_object_start: return sink->object_start ? sink->object_start(sink->opaque) : 0;
    case jr_type_object_end: return sink->object_end ? sink->object_end(sink->opaque) : 0;
    case jr_type_key: return sink->key ? sink->key(sink->opaque, ev->str, ev->len) : 0;
  }

  return 0;
}
