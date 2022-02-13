#ifndef QUICKJS_STREAM_H
#define QUICKJS_STREAM_H

#include "defines.h"
#include "js-utils.h"
#include "buffer-utils.h"
#include "queue.h"
#include <quickjs.h>
#include <cutils.h>
#include <stdatomic.h>

/**
 * \defgroup quickjs-stream QuickJS module: stream - Streams
 * @{
 */

typedef enum { EVENT_CLOSE = 0, EVENT_CANCEL = 1, EVENT_READ = 2 } StreamEvent;

typedef struct read_operation {
  struct list_head link;
  ResolveFunctions handlers;
} Read;

enum { READER_CLOSED=0,READER_CANCELLED };

typedef struct streamreader {
  BOOL binary : 1;
  _Atomic(struct readable_stream*) stream;
  union {
    struct {
      Promise closed, cancelled;
    } on;
    Promise events[2];
  };
  struct list_head reads;
} Reader;


enum { WRITER_CLOSED=0,WRITER_READY };

typedef struct streamwriter {
  size_t desired_size;
  _Atomic(struct writable_stream*) stream;
  union {
    struct {
      Promise closed, ready;
    } on;
    Promise events[2];
  };
  Queue q;
} Writer;

enum { READABLE_START = 0, READABLE_PULL, READABLE_CANCEL };

typedef struct readable_stream {
  int ref_count;
  _Atomic(BOOL) closed;
  _Atomic(char*) reason;
  _Atomic(Reader*) reader;
  union {
    struct {
      JSValue start, pull, cancel;
    } on;
    JSValue events[3];
  };
  JSValue underlying_source;
} Readable;

enum { WRITABLE_START = 0, WRITABLE_WRITE, WRITABLE_CLOSE, WRITABLE_ABORT };

typedef struct writable_stream {
  int ref_count;
  _Atomic(BOOL) closed;
  _Atomic(char*) reason;
  _Atomic(Writer*) writer;
  union {
    struct {
      JSValue start, write, close, abort;
    } on;
    JSValue events[4];
  };
  JSValue underlying_sink;
} Writable;

extern thread_local JSClassID js_reader_class_id, js_writer_class_id, js_readable_class_id, js_writable_class_id;
extern thread_local JSValue reader_proto, reader_ctor, writer_proto, writer_ctor, readable_proto, readable_ctor, writable_proto, writable_ctor;

void chunk_unref(JSRuntime*, void*, void*);
JSValue chunk_arraybuf(Chunk*, JSContext*);
Reader* reader_new(JSContext*, Readable*);
BOOL reader_release_lock(Reader*, JSContext*);
BOOL reader_cancel(Reader*, JSContext*);
JSValue reader_read(Reader*, JSContext*);
JSValue reader_signal(Reader*, StreamEvent, JSValueConst, JSContext* ctx);
Writer* writer_new(JSContext*, Writable*);
BOOL writer_release_lock(Writer*, JSContext*);
JSValue writer_write(Writer*, const MemoryBlock*, JSContext*);
BOOL writer_ready(Writer*, JSContext*);
JSValue writer_signal(Writer*, StreamEvent, JSValueConst, JSContext* ctx);
Readable* readable_new(JSContext*);
void readable_close(Readable*);
void readable_abort(Readable*, JSValueConst, JSContext*);
void readable_unref(void*);
int readable_lock(Readable*, Reader*);
int readable_unlock(Readable*, Reader*);
Reader* readable_get_reader(Readable*, JSContext*);
Writable* writable_new(JSContext*);
size_t writable_size(Writable*);
void writable_close(Writable*);
void writable_abort(Writable*, JSValueConst, JSContext*);
void writable_unref(void*);
JSValue writable_next(Writable*, JSContext*);
int writable_at(Writable*, int64_t);
int writable_lock(Writable*, Writer*);
int writable_unlock(Writable*, Writer*);
Writer* writable_get_writer(Writable*, size_t, JSContext*);
JSModuleDef* js_init_module_stream(JSContext*, const char*);

static inline BOOL
readable_closed(Readable* st) {
  return atomic_load(&st->closed);
}

static inline Reader*
readable_locked(Readable* st) {
  return atomic_load(&st->reader);
}

static inline BOOL
writable_closed(Writable* st) {
  return atomic_load(&st->closed);
}

static inline Writer*
writable_locked(Writable* st) {
  return atomic_load(&st->writer);
}

static inline Reader*
js_reader_data(JSValueConst value) {
  return JS_GetOpaque(value, js_reader_class_id);
}

static inline Reader*
js_reader_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_reader_class_id);
}

static inline Writer*
js_writer_data(JSValueConst value) {
  return JS_GetOpaque(value, js_writer_class_id);
}

static inline Writer*
js_writer_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_writer_class_id);
}

static inline Readable*
js_readable_data(JSValueConst value) {
  return JS_GetOpaque(value, js_readable_class_id);
}

static inline Readable*
js_readable_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_readable_class_id);
}

static inline Writable*
js_writable_data(JSValueConst value) {
  return JS_GetOpaque(value, js_writable_class_id);
}

static inline Writable*
js_writable_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_writable_class_id);
}

/**
 * @}
 */

#endif /* defined(QUICKJS_STREAM_H) */
