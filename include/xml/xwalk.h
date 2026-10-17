#ifndef XWALK_H
#define XWALK_H

#include "walk.h"

/* XwalkShape: names the three keys of a tree of elements; NULL fields = the default.
 *
 * ```js
 * { tagName: 'a', attributes: { href: '/' }, children: ['text', { tagName: 'b' }] }
 * ```
 *
 *   field       default       holds
 *   tag         "tagName"     the element's name; a value
 *   attributes  "attributes"  an object, walked by its own enumerable keys; its values are values
 *   children    "children"    an array, walked by index; an object item is an element, the rest values
 *
 * An element reports exactly those three keys, in that order, each read with a plain
 * property get (a prototype getter works) and skipped when undefined. Any other
 * property is never read, so a back-link such as `parentNode` is never followed.
 * A root array is a list of elements (and texts). */
typedef struct {
  const char *tag, *attributes, *children;
} XwalkShape;

/* XwalkIterator: a walk.h WalkIterator that walks a tree of elements.
 *
 * ```c
 * XwalkIterator it;
 * xwalk_init(&it, ctx, tree, iface, NULL);   // NULL: tagName, attributes, children
 * while((r = xwalk_next(&it, ctx)) > 0) {}
 * xwalk_close(&it, ctx);                     // r is 0 when done, -1 on error
 * ```
 *
 * `<a href="/">hi</a>` as a tree gives: object_start, key(tagName), value(a),
 * key(attributes), object_start, key(href), value(/), object_end, key(children),
 * array_start, value(hi), array_end, object_end. */
typedef struct {
  WalkIterator walk; /* the stepper; its hooks point back at this struct */
  JSAtom atom[3];    /* tag, attributes, children */
} XwalkIterator;

int xwalk_init(XwalkIterator*, JSContext*, JSValueConst root, WalkInterface iface, const XwalkShape* shape);
void xwalk_close(XwalkIterator*, JSContext*);
void xwalk_close_rt(XwalkIterator*, JSRuntime*);

static inline int
xwalk_next(XwalkIterator* it, JSContext* ctx) {
  return walk_next(&it->walk, ctx);
}

static inline int
xwalk_skip(XwalkIterator* it, JSContext* ctx) {
  return walk_skip(&it->walk, ctx);
}

static inline uint32_t
xwalk_depth(const XwalkIterator* it) {
  return walk_depth(&it->walk);
}

#endif
