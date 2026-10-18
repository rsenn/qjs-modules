/* xwalkev.c: XwalkSource, xwalk_event(); an element tree or flat list as a pull source of XmlEvent.
 * depends on: xwalk.h (the iterator), xevent.h (the event).
 * rule: the walk calls a WalkInterface that turns each step into queued XmlEvents, xwalk_event()
 * hands them out one by one. Nothing here writes bytes. */

#define _GNU_SOURCE 1

#include "xml/xwalk.h"
#include <string.h>

/**
 * \addtogroup xwalk
 * @{
 */

#define XWE_BALANCED(s) (((s)->flags & (XWALK_FLAT | XWALK_BALANCED)) == (XWALK_FLAT | XWALK_BALANCED))

/* what an XwalkFrame is: a list of items, an element, or an element's attributes */
enum { XF_LIST, XF_ELEMENT, XF_ATTRS };

typedef struct {
  int kind;
  char* tag; /* ELEMENT: its tagName, owned, NULL until the key is seen */
  size_t len;
  int key;               /* ELEMENT: which key is current, 1 tag 2 attributes 3 children */
  unsigned markup : 2;   /* ELEMENT: 0 normal, 1 comment or declaration or CDATA, 2 instruction, 3 instruction already queued */
  unsigned started : 1;  /* ELEMENT: XE_ELEMENT_START was queued */
  unsigned marker : 1;   /* ELEMENT: tagName was /x, an end event was queued instead of a start */
} XwalkFrame;

typedef struct {
  char* name; /* owned */
  size_t len;
} XwalkOpen;

typedef struct {
  XmlEventType type;
  size_t noff, nlen, voff, vlen;
  unsigned has_value : 1;
} XwalkItem;

static XwalkFrame*
xwe_top(XwalkSource* s) {
  return vector_empty(&s->frames) ? 0 : vector_back(&s->frames, sizeof(XwalkFrame));
}

static ssize_t
xwe_fail(XwalkSource* s) {
  if(!JS_HasException(s->ctx))
    JS_ThrowInternalError(s->ctx, "xwalk: out of memory");

  return -1;
}

/* xwe_queue: appends an event; the texts are copied. returns 0, or -1 on allocation failure. */
static int
xwe_queue(XwalkSource* s, XmlEventType type, const char* name, size_t nlen, const char* value, size_t vlen) {
  XwalkItem* it = vector_emplace(&s->queue, sizeof(XwalkItem));

  if(!it)
    return -1;

  memset(it, 0, sizeof(*it));
  it->type = type;
  it->noff = s->text.size;
  it->nlen = nlen;

  if(name)
    dbuf_put(&s->text, (const uint8_t*)name, nlen);

  if(value) {
    it->has_value = 1;
    it->voff = s->text.size;
    it->vlen = vlen;
    dbuf_put(&s->text, (const uint8_t*)value, vlen);
  }

  return 0;
}

/* xwe_end: queues the end of the element `name`, with XWALK_BALANCED ending what is open inside it first. */
static int
xwe_end(XwalkSource* s, const char* name, size_t len) {
  if(!XWE_BALANCED(s))
    return xwe_queue(s, XE_ELEMENT_END, name, len, 0, 0);

  {
    size_t n = vector_size(&s->open, sizeof(XwalkOpen)), i = n;

    while(i > 0) {
      XwalkOpen* o = vector_at(&s->open, sizeof(XwalkOpen), (int32_t)i - 1);

      if(o->len == len && !memcmp(o->name, name, len))
        break;

      i--;
    }

    if(i == 0)
      return 0; /* a marker for nothing that is open */

    while(n >= i) {
      XwalkOpen* o = vector_back(&s->open, sizeof(XwalkOpen));
      int r = xwe_queue(s, XE_ELEMENT_END, o->name, o->len, 0, 0);

      js_free(s->ctx, o->name);
      vector_pop(&s->open, sizeof(XwalkOpen));
      n--;

      if(r < 0)
        return -1;
    }
  }

  return 0;
}

/* xwe_end_all: ends every element a balanced flat list left open. */
static int
xwe_end_all(XwalkSource* s) {
  while(!vector_empty(&s->open)) {
    XwalkOpen* o = vector_back(&s->open, sizeof(XwalkOpen));
    int r = xwe_queue(s, XE_ELEMENT_END, o->name, o->len, 0, 0);

    js_free(s->ctx, o->name);
    vector_pop(&s->open, sizeof(XwalkOpen));

    if(r < 0)
      return -1;
  }

  return 0;
}

