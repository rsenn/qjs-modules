#include "quickjs-stream.h"
#include "buffer-utils.h"
#include "debug.h"
#include <assert.h>

/**
 * \defgroup quickjs-stream QuickJS module: stream - Buffered stream
 * @{
 */

thread_local VISIBLE JSClassID js_readable_class_id = 0, js_writable_class_id = 0, js_reader_class_id = 0, js_writer_class_id = 0;
thread_local JSValue readable_proto = {{JS_TAG_UNDEFINED}}, readable_controller = {{JS_TAG_UNDEFINED}}, readable_ctor = {{JS_TAG_UNDEFINED}},
                     writable_proto = {{JS_TAG_UNDEFINED}}, writable_controller = {{JS_TAG_UNDEFINED}}, writable_ctor = {{JS_TAG_UNDEFINED}},
                     reader_proto = {{JS_TAG_UNDEFINED}}, reader_ctor = {{JS_TAG_UNDEFINED}}, writer_proto = {{JS_TAG_UNDEFINED}},
                     writer_ctor = {{JS_TAG_UNDEFINED}};

void
chunk_unref(JSRuntime* rt, void* opaque, void* ptr) {
  Chunk* ch = opaque;

  chunk_free(ch);
}

JSValue
chunk_arraybuf(Chunk* ch, JSContext* ctx) {
  chunk_dup(ch);

  return JS_NewArrayBuffer(ctx, ch->data + ch->pos, ch->size - ch->pos, chunk_unref, ch, FALSE);
}

Reader*
reader_new(JSContext* ctx, Readable* st) {
  Reader* rd;

  if((rd = js_mallocz(ctx, sizeof(Reader)))) {
    atomic_store(&rd->stream, st);

    promise_init(&rd->closed, ctx);
    promise_zero(&rd->cancelled);

    init_list_head(&rd->reads);
  }

  return rd;
}

BOOL
reader_release_lock(Reader* rd, JSContext* ctx) {
  BOOL ret;

  if((ret = readable_unlock(&rd->stream, rd))) {
    atomic_store(&rd->stream, (Readable*)0);
  }

  return ret;
}

BOOL
reader_cancel(Reader* rd, JSContext* ctx) {
  BOOL ret = FALSE;

  if(JS_IsUndefined(rd->cancelled.promise)) {
    ret = promise_init(&rd->cancelled, ctx);
  }

  return ret;
}

JSValue
reader_read(Reader* rd, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  struct read_operation* op;

  if(!(op = js_mallocz(ctx, sizeof(struct read_operation))))
    return JS_ThrowOutOfMemory(ctx);

  list_add(&op->link, &rd->reads);

  ret = promise_create(ctx, &op->handlers);
  return ret;
}

JSValue
reader_signal(Reader* rd, StreamEvent event, JSValueConst arg, JSContext* ctx) {
  JSValue ret;

  assert(event <= EVENT_READ);
  assert(event >= EVENT_CLOSE);

  ret = promise_resolve(ctx, &rd->events[event], arg);

  return ret;
}

BOOL
reader_passthrough(Reader* rd, JSValueConst chunk, JSContext* ctx) {
  Read* r;

  if((r = rd->reads.prev) != &rd->reads) {
    JSValue ret = promise_resolve(ctx, &r->handlers, chunk);
    JS_FreeValue(ctx, ret);
    list_del(&r->link);
    js_free(ctx, r);
    return TRUE;
  }

  return FALSE;
}

Readable*
readable_new(JSContext* ctx) {
  Readable* st;

  if((st = js_mallocz(ctx, sizeof(Readable)))) {
    st->ref_count = 1;

    queue_init(&st->q);
  }

  return st;
}

JSValue
readable_close(Readable* st, JSContext* ctx) {
  static const BOOL expected = FALSE;

  if(atomic_compare_exchange_weak(&st->closed, &expected, TRUE))
    return js_readable_callback(ctx, st, READABLE_CANCEL, 0, 0);

  return JS_ThrowInternalError(ctx, "No locked ReadableStream associated");
}

JSValue
readable_abort(Readable* st, JSValueConst reason, JSContext* ctx) {
  static const BOOL expected = FALSE;

  if(atomic_compare_exchange_weak(&st->closed, &expected, TRUE))
    return js_readable_callback(ctx, st, READABLE_CANCEL, 1, &reason);

  return JS_ThrowInternalError(ctx, "No locked ReadableStream associated");
}

