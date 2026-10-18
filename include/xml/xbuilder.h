#ifndef XBUILDER_H
#define XBUILDER_H

#include <stddef.h>
#include <quickjs.h>
#include "json/jbuilder.h"
#include "xml/xevent.h"

/**
 * \defgroup xbuilder xbuilder: element tree builder from XML events
 * @{
 */

/* XmlBuilder: builds the {tagName, attributes, children} tree from XML events; the twin
 * of JsonBuilder, layered on it (it owns no frame stack of its own).
 *
 * ```c
 * XmlBuilder b;
 * xbuild_init(&b, ctx, 0);
 * xbuild_start(&b, "a", 1);
 * xbuild_attribute(&b, "id", 2, "7", 1);
 * xbuild_text(&b, "x", 1);
 * xbuild_end(&b, "a", 1, 0);
 * JSValue v = xbuild_root(&b);   // [{tagName: "a", attributes: {id: "7"}, children: ["x"]}]
 * xbuild_free(&b, rt);
 * ```
 *
 * The root is an array of the top-level elements and texts. Shape of an element:
 *
 *   element                     tagName   attributes   children
 *   normal                      yes       object       array; only when a child, a text or an
 *                                                      end that is not self-closing came
 *   self-closing, <a/>          yes       object       none
 *   name starting `!` or `-`    yes       none         none (comments and declarations)
 *
 * `attributes` holds the string of each attribute, `true` for one without a value. The partial
 * tree is always reachable from xbuild_root(). */
typedef struct XmlBuilder {
  JsonBuilder json;
  JSValue names;  /* tag names of the open elements, root first */
  JSValue last;   /* the element or end marker the latest start/end made, or undefined */
  uint32_t depth; /* open elements */
  unsigned state : 2;     /* what the innermost element has open: XB_* */
  unsigned flat : 1;      /* XB_FLAT */
  unsigned skip_end : 1;  /* flat: the next end belongs to a comment/declaration */
  unsigned open_flat : 1; /* flat: an element object is still open for attributes */
} XmlBuilder;

enum {
  XB_PLAIN = 1, /* objects have Object.prototype (read() trees); default: no prototype */
  XB_FLAT = 2,  /* a flat list: every element is a root entry, an end is a {tagName: "/name"} entry */
};

void xbuild_init(XmlBuilder*, JSContext*, unsigned flags);

/* xbuild_start: opens an element.
 * xbuild_attribute: `value` NULL stores `true`; ignored where the element takes none.
 * xbuild_text: a string child, or a top-level string when nothing is open.
 * xbuild_end: closes the innermost element (the name is not checked; flat lists use it for
 *   the marker); `self_closing` leaves `children` out of an element that has none.
 * xbuild_finish: ends every element still open, as ended by the end of the input. */
void xbuild_start(XmlBuilder*, const char* name, size_t len);
void xbuild_attribute(XmlBuilder*, const char* name, size_t nlen, const char* value, size_t vlen);
void xbuild_text(XmlBuilder*, const char* str, size_t len);
void xbuild_end(XmlBuilder*, const char* name, size_t len, int self_closing);
void xbuild_finish(XmlBuilder*);

/* xbuild_last: the element (or flat end marker) the latest xbuild_start()/xbuild_end() made
 * (refcount: a new reference), or undefined; for attaching data to it, such as a location. */
JSValue xbuild_last(XmlBuilder*);

/* xbuild_depth: the number of open elements. */
uint32_t xbuild_depth(const XmlBuilder*);

/* xbuild_root: the top-level array (refcount: a new reference). */
JSValue xbuild_root(XmlBuilder*);

/* xbuild_path: the tag names from the root down to the innermost open element, a new array. */
JSValue xbuild_path(XmlBuilder*);

/* xbuild_free: releases everything, also an unfinished tree, rt because finalizers have no context.
 * dup what you need out of xbuild_root() first. */
void xbuild_free(XmlBuilder*, JSRuntime*);

/* xbuild_handler: an XmlEvent into the builder, the XmlEventHandler form of xbuild_sink().
 *
 *   event            builder gets
 *   element_start    xbuild_start
 *   attribute        xbuild_attribute
 *   text             xbuild_text
 *   element_end      xbuild_end, with ev->self_closing
 *   comment          a node {tagName: "!--text--"}, no attributes and no children
 *   declaration      a node {tagName: "!text"}
 *   cdata            a node {tagName: "![CDATA[text]]"}
 *   instruction      an element {tagName: "?name", attributes} that stays open, so what follows
 *                    is its children, as xml_parse() delivers it (text: name k="v" k2 ...)
 *   error            nothing
 *
 * returns 0. user is the XmlBuilder. */
ssize_t xbuild_handler(void* user, const XmlEvent* ev);

/* xbuild_sink: the sink of an XmlBuilder, opaque is b.
 *
 * the members do what xbuild_handler() does (an end is never self-closing, the sink has no
 * flag for it), every member returns 0. b must outlive the returned struct. */
XmlEventSink xbuild_sink(XmlBuilder* b);

/**
 * @}
 */

#endif /* XBUILDER_H */
