#ifndef XWALK_H
#define XWALK_H

#include "walk.h"
#include "xml/xevent.h"

/**
 * \defgroup xwalk xwalk: walk an element tree as XML events
 * @{
 */

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
 * A root array is a list of elements (and texts): a tree's top level, or a whole flat list,
 * whose elements have no `children` and whose `{tagName: '/x'}` end markers are plain
 * elements here; what they mean is the consumer's (quickjs-xml.c, xml_write_tree). */
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

/* XwalkSource flags */
enum {
  XWALK_FLAT = 1,     /* the root is a flat list: no `children`, {tagName: '/x'} entries are end events */
  XWALK_BALANCED = 2, /* flat only: every start gets an end, an end names an open element or is dropped */
};

/* XwalkSource: an element tree or flat list as a pull source of XmlEvent.
 *
 * ```c
 * XwalkSource src;
 * xwalk_source_init(&src, ctx, tree, 0, NULL);
 * xevent_pump(xwalk_source(&src), &sink);   // sink: an XmlWriter, an XmlBuilder, ...
 * xwalk_source_close(&src, ctx);
 * ```
 *
 * `<a href="/">hi</a>` as a tree gives element_start a, attribute href /, text hi, element_end a.
 *
 *   tree part                          event
 *   tagName "a"                        XE_ELEMENT_START a
 *   attributes {k: "v"}                XE_ATTRIBUTE k v; `true` is a bare attribute, other values their text
 *   string in children or root list    XE_TEXT
 *   end of an element                  XE_ELEMENT_END (a tree), a {tagName: '/a'} entry (flat)
 *   tagName "!--c--", "!DOCTYPE x",
 *   "![CDATA[c]]"                      XE_COMMENT, XE_DECLARATION, XE_CDATA; attributes and children ignored
 *   tagName "?xml", attributes         XE_INSTRUCTION with the attributes folded into its text
 *
 * `max_depth` cuts the children below that many levels: the element at the cut is reported
 * without them, so it is written <b />. In a flat list an element stays open until a marker for it. With
 * XWALK_BALANCED a marker for an outer element first ends the elements inside it, a marker for no
 * open element is dropped and the end of the list ends what is still open.
 *
 * text is valid until the next call. The source must not move while in use. */
typedef struct {
  XwalkIterator it;
  JSContext* ctx;
  int flags;
  int32_t max_depth; /* INT32_MAX: all */
  Vector frames;     /* XwalkFrame, one per open container */
  Vector open;       /* XwalkOpen, the open elements of a balanced flat list */
  Vector queue;      /* XwalkItem, events made by the last walk step and not yet returned */
  DynBuf text;       /* the text of the queued events */
  DynBuf attr;       /* the attribute name just reported */
  DynBuf pi;         /* the instruction being collected */
  size_t qpos;
} XwalkSource;

/* xwalk_source_init: starts a walk of `root`.
 *
 *   flags  XWALK_FLAT, XWALK_BALANCED; set source->max_depth afterwards to cut the children
 *
 * returns 0, or -1 with an exception pending. */
int xwalk_source_init(XwalkSource*, JSContext*, JSValueConst root, int flags, const XwalkShape* shape);

/* xwalk_event: the next event of the walk, written to *ev.
 *
 * returns XML_PULL_EVENT, XML_PULL_END, or XML_PULL_FAIL with an exception pending. */
XmlPull xwalk_event(XwalkSource*, XmlEvent* ev);

/* xwalk_source: the walk as an XmlEventSource, for xevent_pump(). */
XmlEventSource xwalk_source(XwalkSource*);

/* xwalk_source_close_rt, xwalk_source_close: free everything; never throws, safe after done or error. */
void xwalk_source_close_rt(XwalkSource*, JSRuntime*);
void xwalk_source_close(XwalkSource*, JSContext*);

/**
 * @}
 */

#endif
