#include "stream-utils.h"

Writer
writer_from_dynbuf(DynBuf* db) {
  return (Writer){(WriteFunction*)&dbuf_put, db, (WriterFinalizer*)&dbuf_free};
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
