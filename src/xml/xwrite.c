/* xwrite.c: XmlWriter, the xwrite_*() events, xwrite_put_escaped().
 * depends on: stream-utils.h (Writer), vector.h (the stacks).
 * rule: no JSContext or JSValue anywhere; every byte goes through
 * xwrite_out(), so `written` counts all output. */

#define _GNU_SOURCE 1

#include "xml/xwrite.h"
#include <string.h>

/* what the previous event left behind */
enum { XW_PREV_NONE, XW_PREV_START, XW_PREV_ATTRIBUTE, XW_PREV_TEXT, XW_PREV_END, XW_PREV_MARKUP };

static const char* const xwrite_messages[] = {
    0,
    "write failed",
    "out of memory",
    "elementEnd with nothing open",
    "elementEnd names another element than the open one",
    "attribute outside a start tag",
    "elements still open",
};

/* xwrite_error_message: the text for a XWRITE_E_* code (not the negative).
 *
 * returns NULL for an unknown code. */
const char*
xwrite_error_message(int code) {
  return code > 0 && code < (int)(sizeof(xwrite_messages) / sizeof(*xwrite_messages)) ? xwrite_messages[code] : 0;
}

void
xwrite_init(XmlWriter* wr, DynBufReallocFunc* realloc_func, void* opaque) {
  dbuf_init2(&wr->stack, opaque, realloc_func);
  dbuf_init2(&wr->names, opaque, realloc_func);
  wr->written = 0;
  wr->prev = XW_PREV_NONE;
}

/* xwrite_free: frees the writer and the stacks; not `wr` itself. */
void
xwrite_free(XmlWriter* wr) {
  writer_free(&wr->writer);
  vector_free(&wr->stack);
  vector_free(&wr->names);
}

/* the one path for output: counts into `written`.
 *
 * returns len, or -XWRITE_E_WRITE. */
static ssize_t
xwrite_out(XmlWriter* wr, const void* buf, size_t len) {
  if(len == 0)
    return 0;

  if(writer_write(&wr->writer, buf, len) != (ssize_t)len)
    return -XWRITE_E_WRITE;

  wr->written += len;
  return (ssize_t)len;
}

#define XW_TRY(total, call) \
  do { \
    ssize_t r_ = (call); \
    if(r_ < 0) \
      return r_; \
    (total) += r_; \
  } while(0)

static ssize_t
xwrite_outs(XmlWriter* wr, const char* s) {
  return xwrite_out(wr, s, strlen(s));
}

static int
xwrite_open_tag(const XmlWriter* wr) {
  return wr->prev == XW_PREV_START || wr->prev == XW_PREV_ATTRIBUTE;
}

/* newline plus indent for `level` open elements, for XW_PRETTY, when the
 * previous event was not text (a break next to text would become text). */
static ssize_t
xwrite_break(XmlWriter* wr, size_t level) {
  ssize_t w = 0;

  if(wr->opts.layout != XW_PRETTY || wr->prev == XW_PREV_NONE || wr->prev == XW_PREV_TEXT)
    return 0;

  XW_TRY(w, xwrite_out(wr, "\n", 1));

  for(size_t i = 0, n = level * wr->opts.indent; i < n; i++)
    XW_TRY(w, xwrite_out(wr, " ", 1));

  return w;
}

ssize_t
xwrite_put_escaped(Writer* wr, const char* s, size_t len, int is_attr) {
  ssize_t total = 0;
  size_t run = 0;

  for(size_t i = 0; i <= len; i++) {
    const char* ent = 0;

    if(i < len) {
      switch(s[i]) {
        case '&': ent = "&amp;"; break;
        case '<': ent = "&lt;"; break;
        case '>': ent = "&gt;"; break;
        case '"': ent = is_attr ? "&quot;" : 0; break;
      }

      if(!ent)
        continue;
    }

    if(i > run) {
      if(writer_write(wr, s + run, i - run) != (ssize_t)(i - run))
        return -1;

      total += i - run;
    }

    if(ent) {
      size_t n = strlen(ent);

      if(writer_write(wr, ent, n) != (ssize_t)n)
        return -1;

      total += n;
    }

    run = i + 1;
  }

  return total;
}

/* xwrite_out_escaped: xwrite_put_escaped() through xwrite_out()'s accounting. */
static ssize_t
xwrite_out_escaped(XmlWriter* wr, const char* s, size_t len, int is_attr) {
  ssize_t w = xwrite_put_escaped(&wr->writer, s, len, is_attr);

  if(w < 0)
    return -XWRITE_E_WRITE;

  wr->written += w;
  return w;
}

