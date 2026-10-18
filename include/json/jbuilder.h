#ifndef JBUILDER_H
#define JBUILDER_H

#include <stddef.h>
#include <quickjs.h>
#include "json/jevent.h"
#include "json/jread.h"

/**
 * \defgroup jbuilder jbuilder: JS value builder from JSON events
 * @{
 */

typedef struct json_builder_frame JsonBuilderFrame; /* private to jbuilder.c */

/* JsonBuilder: builds a JS value from a stream of JSON events.
 *
 *   JsonBuilder b;
 *   jbuild_init(&b, ctx);
 *   jbuild_push(&b, JSON_EVENT_OBJECT_START);
 *   jbuild_key(&b, "a", 1);
 *   jbuild_value(&b, JSON_EVENT_NUMBER, "1", 1);
 *   jbuild_pop(&b);
 *   JSValue v = jbuild_root(&b);   v is {a: 1}
 *   jbuild_free(&b, rt);
 *
 * Fed from jread callbacks (JsonPushParser), the event types are JsonEventType. */
typedef struct json_builder {
  JSContext* ctx;
  JsonBuilderFrame* top;
  JSValue root;
  unsigned has_root : 1;
  unsigned plain_objects : 1; /* objects get Object.prototype instead of no prototype */
  JSValue done;               /* array of the completed top-level values not yet taken, or undefined */
  uint32_t done_len;
  /* called each time a top-level value is complete, after it is appended to `done`; a pump
     driver reads the parser position here (JSONL.parseChunk's `read`). */
  void (*on_root)(void* user, struct json_builder* b);
  void* on_root_user;
} JsonBuilder;

void jbuild_init(JsonBuilder*, JSContext*);
void jbuild_push(JsonBuilder*, JsonEventType type);
void jbuild_pop(JsonBuilder*);
void jbuild_key(JsonBuilder*, const char* name, size_t len);
void jbuild_value(JsonBuilder*, JsonEventType type, const char* data, size_t len);

/* jbuild_value_js: adds a finished JS value where the next scalar would go.
 *
 * refcount: takes ownership of val. never throws. */
void jbuild_value_js(JsonBuilder*, JSValue val);

/* jbuild_take: the top-level values completed since the last call, as a new array; clears the queue.
 *
 *   {"a":1} 2 [3]  fed as three documents gives [{a: 1}, 2, [3]]
 *
 * for multi-document input (JSONL), where root holds only the last one. refcount: a new reference. */
JSValue jbuild_take(JsonBuilder*);
/* jbuild_top: the innermost open container (refcount: a new reference), or undefined. */
JSValue jbuild_top(JsonBuilder*);
JSValue jbuild_path(JsonBuilder*);
JSValue jbuild_root(JsonBuilder*);
void jbuild_free(JsonBuilder*, JSRuntime*);

/* jbuild_sink: the sink of a JsonBuilder, opaque is b.
 *
 * the members are the jbuild_*() events (jr_type_* ids mapped to push/pop/key/value),
 * error does nothing and every member returns 0.
 * b must outlive the returned struct. */
JsonEventSink jbuild_sink(JsonBuilder* b);

/**
 * @}
 */

#endif
