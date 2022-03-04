#ifndef QUICKJS_STREAM_H
#define QUICKJS_STREAM_H

#include "include/defines.h"
#include "include/js-utils.h"
#include "include/buffer-utils.h"
#include "include/queue.h"
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
  Promise events[2];
  struct list_head reads;
} Reader;

enum { WRITER_CLOSED = 0, WRITER_READY };

typedef struct stream_writer {
  int64_t desired_size;
  _Atomic(struct writable_stream*) stream;
  Promise events[2];
  Queue q;
} Writer;

typedef enum { READABLE_START = 0, READABLE_PULL, READABLE_CANCEL } ReadableEvent;

typedef struct readable_stream {
  int ref_count;
  Queue q;
  _Atomic(BOOL) closed;
  _Atomic(char*) reason;
  _Atomic(Reader*) reader;
  JSValue on[3];
  JSValue underlying_source, controller;
} Readable;

typedef enum { WRITABLE_START = 0, WRITABLE_WRITE, WRITABLE_CLOSE, WRITABLE_ABORT } WritableEvent;

typedef struct writable_stream {
  int ref_count;
  Queue q;
  _Atomic(BOOL) closed;
  _Atomic(char*) reason;
  _Atomic(Writer*) writer;
  JSValue on[4];
  JSValue underlying_sink, controller;
} Writable;

typedef enum { TRANSFORM_START = 0, TRANSFORM_TRANSFORM, TRANSFORM_FLUSH } TransformEvent;

typedef struct transform_stream {
  int ref_count;
  Readable* readable;
  Writable* writable;
  JSValue on[3];
  JSValue underlying_transform, controller;
} Transform;

typedef enum { TRANSFORM_READABLE = 0, TRANSFORM_WRITABLE } TransformProperties;

extern thread_local JSClassID js_reader_class_id, js_writer_class_id, js_readable_class_id, js_writable_class_id, js_transform_class_id;
extern thread_local JSValue reader_proto, reader_ctor, writer_proto, writer_ctor, readable_proto, readable_ctor, writable_proto, writable_ctor, transform_proto,
    transform_ctor;

Reader* reader_new(JSContext*, Readable*);
BOOL reader_release_lock(Reader*, JSContext*);
int reader_clear(Reader*, JSContext*);
int reader_cancel(Reader*, JSContext*);
JSValue reader_read(Reader*, JSContext*);
JSValue reader_signal(Reader*, StreamEvent, JSValue, JSContext* ctx);
int reader_update(Reader*, JSContext*);
BOOL reader_passthrough(Reader*, JSValue, JSContext*);
void readable_close(Readable*, JSContext*);
void readable_abort(Readable*, JSValue, JSContext*);
JSValue readable_enqueue(Readable*, JSValue, JSContext*);
int readable_lock(Readable*, Reader*);
int readable_unlock(Readable*, Reader*);
Reader* readable_get_reader(Readable*, JSContext*);
void readable_free(Readable*, JSRuntime*);
JSValue js_readable_callback(JSContext*, Readable*, ReadableEvent, int argc, JSValue argv[]);
Writer* writer_new(JSContext*, Writable*);
BOOL writer_release_lock(Writer*, JSContext*);
JSValue writer_write(Writer*, JSValue, JSContext*);
JSValue writer_close(Writer*, JSContext*);
JSValue writer_abort(Writer*, JSValue, JSContext*);
JSValue writer_signal(Writer*, StreamEvent, JSValue, JSContext* ctx);
void writable_abort(Writable*, JSValue, JSContext*);
int writable_lock(Writable*, Writer*);
int writable_unlock(Writable*, Writer*);
Writer* writable_get_writer(Writable*, size_t, JSContext*);
void writable_free(Writable*, JSRuntime*);
JSValue js_writable_callback(JSContext*, Writable*, WritableEvent, int argc, JSValue argv[]);
Transform* transform_new(JSContext*);
JSModuleDef* js_init_module_stream(JSContext*, const char*);

/* clang-format off */
static inline BOOL    reader_closed(Reader* rd) { return promise_done(&rd->events[READER_CLOSED]); }
static inline BOOL    reader_cancelled(Reader* rd) { return promise_done(&rd->events[READER_CANCELLED]); }
static inline BOOL    readable_closed(Readable* st) { return atomic_load(&st->closed); }
static inline Reader* readable_locked(Readable* st) { return atomic_load(&st->reader); }
static inline BOOL    writer_closed(Writer* wr) { return promise_done(&wr->events[WRITER_CLOSED]); }
static inline BOOL    writer_ready(Writer* wr) { return promise_done(&wr->events[WRITER_READY]); }
static inline BOOL    writable_closed(Writable* st) { return atomic_load(&st->closed); }
static inline Writer* writable_locked(Writable* st) { return atomic_load(&st->writer); }
static inline Reader* js_reader_data(JSValueConst value) { return JS_GetOpaque(value, js_reader_class_id); }
static inline Reader* js_reader_data2(JSContext* ctx, JSValueConst value) { return JS_GetOpaque2(ctx, value, js_reader_class_id); }
static inline Writer* js_writer_data(JSValueConst value) { return JS_GetOpaque(value, js_writer_class_id); }
static inline Writer* js_writer_data2(JSContext* ctx, JSValueConst value) { return JS_GetOpaque2(ctx, value, js_writer_class_id); }
static inline Readable* js_readable_data(JSValueConst value) { return JS_GetOpaque(value, js_readable_class_id); }
static inline Readable* js_readable_data2(JSContext* ctx, JSValueConst value) { return JS_GetOpaque2(ctx, value, js_readable_class_id); }
static inline Writable* js_writable_data(JSValueConst value) { return JS_GetOpaque(value, js_writable_class_id); }
static inline Writable* js_writable_data2(JSContext* ctx, JSValueConst value) { return JS_GetOpaque2(ctx, value, js_writable_class_id); }
static inline Transform* js_transform_data(JSValueConst value) { return JS_GetOpaque(value, js_transform_class_id); }
static inline Transform* js_transform_data2(JSContext* ctx, JSValueConst value) { return JS_GetOpaque2(ctx, value, js_transform_class_id); }
/* clang-format on */

/**
 * @}
 */

#endif /* defined(QUICKJS_STREAM_H) */
