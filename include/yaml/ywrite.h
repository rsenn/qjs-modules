#ifndef YWRITE_H
#define YWRITE_H

#include <quickjs.h>
#include <cutils.h>

/**
 * \defgroup ywrite ywrite: minimal YAML writer
 * @{
 */

/* ywrite_*: append block-style YAML for a JS value to a DynBuf.
 *
 *   DynBuf db;
 *   dbuf_init_ctx(ctx, &db);
 *   ywrite_document(ctx, &db, val, 2);   val {a: [1, 2]} gives "a:\n  - 1\n  - 2\n"
 *
 * Supports null, undefined, booleans, numbers, strings, arrays and plain objects.
 */

/** quote s as a double-quoted YAML string, escaping as needed. */
void ywrite_quoted(DynBuf* db, const char* s, size_t len);

/** write s plain when that reads back as the same string, else quoted. */
void ywrite_string(DynBuf* db, const char* s, size_t len);

/**
 * write one scalar, never a collection.
 *
 * \return int  0, or -1 with an exception pending (unsupported type).
 */
int ywrite_scalar(JSContext* ctx, DynBuf* db, JSValueConst val);

/**
 * write val as the value half of a key: or - line.
 *
 * \return int  0, or -1 with an exception pending.
 */
int ywrite_node(JSContext* ctx, DynBuf* db, JSValueConst val, int indent, int depth);

/** write the own enumerable string-keyed properties of obj, one key: per line. */
int ywrite_mapping(JSContext* ctx, DynBuf* db, JSValueConst obj, int indent, int depth);

/** write the elements of arr, one - per line. */
int ywrite_sequence(JSContext* ctx, DynBuf* db, JSValueConst arr, int indent, int depth);

/**
 * write val as a whole document, ending in a newline.
 *
 *   JSContext*   ctx     context
 *   DynBuf*      db      output, appended to
 *   JSValueConst val     value to write
 *   int          indent  spaces per nesting level
 *
 * \return int  0, or -1 with an exception pending.
 */
int ywrite_document(JSContext* ctx, DynBuf* db, JSValueConst val, int indent);

/** @} */

#endif /* YWRITE_H */
