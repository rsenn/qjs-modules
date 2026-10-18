#ifndef JWALK_H
#define JWALK_H

#include "walk.h"

/**
 * \defgroup jwalk jwalk: walk a JS value as JSON events
 * @{
 */

/* JwalkIterator: walks a JS value and reports it to a WalkInterface, one
 * event per jwalk_next() call (a walk.h WalkIterator with the JSON hooks),
 * with JSON.stringify's rules for what a value is.
 *
 * ```js
 * { a: new Date(0), b: undefined, c: [1, , 2n] }   // JSON.stringify
 * ```
 *
 *   JS value                              reported as
 *   toJSON() result, replacer() result    the result (toJSON first, then the replacer)
 *   null, boolean, string                 value()
 *   finite number                         value()
 *   NaN, Infinity                         value(null)
 *   Number/String/Boolean object          value() of its primitive
 *   undefined, symbol, function           nothing in an object (key and value skipped);
 *                                         value(null) in an array; no events at the root
 *   array hole                            value(null)
 *   object, array                         object_start/array_start ... end
 *   JSON.rawJSON() object                 raw(), or value() of the object when raw is NULL
 *   bigint                                TypeError
 *   circular reference                    TypeError
 *
 * Only the array's indices are walked, only an object's own enumerable string
 * keys, in the order JSON.stringify uses.
 *
 * ```c
 * JwalkIterator it;
 * jwalk_init(&it, ctx, value, iface);
 * while((r = jwalk_next(&it, ctx)) > 0) {}
 * jwalk_close(&it, ctx);   // r is 0 when done, -1 on error
 *
 * // skip the contents of anything nested deeper than 2:
 * while((ev = jwalk_next(&it, ctx)) > 0)
 *   if((ev == WALK_EVENT_OBJECT_START || ev == WALK_EVENT_ARRAY_START) && jwalk_depth(&it) > 2)
 *     jwalk_skip(&it, ctx);
 * ```
 *
 * {a: [1]} gives: object_start, key("a"), array_start, value(1),
 * array_end, object_end. */
typedef struct {
  WalkIterator walk;      /* the stepper; its hooks point back at this struct */
  JSValue replacer;       /* function(key, value) called with the holder as this, or undefined */
  JSValue holder;         /* {"": root} while a replacer function is set, else undefined */
  JSValue is_raw;         /* JSON.isRawJSON, or undefined */
  JSAtom* allow;          /* keys an array replacer lets through, in its order; NULL = all */
  uint32_t allow_len;
} JwalkIterator;

int jwalk_init(JwalkIterator*, JSContext*, JSValueConst root, WalkInterface iface);
int jwalk_set_replacer(JwalkIterator*, JSContext*, JSValueConst replacer);
void jwalk_close(JwalkIterator*, JSContext*);
void jwalk_close_rt(JwalkIterator*, JSRuntime*);

/* jwalk_next, jwalk_skip, jwalk_depth: walk_next, walk_skip, walk_depth */
static inline int
jwalk_next(JwalkIterator* it, JSContext* ctx) {
  return walk_next(&it->walk, ctx);
}

static inline int
jwalk_skip(JwalkIterator* it, JSContext* ctx) {
  return walk_skip(&it->walk, ctx);
}

static inline uint32_t
jwalk_depth(const JwalkIterator* it) {
  return walk_depth(&it->walk);
}

/* true when the walk ended without a single event because the root has no JSON form
 * (JSON.stringify(undefined) is undefined, not "null"). */
static inline int
jwalk_root_omitted(const JwalkIterator* it) {
  return walk_root_omitted(&it->walk);
}

/**
 * @}
 */

#endif
