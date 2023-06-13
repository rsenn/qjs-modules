#ifndef STREAM_UTILS_H
#define STREAM_UTILS_H

#include "cutils.h"
#include <sys/types.h>

typedef ssize_t WriteFunction(intptr_t, const void*, size_t);
typedef ssize_t WriterFinalizer(void*);

typedef struct {
  WriteFunction* write;
    void* opaque;
  WriterFinalizer* finalizer;
} Writer;

Writer writer_from_dynbuf(DynBuf*);
ssize_t writer_write(Writer*, const void*, size_t);
void writer_free(Writer*);

#endif /* defined(STREAM_UTILS_H) */