JSValue
readable_enqueue(Readable* st, JSValueConst chunk, JSContext* ctx) {
  MemoryBlock b;
  InputBuffer input;
  int64_t ret;
  Reader* rd;

  if(readable_locked(st) && (rd = st->reader)) {
    if(reader_passthrough(rd, chunk, ctx))
      return JS_UNDEFINED;
  }

  input = js_input_chars(ctx, chunk);

  ret = queue_write(&st->q, input.data, input.size);

  input_buffer_free(&input, ctx);
  return ret < 0 ? JS_ThrowInternalError(ctx, "enqueue() returned %" PRId64, ret) : JS_NewInt64(ctx, ret);
}

/*
void
readable_unref(void* opaque) {
  Readable* st = opaque;

  --st->ref_count;
}
*/
int
readable_lock(Readable* st, Reader* rd) {
  const Reader* expected = 0;
  return atomic_compare_exchange_weak(&st->reader, &expected, rd);
}

int
readable_unlock(Readable* st, Reader* rd) {
  return atomic_compare_exchange_weak(&st->reader, &rd, 0);
}

Reader*
readable_get_reader(Readable* st, JSContext* ctx) {
  Reader* rd;

  if(!(rd = reader_new(ctx, st)))
    return 0;

  if(!readable_lock(st, rd)) {
    js_free(ctx, rd);
    rd = 0;
  }

  return rd;
}

enum {
  FUNC_PEEK,

};

