#include "stream-utils.h"
#include "buffer-utils.h"
#include "defines.h"

#include <assert.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

static ssize_t
dbuf_writer(DynBuf* db, const void* buf, size_t len) {
  if(dbuf_put(db, buf, len))
    return -1;
  return len;
}

static ssize_t
inputbuffer_reader(InputBuffer* ib, void* buf, size_t len) {
  size_t remain = ib->size - ib->pos;
  if(len > remain)
    len = remain;

  if(len)
    memcpy(buf, &ib->data[ib->pos], len);

  ib->pos += len;
  return len;
}
/**
 * \addtogroup stream-utils
 * @{
 */
Writer
writer_from_dynbuf(DynBuf* db) {
  return (Writer){(WriteFunction*)(void*)&dbuf_writer, db, (WriterFinalizer*)(void*)&dbuf_free};
}

Writer
writer_from_fd(intptr_t fd, bool close_on_end) {
  return (Writer){(WriteFunction*)(void*)&write, (void*)fd, close_on_end ? (WriterFinalizer*)(void*)&close : NULL};
}

ssize_t
writer_tee_write(intptr_t opaque, const void* buf, size_t len) {
  Writer* wptr = (void*)opaque;
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

  return (Writer){writer_tee_write, (void*)opaque, (WriterFinalizer*)(void*)&orig_free};
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

Reader
reader_from_buf(InputBuffer* ib, JSContext* ctx) {
  return (Reader){(ReadFunction*)(void*)&inputbuffer_reader, ib, ctx, (ReaderFinalizer*)(void*)&input_buffer_free};
}

Reader
reader_from_fd(intptr_t fd, bool close_on_end) {
  return (Reader){(ReadFunction*)(void*)&read, (void*)fd, NULL, close_on_end ? (ReaderFinalizer*)(void*)&close : NULL};
}

ssize_t
reader_read(Reader* rd, void* buf, size_t len) {
  return rd->read((intptr_t)rd->opaque, buf, len);
}

void
reader_free(Reader* rd) {
  if(rd->finalizer)
    rd->finalizer(rd->opaque, rd->opaque2);
}
/**
 * @}
 */
