#include "stream-utils.h"
#include "buffer-utils.h"
#include "defines.h"

#include <assert.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

/**
 * \addtogroup stream-utils
 * @{
 */
Writer
writer_from_dynbuf(DynBuf* db) {
  return (Writer){(WriteFunction*)&dbuf_writer, db, (WriterFinalizer*)&dbuf_free};
}

Writer
writer_from_fd(intptr_t fd, bool close_on_end) {
  return (Writer){(WriteFunction*)&write, (void*)fd, close_on_end ? (WriterFinalizer*)&close : NULL};
}

ssize_t
writer_tee_write(void* opaque, const void* buf, size_t len) {
  Writer* wptr = opaque;
  ssize_t r[2];

  r[0] = writer_write(&wptr[0], buf, len);
  r[1] = writer_write(&wptr[1], buf, len);

  return MIN_NUM(r[0], r[1]);
}

Writer
writer_tee(const Writer a, const Writer b) {
  Writer* opaque;

  if((opaque = malloc(sizeof(Writer) * 2))) {
    opaque[0] = a;
    opaque[1] = b;
  }

  assert(opaque);

  return (Writer){(WriteFunction*)&writer_tee_write, (void*)opaque, (WriterFinalizer*)&free};
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

/**
 * @}
 */
