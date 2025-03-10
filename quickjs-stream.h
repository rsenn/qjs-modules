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

typedef enum {
  EVENT_CLOSE = 0,
  EVENT_CANCEL = 1,
  EVENT_READ = 2,
} StreamEvent;

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
} Read;

enum {
  READER_CLOSED = 0,
  READER_CANCELLED,
};

typedef struct stream_reader {
  int ref_count;
  int64_t desired_size;
  _Atomic(struct readable_stream*) stream;
  Promise events[2];
  union {
    struct list_head list;
    Read reads;
  };
} Reader;

enum {
  WRITER_CLOSED = 0,
  WRITER_READY,
};

typedef struct stream_writer {
  int64_t desired_size;
  _Atomic(struct writable_stream*) stream;
  Promise events[2];
  Queue q;
} Writer;

typedef enum {
  READABLE_START = 0,
  READABLE_PULL,
  READABLE_CANCEL,
} ReadableEvent;

typedef struct readable_stream {
  int ref_count;
  Queue q;
  _Atomic(BOOL) closed;
  _Atomic(char*) reason;
  _Atomic(Reader*) reader;
  JSValue on[3];
  JSValue underlying_source, controller;
} Readable;

typedef enum {
  WRITABLE_START = 0,
  WRITABLE_WRITE,
  WRITABLE_CLOSE,
  WRITABLE_ABORT,
} WritableEvent;

typedef struct writable_stream {
  int ref_count;
  Queue q;
  _Atomic(BOOL) closed;
  _Atomic(char*) reason;
  _Atomic(Writer*) writer;
  JSValue on[4];
  JSValue underlying_sink, controller;
} Writable;

typedef enum {
  TRANSFORM_START = 0,
  TRANSFORM_TRANSFORM,
  TRANSFORM_FLUSH,
} TransformEvent;

typedef struct transform_stream {
  int ref_count;
  Readable* readable;
  Writable* writable;
  JSValue on[3];
  JSValue underlying_transform, controller;
} Transform;

typedef enum {
  TRANSFORM_READABLE = 0,
  TRANSFORM_WRITABLE,
} TransformProperties;

/*extern VISIBLE JSClassID js_reader_class_id, js_writer_class_id, js_readable_class_id, js_writable_class_id, js_transform_class_id;
extern VISIBLE JSValue reader_proto, reader_ctor, writer_proto, writer_ctor, readable_proto, readable_ctor, writable_proto, writable_ctor, transform_proto, transform_ctor;*/

int js_stream_init(JSContext*, JSModuleDef*);
JSModuleDef* js_init_module_stream(JSContext*, const char*);

/* clang-format off */
static inline BOOL    reader_closed(Reader* rd) { return promise_done(&rd->events[READER_CLOSED].funcs); }
static inline BOOL    reader_cancelled(Reader* rd) { return promise_done(&rd->events[READER_CANCELLED].funcs); }
static inline BOOL    readable_closed(Readable* st) { return atomic_load(&st->closed); }
static inline Reader* readable_locked(Readable* st) { return atomic_load(&st->reader); }
static inline BOOL    writer_closed(Writer* wr) { return promise_done(&wr->events[WRITER_CLOSED].funcs); }
static inline BOOL    writer_ready(Writer* wr) { return promise_done(&wr->events[WRITER_READY].funcs); }
static inline BOOL    writable_closed(Writable* st) { return atomic_load(&st->closed); }
static inline Writer* writable_locked(Writable* st) { return atomic_load(&st->writer); }
/* clang-format on */

/**
 * @}
 */

#endif /* defined(QUICKJS_STREAM_H) */