ssize_t
xwrite_element_start(XmlWriter* wr, const char* name, size_t len) {
  XmlWriterFrame frame = {wr->names.size, len};
  ssize_t w = 0;

  if(xwrite_open_tag(wr))
    XW_TRY(w, xwrite_out(wr, ">", 1));

  XW_TRY(w, xwrite_break(wr, vector_size(&wr->stack, sizeof(XmlWriterFrame))));

  if(!vector_put(&wr->names, name, len) || !vector_put(&wr->stack, &frame, sizeof(frame)))
    return -XWRITE_E_NOMEM;

  XW_TRY(w, xwrite_out(wr, "<", 1));
  XW_TRY(w, xwrite_out(wr, name, len));
  wr->prev = XW_PREV_START;
  return w;
}

ssize_t
xwrite_attribute(XmlWriter* wr, const char* name, size_t nlen, const char* value, size_t vlen) {
  ssize_t w = 0;

  if(!xwrite_open_tag(wr))
    return -XWRITE_E_NOT_IN_TAG;

  XW_TRY(w, xwrite_out(wr, " ", 1));
  XW_TRY(w, xwrite_out(wr, name, nlen));

  if(value) {
    XW_TRY(w, xwrite_out(wr, "=\"", 2));
    XW_TRY(w, xwrite_out_escaped(wr, value, vlen, 1));
    XW_TRY(w, xwrite_out(wr, "\"", 1));
  }

  wr->prev = XW_PREV_ATTRIBUTE;
  return w;
}

ssize_t
xwrite_text(XmlWriter* wr, const char* str, size_t len) {
  ssize_t w = 0;

  if(xwrite_open_tag(wr))
    XW_TRY(w, xwrite_out(wr, ">", 1));

  XW_TRY(w, xwrite_out_escaped(wr, str, len, 0));
  wr->prev = XW_PREV_TEXT;
  return w;
}

ssize_t
xwrite_element_end(XmlWriter* wr, const char* name, size_t len) {
  XmlWriterFrame* top;
  const char* open;
  ssize_t w = 0;
  int self_closing = xwrite_open_tag(wr);

  if(vector_empty(&wr->stack))
    return -XWRITE_E_UNMATCHED;

  top = vector_back(&wr->stack, sizeof(XmlWriterFrame));
  open = (const char*)wr->names.buf + top->name_off;

  if(name && (len != top->name_len || memcmp(name, open, len)))
    return -XWRITE_E_MISMATCH;

  if(self_closing) {
    XW_TRY(w, xwrite_out(wr, " />", 3));
  } else {
    /* the break is for the level of the element's own start tag */
    XW_TRY(w, xwrite_break(wr, vector_size(&wr->stack, sizeof(XmlWriterFrame)) - 1));
    XW_TRY(w, xwrite_out(wr, "</", 2));
    XW_TRY(w, xwrite_out(wr, open, top->name_len));
    XW_TRY(w, xwrite_out(wr, ">", 1));
  }

  wr->names.size = top->name_off;
  vector_pop(&wr->stack, sizeof(XmlWriterFrame));
  wr->prev = XW_PREV_END;
  return w;
}

/* a complete tag `prefix str suffix`, laid out like a start tag but opening nothing. */
static ssize_t
xwrite_tag(XmlWriter* wr, const char* prefix, const char* str, size_t len, const char* suffix) {
  ssize_t w = 0;

  if(xwrite_open_tag(wr))
    XW_TRY(w, xwrite_out(wr, ">", 1));

  XW_TRY(w, xwrite_break(wr, vector_size(&wr->stack, sizeof(XmlWriterFrame))));
  XW_TRY(w, xwrite_outs(wr, prefix));
  XW_TRY(w, xwrite_out(wr, str, len));
  XW_TRY(w, xwrite_outs(wr, suffix));
  wr->prev = XW_PREV_MARKUP;
  return w;
}

ssize_t
xwrite_markup(XmlWriter* wr, const char* name, size_t len) {
  return xwrite_tag(wr, "<", name, len, len && name[0] == '?' ? "?>" : ">");
}

ssize_t
xwrite_comment(XmlWriter* wr, const char* str, size_t len) {
  return xwrite_tag(wr, "<!--", str, len, "-->");
}

ssize_t
xwrite_declaration(XmlWriter* wr, const char* str, size_t len) {
  return xwrite_tag(wr, "<!", str, len, ">");
}

ssize_t
xwrite_instruction(XmlWriter* wr, const char* str, size_t len) {
  return xwrite_tag(wr, "<?", str, len, "?>");
}

ssize_t
xwrite_cdata(XmlWriter* wr, const char* str, size_t len) {
  return xwrite_tag(wr, "<![CDATA[", str, len, "]]>");
}

ssize_t
xwrite_finish(XmlWriter* wr) {
  return vector_empty(&wr->stack) ? 0 : -XWRITE_E_UNCLOSED;
}