static JSValue
js_reader_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Reader* rd;
  Readable* st;

  if(argc < 1 || !(st = js_readable_data(argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a Readable");

  if(!(rd = reader_new(ctx, st)))
    return JS_ThrowOutOfMemory(ctx);

  if(!readable_unlock(st, rd)) {
    JS_ThrowInternalError(ctx, "unable to lock Readable");
    goto fail;
  }

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = reader_proto;

  obj = JS_NewObjectProtoClass(ctx, proto, js_reader_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, rd);

  return obj;

fail:
  js_free(ctx, rd);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_reader_wrap(JSContext* ctx, Reader* rd) {
  JSValue obj;
  obj = JS_NewObjectProtoClass(ctx, reader_proto, js_reader_class_id);
  JS_SetOpaque(obj, rd);
  return obj;
}

/*static JSValue
js_reader_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Reader* rd;
  Readable* st;
  MemoryBlock b;
  InputBuffer output;
  ssize_t ret;

  if(!(rd = js_reader_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(st = rd->stream))
    return JS_ThrowInternalError(ctx, "StreamReader has no stream");

  output = js_output_args(ctx, argc, argv);
  b = block_range(input_buffer_blockptr(&output), &output.range);

  ret = magic == FUNC_PEEK ? queue_peek(&st->q, b.base, b.size) : queue_read(&st->q, b.base, b.size);

  input_buffer_free(&output, ctx);

  return JS_NewInt64(ctx, ret);
}*/

enum {
  READER_CANCEL,
  READER_READ,
  READER_RELEASE_LOCK,
};

static JSValue
js_reader_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  Reader* rd;

  if(!(rd = js_reader_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case READER_CANCEL: {
      reader_cancel(rd, ctx);
      ret = JS_DupValue(ctx, rd->cancelled.promise);
      break;
    }
    case READER_READ: {
      ret = reader_read(rd, ctx);
      break;
    }
    case READER_RELEASE_LOCK: {
      reader_release_lock(rd, ctx);
      break;
    }
  }

  return ret;
}

// enum { READER_CLOSED };

static JSValue
js_reader_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Reader* rd;
  JSValue ret = JS_UNDEFINED;

  if(!(rd = js_reader_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case READER_CLOSED: {
      ret = JS_DupValue(ctx, rd->closed.promise);
      break;
    }
  }
  return ret;
}

static void
js_reader_finalizer(JSRuntime* rt, JSValue val) {
  Reader* rd;

  if((rd = JS_GetOpaque(val, js_reader_class_id))) {
    js_free_rt(rt, rd);
  }

  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_reader_class = {
    .class_name = "StreamReader",
    .finalizer = js_reader_finalizer,
};

static const JSCFunctionListEntry js_reader_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("read", 0, js_reader_method, READER_READ),
    // JS_CFUNC_MAGIC_DEF("peek", 1, js_reader_read, READER_PEEK),
    JS_CFUNC_MAGIC_DEF("cancel", 0, js_reader_method, READER_CANCEL),
    JS_CFUNC_MAGIC_DEF("releaseLock", 0, js_reader_method, READER_RELEASE_LOCK),
    JS_CGETSET_MAGIC_DEF("closed", js_reader_get, 0, READER_CLOSED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StreamReader", JS_PROP_CONFIGURABLE),
};

JSValue
js_readable_callback(JSContext* ctx, Readable* st, ReadableEvent event, int argc, JSValueConst argv[]) {
  assert(event >= 0);
  assert(event < countof(st->events));

  if(JS_IsFunction(ctx, st->events[event]))
    return JS_Call(ctx, st->events[event], st->underlying_source, argc, argv);

  return JS_UNDEFINED;
}
/*
JSValue
js_readable_start(JSContext* ctx, Readable* st) {
  JSValueConst args[] = {st->controller};
  return js_readable_callback(ctx, st, READABLE_START, countof(args), args);
}

JSValue
js_readable_pull(JSContext* ctx, Readable* st, JSValueConst chunk) {
  JSValueConst args[] = {st->controller};
  return js_readable_callback(ctx, st, READABLE_PULL, countof(args), args);
}

JSValue
js_readable_cancel(JSContext* ctx, Readable* st, JSValueConst reason) {
  return js_readable_callback(ctx, st, READABLE_CANCEL, 1, &reason);
}*/

static JSValue
js_readable_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Readable* st;

  if(!(st = readable_new(ctx)))
    return JS_EXCEPTION;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, readable_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_readable_class_id);
  if(JS_IsException(obj))
    goto fail;

  if(argc >= 1 && !JS_IsNull(argv[0]) && JS_IsObject(argv[0])) {
    st->on.start = JS_GetPropertyStr(ctx, argv[0], "start");
    st->on.pull = JS_GetPropertyStr(ctx, argv[0], "pull");
    st->on.cancel = JS_GetPropertyStr(ctx, argv[0], "cancel");
    st->underlying_source = JS_DupValue(ctx, argv[0]);
    st->controller = JS_NewObjectProtoClass(ctx, readable_controller, js_readable_class_id);
    JS_SetOpaque(st->controller, st);
  }

  JS_SetOpaque(obj, st);

  return obj;
fail:
  js_free(ctx, st);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

/*static JSValue
js_readable_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  Readable* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_readable_class_id)))
    return JS_EXCEPTION;

  *pdone = queue_empty(&st->q);

  if(!*pdone)
    ret = readable_next(st, ctx);

  return ret;
}

static JSValue
js_readable_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}*/

enum {
  READABLE_ABORT = 0,
  READABLE_GET_READER,
};

static JSValue
js_readable_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Readable* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_readable_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case READABLE_ABORT: {
      Reader* rd;

      if((rd = readable_locked(st))) {
        readable_abort(rd, argc >= 1 ? argv[0] : JS_UNDEFINED, ctx);
      }

      break;
    }
    case READABLE_GET_READER: {
      Reader* rd;

      if((rd = readable_get_reader(st, ctx)))
        ret = js_reader_wrap(ctx, rd);
      break;
    }
  }

  return ret;
}
enum { STREAM_CLOSED, STREAM_LOCKED };

static JSValue
js_readable_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Readable* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_readable_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case STREAM_CLOSED: {
      ret = JS_NewBool(ctx, readable_closed(st));
      break;
    }
    case STREAM_LOCKED: {
      ret = JS_NewBool(ctx, readable_locked(st) || readable_locked(st));
      break;
    }
  }
  return ret;
}

enum { READABLE_CLOSE = 0, READABLE_ENQUEUE, READABLE_ERROR };

static JSValue
js_readable_controller(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Readable* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_readable_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case READABLE_CLOSE: {
      ret = readable_close(st, ctx);
      break;
    }
    case READABLE_ENQUEUE: {
      ret = readable_enqueue(st, argv[0], ctx);
      break;
    }
    case READABLE_ERROR: {
      readable_abort(st, argc >= 1 ? argv[0] : JS_UNDEFINED, ctx);
      break;
    }
  }

  return ret;
}

