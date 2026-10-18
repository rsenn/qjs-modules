/* jserial.c: JsonSerial, jserial_read(); a JSON source pulled through a writer on demand.
 * depends on: jevent.h (source, sink), jwrite.h (the writer), stream-utils.h (a Writer into a DynBuf).
 * rule: an event is taken from the source once and written once; nothing is replayed. */

#define _GNU_SOURCE 1

#include "json/jserial.h"
#include "stream-utils.h"
#include <string.h>

/**
 * \addtogroup jserial
 * @{
 */

void
jserial_init(JsonSerial* s, JsonEventSource src, JsonWriteOptions opts, JSRuntime* rt) {
  s->src = src;
  s->pos = 0;
  s->status = JSON_PULL_EVENT;
  s->error = 0;
  dbuf_init2(&s->out, rt, (DynBufReallocFunc*)&js_realloc_rt);
  s->wr = (JsonWriter){.writer = writer_from_dynbuf(&s->out), .opts = opts};
  jwrite_init(&s->wr, (DynBufReallocFunc*)&js_realloc_rt, rt);
}

/* jserial_fill: steps the source until n bytes are waiting or it stops. */
static void
jserial_fill(JsonSerial* s, size_t n) {
  JsonEventSink sink = jwrite_sink(&s->wr);

  while(s->status == JSON_PULL_EVENT && !s->error && s->out.size - s->pos < n) {
    JsonEvent ev;
    ssize_t r;

    if((s->status = s->src.next(s->src.opaque, &ev)) != JSON_PULL_EVENT)
      break;

    if((r = jevent_send(&sink, &ev)) < 0)
      s->error = r;
  }
}

/* jserial_cut: the largest m <= n that does not end inside a UTF-8 sequence of buf. */
static size_t
jserial_cut(const uint8_t* buf, size_t n, size_t avail) {
  size_t i = n;

  if(n >= avail)
    return n;

  while(i > 0 && (buf[i] & 0xc0) == 0x80)
    i--;

  return i ? i : n;
}

ssize_t
jserial_read(JsonSerial* s, void* buf, size_t n, int chars) {
  size_t avail, m;

  if(n == 0)
    return 0;

  jserial_fill(s, n + (chars ? 4 : 0));

  if(s->error)
    return -1;

  if(s->status == JSON_PULL_FAIL)
    return -1;

  avail = s->out.size - s->pos;
  m = MIN_NUM(n, avail);

  if(chars)
    m = jserial_cut(s->out.buf + s->pos, m, avail);

  if(m == 0)
    return s->status == JSON_PULL_AGAIN ? JSERIAL_AGAIN : 0;

  memcpy(buf, s->out.buf + s->pos, m);
  s->pos += m;

  if(s->pos == s->out.size)
    s->pos = s->out.size = 0;

  return m;
}

void
jserial_free(JsonSerial* s) {
  jwrite_free(&s->wr);
  dbuf_free(&s->out);
}

/**
 * @}
 */
