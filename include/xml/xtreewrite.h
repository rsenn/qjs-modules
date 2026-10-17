#ifndef XTREEWRITE_H
#define XTREEWRITE_H

#include <quickjs.h>
#include "stream-utils.h"

/* xwrite_tree: xml.write() of an element tree or a flat list; xwalk events in, XmlLayout bytes out.
 *
 * ```c
 * DynBuf db;
 * dbuf_init2(&db, 0, 0);
 * if(xwrite_tree(ctx, tree, INT32_MAX, 0, writer_from_dynbuf(&db)) == 0) {}  // db holds the text
 * ```
 *
 *   JSValueConst  tree       array of `{tagName, attributes, children}` elements and texts
 *   int32_t       max_depth  levels of children written; an element cut off is left open
 *   int           flat       1: a flat list `[{tagName}, 'text', {tagName: '/x'}]`, 0: a tree
 *   Writer        writer     destination
 *
 * returns 0, or -1 with an exception pending. */
int xwrite_tree(JSContext*, JSValueConst tree, int32_t max_depth, int flat, Writer writer);

#endif