static JSValue
js_readable_desired(JSContext* ctx, JSValueConst this_val) {
  Readable* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_readable_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(readable_locked(st)) {
    Reader* rd;

    if((rd = st->reader)) {
      ret = JS_NewUint32(ctx, rd->desired_size);
    }
  }

  return ret;
}

static void
js_readable_finalizer(JSRuntime* rt, JSValue val) {
  Readable* st;

  if((st = JS_GetOpaque(val, js_readable_class_id))) {
    if(--st->ref_count == 0) {
      js_free_rt(rt, st);
    }
  }
}

static JSClassDef js_readable_class = {
    .class_name = "Readable",
    .finalizer = js_readable_finalizer,
};

static const JSCFunctionListEntry js_readable_proto_funcs[] = {
    /*    JS_ITERATOR_NEXT_DEF("next", 0, js_readable_next, 0),
        JS_CFUNC_DEF("write", 1, js_readable_write),
        JS_CFUNC_MAGIC_DEF("read", 1, js_readable_read, 0),
        JS_CFUNC_MAGIC_DEF("peek", 1, js_readable_read, 1),*/
    JS_CFUNC_MAGIC_DEF("cancel", 0, js_readable_method, READABLE_ABORT),
    JS_CFUNC_MAGIC_DEF("getReader", 0, js_readable_method, READABLE_GET_READER),
    JS_CGETSET_MAGIC_FLAGS_DEF("locked", js_readable_get, 0, STREAM_LOCKED, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Readable", JS_PROP_CONFIGURABLE),
    //    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_readable_iterator),
};

static const JSCFunctionListEntry js_readable_controller_funcs[] = {
    JS_CFUNC_MAGIC_DEF("close", 0, js_readable_controller, READABLE_CLOSE),
    JS_CFUNC_MAGIC_DEF("enqueue", 1, js_readable_controller, READABLE_ENQUEUE),
    JS_CFUNC_MAGIC_DEF("error", 1, js_readable_controller, READABLE_ERROR),
    JS_CGETSET_DEF("desiredSize", js_readable_desired, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ReadableStreamDefaultController", JS_PROP_CONFIGURABLE),
};

Writer*
writer_new(JSContext* ctx, Writable* st) {
  Writer* wr;

  if((wr = js_mallocz(ctx, sizeof(Writer)))) {
    atomic_store(&wr->stream, st);
    promise_init(&wr->closed, ctx);
    promise_init(&wr->ready, ctx);
  }

  return wr;
}

BOOL
writer_release_lock(Writer* wr, JSContext* ctx) {
  BOOL ret;

  if((ret = readable_unlock(&wr->stream, wr))) {
    atomic_store(&wr->stream, (Writable*)0);
  }

  return ret;
}

JSValue
writer_write(Writer* wr, JSValueConst chunk, JSContext* ctx) {
  /* JSValue ret = JS_UNDEFINED;
ssize_t bytes;

 if((bytes = queue_write(&wr->q, block->base, block->size)) == block->size) {
   Chunk* chunk = queue_tail(&wr->q);

   chunk->opaque = promise_new(ctx, &ret);
 }*/
  if(!wr->stream)
    return JS_ThrowInternalError(ctx, "no WriteableStream");

  return js_writable_write(ctx, wr->stream, chunk);
}

JSValue
writer_close(Writer* wr, JSContext* ctx) {

  if(!wr->stream)
    return JS_ThrowInternalError(ctx, "no WriteableStream");

  return js_writable_close(ctx, wr->stream);
}

JSValue
writer_abort(Writer* wr, JSValueConst reason, JSContext* ctx) {

  if(!wr->stream)
    return JS_ThrowInternalError(ctx, "no WriteableStream");

  return js_writable_abort(ctx, wr->stream, reason);
}

BOOL
writer_ready(Writer* wr, JSContext* ctx) {
  BOOL ret = FALSE;

  if(JS_IsUndefined(wr->ready.promise)) {
    ret = promise_init(&wr->ready, ctx);
  }

  return ret;
}

JSValue
writer_signal(Writer* wr, StreamEvent event, JSValueConst arg, JSContext* ctx) {
  JSValue ret;

  assert(event <= EVENT_READ);
  assert(event >= EVENT_CLOSE);

  ret = promise_resolve(ctx, &wr->events[event], arg);

  return ret;
}

Writable*
writable_new(JSContext* ctx) {
  Writable* st;

  if((st = js_mallocz(ctx, sizeof(Writable)))) {
    st->ref_count = 1;
    // queue_init(&st->q);
  }

  return st;
}

void
writable_abort(Writable* st, JSValueConst reason, JSContext* ctx) {
  static const BOOL expected = FALSE;
  if(atomic_compare_exchange_weak(&st->closed, &expected, TRUE)) {
    st->reason = js_tostring(ctx, reason);
    // queue_clear(&st->q);
  }
}

/*void
writable_unref(void* opaque) {
  Writable* st = opaque;

  --st->ref_count;
}
*/
/*JSValue
writable_next(Writable* st, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  Chunk* chunk;

  if((chunk = queue_next(&st->q))) {
    ret = chunk_arraybuf(chunk, ctx);
  }

  return ret;
}

int
writable_at(Writable* st, int64_t offset) {
  size_t length = writable_size(st);
  struct list_head* el;
  int64_t pos = 0;

  offset = MOD_NUM(offset, (int64_t)length);

  if(offset < 0 || offset >= length)
    return -1;

  list_for_each_prev(el, &st->q.list) {
    Chunk* chunk = list_entry(el, Chunk, link);

    if(offset >= pos && offset < pos + chunk->size)
      return chunk->data[offset - pos];

    pos += chunk->size;
  }

  return -1;
}
*/
int
writable_lock(Writable* st, Writer* wr) {
  const Writer* expected = 0;
  return atomic_compare_exchange_weak(&st->writer, &expected, wr);
}

int
writable_unlock(Writable* st, Writer* wr) {
  return atomic_compare_exchange_weak(&st->writer, &wr, 0);
}

Writer*
writable_get_writer(Writable* st, size_t desired_size, JSContext* ctx) {
  Writer* wr;
  if(!(wr = writer_new(ctx, st)))
    return 0;
  wr->desired_size = desired_size;
  if(!writable_lock(st, wr)) {
    js_free(ctx, wr);
    wr = 0;
  }
  return wr;
}

static JSValue
js_writer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Writer* wr;
  Writable* st;

  if(argc < 1 || !(st = js_writable_data(argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a Writable");

  if(!(wr = writer_new(ctx, st)))
    return JS_ThrowOutOfMemory(ctx);

  if(!writable_lock(st, wr)) {
    JS_ThrowInternalError(ctx, "unable to lock Writable");
    goto fail;
  }

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = writer_proto;

  obj = JS_NewObjectProtoClass(ctx, proto, js_writer_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, wr);

  return obj;

fail:
  js_free(ctx, wr);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_writer_wrap(JSContext* ctx, Writer* wr) {
  JSValue obj;
  obj = JS_NewObjectProtoClass(ctx, writer_proto, js_writer_class_id);
  JS_SetOpaque(obj, wr);
  return obj;
}

enum {
  WRITER_ABORT,
  WRITER_CLOSE,
  WRITER_WRITE,
  WRITER_RELEASE_LOCK,
};

static JSValue
js_writer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  Writer* wr;
  Writable* st;

  if(!(wr = js_writer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  st = wr->stream;

  switch(magic) {
    case WRITER_ABORT: {
      ret = writer_abort(wr, argv[0], ctx);
      break;
    }
    case WRITER_CLOSE: {
      ret = writer_close(wr, ctx);
      break;
    }
    case WRITER_WRITE: {
      /*  MemoryBlock b;
        InputBuffer input;
        input = js_input_args(ctx, argc, argv);
        b = block_range(input_buffer_blockptr(&input), &input.range);
        ret = writer_write(wr, &b, ctx);
        input_buffer_free(&input, ctx);*/
      ret = writer_write(wr, argv[0], ctx);
      break;
    }
    case WRITER_RELEASE_LOCK: {
      writer_release_lock(wr, ctx);
      break;
    }
  }

  return ret;
}

static JSValue
js_writer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Writer* wr;
  JSValue ret = JS_UNDEFINED;

  if(!(wr = js_writer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITER_CLOSED: {
      ret = JS_DupValue(ctx, wr->closed.promise);
      break;
    }
    case WRITER_READY: {
      ret = JS_DupValue(ctx, wr->ready.promise);
      break;
    }
  }
  return ret;
}

static void
js_writer_finalizer(JSRuntime* rt, JSValue val) {
  Writer* wr;

  if((wr = JS_GetOpaque(val, js_writer_class_id))) {
    js_free_rt(rt, wr);
  }
}

static JSClassDef js_writer_class = {
    .class_name = "StreamWriter",
    .finalizer = js_writer_finalizer,
};

static const JSCFunctionListEntry js_writer_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("write", 0, js_writer_method, WRITER_WRITE),
    JS_CFUNC_MAGIC_DEF("abort", 0, js_writer_method, WRITER_ABORT),
    JS_CFUNC_MAGIC_DEF("close", 0, js_writer_method, WRITER_CLOSE),
    JS_CFUNC_MAGIC_DEF("releaseLock", 0, js_writer_method, WRITER_RELEASE_LOCK),
    JS_CGETSET_MAGIC_DEF("closed", js_writer_get, 0, WRITER_CLOSED),
    JS_CGETSET_MAGIC_DEF("ready", js_writer_get, 0, WRITER_READY),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StreamWriter", JS_PROP_CONFIGURABLE),
};

static inline JSValue
js_writable_callback(JSContext* ctx, Writable* st, WritableEvent event, int argc, JSValueConst argv[]) {
  assert(event >= 0);
  assert(event < countof(st->events));

  if(JS_IsFunction(ctx, st->events[event]))
    return JS_Call(ctx, st->events[event], st->underlying_sink, argc, argv);

  return JS_UNDEFINED;
}

JSValue
js_writable_start(JSContext* ctx, Writable* st) {
  JSValueConst args[] = {st->controller};
  return js_writable_callback(ctx, st, WRITABLE_START, countof(args), args);
}

JSValue
js_writable_write(JSContext* ctx, Writable* st, JSValueConst chunk) {
  JSValueConst args[] = {chunk, st->controller};
  return js_writable_callback(ctx, st, WRITABLE_START, countof(args), args);
}

JSValue
js_writable_close(JSContext* ctx, Writable* st) {
  JSValueConst args[] = {st->controller};
  return js_writable_callback(ctx, st, WRITABLE_CLOSE, countof(args), args);
}

JSValue
js_writable_abort(JSContext* ctx, Writable* st, JSValueConst err) {
  JSValueConst args[] = {err};
  return js_writable_callback(ctx, st, WRITABLE_CLOSE, countof(args), args);
}

static JSValue
js_writable_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Writable* st;

  if(!(st = writable_new(ctx)))
    return JS_EXCEPTION;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, writable_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_writable_class_id);
  if(JS_IsException(obj))
    goto fail;

  if(argc >= 1 && !JS_IsNull(argv[0]) && JS_IsObject(argv[0])) {
    st->on.start = JS_GetPropertyStr(ctx, argv[0], "start");
    st->on.write = JS_GetPropertyStr(ctx, argv[0], "write");
    st->on.close = JS_GetPropertyStr(ctx, argv[0], "close");
    st->on.abort = JS_GetPropertyStr(ctx, argv[0], "abort");
    st->underlying_sink = JS_DupValue(ctx, argv[0]);

    st->controller = JS_NewObjectProtoClass(ctx, writable_controller, js_writable_class_id);
    JS_SetOpaque(st->controller, st);
  }

  JS_SetOpaque(obj, st);

  return obj;
fail:
  js_free(ctx, st);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

/*static JSValue
js_writable_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  Writable* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_writable_class_id)))
    return JS_EXCEPTION;

  *pdone = queue_empty(&st->q);

  if(!*pdone)
    ret = writable_next(st, ctx);

  return ret;
}*/

static JSValue
js_writable_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

enum {
  WRITABLE_METHOD_CLOSE = 0,
  WRITABLE_METHOD_ABORT,
  WRITABLE_GET_WRITER,
};

static JSValue
js_writable_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Writable* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_writable_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITABLE_METHOD_ABORT: {
      writable_abort(st, argv[0], ctx);
      break;
    }
     case WRITABLE_METHOD_CLOSE: {
      writable_abort(st, JS_UNDEFINED, ctx);
      break;
    }
    case WRITABLE_GET_WRITER: {
      Writer* wr;

      if((wr = writable_get_writer(st, 0, ctx)))
        ret = js_writer_wrap(ctx, wr);
      break;
    }
  }

  return ret;
}

enum { WRITABLE_CLOSED, WRITABLE_LOCKED };

static JSValue
js_writable_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Writable* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_writable_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITABLE_CLOSED: {
      ret = JS_NewBool(ctx, writable_closed(st));
      break;
    }
    case WRITABLE_LOCKED: {
      ret = JS_NewBool(ctx, writable_locked(st) || writable_locked(st));
      break;
    }
  }
  return ret;
}

