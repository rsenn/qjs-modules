/* xtreewrite.c: xwrite_tree(), xml.write() of an element tree or a flat list.
 * depends on: xwalk.h (the events), xlayout.h (the bytes).
 * rule: the start tag is written when its element ends or gets children, so
 * self-closing follows from `children`. */

#define _GNU_SOURCE 1

#include "xml/xtreewrite.h"
#include "xml/xlayout.h"
#include "xml/xwalk.h"
#include "vector.h"
#include <string.h>
#include <strings.h>

/* what an XmlTreeFrame is: a list of items, an element, or an element's attributes */
enum { XTW_LIST, XTW_ELEMENT, XTW_ATTRS };

typedef struct {
  int kind;
  char* tag;       /* ELEMENT: its tagName, owned; NULL until the key is seen */
  size_t len;
  int depth;       /* ELEMENT: layout depth of its tags */
  unsigned open : 1;    /* ELEMENT: start tag not ended yet */
  unsigned closes : 1;  /* ELEMENT: children were written, so a close tag follows */
  int key;         /* ELEMENT: which key is current, 1 tagName 2 attributes 3 children */
} XmlTreeFrame;

/* XmlTreeWriter: xml.write() of an element tree; xwalk events in, XmlLayout calls out. */
typedef struct {
  XmlLayout lay;
  XwalkIterator it;
  Vector frames; /* XmlTreeFrame, one per open container */
  char* attr;    /* ATTRS: the attribute name just reported, owned */
  int32_t max_depth;
  unsigned flat : 1;        /* flat list: no `children`, end markers are entries */
  int depth;     /* flat: the depth of the next tag; moves on `dt` and `/x` */
  unsigned single_line : 1; /* flat: the last entry was a text without newline */
  char* pend;    /* flat: the element whose start tag waits for the next entry, owned */
  size_t pend_len;
  unsigned pend_dt : 1;     /* flat: pend is a `dt`, the next entries nest */
} XmlTreeWriter;

static XmlTreeFrame*
xtw_top(XmlTreeWriter* tw) {
  return vector_empty(&tw->frames) ? 0 : vector_back(&tw->frames, sizeof(XmlTreeFrame));
}

static ssize_t
xtw_fail(JSContext* ctx) {
  if(!JS_HasException(ctx))
    JS_ThrowInternalError(ctx, "xml.write(): out of memory");

  return -1;
}

static ssize_t
xtw_key(void* opaque, JSContext* ctx, JSValueConst key) {
  XmlTreeWriter* tw = opaque;
  XmlTreeFrame* f = xtw_top(tw);
  const char* s = JS_ToCString(ctx, key);

  if(!s)
    return -1;

  if(f->kind == XTW_ELEMENT) {
    f->key = !strcmp(s, "tagName") ? 1 : !strcmp(s, "attributes") ? 2 : 3;
  } else if(f->kind == XTW_ATTRS) {
    js_free(ctx, tw->attr);
    tw->attr = js_strdup(ctx, s);
  }

  JS_FreeCString(ctx, s);
  return 0;
}

static ssize_t
xtw_push(XmlTreeWriter* tw, JSContext* ctx, int kind) {
  XmlTreeFrame* f;

  if(!(f = vector_emplace(&tw->frames, sizeof(XmlTreeFrame))))
    return xtw_fail(ctx);

  memset(f, 0, sizeof(*f));
  f->kind = kind;
  return 0;
}

static ssize_t
xtw_object_start(void* opaque, JSContext* ctx) {
  XmlTreeWriter* tw = opaque;
  XmlTreeFrame* f = xtw_top(tw);
  int kind = !f || f->kind == XTW_LIST ? XTW_ELEMENT : XTW_ATTRS;
  int level = walk_depth(&tw->it.walk) / 2;

  if(xtw_push(tw, ctx, kind) < 0)
    return -1;

  if(kind == XTW_ELEMENT) {
    f = xtw_top(tw);
    f->depth = MAX_NUM(0, level - 2);
  }

  return 0;
}

/* xtw_open_end: ends the start tag of `f` if it is still open. */
static int
xtw_open_end(XmlTreeWriter* tw, XmlTreeFrame* f, int self_closing) {
  if(!f->open)
    return 0;

  f->open = 0;
  return xlay_open_end(&tw->lay, self_closing);
}

