#ifndef XWRITE_H
#define XWRITE_H

#include <stddef.h>
#include <stdint.h>
#include "stream-utils.h"
#include "xml/xevent.h"
#include "vector.h"

/**
 * \defgroup xwrite xwrite: incremental XML writer
 * @{
 */

/* XmlWriteOptions: how an XmlWriter lays out its output. */
typedef struct {
  unsigned indent : 10; /* spaces per level, with XW_PRETTY */
  unsigned layout : 2;  /* XW_COMPACT or XW_PRETTY */
} XmlWriteOptions;

enum {
  XW_COMPACT, /* no whitespace added: <a><b>t</b><c /></a> */
  XW_PRETTY,  /* newline plus indent before a tag, never next to text */
};

typedef struct {
  uint32_t name_off, name_len; /* the element's name in XmlWriter.names */
} XmlWriterFrame;

/* XmlWriter: push-based incremental XML writer.
 *
 * The caller drives it with events, in document order:
 *
 * ```c
 * XmlWriter wr = {.writer = w, .opts = {.indent = 2, .layout = XW_PRETTY}};
 * xwrite_init(&wr, NULL, NULL);
 * xwrite_element_start(&wr, "a", 1);
 * xwrite_attribute(&wr, "id", 2, "7", 1);
 * xwrite_text(&wr, "x < y", 5);
 * xwrite_element_end(&wr, "a", 1);
 * xwrite_free(&wr);          // <a id="7">x &lt; y</a>
 * ```
 *
 * The start tag stays open for attributes until the next event; an element
 * ended with nothing in between is written `<a />`. Text and attribute
 * values are escaped here, names are written as given. Every event returns
 * the bytes written (>= 0), or -XWRITE_E_* on failure; */
typedef struct {
  Writer writer;
  size_t written; /* bytes put through the writer so far */
  XmlWriteOptions opts;
  Vector stack;      /* XmlWriterFrame, one per open element */
  Vector names;      /* the names of the open elements, back to back */
  unsigned prev : 3; /* the previous event, an XW_PREV_* */
} XmlWriter;

enum {
  XWRITE_E_WRITE = 1,  /* the Writer refused the bytes */
  XWRITE_E_NOMEM,      /* the frame stack could not grow */
  XWRITE_E_UNMATCHED,  /* element_end with nothing open */
  XWRITE_E_MISMATCH,   /* element_end names another element than the open one */
  XWRITE_E_NOT_IN_TAG, /* attribute when no start tag is open */
  XWRITE_E_UNCLOSED,   /* xwrite_finish() with elements still open */
};

/* the text for a failed event's return value: xwrite_error_message(-r) */
const char* xwrite_error_message(int code);

/* xwrite_error_code: the stable kebab-case name of a XWRITE_E_* code (not the negative),
 * what a binding exposes as `err.code`.
 *
 *   XWRITE_E_MISMATCH   mismatched-end-tag      XWRITE_E_UNMATCHED  unmatched-end-tag
 *   XWRITE_E_NOT_IN_TAG attribute-outside-tag   XWRITE_E_UNCLOSED   unclosed-element
 *   XWRITE_E_WRITE      write-failed            XWRITE_E_NOMEM      out-of-memory
 *
 * returns "" for an unknown code. */
const char* xwrite_error_code(int code);

/* xwrite_set_indent: sets the layout from an `indent` option.
 *
 *   xwrite_set_indent(wr, 2)    // XW_PRETTY, 2 spaces per level
 *   xwrite_set_indent(wr, -1)   // XW_COMPACT
 *
 * a negative `indent` is compact; more than 1023 is 1023 (the field's width). Call it before
 * the first event. */
void xwrite_set_indent(XmlWriter*, int indent);

/* xwrite_init: `realloc_func`/`opaque` allocate the stacks; NULL, NULL uses
 * libc realloc. `wr->writer` and `wr->opts` are set by the caller. */
void xwrite_init(XmlWriter*, DynBufReallocFunc* realloc_func, void* opaque);
void xwrite_free(XmlWriter*);

/* The events. `name`/`str` are UTF-8 with an explicit length.
 *
 *   call                                  writes
 *   xwrite_element_start(name)            <name   (the tag stays open)
 *   xwrite_attribute(name, value)         ` name="value"`, or ` name` when value is NULL
 *   xwrite_text(str)                      the escaped text, closing an open tag with >
 *   xwrite_element_end(name)              ` />` if nothing was written since the start,
 *                                         else </name>; name NULL: the open element's
 *   xwrite_markup(name)                   <name> verbatim; <?...?> when name starts with ?
 *   xwrite_comment(str)                   <!--str-->
 *   xwrite_declaration(str)               <!str>
 *   xwrite_instruction(str)               <?str?>
 *   xwrite_cdata(str)                     <![CDATA[str]]>
 *
 * xwrite_markup() is the form of the legacy trees, where a comment is an element
 * named "!-- c --" and a processing instruction one named "?xml version=...".
 * Markup is a complete tag: it never opens an element.
 *
 * xwrite_element_end() with a name that is not the open element's is
 * -XWRITE_E_MISMATCH, with nothing open -XWRITE_E_UNMATCHED. */
ssize_t xwrite_element_start(XmlWriter*, const char* name, size_t len);
ssize_t xwrite_attribute(XmlWriter*, const char* name, size_t nlen, const char* value, size_t vlen);
ssize_t xwrite_text(XmlWriter*, const char* str, size_t len);
ssize_t xwrite_element_end(XmlWriter*, const char* name, size_t len);
ssize_t xwrite_markup(XmlWriter*, const char* name, size_t len);
ssize_t xwrite_comment(XmlWriter*, const char* str, size_t len);
ssize_t xwrite_declaration(XmlWriter*, const char* str, size_t len);
ssize_t xwrite_instruction(XmlWriter*, const char* str, size_t len);
ssize_t xwrite_cdata(XmlWriter*, const char* str, size_t len);

/* xwrite_sink: the sink of an XmlWriter, opaque is wr.
 *
 * the members are the xwrite_*() events; error writes nothing and returns 0 (the
 * output has no form for it), element_end with a NULL name closes the open element.
 * never throws, and wr must outlive the returned struct. */
XmlEventSink xwrite_sink(XmlWriter* wr);

/* xwrite_finish: checks the document is complete.
 *
 * returns 0, or -XWRITE_E_UNCLOSED when elements are still open. */
ssize_t xwrite_finish(XmlWriter*);

/* xwrite_put_escaped: writes `str` with & < > as entities, and " too when `is_attr`.
 *
 *   xwrite_put_escaped(w, "a<b", 3, 0)    // a&lt;b
 *   xwrite_put_escaped(w, "say \"x\"", 7, 1)   // say &quot;x&quot;
 *
 * returns the bytes written, or -1 when the Writer refused them. */
ssize_t xwrite_put_escaped(Writer*, const char* str, size_t len, int is_attr);

/**
 * @}
 */

#endif