enum { WRITABLE_ERROR = 0 };

static JSValue
js_writable_controller(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Writable* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_writable_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITABLE_ERROR: {
      writable_abort(st, argc >= 1 ? argv[0] : JS_UNDEFINED, ctx);
      break;
    }
  }

  return ret;
}

static void
js_writable_finalizer(JSRuntime* rt, JSValue val) {
  Writable* st;

  if((st = JS_GetOpaque(val, js_writable_class_id))) {
    if(--st->ref_count == 0) {
      js_free_rt(rt, st);
    }
  }
}
/*
static int
js_writable_get_own_property(JSContext* ctx, JSPropertyDescriptor* pdesc, JSValueConst obj, JSAtom prop) {
  Writable* st = js_writable_data2(ctx, obj);
  JSValue value = JS_UNDEFINED;
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    size_t length = writable_size(st);

    if(index < 0 && ABS_NUM(index) < (int64_t)length)
      index = MOD_NUM(index, (int64_t)length);

    if(index >= 0 && index < (int64_t)length)
      value = JS_NewInt32(ctx, writable_at(st, index));

  } else if(js_atom_is_string(ctx, prop, "size")) {
    value = JS_NewInt64(ctx, writable_size(st));
  }

  if(!JS_IsUndefined(value)) {
    if(pdesc) {
      pdesc->flags = JS_PROP_ENUMERABLE;
      pdesc->value = value;
      pdesc->getter = JS_UNDEFINED;
      pdesc->setter = JS_UNDEFINED;
    }
    return TRUE;
  }
  return FALSE;
}

static int
js_writable_has_property(JSContext* ctx, JSValueConst obj, JSAtom prop) {
  Writable* st = js_writable_data2(ctx, obj);
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    size_t length = writable_size(st);

    if(index < 0 && ABS_NUM(index) < length)
      index = MOD_NUM(index, (int64_t)length);

    if(index >= 0 && index < (int64_t)length)
      return TRUE;

  } else if(js_atom_is_string(ctx, prop, "size")) {
    return TRUE;
  }

  return FALSE;
}

static JSValue
js_writable_get_property(JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst receiver) {
  Writable* st = js_writable_data2(ctx, obj);
  JSValue value = JS_UNDEFINED;
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    size_t length = writable_size(st);

    if(index < 0 && ABS_NUM(index) < (int64_t)length)
      index = MOD_NUM(index, (int64_t)length);

    if(index >= 0 && index < (int64_t)length)
      value = JS_NewUint32(ctx, writable_at(st, index));

  } else if(js_atom_is_string(ctx, prop, "size")) {
    value = JS_NewInt64(ctx, writable_size(st));

  } else {
    JSValue proto = JS_IsUndefined(writable_proto) ? JS_GetPrototype(ctx, obj) : writable_proto;
    if(JS_IsObject(proto)) {
      JSValue method = JS_GetProperty(ctx, proto, prop);

      if(JS_IsFunction(ctx, method))
        value = method;
      else
        JS_FreeValue(ctx, method);
    }
  }

  return value;
}

static JSClassExoticMethods js_writable_exotic_methods = {
    .has_property = js_writable_has_property,
    .get_property = js_writable_get_property,
    .get_own_property = js_writable_get_own_property,

};*/

