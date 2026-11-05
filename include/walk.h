#ifndef WALK_H
#define WALK_H

#include <stddef.h>
#include <quickjs.h>
#include "vector.h"
#include "property-enumeration.h"

/**
 * \defgroup walk walk: generic value tree walk
 * @{
 */

/* what walk_next() reports: its return value is the event kind (> 0), also left in WalkIterator.event */
typedef enum {
  WALK_EVENT_KEY = 1,
  WALK_EVENT_OBJECT_START,
  WALK_EVENT_ARRAY_START,
  WALK_EVENT_OBJECT_END,
  WALK_EVENT_ARRAY_END,
  WALK_EVENT_VALUE,
} WalkEvent;

/* what WalkHooks.prepare makes of a property */
typedef enum {
  WALK_KIND_OMIT,      /* not reported in an object; value(null) in an array */
  WALK_KIND_VALUE,     /* value() */
  WALK_KIND_CONTAINER, /* object_start/array_start ... end */
  WALK_KIND_RAW,       /* raw(), or value() when the interface has no raw */
} WalkKind;

/* WalkHooks: how a walk decides what a property is; all optional.
 *
 *   prepare   (holder, key, &val): in `*val` the property value (owned), out what to
 *             report (owned); returns a WalkKind, or -1 with an exception pending
 *             (`*val` is freed). holder is undefined for the root, whose key is "".
 *             NULL: objects are containers, everything else a value.
 *   keys      (top, array): called when a container is entered; may replace the
 *             atoms in top->tab_atom (the keys to visit). returns 0, or -1 with an
 *             exception pending.
 *   circular  message of the TypeError for a container inside itself; NULL: a default.
 *
 * refcount: prepare owns and returns `*val`; keys owns top->tab_atom while it replaces it. */
typedef struct {
  void* opaque;
  int (*prepare)(void*, JSContext*, JSValueConst holder, JSValueConst key, JSValue* val);
  int (*keys)(void*, JSContext*, PropertyEnumeration* top, int array);
  const char* circular;
} WalkHooks;

/* WalkIterator: walks a JS value, one event per walk_next() call; what a value is
 * comes from the WalkHooks. The event is left in the iterator:
 *
 *   event  the WalkEvent, 0 before the first call and after the end
 *   val    KEY: the key, VALUE: the value (null for an omitted array item), else undefined
 *   raw    VALUE: val is a raw JSON object, the hook said WALK_KIND_RAW
 *
 * borrowed: val is owned by the iterator, valid until the next walk_next() or close.
 *
 * ```c
 * WalkIterator it;
 * walk_init(&it, ctx, value, hooks);
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
 * array_end, object_end. Only an array's indices and an object's own
 * enumerable string keys are walked. JSON.stringify's rules are jwalk's. */
typedef struct {
  Vector stack;                    /* PropertyEnumeration, one per open container */
  Vector meta;                     /* array position per open container, parallel to stack */
  uintptr_t* active;               /* hash set of the objects in `stack`: the circularity check is O(1) */
  uint32_t active_cap, active_len; /* slots (a power of two or 0) and used slots */
  JSValue val;                     /* the value of the event just made, owned */
  int event;                       /* the WalkEvent just made, 0 for none */
  WalkHooks hooks;                 /* copied by value */
  int flags;                       /* JS_GPN_* flags for the property enumeration */
  JSValue root;                    /* the value being walked, owned */
  JSValue pending;                 /* value of the key just reported, owned, until its own event */
  unsigned state : 2;              /* start, running, done */
  unsigned key_done : 1;           /* key reported for the top frame's property */
  unsigned entered : 1;            /* the last event was a container start */
  unsigned pending_kind : 2;       /* what `pending` is: a WalkKind */
  unsigned root_omitted : 1;       /* the root was omitted by prepare: no events at all */
  unsigned raw : 1;                /* `val` is a WALK_KIND_RAW value */
} WalkIterator;

int walk_init(WalkIterator*, JSContext*, JSValueConst root, WalkHooks hooks);
int walk_next(WalkIterator*, JSContext*);
int walk_skip(WalkIterator*, JSContext*);
void walk_close(WalkIterator*, JSContext*);
void walk_close_rt(WalkIterator*, JSRuntime*);

/* true when the walk ended without a single event because prepare omitted the root */
static inline int
walk_root_omitted(const WalkIterator* it) {
  return it->root_omitted;
}

/* open containers, counting the one just started: 1 at the root */
static inline uint32_t
walk_depth(const WalkIterator* it) {
  return vector_size(&it->stack, sizeof(PropertyEnumeration));
}

/* the frame of the open container `back` levels below the top (0: the one just entered), or NULL */
#define WALK_FRAME(it, back) ((PropertyEnumeration*)vector_at(&(it)->stack, sizeof(PropertyEnumeration), (int32_t)walk_depth(it) - 1 - (back)))
#define WALK_TOP(it) WALK_FRAME(it, 0)

/**
 * @}
 */

#endif
