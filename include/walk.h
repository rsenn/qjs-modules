#ifndef WALK_H
#define WALK_H

#include <stddef.h>
#include <quickjs.h>
#include "vector.h"
#include "property-enumeration.h"

/* WalkInterface: the events a walk of a value tree reports, one callback each.
 *
 * Every callback returns the bytes written (>= 0), or -1 with an
 * exception pending. `opaque` is passed as the first argument. */
typedef struct {
  void* opaque;
  ssize_t (*key)(void*, JSContext*, JSValueConst);
  ssize_t (*object_start)(void*, JSContext*);
  ssize_t (*array_start)(void*, JSContext*);
  ssize_t (*object_end)(void*, JSContext*);
  ssize_t (*array_end)(void*, JSContext*);
  ssize_t (*value)(void*, JSContext*, JSValueConst);
} WalkInterface;

/* what walk_next() reports: its return value is the event kind (> 0) */
typedef enum {
  WALK_EVENT_KEY = 1,
  WALK_EVENT_OBJECT_START,
  WALK_EVENT_ARRAY_START,
  WALK_EVENT_OBJECT_END,
  WALK_EVENT_ARRAY_END,
  WALK_EVENT_VALUE,
} WalkEvent;

/* WalkIterator: walks a JS value and reports it to a WalkInterface, one
 * event per walk_next() call.
 *
 * ```c
 * WalkIterator it;
 * walk_init(&it, ctx, value, iface);
 * while((r = walk_next(&it, ctx)) > 0) {}
 * walk_close(&it, ctx);   // r is 0 when done, -1 on error
 *
 * // skip the contents of anything nested deeper than 2:
 * while((ev = walk_next(&it, ctx)) > 0)
 *   if((ev == WALK_EVENT_OBJECT_START || ev == WALK_EVENT_ARRAY_START) && walk_depth(&it) > 2)
 *     walk_skip(&it, ctx);
 * ```
 *
 * {a: [1]} gives: object_start, key("a"), array_start, value(1),
 * array_end, object_end. */
typedef struct {
  Vector stack;        /* PropertyEnumeration, one per open container */
  WalkInterface walk;  /* the callbacks, copied by value */
  int flags;           /* JS_GPN_* flags for the property enumeration */
  JSValue root;        /* the value being walked, owned */
  unsigned state : 2;  /* WALK_START, WALK_RUNNING, WALK_DONE */
  unsigned key_done : 1; /* key reported for the top frame's property */
  unsigned entered : 1;  /* the last event was a container start */
} WalkIterator;

int walk_init(WalkIterator*, JSContext*, JSValueConst root, WalkInterface iface);
int walk_next(WalkIterator*, JSContext*);
int walk_skip(WalkIterator*, JSContext*);
void walk_close(WalkIterator*, JSContext*);

/* open containers, counting the one just started: 1 at the root */
static inline uint32_t
walk_depth(const WalkIterator* it) {
  return vector_size(&it->stack, sizeof(PropertyEnumeration));
}

typedef WalkInterface WalkFromOpaque(void* opaque);

int js_walk_register(JSClassID, WalkFromOpaque*);
int js_walk_define(JSContext*, JSValueConst obj);
JSValue js_walk_wrap(JSContext*, WalkInterface iface);

#endif
