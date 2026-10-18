/* xnext.c: XMLParser, xml_next(), xml_source(); the pull parser as an XmlEvent source.
 * depends on: xread.h (the XR_COMPAT parser), xevent.h (the event).
 * rule: xread does the parsing, this reads blocks, queues what it made and hands it out one by one. */

#define _GNU_SOURCE 1

#include "xml/xml.h"
#include "xml/xtags.h"
#include <stdlib.h>
#include <string.h>

/**
 * \addtogroup xml
 * @{
 */

void
xml_init(XMLParser* p, Reader* reader) {
  memset(p, 0, sizeof(*p));
  p->reader = reader;
  p->self_closing_tags = xtag_html_self_closing;
}

void
xml_free(XMLParser* p) {
  if(p->started)
    xr_state_free(&p->st);

  free(p->queue.ev);
  free(p->queue.text);
}

void
xml_set_tolerant(XMLParser* p, int tolerant) {
  p->tolerant = !!tolerant;
}

void
xml_set_flat(XMLParser* p, int flat) {
  p->flat = !!flat;
}

void
xml_set_implied_end(XMLParser* p, int enable) {
  p->no_implied_end = !enable;
}

/* xnext_text: copies len bytes (and a NUL if nul) into the queue's text, returns the offset plus one. */
static size_t
xnext_text(struct xml_queue* q, const char* str, size_t len, int nul) {
  size_t off = q->text_len;

  if(q->text_len + len + 1 > q->text_cap) {
    size_t cap = q->text_cap ? q->text_cap : 256;
    char* t;

    while(cap < q->text_len + len + 1)
      cap *= 2;

    if(!(t = realloc(q->text, cap)))
      return 0;

    q->text = t;
    q->text_cap = cap;
  }

  memcpy(q->text + off, str, len);
  q->text_len += len;

  if(nul)
    q->text[q->text_len++] = '\0';

  return off + 1;
}

/* xnext_push: copies an event, name, value and text included, into the queue. the strings are kept as
 * an offset plus one until the event is handed out. */
static ssize_t
xnext_push(void* user, const XmlEvent* ev) {
  XMLParser* p = user;
  struct xml_queue* q = &p->queue;
  XmlEvent copy = *ev;

  if(q->head + q->len == q->cap) {
    size_t cap = q->cap ? q->cap * 2 : 64;
    XmlEvent* e = realloc(q->ev, cap * sizeof(XmlEvent));

    if(!e)
      return -1;

    q->ev = e;
    q->cap = cap;
  }

  if(ev->name)
    copy.name = (const char*)(uintptr_t)xnext_text(q, ev->name, ev->nlen, ev->type == XE_ERROR);

  if(ev->value)
    copy.value = (const char*)(uintptr_t)xnext_text(q, ev->value, ev->vlen, 0);

  q->ev[q->head + q->len++] = copy;
  return 0;
}

/* xnext_pop: writes the oldest queued event to *ev, XML_PULL_AGAIN when the queue is empty. */
static XmlPull
xnext_pop(XMLParser* p, XmlEvent* ev) {
  struct xml_queue* q = &p->queue;

  if(!q->len)
    return XML_PULL_AGAIN;

  *ev = q->ev[q->head++];
  q->len--;

  if(ev->name)
    ev->name = q->text + ((uintptr_t)ev->name - 1);

  if(ev->value)
    ev->value = q->text + ((uintptr_t)ev->value - 1);

  if(!q->len) {
    q->head = 0;
    q->text_len = 0;
  }

  /* an element counts as open from the first event after its attributes */
  if(p->opening && ev->type != XE_ATTRIBUTE) {
    p->opening = 0;
    p->depth++;
  }

  if(ev->type == XE_ELEMENT_START)
    p->opening = 1;
  else if(ev->type == XE_ELEMENT_END && p->depth)
    p->depth--;

  if(ev->has_at)
    location_from_srcpos(&p->loc, &ev->at);

  return XML_PULL_EVENT;
}

XmlPull
xml_next(XMLParser* p, XmlEvent* ev) {
  for(;;) {
    ssize_t n;

    if(xnext_pop(p, ev) == XML_PULL_EVENT)
      return XML_PULL_EVENT;

    if(p->ended || (p->started && p->st.done))
      return XML_PULL_END;

    /* the Reader answers 0 at the end of the input, less than 0 when it has no data now */
    if((n = reader_read(p->reader, p->block, sizeof(p->block))) < 0)
      return XML_PULL_AGAIN;

    if(!p->started) {
      unsigned flags = XR_COMPAT | (p->tolerant ? XR_TOLERANT : 0) | (p->flat ? XR_FLAT : 0) | (p->no_implied_end ? XR_NO_IMPLIED_END : 0);

      p->started = 1;
      xr_state_init_flags(&p->st, flags);
      p->st.self_closing_tags = p->self_closing_tags;
    }

    if(n == 0) {
      p->ended = 1;
      xr_end(&p->st, xnext_push, p);
    } else {
      xr_feed(&p->st, (const char*)p->block, (size_t)n, xnext_push, p);
    }
  }
}

static XmlPull
xnext_step(void* opaque, XmlEvent* ev) {
  return xml_next(opaque, ev);
}

XmlEventSource
xml_source(XMLParser* p) {
  return (XmlEventSource){p, xnext_step};
}

/**
 * @}
 */
