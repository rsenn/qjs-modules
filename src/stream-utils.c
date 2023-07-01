#include "stream-utils.h"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

Writer
writer_from_dynbuf(DynBuf* db) {
  return (Writer){(WriteFunction*)&dbuf_put, db, (WriterFinalizer*)&dbuf_free};
}

Writer
writer_from_fd(intptr_t fd, bool close_on_end) {
  return (Writer){(WriteFunction*)&write, (void*)fd, close_on_end ? (WriterFinalizer*)&close : NULL};
}

ssize_t
writer_write(Writer* wr, const void* buf, size_t len) {
  return wr->write((intptr_t)wr->opaque, buf, len);
}

void
writer_free(Writer* wr) {
  if(wr->finalizer)
    wr->finalizer(wr->opaque);
}
