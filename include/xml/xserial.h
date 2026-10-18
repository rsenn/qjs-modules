#ifndef XSERIAL_H
#define XSERIAL_H

#include <stddef.h>
#include <sys/types.h>
#include "xml/xevent.h"
#include "xml/xwrite.h"

/**
 * \defgroup xserial xserial: demand-driven XML serializer
 * @{
 */

/* XmlSerial: a pull sink; the twin of JsonSerial (json/jserial.h). An XmlEventSource feeds an
 * XmlWriter, but only as far as the client has asked for bytes.
 *
 * ```c
 * XwalkSource w;  xwalk_source_init(&w, ctx, tree, 0, NULL);
 * XmlSerial s;    xserial_init(&s, xwalk_source(&w), (XmlWriteOptions){0}, rt);
 * while((n = xserial_read(&s, buf, 4096, 1)) > 0) {}
 * xserial_free(&s);
 * ```
 *
 * the source can be a tree walker or an XML parser. At the end of the source the writer's
 * xwrite_finish() runs, so an element left open is an error. The serial must not move while in
 * use; it does not own the source. */
typedef struct {
  XmlEventSource src;
  XmlWriter wr;
  DynBuf out;
  size_t pos;
  XmlPull status;
  ssize_t error; /* the first negative result of the writer, or 0 */
} XmlSerial;

enum { XSERIAL_AGAIN = -2 };

void xserial_init(XmlSerial*, XmlEventSource src, XmlWriteOptions opts, JSRuntime* rt);

/* xserial_read: as jserial_read(), with -XWRITE_E_* in s->error. */
ssize_t xserial_read(XmlSerial*, void* buf, size_t n, int chars);

void xserial_free(XmlSerial*);

/**
 * @}
 */

#endif
