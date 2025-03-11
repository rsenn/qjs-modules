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
 * \defgroup quickjs-stream quickjs-stream: Streams
 * @{
 */

#define HEAD(st) \
  struct { \
    st *prev, *next; \
  }

#define LINK(name, st) \
  union { \
    struct list_head name; \
    HEAD(st); \
  }

typedef struct read_next {
  LINK(link, struct read_next);
  int seq;
  union {
    ResolveFunctions handlers;
    Promise promise;
  };
} ReadRequest;

typedef struct stream_reader {
  int ref_count;
  int64_t desired_size;
  _Atomic(struct readable_stream*) stream;
  union {
    struct list_head list;
    ReadRequest reads;
  };
  struct {
    Promise cancelled, closed;
  } events;
} ReadableStreamReader;

typedef struct stream_writer {
  int64_t desired_size;
  _Atomic(struct writable_stream*) stream;
  Queue q;
  struct {
    Promise closed, ready
  } events;
} WritableStreamWriter;

typedef struct readable_stream {
  int ref_count;
  Queue q;
  _Atomic(BOOL) closed;
  _Atomic(char*) reason;
  _Atomic(ReadableStreamReader*) reader;
  JSValue on[3];
  JSValue underlying_source, controller;
} ReadableStream;

typedef struct writable_stream {
  int ref_count;
  Queue q;
  _Atomic(BOOL) closed;
  _Atomic(char*) reason;
  _Atomic(WritableStreamWriter*) writer;
  JSValue on[4];
  JSValue underlying_sink, controller;
} WritableStream;

typedef struct transform_stream {
  int ref_count;
  ReadableStream* readable;
  WritableStream* writable;
  JSValue on[3];
  JSValue underlying_transform, controller;
} TransformStream;

/*extern VISIBLE JSClassID js_reader_class_id, js_writer_class_id, js_readable_class_id, js_writable_class_id, js_transform_class_id;
extern VISIBLE JSValue reader_proto, reader_ctor, writer_proto, writer_ctor, readable_proto, readable_ctor, writable_proto, writable_ctor, transform_proto, transform_ctor;*/

int js_stream_init(JSContext*, JSModuleDef*);
JSModuleDef* js_init_module_stream(JSContext*, const char*);

/* clang-format off */
static inline BOOL reader_closed(ReadableStreamReader* rd) { return promise_done(&rd->events.closed.funcs); }
static inline BOOL reader_cancelled(ReadableStreamReader* rd) { return promise_done(&rd->events.cancelled.funcs); }
static inline BOOL readable_closed(ReadableStream* st) { return atomic_load(&st->closed); }
static inline ReadableStreamReader* readable_locked(ReadableStream* st) { return atomic_load(&st->reader); }
static inline BOOL writer_closed(WritableStreamWriter* wr) { return promise_done(&wr->events.closed.funcs); }
static inline BOOL writer_ready(WritableStreamWriter* wr) { return promise_done(&wr->events.ready.funcs); }
static inline BOOL writable_closed(WritableStream* st) { return atomic_load(&st->closed); }
static inline WritableStreamWriter* writable_locked(WritableStream* st) { return atomic_load(&st->writer); }
/* clang-format on */

/**
 * @}
 */

#endif /* defined(QUICKJS_STREAM_H) */