static JSClassDef js_writable_class = {
    .class_name = "Writable", .finalizer = js_writable_finalizer,
    //  .exotic = &js_writable_exotic_methods,
};

static const JSCFunctionListEntry js_writable_proto_funcs[] = {
    /*    JS_ITERATOR_NEXT_DEF("next", 0, js_writable_next, 0),
        JS_CFUNC_DEF("write", 1, js_writable_write),
      JS_CFUNC_MAGIC_DEF("write", 1, js_writable_write, 0),
        JS_CFUNC_MAGIC_DEF("peek", 1, js_writable_write, 1),*/
    JS_CFUNC_MAGIC_DEF("abort", 1, js_writable_method, WRITABLE_METHOD_ABORT),
    JS_CFUNC_MAGIC_DEF("close", 0, js_writable_method, WRITABLE_METHOD_CLOSE),
    JS_CFUNC_MAGIC_DEF("getWriter", 0, js_writable_method, WRITABLE_GET_WRITER),
    JS_CGETSET_MAGIC_FLAGS_DEF("locked", js_writable_get, 0, WRITABLE_LOCKED, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WritableStream", JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_writable_iterator),
};

static const JSCFunctionListEntry js_writable_controller_funcs[] = {
    JS_CFUNC_MAGIC_DEF("error", 0, js_writable_controller, WRITABLE_ERROR),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WritableStreamDefaultController", JS_PROP_CONFIGURABLE),
};

static int
js_stream_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_reader_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_reader_class_id, &js_reader_class);

  reader_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, reader_proto, js_reader_proto_funcs, countof(js_reader_proto_funcs));
  JS_SetClassProto(ctx, js_reader_class_id, reader_proto);

  reader_ctor = JS_NewCFunction2(ctx, js_reader_constructor, "StreamReader", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, reader_ctor, reader_proto);

  JS_NewClassID(&js_writer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_writer_class_id, &js_writer_class);

  writer_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, writer_proto, js_writer_proto_funcs, countof(js_writer_proto_funcs));
  JS_SetClassProto(ctx, js_writer_class_id, writer_proto);

  writer_ctor = JS_NewCFunction2(ctx, js_writer_constructor, "StreamWriter", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, writer_ctor, writer_proto);

  JS_NewClassID(&js_readable_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_readable_class_id, &js_readable_class);

  readable_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, readable_proto, js_readable_proto_funcs, countof(js_readable_proto_funcs));
  JS_SetClassProto(ctx, js_readable_class_id, readable_proto);

  readable_ctor = JS_NewCFunction2(ctx, js_readable_constructor, "ReadableStream", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, readable_ctor, readable_proto);

  readable_controller = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, readable_controller, js_readable_controller_funcs, countof(js_readable_controller_funcs));
  JS_SetClassProto(ctx, js_readable_class_id, readable_controller);

  JS_NewClassID(&js_writable_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_writable_class_id, &js_writable_class);

  writable_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, writable_proto, js_writable_proto_funcs, countof(js_writable_proto_funcs));
  JS_SetClassProto(ctx, js_writable_class_id, writable_proto);

  writable_ctor = JS_NewCFunction2(ctx, js_writable_constructor, "WritableStream", 1, JS_CFUNC_constructor, 0);
  
  JS_SetConstructor(ctx, writable_ctor, writable_proto);

  writable_controller = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, writable_controller, js_writable_controller_funcs, countof(js_writable_controller_funcs));
  JS_SetClassProto(ctx, js_writable_class_id, writable_controller);


  // JS_SetPropertyFunctionList(ctx, stream_ctor, js_stream_static_funcs, countof(js_stream_static_funcs));

  if(m) {
    JS_SetModuleExport(ctx, m, "StreamReader", reader_ctor);
    JS_SetModuleExport(ctx, m, "StreamWriter", writer_ctor);
    JS_SetModuleExport(ctx, m, "ReadableStream", readable_ctor);
    JS_SetModuleExport(ctx, m, "WritableStream", writable_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_stream
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, &js_stream_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "StreamReader");
  JS_AddModuleExport(ctx, m, "StreamWriter");
  JS_AddModuleExport(ctx, m, "ReadableStream");
  JS_AddModuleExport(ctx, m, "WritableStream");
  return m;
}

/**
 * @}
 */
