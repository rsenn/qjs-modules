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

enum { READER_CLOSED = 0, READER_CANCELLED };

typedef struct stream_reader {
  int64_t desired_size;
  _Atomic(struct readable_stream*) stream;
  union {
    struct {
      Promise closed, cancelled;
    };
    Promise events[2];
  };
  struct list_head reads;
} Reader;

enum { WRITER_CLOSED = 0, WRITER_READY };

typedef struct stream_writer {
  int64_t desired_size;
  _Atomic(struct writable_stream*) stream;
  union {
    struct {
      Promise closed, ready;
    };
    Promise events[2];
  };
  Queue q;
} Writer;

typedef enum { READABLE_START = 0, READABLE_PULL, READABLE_CANCEL } ReadableEvent;

typedef struct readable_stream {
  int ref_count;
  Queue q;
  _Atomic(BOOL) closed;
  _Atomic(char*) reason;
  _Atomic(Reader*) reader;
  union {
    struct {
      JSValue start, pull, cancel;
    } on;
    JSValue events[3];
  };
  JSValue underlying_source, controller;
} Readable;

typedef enum { WRITABLE_START = 0, WRITABLE_WRITE, WRITABLE_CLOSE, WRITABLE_ABORT } WritableEvent;

typedef struct writable_stream {
  int ref_count;
  Queue q;
  _Atomic(BOOL) closed;
  _Atomic(char*) reason;
  _Atomic(Writer*) writer;
  union {
    struct {
      JSValue start, write, close, abort;
    } on;
    JSValue events[4];
  };
  JSValue underlying_sink, controller;
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
Readable* readable_new(JSContext*);
JSValue readable_close(Readable*, JSContext*);
JSValue readable_abort(Readable*, JSValueConst, JSContext*);
JSValue readable_enqueue(Readable*, JSValueConst, JSContext*);
void readable_unref(void*);
int readable_lock(Readable*, Reader*);
int readable_unlock(Readable*, Reader*);
Reader* readable_get_reader(Readable*, JSContext*);
JSValue js_readable_callback(JSContext*, Readable*, ReadableEvent, int argc, JSValueConst argv[]);
Writer* writer_new(JSContext*, Writable*);
BOOL writer_release_lock(Writer*, JSContext*);
JSValue writer_write(Writer*, JSValueConst, JSContext*);
JSValue writer_close(Writer*, JSContext*);
JSValue writer_abort(Writer*, JSValueConst, JSContext*);
BOOL writer_ready(Writer*, JSContext*);
JSValue writer_signal(Writer*, StreamEvent, JSValueConst, JSContext* ctx);
Writable* writable_new(JSContext*);
void writable_abort(Writable*, JSValueConst, JSContext*);
void writable_unref(void*);
int writable_lock(Writable*, Writer*);
int writable_unlock(Writable*, Writer*);
Writer* writable_get_writer(Writable*, size_t, JSContext*);
JSValue js_writable_start(JSContext*, Writable*);
JSValue js_writable_write(JSContext*, Writable*, JSValueConst);
JSValue js_writable_close(JSContext*, Writable*);
JSValue js_writable_abort(JSContext*, Writable*, JSValueConst);
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