static ssize_t
xwe_key(void* o, JSContext* ctx, JSValueConst key) {
  XwalkSource* s = o;
  XwalkFrame* f = xwe_top(s);

  if(f->kind == XF_ELEMENT) {
    JSAtom a = JS_ValueToAtom(ctx, key);

    f->key = a == s->it.atom[0] ? 1 : a == s->it.atom[1] ? 2 : 3;
    JS_FreeAtom(ctx, a);
  } else if(f->kind == XF_ATTRS) {
    size_t len;
    const char* str = JS_ToCStringLen(ctx, &len, key);

    if(!str)
      return -1;

    s->attr.size = 0;
    dbuf_put(&s->attr, (const uint8_t*)str, len);
    JS_FreeCString(ctx, str);
  }

  return 0;
}

static ssize_t
xwe_push(XwalkSource* s, int kind) {
  XwalkFrame* f = vector_emplace(&s->frames, sizeof(XwalkFrame));

  if(!f)
    return xwe_fail(s);

  memset(f, 0, sizeof(*f));
  f->kind = kind;
  return 0;
}

static ssize_t
xwe_object_start(void* o, JSContext* ctx) {
  XwalkSource* s = o;
  XwalkFrame* f = xwe_top(s);

  return xwe_push(s, !f || f->kind == XF_LIST ? XF_ELEMENT : XF_ATTRS);
}

static ssize_t
xwe_array_start(void* o, JSContext* ctx) {
  XwalkSource* s = o;
  XwalkFrame* f = xwe_top(s);

  /* the instruction comes before its children, its attributes are all in by now */
  if(f && f->kind == XF_ELEMENT && f->markup == 2) {
    f->markup = 3;

    if(xwe_queue(s, XE_INSTRUCTION, (const char*)s->pi.buf, s->pi.size, 0, 0) < 0)
      return xwe_fail(s);
  }

  if(f && f->kind == XF_ELEMENT && WALK_TOP(&s->it.walk)->tab_atom_len > 0) {
    int level = walk_depth(&s->it.walk) / 2;

    if(s->max_depth != INT32_MAX && level >= s->max_depth)
      walk_skip(&s->it.walk, ctx);
  }

  return xwe_push(s, XF_LIST);
}

/* xwe_tag: the tagName of the current element is `str`. */
static int
xwe_tag(XwalkSource* s, XwalkFrame* f, const char* str, size_t len) {
  if(!len)
    return 0;

  if(!(f->tag = js_strndup(s->ctx, str, len)))
    return -1;

  f->len = len;

  if(str[0] == '/') {
    f->marker = 1;
    return xwe_end(s, str + 1, len - 1);
  }

  if(str[0] == '!') {
    XmlEvent ev = {0};

    f->markup = 1;
    xevent_markup(&ev, str, len);
    return xwe_queue(s, ev.type, ev.str, ev.len, 0, 0);
  }

  if(str[0] == '?') {
    f->markup = 2;
    s->pi.size = 0;
    dbuf_put(&s->pi, (const uint8_t*)str + 1, len - 1);
    return 0;
  }

  f->started = 1;

  if(XWE_BALANCED(s)) {
    XwalkOpen* o = vector_emplace(&s->open, sizeof(XwalkOpen));

    if(!o)
      return -1;

    if(!(o->name = js_strndup(s->ctx, str, len))) {
      vector_pop(&s->open, sizeof(XwalkOpen));
      return -1;
    }

    o->len = len;
  }

  return xwe_queue(s, XE_ELEMENT_START, str, len, 0, 0);
}

static ssize_t
xwe_value(void* o, JSContext* ctx, JSValueConst value) {
  XwalkSource* s = o;
  XwalkFrame* f = xwe_top(s);

  if(f->kind == XF_ELEMENT && f->key == 1) {
    size_t len;
    const char* str = JS_ToCStringLen(ctx, &len, value);
    int r;

    if(!str)
      return -1;

    r = xwe_tag(s, f, str, len);
    JS_FreeCString(ctx, str);
    return r < 0 ? xwe_fail(s) : 0;
  }

  if(f->kind == XF_ATTRS) {
    XwalkFrame* e = vector_at(&s->frames, sizeof(XwalkFrame), (int32_t)vector_size(&s->frames, sizeof(XwalkFrame)) - 2);
    int bare = JS_IsBool(value) && JS_ToBool(ctx, value);
    size_t vlen = 0;
    const char* v = 0;
    int r = 0;

    if(!e->started && e->markup != 2)
      return 0;

    if(!bare && !(v = JS_ToCStringLen(ctx, &vlen, value)))
      return -1;

    if(e->markup == 2) {
      dbuf_putc(&s->pi, ' ');
      dbuf_put(&s->pi, s->attr.buf, s->attr.size);

      if(!bare) {
        dbuf_put(&s->pi, (const uint8_t*)"=\"", 2);
        dbuf_put(&s->pi, (const uint8_t*)v, vlen);
        dbuf_putc(&s->pi, '"');
      }
    } else {
      r = xwe_queue(s, XE_ATTRIBUTE, (const char*)s->attr.buf, s->attr.size, bare ? 0 : v, vlen);
    }

    if(v)
      JS_FreeCString(ctx, v);

    return r < 0 ? xwe_fail(s) : 0;
  }

  if(f->kind == XF_LIST && JS_IsString(value)) {
    size_t len;
    const char* str = JS_ToCStringLen(ctx, &len, value);
    int r;

    if(!str)
      return -1;

    r = xwe_queue(s, XE_TEXT, str, len, 0, 0);
    JS_FreeCString(ctx, str);
    return r < 0 ? xwe_fail(s) : 0;
  }

  return 0;
}

