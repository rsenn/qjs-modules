#ifndef QUICKJS_STREAM_H
#define QUICKJS_STREAM_H

#include "defines.h"
#include "js-utils.h"
#include "queue.h"
#include <quickjs.h>
#include <cutils.h>
#include <stdatomic.h>

/**
 * \defgroup quickjs-stream QuickJS module: stream - Streams
 * @{
 */

typedef enum { EVENT_CLOSE = 0, EVENT_CANCEL = 1, EVENT_READ = 2 } StreamEvent;

typedef struct streamreader {
  BOOL binary : 1;
  _Atomic(struct stream_object*) stream;
  union {
    struct {
      Promise closed, cancelled, read;
    };
    Promise events[3];
  };
} Reader;

typedef struct streamwriter {
  size_t desired_size;
  _Atomic(struct stream_object*) stream;
  union {
    struct {
      Promise closed, ready, write;
    };
    Promise events[3];
  };
  Queue q;
} Writer;

typedef enum { STREAM_READY = 0, STREAM_CLOSED = 1, STREAM_LOCKED = 2 } StreamState;

typedef struct stream_object {
  int ref_count;
  BOOL closed;
  _Atomic(Reader*) reader;
  _Atomic(Writer*) writer;
  Queue q;
} Stream;

extern thread_local JSClassID js_reader_class_id, js_writer_class_id, js_stream_class_id;
extern thread_local JSValue reader_proto, reader_ctor, writer_proto, writer_ctor, stream_proto, stream_ctor;

Reader* reader_new(JSContext*, Stream*);
void reader_free(JSContext*, Reader*);
void reader_free_rt(JSRuntime*, Reader*);
BOOL reader_release_lock(Reader*, JSContext*);
BOOL reader_cancel(Reader*, JSContext*);
BOOL reader_read(Reader*, JSContext*);
JSValue reader_signal(Reader*, StreamEvent, JSValueConst, JSContext* ctx);
void chunk_unref(JSRuntime*, void*, void*);
JSValue chunk_arraybuf(Chunk*, JSContext*);
Stream* stream_new(JSContext*);
size_t stream_length(Stream*);
void stream_unref(void*);
JSValue stream_next(Stream*, JSContext*);
int stream_at(Stream*, int64_t);
BOOL stream_lock_rd(Stream*, Reader*);
BOOL stream_unlock_rd(Stream*, Reader*);
Reader* stream_get_reader(Stream*, JSContext*);
JSValue js_stream_new(JSContext*, JSValueConst);
JSValue js_stream_wrap(JSContext*, Stream*);
JSModuleDef* js_init_module_stream(JSContext*, const char*);

static inline BOOL
stream_locked_rd(Stream* st) {
  return atomic_load(&st->reader) != 0;
}

static inline BOOL
stream_locked_wr(Stream* st) {
  return atomic_load(&st->writer) != 0;
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

static inline Stream*
js_stream_data(JSValueConst value) {
  return JS_GetOpaque(value, js_stream_class_id);
}

static inline Stream*
js_stream_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_stream_class_id);
}

/**
 * @}
 */

#endif /* defined(QUICKJS_STREAM_H) */