/* xtw_resolve: ends the start tag that waits in a flat list.
 *
 *   self_closing  when
 *   1             the next entry is its own `/name` marker, or the list ended
 *   0             anything else follows; a `dt` then nests the next entries
 *
 * returns 0, or -1 when the Writer refused bytes. */
static int
xtw_resolve(XmlTreeWriter* tw, JSContext* ctx, int self_closing) {
  int r;

  if(!tw->pend)
    return 0;

  r = xlay_open_end(&tw->lay, self_closing);

  if(!self_closing && tw->pend_dt)
    tw->depth++;

  js_free(ctx, tw->pend);
  tw->pend = 0;
  return r;
}

static ssize_t
xtw_array_start(void* opaque, JSContext* ctx) {
  XmlTreeWriter* tw = opaque;
  XmlTreeFrame* f = xtw_top(tw);
  uint32_t len = WALK_TOP(&tw->it.walk)->tab_atom_len;

  if(f && f->kind == XTW_ELEMENT && len > 0) {
    int level = walk_depth(&tw->it.walk) / 2;

    if(xtw_open_end(tw, f, 0) < 0)
      return xtw_fail(ctx);

    if(tw->max_depth == INT32_MAX || level < tw->max_depth)
      f->closes = 1;
    else
      walk_skip(&tw->it.walk, ctx);
  }

  return xtw_push(tw, ctx, XTW_LIST);
}

static ssize_t
xtw_value(void* opaque, JSContext* ctx, JSValueConst value) {
  XmlTreeWriter* tw = opaque;
  XmlTreeFrame* f = xtw_top(tw);

  if(f->kind == XTW_ELEMENT && f->key == 1) {
    size_t len;
    const char* s = JS_ToCStringLen(ctx, &len, value);

    if(!s)
      return -1;

    if(tw->flat) {
      int marker = len > 0 && s[0] == '/', own = marker && tw->pend && tw->pend_len == len - 1 && !memcmp(tw->pend, s + 1, len - 1), r = 0;

      if(own) {
        r = xtw_resolve(tw, ctx, 1); /* the marker of the waiting element: no entry of its own */
      } else if(xtw_resolve(tw, ctx, 0) < 0) {
        r = -1;
      } else if(marker) {
        tw->depth--;
        r = xlay_end_marker(&tw->lay, tw->single_line ? 0 : tw->depth, s + 1, len - 1);
      } else if(len) {
        r = xlay_start(&tw->lay, tw->single_line ? 0 : tw->depth, s, len);
        f->tag = js_strndup(ctx, s, len);
        f->len = len;
        f->open = f->tag != 0;
      }

      tw->single_line = 0;
      JS_FreeCString(ctx, s);
      return r < 0 || (len && !marker && !f->tag) ? xtw_fail(ctx) : 0;
    }

    if(len && (f->tag = js_strndup(ctx, s, len))) {
      f->len = len;
      f->open = s[0] != '/';

      if(s[0] == '/' ? xlay_end_marker(&tw->lay, f->depth, s + 1, len - 1) < 0 : xlay_start(&tw->lay, f->depth, s, len) < 0) {
        JS_FreeCString(ctx, s);
        return xtw_fail(ctx);
      }
    }

    JS_FreeCString(ctx, s);
  } else if(f->kind == XTW_ATTRS) {
    XmlTreeFrame* e = (XmlTreeFrame*)vector_at(&tw->frames, sizeof(XmlTreeFrame), (int32_t)vector_size(&tw->frames, sizeof(XmlTreeFrame)) - 2);

    if(e->open && tw->attr) {
      JSValue v = JS_DupValue(ctx, value);
      size_t vlen = 0;
      const char* s = 0;
      int r;

      if(JS_IsBool(v) && JS_ToBool(ctx, v)) {
        r = xlay_attribute(&tw->lay, tw->attr, strlen(tw->attr), 0, 0);
      } else {
        s = JS_ToCStringLen(ctx, &vlen, v);
        r = s ? xlay_attribute(&tw->lay, tw->attr, strlen(tw->attr), s, vlen) : -1;
      }

      JS_FreeCString(ctx, s);
      JS_FreeValue(ctx, v);

      if(r < 0)
        return xtw_fail(ctx);
    }
  } else if(f->kind == XTW_LIST && tw->flat && !JS_IsString(value) && xtw_resolve(tw, ctx, 0) < 0) {
    return xtw_fail(ctx);
  } else if(f->kind == XTW_LIST && JS_IsString(value)) {
    size_t len;
    const char* s = JS_ToCStringLen(ctx, &len, value);
    int level = (walk_depth(&tw->it.walk) + 1) / 2, r;

    if(!s)
      return -1;

    if(tw->flat) {
      int multiline = memchr(s, '\n', len) != 0;

      r = xtw_resolve(tw, ctx, 0);

      if(r == 0)
        r = xlay_text(&tw->lay, tw->depth, multiline, s, len);

      tw->single_line = !multiline;
    } else {
      r = xlay_text(&tw->lay, MAX_NUM(0, level - 2), WALK_TOP(&tw->it.walk)->tab_atom_len > 1, s, len);
    }

    JS_FreeCString(ctx, s);

    if(r < 0)
      return xtw_fail(ctx);
  }

  return 0;
}