static ssize_t
xwe_object_end(void* o, JSContext* ctx) {
  XwalkSource* s = o;
  XwalkFrame* f = xwe_top(s);
  int r = 0;

  if(f->kind == XF_ELEMENT) {
    if(f->markup == 2)
      r = xwe_queue(s, XE_INSTRUCTION, (const char*)s->pi.buf, s->pi.size, 0, 0);
    else if(f->started && !(s->flags & XWALK_FLAT))
      r = xwe_queue(s, XE_ELEMENT_END, f->tag, f->len, 0, 0);

    js_free(ctx, f->tag);
  }

  vector_pop(&s->frames, sizeof(XwalkFrame));
  return r < 0 ? xwe_fail(s) : 0;
}

static ssize_t
xwe_array_end(void* o, JSContext* ctx) {
  XwalkSource* s = o;

  vector_pop(&s->frames, sizeof(XwalkFrame));
  return vector_empty(&s->frames) && xwe_end_all(s) < 0 ? xwe_fail(s) : 0;
}

int
xwalk_source_init(XwalkSource* s, JSContext* ctx, JSValueConst root, int flags, const XwalkShape* shape) {
  WalkInterface iface = {s, xwe_key, xwe_object_start, xwe_array_start, xwe_object_end, xwe_array_end, xwe_value, 0};
  JSRuntime* rt = JS_GetRuntime(ctx);

  s->ctx = ctx;
  s->flags = flags;
  s->max_depth = INT32_MAX;
  s->qpos = 0;
  vector_init(&s->frames, ctx);
  vector_init(&s->open, ctx);
  vector_init(&s->queue, ctx);
  vector_init_rt(&s->text, rt);
  vector_init_rt(&s->attr, rt);
  vector_init_rt(&s->pi, rt);
  return xwalk_init(&s->it, ctx, root, iface, shape);
}

XmlPull
xwalk_event(XwalkSource* s, XmlEvent* ev) {
  *ev = (XmlEvent){0};

  for(;;) {
    size_t n = vector_size(&s->queue, sizeof(XwalkItem));
    int r;

    if(s->qpos < n) {
      XwalkItem* it = vector_at(&s->queue, sizeof(XwalkItem), (int32_t)s->qpos++);

      ev->type = it->type;
      ev->name = (const char*)s->text.buf + it->noff;
      ev->nlen = it->nlen;

      if(it->has_value) {
        ev->value = (const char*)s->text.buf + it->voff;
        ev->vlen = it->vlen;
      }

      return XML_PULL_EVENT;
    }

    s->queue.size = 0;
    s->text.size = 0;
    s->qpos = 0;

    if((r = walk_next(&s->it.walk, s->ctx)) < 0)
      return XML_PULL_FAIL;

    if(r == 0)
      return XML_PULL_END;
  }
}

static XmlPull
xwe_step(void* opaque, XmlEvent* ev) {
  return xwalk_event(opaque, ev);
}

XmlEventSource
xwalk_source(XwalkSource* s) {
  return (XmlEventSource){s, xwe_step};
}

void
xwalk_source_close_rt(XwalkSource* s, JSRuntime* rt) {
  XwalkFrame* f;
  XwalkOpen* o;

  xwalk_close_rt(&s->it, rt);
  vector_foreach_t(&s->frames, f) js_free_rt(rt, f->tag);
  vector_foreach_t(&s->open, o) js_free_rt(rt, o->name);
  dbuf_free(&s->frames);
  dbuf_free(&s->open);
  dbuf_free(&s->queue);
  dbuf_free(&s->text);
  dbuf_free(&s->attr);
  dbuf_free(&s->pi);
}

void
xwalk_source_close(XwalkSource* s, JSContext* ctx) {
  xwalk_source_close_rt(s, JS_GetRuntime(ctx));
}

/**
 * @}
 */
