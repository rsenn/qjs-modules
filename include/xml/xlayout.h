#ifndef XLAYOUT_H
#define XLAYOUT_H

#include <stddef.h>
#include <sys/types.h>
#include "stream-utils.h"

/**
 * \defgroup xlayout xlayout: indented XML layout
 * @{
 */

/* XmlLayout: the indented layout of xml.write(), driven with explicit depths.
 *
 * ```c
 * XmlLayout l;
 * xlay_init(&l, writer, NULL, NULL);
 * xlay_start(&l, 1, "b", 1);        //   <b
 * xlay_attribute(&l, "id", 2, "7", 1);
 * xlay_open_end(&l, 0);             // >\n
 * xlay_text(&l, 2, 1, "x", 1);      //     x\n
 * xlay_close(&l, 1, "b", 1);        //   </b>\n
 * xlay_finish(&l, 1);
 * xlay_free(&l);
 * ```
 *
 * rule: the caller owns the depth (2 spaces per level) and decides self-closing;
 * whitespace at the end of the output is held back until more output follows,
 * so a non-multiline text and xlay_finish() can drop it. Every call returns 0,
 * or -1 when the Writer refused bytes; none touches a JSContext. */
typedef struct {
  Writer writer;
  size_t written; /* bytes put through the writer so far */
  DynBuf pending; /* trailing whitespace not yet written */
  int flushed;    /* the last byte written, or -1 */
  int tag0;       /* first byte of the start tag in progress */
} XmlLayout;

void xlay_init(XmlLayout*, Writer writer, DynBufReallocFunc* realloc_func, void* opaque);
void xlay_free(XmlLayout*);

/* xlay_start: `<name` after the indent of `depth`; attributes follow
 * (none for a name starting with `!`). */
int xlay_start(XmlLayout*, int depth, const char* name, size_t len);
int xlay_attribute(XmlLayout*, const char* name, size_t nlen, const char* value, size_t vlen);

/* xlay_open_end: ends the start tag and its line.
 *
 *   tag    self_closing  writes
 *   ?xml   any           ?>
 *   !-- !  any           >
 *   other  1 / 0         ` />` / `>`
 */
int xlay_open_end(XmlLayout*, int self_closing);

/* xlay_end_marker: the `</name>` line of a flat list's `{tagName: '/name'}` entry. */
int xlay_end_marker(XmlLayout*, int depth, const char* name, size_t len);

/* xlay_close: `</name>` for an element that had children; indented only when
 * the output so far ends in a newline; nothing for `?` names. */
int xlay_close(XmlLayout*, int depth, const char* name, size_t len);

/* xlay_text: escaped text.
 *
 *   multiline  layout
 *   1          indent(depth), text, newline; each later line re-indented at depth+1
 *   0          trailing whitespace of the output dropped, text follows directly
 *
 * with depth 0 the newlines inside the text are dropped. */
int xlay_text(XmlLayout*, int depth, int multiline, const char* str, size_t len);

/* xlay_finish: `trim` drops the held whitespace (and NULs); otherwise it is written. */
int xlay_finish(XmlLayout*, int trim);

/**
 * @}
 */

#endif