static ssize_t
xtw_object_end(void* opaque, JSContext* ctx) {
  XmlTreeWriter* tw = opaque;
  XmlTreeFrame* f = xtw_top(tw);
  int r = 0;

  if(f->kind == XTW_ELEMENT) {
    if(f->tag && tw->flat) {
      /* flat: the start tag waits for the next entry */
      tw->pend = f->tag;
      tw->pend_len = f->len;
      tw->pend_dt = f->tag[0] != '/' && f->tag[0] != '?' && f->tag[0] != '!' && !strcasecmp(f->tag, "dt");
      f->tag = 0;
    } else if(f->tag) {
      if(f->open)
        r = xtw_open_end(tw, f, 1);
      else if(f->closes)
        r = xlay_close(&tw->lay, f->depth, f->tag, f->len);
    }

    js_free(ctx, f->tag);
  } else {
    js_free(ctx, tw->attr);
    tw->attr = 0;
  }

  vector_pop(&tw->frames, sizeof(XmlTreeFrame));
  return r < 0 ? xtw_fail(ctx) : 0;
}

static ssize_t
xtw_array_end(void* opaque, JSContext* ctx) {
  XmlTreeWriter* tw = opaque;

  vector_pop(&tw->frames, sizeof(XmlTreeFrame));

  /* flat: an element left waiting at the end of the list is self-closing */
  return tw->flat && vector_empty(&tw->frames) && xtw_resolve(tw, ctx, 1) < 0 ? xtw_fail(ctx) : 0;
}

/* xwrite_tree: writes an element tree or a flat list through `writer` in the xml.write() layout.
 *
 *   JSValueConst  tree       array of elements and texts; with `flat` of elements without
 *                            `children` and `{tagName: '/x'}` end markers
 *   int32_t       max_depth  levels of children written; INT32_MAX: all (tree only)
 *   int           flat       1: the list is flat; its output keeps the final newline
 *   Writer        writer     destination
 *
 * returns 0, or -1 with an exception pending. */
int
xwrite_tree(JSContext* ctx, JSValueConst tree, int32_t max_depth, int flat, Writer writer) {
  XmlTreeWriter tw = {.max_depth = max_depth, .flat = flat};
  WalkInterface iface = {&tw, xtw_key, xtw_object_start, xtw_array_start, xtw_object_end, xtw_array_end, xtw_value, 0};
  XmlTreeFrame* f;
  int r = -1;

  xlay_init(&tw.lay, writer, 0, 0);
  vector_init(&tw.frames, ctx);

  if(xwalk_init(&tw.it, ctx, tree, iface, 0) == 0) {
    int n;

    while((n = xwalk_next(&tw.it, ctx)) > 0) {}

    if(n == 0 && xlay_finish(&tw.lay, !flat) == 0)
      r = 0;
    else if(!JS_HasException(ctx))
      JS_ThrowInternalError(ctx, "xml.write(): out of memory");

    xwalk_close(&tw.it, ctx);
  }

  vector_foreach_t(&tw.frames, f) js_free(ctx, f->tag);
  js_free(ctx, tw.attr);
  js_free(ctx, tw.pend);
  vector_free(&tw.frames);
  xlay_free(&tw.lay);
  return r;
}
