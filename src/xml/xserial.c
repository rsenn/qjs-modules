/* xserial.c: XmlSerial, xserial_read(); an XML source pulled through a writer on demand.
 * depends on: xevent.h (source, sink), xwrite.h (the writer), stream-utils.h (a Writer into a DynBuf).
 * rule: an event is taken from the source once and written once; nothing is replayed. */

#define _GNU_SOURCE 1

#include "xml/xserial.h"
#include "stream-utils.h"
#include <string.h>

/**
 * \addtogroup xserial
 * @{
 */

void
xserial_init(XmlSerial* s, XmlEventSource src, XmlWriteOptions opts, JSRuntime* rt) {
  s->src = src;
  s->pos = 0;
  s->status = XML_PULL_EVENT;
  s->error = 0;
  dbuf_init2(&s->out, rt, (DynBufReallocFunc*)&js_realloc_rt);
  s->wr = (XmlWriter){.writer = writer_from_dynbuf(&s->out), .opts = opts};
  xwrite_init(&s->wr, (DynBufReallocFunc*)&js_realloc_rt, rt);
}

/* xserial_fill: steps the source until n bytes are waiting or it stops; at the end the writer is finished. */
static void
xserial_fill(XmlSerial* s, size_t n) {
  XmlEventSink sink = xwrite_sink(&s->wr);

  while(s->status == XML_PULL_EVENT && !s->error && s->out.size - s->pos < n) {
    XmlEvent ev;
    ssize_t r;

    if((s->status = s->src.next(s->src.opaque, &ev)) != XML_PULL_EVENT) {
      if(s->status == XML_PULL_END && (r = xwrite_finish(&s->wr)) < 0)
        s->error = r;

      break;
    }

    if((r = xevent_send(&sink, &ev)) < 0)
      s->error = r;
  }
}

static size_t
xserial_cut(const uint8_t* buf, size_t n, size_t avail) {
  size_t i = n;

  if(n >= avail)
    return n;

  while(i > 0 && (buf[i] & 0xc0) == 0x80)
    i--;

  return i ? i : n;
}

ssize_t
xserial_read(XmlSerial* s, void* buf, size_t n, int chars) {
  size_t avail, m;

  if(n == 0)
    return 0;

  xserial_fill(s, n + (chars ? 4 : 0));

  if(s->error || s->status == XML_PULL_FAIL)
    return -1;

  avail = s->out.size - s->pos;
  m = MIN_NUM(n, avail);

  if(chars)
    m = xserial_cut(s->out.buf + s->pos, m, avail);

  if(m == 0)
    return s->status == XML_PULL_AGAIN ? XSERIAL_AGAIN : 0;

  memcpy(buf, s->out.buf + s->pos, m);
  s->pos += m;

  if(s->pos == s->out.size)
    s->pos = s->out.size = 0;

  return m;
}

void
xserial_free(XmlSerial* s) {
  xwrite_free(&s->wr);
  dbuf_free(&s->out);
}

/**
 * @}
 */
