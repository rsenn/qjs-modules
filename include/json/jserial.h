#ifndef JSERIAL_H
#define JSERIAL_H

#include <stddef.h>
#include <sys/types.h>
#include "json/jevent.h"
#include "json/jwrite.h"

/**
 * \defgroup jserial jserial: demand-driven JSON serializer
 * @{
 */

/* JsonSerial: a pull sink. A JsonEventSource feeds a JsonWriter, but only as far as the
 * client has asked for bytes.
 *
 * ```c
 * JwalkSource w;  jwalk_source_init(&w, ctx, value);
 * JsonSerial s;   jserial_init(&s, jwalk_source(&w), (JsonWriteOptions){.indent = 2}, rt);
 * while((n = jserial_read(&s, buf, 4096, 1)) > 0) {}   // n == 0: the end
 * jserial_free(&s);
 * ```
 *
 * `read(n)` takes one event at a time from the source and sends it to the writer until n bytes are
 * buffered or the source stops. An event is never taken twice, so what the source runs (toJSON,
 * getters, a replacer) runs once. The source can be a walker (stringify) or a parser (a
 * streaming reformatter).
 *
 * the bytes wait in `out`, which is unbounded: one event may write a long string. The serial
 * must not move while in use; it does not own the source. */
typedef struct {
  JsonEventSource src;
  JsonWriter wr;
  DynBuf out; /* bytes written by the writer, served from `pos` */
  size_t pos;
  JsonPull status; /* of the source: EVENT while it can go on */
  ssize_t error;   /* the first negative result of the writer, or 0 */
} JsonSerial;

enum { JSERIAL_AGAIN = -2 }; /* jserial_read(): no bytes now, the source's Reader is dry */

/* jserial_init: `opts` are the writer's options; rt allocates (a finalizer has no context). */
void jserial_init(JsonSerial*, JsonEventSource src, JsonWriteOptions opts, JSRuntime* rt);

/* jserial_read: copies up to n serialized bytes to buf, stepping the source as needed.
 *
 *   JsonSerial*  s      the serial
 *   void*        buf    destination
 *   size_t       n      bytes wanted
 *   int          chars  1: end the copy on a character boundary (a UTF-8 sequence is not cut)
 *                       2: as 1, and n counts characters, not bytes (buf holds up to 4n)
 *
 *   returns ssize_t  the bytes copied (> 0); 0 at the end of the output; JSERIAL_AGAIN when there
 *                    is none yet and the source needs data; -1 on failure, with s->error the
 *                    writer's -JWRITE_E_*, or 0 and an exception pending when the source failed
 *
 * never throws itself. */
ssize_t jserial_read(JsonSerial*, void* buf, size_t n, int chars);

void jserial_free(JsonSerial*);

/**
 * @}
 */

#endif
