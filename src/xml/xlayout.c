#define _GNU_SOURCE 1

/* xlayout.c: XmlLayout, the xlay_*() calls of xml.write().
 * depends on: stream-utils.h (Writer), char-utils.h (is_whitespace_char).
 * rule: every byte goes through xlay_put(), which holds trailing whitespace back. */

#include "xml/xlayout.h"
#include "char-utils.h"
#include <string.h>

/**
 * \addtogroup xlayout
 * @{
 */

void
xlay_init(XmlLayout* l, Writer writer, DynBufReallocFunc* realloc_func, void* opaque) {
  l->writer = writer;
  l->written = 0;
  l->flushed = -1;
  l->tag0 = 0;
  dbuf_init2(&l->pending, opaque, realloc_func);
}

void
xlay_free(XmlLayout* l) {
  dbuf_free(&l->pending);
}

static int
xlay_write(XmlLayout* l, const void* buf, size_t len) {
  if(len == 0)
    return 0;

  if(writer_write(&l->writer, buf, len) != (ssize_t)len)
    return -1;

  l->written += len;
  return 0;
}

/* xlay_put: writes `buf`, keeping its trailing whitespace pending.
 *
 * returns 0, or -1 when the Writer refused bytes. */
static int
xlay_put(XmlLayout* l, const char* buf, size_t len) {
  size_t k = len;

  while(k > 0 && is_whitespace_char((unsigned char)buf[k - 1]))
    k--;

  if(k > 0) {
    if(xlay_write(l, l->pending.buf, l->pending.size) < 0 || xlay_write(l, buf, k) < 0)
      return -1;

    l->pending.size = 0;
    l->flushed = (unsigned char)buf[k - 1];
  }

  return dbuf_put(&l->pending, (const uint8_t*)buf + k, len - k) ? -1 : 0;
}

static int
xlay_puts(XmlLayout* l, const char* s) {
  return xlay_put(l, s, strlen(s));
}

static int
xlay_last(const XmlLayout* l) {
  return l->pending.size ? l->pending.buf[l->pending.size - 1] : l->flushed;
}

static int
xlay_indent(XmlLayout* l, int depth) {
  while(depth-- > 0)
    if(xlay_puts(l, "  ") < 0)
      return -1;

  return 0;
}

static int
xlay_escaped(XmlLayout* l, const char* s, size_t len, int is_attr) {
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

    if(i > run && xlay_put(l, s + run, i - run) < 0)
      return -1;

    if(ent && xlay_puts(l, ent) < 0)
      return -1;

    run = i + 1;
  }

  return 0;
}

int
xlay_start(XmlLayout* l, int depth, const char* name, size_t len) {
  l->tag0 = len ? (unsigned char)name[0] : 0;

  if(depth > 0 && xlay_indent(l, depth) < 0)
    return -1;

  return xlay_put(l, "<", 1) < 0 || xlay_put(l, name, len) < 0 ? -1 : 0;
}

int
xlay_attribute(XmlLayout* l, const char* name, size_t nlen, const char* value, size_t vlen) {
  if(l->tag0 == '!')
    return 0;

  if(xlay_put(l, " ", 1) < 0 || xlay_put(l, name, nlen) < 0)
    return -1;

  if(!value)
    return 0;

  if(xlay_puts(l, "=\"") < 0 || xlay_escaped(l, value, vlen, 1) < 0 || xlay_put(l, "\"", 1) < 0)
    return -1;

  return 0;
}

int
xlay_open_end(XmlLayout* l, int self_closing) {
  const char* end = l->tag0 == '?' ? "?>" : self_closing && l->tag0 != '!' ? " />" : ">";

  return xlay_puts(l, end) < 0 || xlay_put(l, "\n", 1) < 0 ? -1 : 0;
}

int
xlay_end_marker(XmlLayout* l, int depth, const char* name, size_t len) {
  if(depth > 0 && xlay_indent(l, depth) < 0)
    return -1;

  return xlay_puts(l, "</") < 0 || xlay_put(l, name, len) < 0 || xlay_puts(l, ">\n") < 0 ? -1 : 0;
}

int
xlay_close(XmlLayout* l, int depth, const char* name, size_t len) {
  if(len == 0 || name[0] == '?')
    return 0;

  if(xlay_last(l) == '\n' && xlay_indent(l, depth) < 0)
    return -1;

  return xlay_puts(l, "</") < 0 || xlay_put(l, name, len) < 0 || xlay_puts(l, ">\n") < 0 ? -1 : 0;
}

int
xlay_text(XmlLayout* l, int depth, int multiline, const char* str, size_t len) {
  int d = multiline ? depth : 0;

  if(multiline) {
    if(xlay_indent(l, depth) < 0)
      return -1;
  } else {
    l->pending.size = 0;
  }

  for(const char* p = str;;) {
    size_t n = memchr(p, '\n', len) ? (size_t)((const char*)memchr(p, '\n', len) - p) : len;

    if(xlay_escaped(l, p, n, 0) < 0)
      return -1;

    if(n < len)
      n++;

    p += n;
    len -= n;

    if(len == 0)
      break;

    if(d > 0 && (xlay_put(l, "\n", 1) < 0 || xlay_indent(l, d + 1) < 0))
      return -1;
  }

  return multiline ? xlay_put(l, "\n", 1) : 0;
}

int
xlay_finish(XmlLayout* l, int trim) {
  if(trim) {
    while(l->pending.size && strchr("\r\n\t \0", l->pending.buf[l->pending.size - 1]))
      l->pending.size--;
  }

  if(xlay_write(l, l->pending.buf, l->pending.size) < 0)
    return -1;

  l->pending.size = 0;
  return 0;
}

/**
 * @}
 */
