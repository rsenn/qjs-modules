#include "quickjs-stream.h"
#include "buffer-utils.h"
#include "utils.h"
#include "debug.h"
#include <list.h>
#include <assert.h>

/**
 * \defgroup quickjs-stream quickjs-stream: Buffered stream
 * @{
 */

VISIBLE JSClassID js_readable_class_id = 0, js_writable_class_id = 0, js_reader_class_id = 0, js_writer_class_id = 0, js_transform_class_id = 0;
VISIBLE JSValue readable_proto = {{0}, JS_TAG_UNDEFINED}, readable_controller = {{0}, JS_TAG_UNDEFINED}, readable_ctor = {{0}, JS_TAG_UNDEFINED}, writable_proto = {{0}, JS_TAG_UNDEFINED},
                writable_controller = {{0}, JS_TAG_UNDEFINED}, writable_ctor = {{0}, JS_TAG_UNDEFINED}, transform_proto = {{0}, JS_TAG_UNDEFINED}, transform_controller = {{0}, JS_TAG_UNDEFINED},
                transform_ctor = {{0}, JS_TAG_UNDEFINED}, reader_proto = {{0}, JS_TAG_UNDEFINED}, reader_ctor = {{0}, JS_TAG_UNDEFINED}, writer_proto = {{0}, JS_TAG_UNDEFINED},
                writer_ctor = {{0}, JS_TAG_UNDEFINED};

static int reader_update(Reader* rd, JSContext* ctx);
static BOOL reader_passthrough(Reader* rd, JSValueConst result, JSContext* ctx);
static int readable_unlock(Readable* st, Reader* rd);
static int writable_unlock(Writable* st, Writer* wr);

static void
chunk_unref(JSRuntime* rt, void* opaque, void* ptr) {
  Chunk* ch = opaque;

  chunk_free(ch);
}

static JSValue
chunk_arraybuffer(Chunk* ch, JSContext* ctx) {
  uint8_t* ptr = ch->data + ch->pos;
  size_t len = ch->size - ch->pos;

  chunk_dup(ch);

  return JS_NewArrayBuffer(ctx, ptr, len, chunk_unref, ch, FALSE);
}

static Read*
read_new(Reader* rd, JSContext* ctx) {
  static int read_seq = 0;
  Read* op;

  if((op = js_mallocz(ctx, sizeof(struct read_next)))) {
    op->seq = ++read_seq;
    list_add((struct list_head*)op, &rd->list);

    promise_init(ctx, &op->promise);
  }

  return op;
}

static JSValue
read_next(Reader* rd, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  Read *el, *op = 0;

  list_for_each_prev(el, &rd->reads) {
    if(!JS_IsUndefined(el->promise.value)) {
      op = el;
      break;
    }
  }

  if(!op) {
    if(!(op = read_new(rd, ctx)))
      ret = JS_EXCEPTION;
  }

  if(op) {
    // printf("read_next (%i/%zu)\n", op->seq, list_size(&rd->list));
    ret = op->promise.value;
    op->promise.value = JS_UNDEFINED;
  }

  return ret;
}

static BOOL
read_done(Read* op) {
  return JS_IsUndefined(op->promise.value) && promise_done(&op->promise.funcs);
}

static void
read_free_rt(Read* op, JSRuntime* rt) {
  promise_free(rt, &op->promise);

  list_del(&op->link);
}

static Reader*
reader_new(JSContext* ctx, Readable* st) {
  Reader* rd;

  if((rd = js_mallocz(ctx, sizeof(Reader)))) {
    atomic_store(&rd->stream, st);

    promise_init(ctx, &rd->events[READER_CLOSED]);
    promise_zero(&rd->events[READER_CANCELLED]);

    init_list_head(&rd->list);

    JSValue ret = js_readable_callback(ctx, rd->stream, READABLE_START, 1, &rd->stream->controller);
    JS_FreeValue(ctx, ret);
  }

  return rd;
}

static BOOL
reader_release_lock(Reader* rd, JSContext* ctx) {
  BOOL ret = FALSE;
  Readable* r;

  if((r = atomic_load(&rd->stream))) {
    if((ret = readable_unlock(r, rd))) {
      atomic_store(&rd->stream, (Readable*)0);
    }
  }

  return ret;
}

static int
reader_clear(Reader* rd, JSContext* ctx) {
  int ret = 0;
  Read *el, *next;

  list_for_each_prev_safe(el, next, &rd->reads) {
    promise_reject(ctx, &el->promise.funcs, JS_UNDEFINED);

    read_free_rt(el, JS_GetRuntime(ctx));
  }

  return ret;
}

static JSValue
reader_close(Reader* rd, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  // printf("reader_close (1)  [%zu]\n", list_size(&rd->list));

  if(!rd->stream)
    return JS_ThrowInternalError(ctx, "no ReadableStream");

  ret = js_readable_callback(ctx, rd->stream, READABLE_CANCEL, 0, 0);

  // printf("reader_close (2) promise=%i\n", js_is_promise(ctx, ret));

  rd->stream->closed = TRUE;

  reader_update(rd, ctx);

  if(js_is_promise(ctx, ret)) {
    ret = promise_forward(ctx, ret, &rd->events[READER_CLOSED]);
  }
  // printf("reader_close (2) promise=%i\n", js_is_promise(ctx, ret));

  return ret;
}

static JSValue
reader_cancel(Reader* rd, JSValueConst reason, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  if(!rd->stream)
    return JS_ThrowInternalError(ctx, "no WriteableStream");

  ret = js_readable_callback(ctx, rd->stream, READABLE_CANCEL, 1, &reason);

  if(js_is_promise(ctx, ret)) {
    ret = promise_forward(ctx, ret, &rd->events[READER_CANCELLED]);
  }
  return ret;
}

/*
static int
reader_cancel(Reader* rd, JSContext* ctx) {

  if(JS_IsUndefined(rd->events[READER_CANCELLED].value))
    promise_init(ctx, &rd->events[READER_CANCELLED]);

  return reader_clear(rd, ctx);
}
*/
static JSValue
reader_read(Reader* rd, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  Readable* st;
  // printf("reader_read (1)  [%zu] closed=%i\n", list_size(&rd->list), rd->stream->closed);

  ret = read_next(rd, ctx);

  if(JS_IsException(ret))
    return ret;

  if((st = rd->stream)) {
    if(queue_empty(&st->q)) {
      JSValue tmp = js_readable_callback(ctx, st, READABLE_PULL, 1, &st->controller);
      JS_FreeValue(ctx, tmp);
    }
  }

  reader_update(rd, ctx);
  // printf("reader_read (2) [%zu] closed=%i\n", list_size(&rd->list), rd->stream->closed);
  //  printf("Read (%i) q2[%zu]\n", op->seq, queue_size(&st->q));

  return ret;
}

/*JSValue
reader_signal(Reader* rd, StreamEvent event, JSValueConst arg, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  assert(event <= EVENT_READ);
  assert(event >= EVENT_CLOSE);

  if(promise_resolve(ctx, &rd->events[event].funcs, arg))
    ret = JS_TRUE;

  return ret;
}*/

static size_t
reader_clean(Reader* rd, JSContext* ctx) {
  Read *el, *next;
  size_t ret = 0;

  list_for_each_prev_safe(el, next, (Read*)&rd->reads) {
    if(read_done(el)) {
      // printf("reader_clean() delete[%i]\n", el->seq);
      list_del(&el->link);
      js_free(ctx, el);
      ret++;
      continue;
    }
    break;
  }

  return ret;
}

static int
reader_update(Reader* rd, JSContext* ctx) {
  JSValue result;
  Chunk* ch;
  Readable* st = rd->stream;
  int ret = 0;

  reader_clean(rd, ctx);

  // printf("reader_update(1) [%zu] closed=%d queue.size=%zu\n", list_size(&rd->list), readable_closed(st),
  // queue_size(&st->q));

  if(readable_closed(st)) {
    promise_resolve(ctx, &rd->events[READER_CLOSED].funcs, JS_UNDEFINED);
    //   reader_clear(rd, ctx);
    result = js_iterator_result(ctx, JS_UNDEFINED, TRUE);

    if(reader_passthrough(rd, result, ctx))
      ++ret;
  } else {
    while(!list_empty(&rd->list) && (ch = queue_next(&st->q))) {
      JSValue chunk, result;
      // printf("reader_update(2) Chunk ptr=%p, size=%zu, pos=%zu\n", ch->data, ch->size, ch->pos);
      chunk = chunk_arraybuffer(ch, ctx);
      result = js_iterator_result(ctx, chunk, FALSE);
      JS_FreeValue(ctx, chunk);
      if(!reader_passthrough(rd, result, ctx))
        break;
      ++ret;

      JS_FreeValue(ctx, result);
    }
  }

  // printf("reader_update(3) closed=%d queue.size=%zu result = %d\n", readable_closed(st),
  // queue_size(&st->q), ret);

  return ret;
}

static BOOL
reader_passthrough(Reader* rd, JSValueConst result, JSContext* ctx) {
  Read *op = 0, *el, *next;
  BOOL ret = FALSE;
  list_for_each_prev_safe(el, next, &rd->reads) {
    // printf("reader_passthrough(1) el[%i]\n", el->seq);
    if(promise_pending(&el->promise.funcs)) {
      op = el;
      break;
    }
  }
  // printf("reader_passthrough(2) result=%s\n", JS_ToCString(ctx, result));

  if(op) {
    // printf("reader_passthrough(3) read[%i]\n", op->seq);
    ret = promise_resolve(ctx, &op->promise.funcs, result);
    reader_clean(rd, ctx);
  }
  return ret;
}

static Readable*
readable_new(JSContext* ctx) {
  Readable* st;

  if((st = js_mallocz(ctx, sizeof(Readable)))) {
    st->ref_count = 1;
    st->controller = JS_NULL;
    queue_init(&st->q);
  }

  return st;
}

static Readable*
readable_dup(Readable* st) {
  ++st->ref_count;
  return st;
}

static JSValue
readable_close(Readable* st, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  static BOOL expected = FALSE;
  // printf("readable_close(1) expected=%i, closed=%i\n", st->closed, expected);

  if(atomic_compare_exchange_weak(&st->closed, &expected, TRUE)) {
    if(readable_locked(st)) {
      // printf("readable_close(2) expected=%i, closed=%i\n", st->closed, expected);
      promise_resolve(ctx, &st->reader->events[READER_CLOSED].funcs, JS_UNDEFINED);
      reader_close(st->reader, ctx);
    }
  }
  return ret;
}

/*static JSValue
readable_close(Readable* st, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  if(st->closed)
    return ret;

  if(readable_locked(st)) {
    promise_resolve(ctx, &st->reader->events[READER_CLOSED].funcs, JS_UNDEFINED);

    ret = reader_cancel(st->reader, ctx);
  }
  return ret;
 }*/

static JSValue
readable_cancel(Readable* st, JSValueConst reason, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  if(st->closed)
    return ret;

  /* static const BOOL expected = FALSE;

    if(!atomic_compare_exchange_weak(&st->closed, &expected, TRUE))
      JS_ThrowInternalError(ctx, "No locked ReadableStream associated");*/

  if(readable_locked(st)) {
    promise_resolve(ctx, &st->reader->events[READER_CLOSED].funcs, JS_UNDEFINED);

    ret = reader_cancel(st->reader, reason, ctx);
  }
  return ret;

  /*  ret = js_readable_callback(ctx, st, READABLE_CANCEL, 1, &reason);*/
}

static JSValue
readable_enqueue(Readable* st, JSValueConst chunk, JSContext* ctx) {
  InputBuffer input;
  int64_t ret;
  Reader* rd;
  // size_t old_size;

  if(readable_locked(st) && (rd = st->reader)) {
    JSValue result = js_iterator_result(ctx, chunk, FALSE);
    BOOL ok;
    ok = reader_passthrough(rd, result, ctx);

    JS_FreeValue(ctx, result);
    if(ok)
      return JS_UNDEFINED;
  }

  input = js_input_chars(ctx, chunk);
  // old_size = queue_size(&st->q);
  ret = queue_write(&st->q, input.data, input.size);
  // printf("old queue size: %zu new queue size: %zu\n", old_size, queue_size(&st->q));
  input_buffer_free(&input, ctx);
  return ret < 0 ? JS_ThrowInternalError(ctx, "enqueue() returned %" PRId64, ret) : JS_NewInt64(ctx, ret);
}

static int
readable_lock(Readable* st, Reader* rd) {
  Reader* expected = 0;
  return atomic_compare_exchange_weak(&st->reader, &expected, rd);
}

static int
readable_unlock(Readable* st, Reader* rd) {
  return atomic_compare_exchange_weak(&st->reader, &rd, 0);
}

static Reader*
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

static void
readable_free(Readable* st, JSRuntime* rt) {
  if(--st->ref_count == 0) {
    JS_FreeValueRT(rt, st->underlying_source);
    JS_FreeValueRT(rt, st->controller);

    for(size_t i = 0; i < countof(st->on); i++)
      JS_FreeValueRT(rt, st->on[i]);

    queue_clear(&st->q);
    js_free_rt(rt, st);
  }
}

enum {
  FUNC_PEEK,

};

JSValue
js_reader_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Reader* rd;
  Readable* st;

  if(argc < 1 || !(st = js_readable_data(argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a Readable");

  if(!(rd = reader_new(ctx, st)))
    return JS_EXCEPTION;

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
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, rd);

  return obj;

fail:
  js_free(ctx, rd);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
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

JSValue
js_reader_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  Reader* rd;

  if(!(rd = js_reader_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case READER_CANCEL: {
      reader_close(rd, ctx);
      ret = JS_DupValue(ctx, rd->events[READER_CANCELLED].value);
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

JSValue
js_reader_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Reader* rd;
  JSValue ret = JS_UNDEFINED;

  if(!(rd = js_reader_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case READER_CLOSED: {
      ret = JS_DupValue(ctx, rd->events[READER_CLOSED].value);
      break;
    }
  }
  return ret;
}

void
js_reader_finalizer(JSRuntime* rt, JSValue val) {
  Reader* rd;

  if((rd = JS_GetOpaque(val, js_reader_class_id))) {
    js_free_rt(rt, rd);
  }

  // JS_FreeValueRT(rt, val);
}

JSClassDef js_reader_class = {
    .class_name = "StreamReader",
    .finalizer = js_reader_finalizer,
};

const JSCFunctionListEntry js_reader_proto_funcs[] = {
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
  assert(event < countof(st->on));

  if(JS_IsFunction(ctx, st->on[event]))
    return JS_Call(ctx, st->on[event], st->underlying_source, argc, argv);

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

JSValue
js_readable_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Readable* st;

  if(!(st = readable_new(ctx)))
    return JS_EXCEPTION;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, readable_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_readable_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  if(argc >= 1 && !JS_IsNull(argv[0]) && JS_IsObject(argv[0])) {
    st->on[READABLE_START] = JS_GetPropertyStr(ctx, argv[0], "start");
    st->on[READABLE_PULL] = JS_GetPropertyStr(ctx, argv[0], "pull");
    st->on[READABLE_CANCEL] = JS_GetPropertyStr(ctx, argv[0], "cancel");
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

JSValue
js_readable_wrap(JSContext* ctx, Readable* st) {
  JSValue obj;
  obj = JS_NewObjectProtoClass(ctx, readable_proto, js_readable_class_id);
  ++st->ref_count;
  JS_SetOpaque(obj, st);
  return obj;
}

/*static JSValue
js_readable_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic)
{ Readable* st; JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_readable_class_id)))
    return JS_EXCEPTION;

  *pdone = queue_empty(&st->q);

  if(!*pdone)
    ret = readable_next(st, ctx);

  return ret;
}

JSValue js_readable_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}*/

enum {
  READABLE_ABORT = 0,
  READABLE_GET_READER,
};

JSValue
js_readable_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Readable* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_readable_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case READABLE_ABORT: {
      if(argc >= 1)
        readable_cancel(st, argv[0], ctx);
      else
        readable_close(st, ctx);

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

enum {
  STREAM_CLOSED,
  STREAM_LOCKED,
};

JSValue
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
      ret = JS_NewBool(ctx, !!readable_locked(st));
      break;
    }
  }
  return ret;
}

enum {
  READABLE_CLOSE = 0,
  READABLE_ENQUEUE,
  READABLE_ERROR,
};

JSValue
js_readable_controller(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Readable* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_readable_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case READABLE_CLOSE: {
      readable_close(st, ctx);
      break;
    }

    case READABLE_ENQUEUE: {
      ret = readable_enqueue(st, argv[0], ctx);
      break;
    }

    case READABLE_ERROR: {
      readable_cancel(st, argc >= 1 ? argv[0] : JS_UNDEFINED, ctx);
      break;
    }
  }

  return ret;
}

JSValue
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

void
js_readable_finalizer(JSRuntime* rt, JSValue val) {
  Readable* st;

  if((st = js_readable_data(val)))
    readable_free(st, rt);
}

JSClassDef js_readable_class = {
    .class_name = "ReadableStream",
    .finalizer = js_readable_finalizer,
};

const JSCFunctionListEntry js_readable_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("cancel", 0, js_readable_method, READABLE_ABORT),
    JS_CFUNC_MAGIC_DEF("getReader", 0, js_readable_method, READABLE_GET_READER),
    JS_CGETSET_MAGIC_FLAGS_DEF("closed", js_readable_get, 0, STREAM_CLOSED, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("locked", js_readable_get, 0, STREAM_LOCKED, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Readable", JS_PROP_CONFIGURABLE),
    //    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_readable_iterator),
};

const JSCFunctionListEntry js_readable_controller_funcs[] = {
    JS_CFUNC_MAGIC_DEF("close", 0, js_readable_controller, READABLE_CLOSE),
    JS_CFUNC_MAGIC_DEF("enqueue", 1, js_readable_controller, READABLE_ENQUEUE),
    JS_CFUNC_MAGIC_DEF("error", 1, js_readable_controller, READABLE_ERROR),
    JS_CGETSET_DEF("desiredSize", js_readable_desired, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ReadableStreamDefaultController", JS_PROP_CONFIGURABLE),
};

enum {
  WRITABLE_METHOD_CLOSE = 0,
  WRITABLE_METHOD_ABORT,
  WRITABLE_GET_WRITER,
};

static Writer*
writer_new(JSContext* ctx, Writable* st) {
  Writer* wr;

  if((wr = js_mallocz(ctx, sizeof(Writer)))) {
    atomic_store(&wr->stream, st);
    promise_init(ctx, &wr->events[WRITER_CLOSED]);
    promise_init(ctx, &wr->events[WRITER_READY]);

    JSValue ret = js_writable_callback(ctx, st, WRITABLE_START, 1, &st->controller);

    /*
      if(js_is_promise(ctx, ret))
        ret = promise_forward(ctx, ret, &wr->events[WRITER_READY]);
      else
        promise_resolve(ctx, &wr->events[WRITER_READY].funcs, JS_TRUE);*/

    JS_FreeValue(ctx, ret);
  }

  return wr;
}

static BOOL
writer_release_lock(Writer* wr, JSContext* ctx) {
  BOOL ret = FALSE;
  Writable* r;

  if((r = atomic_load(&wr->stream))) {
    if((ret = writable_unlock(r, wr))) {
      atomic_store(&wr->stream, (Writable*)0);
    }
  }

  return ret;
}

static JSValue
writer_write(Writer* wr, JSValueConst chunk, JSContext* ctx) {
  /* JSValue ret = JS_UNDEFINED;
ssize_t bytes;

 if((bytes = queue_write(&wr->q, block->base, block->size)) == block->size) {
   Chunk* chunk = queue_tail(&wr->q);

   chunk->opaque = promise_new(ctx, &ret);
 }*/
  if(wr->stream) {
    JSValueConst args[2] = {chunk, wr->stream->controller};
    return js_writable_callback(ctx, wr->stream, WRITABLE_WRITE, 2, args);
  }

  return JS_ThrowInternalError(ctx, "no WriteableStream");
}

static JSValue
writer_close(Writer* wr, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  if(!wr->stream)
    return JS_ThrowInternalError(ctx, "no WriteableStream");

  ret = js_writable_callback(ctx, wr->stream, WRITABLE_CLOSE, 0, 0);

  if(js_is_promise(ctx, ret)) {
    ret = promise_forward(ctx, ret, &wr->events[WRITER_CLOSED]);
  }
  return ret;
}

static JSValue
writer_abort(Writer* wr, JSValueConst reason, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  if(!wr->stream)
    return JS_ThrowInternalError(ctx, "no WriteableStream");

  ret = js_writable_callback(ctx, wr->stream, WRITABLE_ABORT, 1, &reason);

  if(js_is_promise(ctx, ret)) {
    ret = promise_forward(ctx, ret, &wr->events[WRITER_CLOSED]);
  }
  return ret;
}

static JSValue
writer_signal(Writer* wr, StreamEvent event, JSValueConst arg, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  assert(event <= EVENT_READ);
  assert(event >= EVENT_CLOSE);

  if(promise_resolve(ctx, &wr->events[event].funcs, arg))
    ret = JS_TRUE;

  return ret;
}

static Writable*
writable_new(JSContext* ctx) {
  Writable* st;

  if((st = js_mallocz(ctx, sizeof(Writable)))) {
    st->ref_count = 1;
    st->controller = JS_NULL;
    queue_init(&st->q);

    st->on[3] = st->on[2] = st->on[1] = st->on[0] = JS_NULL;
    st->underlying_sink = JS_NULL;
    st->controller = JS_NULL;
  }

  return st;
}

static Writable*
writable_dup(Writable* st) {
  ++st->ref_count;
  return st;
}

static JSValue
writable_abort(Writable* st, JSValueConst reason, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  static BOOL expected = FALSE;

  if(atomic_compare_exchange_weak(&st->closed, &expected, TRUE)) {
    st->reason = js_tostring(ctx, reason);
    if(writable_locked(st)) {
      promise_resolve(ctx, &st->writer->events[WRITER_CLOSED].funcs, JS_UNDEFINED);
      ret = writer_abort(st->writer, reason, ctx);
    }
  }

  return ret;
}

static JSValue
writable_close(Writable* st, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  static BOOL expected = FALSE;

  if(atomic_compare_exchange_weak(&st->closed, &expected, TRUE)) {
    if(writable_locked(st)) {
      promise_resolve(ctx, &st->writer->events[WRITER_CLOSED].funcs, JS_UNDEFINED);
      ret = writer_close(st->writer, ctx);
    }
  }
  return ret;
}

static int
writable_lock(Writable* st, Writer* wr) {
  Writer* expected = 0;
  return atomic_compare_exchange_weak(&st->writer, &expected, wr);
}

static int
writable_unlock(Writable* st, Writer* wr) {
  return atomic_compare_exchange_weak(&st->writer, &wr, 0);
}

static Writer*
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

static void
writable_free(Writable* st, JSRuntime* rt) {
  if(--st->ref_count == 0) {
    JS_FreeValueRT(rt, st->underlying_sink);
    JS_FreeValueRT(rt, st->controller);

    for(size_t i = 0; i < countof(st->on); i++)
      JS_FreeValueRT(rt, st->on[i]);

    queue_clear(&st->q);
    js_free_rt(rt, st);
  }
}

JSValue
js_writer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Writer* wr;
  Writable* st;

  if(argc < 1 || !(st = js_writable_data(argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a Writable");

  if(!(wr = writer_new(ctx, st)))
    return JS_EXCEPTION;

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

JSValue
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

JSValue
js_writer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  Writer* wr;

  if(!(wr = js_writer_data2(ctx, this_val)))
    return JS_EXCEPTION;

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

JSValue
js_writer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Writer* wr;
  JSValue ret = JS_UNDEFINED;

  if(!(wr = js_writer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITER_CLOSED: {
      ret = JS_DupValue(ctx, wr->events[WRITER_CLOSED].value);
      break;
    }

    case WRITER_READY: {
      ret = JS_DupValue(ctx, wr->events[WRITER_READY].value);
      break;
    }
  }
  return ret;
}

void
js_writer_finalizer(JSRuntime* rt, JSValue val) {
  Writer* wr;

  if((wr = JS_GetOpaque(val, js_writer_class_id))) {
    js_free_rt(rt, wr);
  }
}

JSClassDef js_writer_class = {
    .class_name = "StreamWriter",
    .finalizer = js_writer_finalizer,
};

const JSCFunctionListEntry js_writer_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("write", 0, js_writer_method, WRITER_WRITE),
    JS_CFUNC_MAGIC_DEF("abort", 0, js_writer_method, WRITER_ABORT),
    JS_CFUNC_MAGIC_DEF("close", 0, js_writer_method, WRITER_CLOSE),
    JS_CFUNC_MAGIC_DEF("releaseLock", 0, js_writer_method, WRITER_RELEASE_LOCK),
    JS_CGETSET_MAGIC_DEF("closed", js_writer_get, 0, WRITER_CLOSED),
    JS_CGETSET_MAGIC_DEF("ready", js_writer_get, 0, WRITER_READY),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StreamWriter", JS_PROP_CONFIGURABLE),
};

JSValue
js_writable_callback(JSContext* ctx, Writable* st, WritableEvent event, int argc, JSValueConst argv[]) {
  assert(event >= 0);
  assert(event < countof(st->on));

  if(JS_IsFunction(ctx, st->on[event]))
    return JS_Call(ctx, st->on[event], st->underlying_sink, argc, argv);

  return JS_UNDEFINED;
}

JSValue
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
    st->on[WRITABLE_START] = JS_GetPropertyStr(ctx, argv[0], "start");
    st->on[WRITABLE_WRITE] = JS_GetPropertyStr(ctx, argv[0], "write");
    st->on[WRITABLE_CLOSE] = JS_GetPropertyStr(ctx, argv[0], "close");
    st->on[WRITABLE_ABORT] = JS_GetPropertyStr(ctx, argv[0], "abort");
    st->underlying_sink = JS_DupValue(ctx, argv[0]);
    st->controller = JS_NewObjectProtoClass(ctx, writable_controller, js_writable_class_id);
    JS_SetOpaque(st->controller, writable_dup(st));
  }

  JS_SetOpaque(obj, st);

  return obj;
fail:
  js_free(ctx, st);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_writable_wrap(JSContext* ctx, Writable* st) {
  JSValue obj;
  obj = JS_NewObjectProtoClass(ctx, writable_proto, js_writable_class_id);
  ++st->ref_count;
  JS_SetOpaque(obj, st);
  return obj;
}

JSValue
js_writable_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

JSValue
js_writable_handler(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue handler, method;

  method = JS_NewCFunctionMagic(ctx, js_writable_method, "handler", 0, JS_CFUNC_generic_magic, magic);
  handler = js_function_bind_this(ctx, method, this_val);
  JS_FreeValue(ctx, method);
  return handler;
}

JSValue
js_writable_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Writable* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_writable_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITABLE_METHOD_ABORT: {
      ret = writable_abort(st, argv[0], ctx);
      break;
    }
      /* case WRITABLE_METHOD_CLOSE: {
         ret = writable_close(st,  ctx);
         break;
       }*/
    case WRITABLE_GET_WRITER: {
      Writer* wr;

      if((wr = writable_get_writer(st, 0, ctx)))
        ret = js_writer_wrap(ctx, wr);
      break;
    }
  }

  return ret;
}

enum {
  WRITABLE_CLOSED,
  WRITABLE_LOCKED,
};

JSValue
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

enum {
  WRITABLE_ERROR = 0,
};

JSValue
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

void
js_writable_finalizer(JSRuntime* rt, JSValue val) {
  Writable* st;

  if((st = js_writable_data(val)))
    writable_free(st, rt);
}

JSClassDef js_writable_class = {
    .class_name = "WritableStream",
    .finalizer = js_writable_finalizer,
};

const JSCFunctionListEntry js_writable_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("abort", 1, js_writable_method, WRITABLE_METHOD_ABORT),
    JS_CFUNC_MAGIC_DEF("close", 0, js_writable_method, WRITABLE_METHOD_CLOSE),
    JS_CFUNC_MAGIC_DEF("getWriter", 0, js_writable_method, WRITABLE_GET_WRITER),
    JS_CGETSET_MAGIC_FLAGS_DEF("locked", js_writable_get, 0, WRITABLE_LOCKED, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WritableStream", JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_writable_iterator),
};

const JSCFunctionListEntry js_writable_controller_funcs[] = {
    JS_CFUNC_MAGIC_DEF("error", 0, js_writable_controller, WRITABLE_ERROR),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WritableStreamDefaultController", JS_PROP_CONFIGURABLE),
};

static Transform*
transform_dup(Transform* st) {
  ++st->ref_count;
  return st;
}

static Transform*
transform_new(JSContext* ctx) {
  Transform* st;

  if((st = js_mallocz(ctx, sizeof(Transform)))) {
    st->ref_count = 1;
    st->readable = readable_new(ctx);
    st->writable = writable_new(ctx);

    st->controller = JS_NewObjectProtoClass(ctx, transform_controller, js_transform_class_id);
    JS_SetOpaque(st->controller, transform_dup(st));
  }

  return st;
}

static void
transform_terminate(Transform* st, JSContext* ctx) {
  readable_close(st->readable, ctx);
  writable_abort(st->writable, JS_UNDEFINED, ctx);
}

static void
transform_error(Transform* st, JSValueConst error, JSContext* ctx) {
  readable_cancel(st->readable, error, ctx);
  writable_abort(st->writable, error, ctx);
}

JSValue
js_transform_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Transform* st;

  if(!(st = transform_new(ctx)))
    return JS_EXCEPTION;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, transform_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_transform_class_id);
  if(JS_IsException(obj))
    goto fail;

  if(argc >= 1 && !JS_IsNull(argv[0]) && JS_IsObject(argv[0])) {
    st->on[TRANSFORM_START] = JS_GetPropertyStr(ctx, argv[0], "start");
    st->on[TRANSFORM_TRANSFORM] = JS_GetPropertyStr(ctx, argv[0], "transform");
    st->on[TRANSFORM_FLUSH] = JS_GetPropertyStr(ctx, argv[0], "flush");
    st->underlying_transform = JS_DupValue(ctx, argv[0]);

    st->writable->on[WRITABLE_START] = JS_DupValue(ctx, st->on[TRANSFORM_START]);
    st->writable->on[WRITABLE_WRITE] = JS_DupValue(ctx, st->on[TRANSFORM_TRANSFORM]);
    st->writable->on[WRITABLE_CLOSE] = JS_DupValue(ctx, st->on[TRANSFORM_FLUSH]);

    st->writable->controller = JS_DupValue(ctx, st->controller);
  }

  JS_SetOpaque(obj, st);

  return obj;
fail:
  js_free(ctx, st);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_transform_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Transform* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_transform_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case TRANSFORM_READABLE: {
      ret = js_readable_wrap(ctx, st->readable);
      break;
    }

    case TRANSFORM_WRITABLE: {
      ret = js_writable_wrap(ctx, st->writable);
      break;
    }
  }
  return ret;
}

enum {
  TRANSFORM_TERMINATE = 0,
  TRANSFORM_ENQUEUE,
  TRANSFORM_ERROR,
};

JSValue
js_transform_controller(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Transform* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_transform_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case TRANSFORM_ENQUEUE: {
      ret = readable_enqueue(st->readable, argv[0], ctx);
      break;
    }

    case TRANSFORM_ERROR: {
      JS_FreeValue(ctx, readable_cancel(st->readable, argc >= 1 ? argv[0] : JS_UNDEFINED, ctx));
      ret = writable_abort(st->writable, argc >= 1 ? argv[0] : JS_UNDEFINED, ctx);
      break;
    }

    case TRANSFORM_TERMINATE: {
      JS_FreeValue(ctx, readable_close(st->readable, ctx));
      ret = writable_close(st->writable, ctx);
      break;
    }
  }

  return ret;
}

JSValue
js_transform_desired(JSContext* ctx, JSValueConst this_val) {
  Transform* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_transform_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(readable_locked(st->readable)) {
    Reader* rd;

    if((rd = st->readable->reader)) {
      ret = JS_NewUint32(ctx, rd->desired_size);
    }
  }

  return ret;
}

void
js_transform_finalizer(JSRuntime* rt, JSValue val) {
  Transform* st;

  if((st = JS_GetOpaque(val, js_transform_class_id))) {
    if(--st->ref_count == 0) {
      writable_free(st->writable, rt);
      readable_free(st->readable, rt);

      JS_FreeValueRT(rt, st->underlying_transform);
      JS_FreeValueRT(rt, st->controller);

      for(size_t i = 0; i < countof(st->on); i++)
        JS_FreeValueRT(rt, st->on[i]);

      js_free_rt(rt, st);
    }
  }
}

JSClassDef js_transform_class = {
    .class_name = "TransformStream",
    .finalizer = js_transform_finalizer,
};

const JSCFunctionListEntry js_transform_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("readable", js_transform_get, 0, TRANSFORM_READABLE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("writable", js_transform_get, 0, TRANSFORM_WRITABLE, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TransformStream", JS_PROP_CONFIGURABLE),
};

const JSCFunctionListEntry js_transform_controller_funcs[] = {
    JS_CFUNC_MAGIC_DEF("terminate", 0, js_transform_controller, TRANSFORM_TERMINATE),
    JS_CFUNC_MAGIC_DEF("enqueue", 1, js_transform_controller, TRANSFORM_ENQUEUE),
    JS_CFUNC_MAGIC_DEF("error", 1, js_transform_controller, TRANSFORM_ERROR),
    JS_CGETSET_DEF("desiredSize", js_transform_desired, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TransformStreamDefaultController", JS_PROP_CONFIGURABLE),
};

int
js_stream_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_reader_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_reader_class_id, &js_reader_class);

  reader_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, reader_proto, js_reader_proto_funcs, countof(js_reader_proto_funcs));
  JS_SetClassProto(ctx, js_reader_class_id, reader_proto);

  reader_ctor = JS_NewCFunction2(ctx, js_reader_constructor, "StreamReader", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, reader_ctor, reader_proto);

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

  JS_NewClassID(&js_writer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_writer_class_id, &js_writer_class);

  writer_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, writer_proto, js_writer_proto_funcs, countof(js_writer_proto_funcs));
  JS_SetClassProto(ctx, js_writer_class_id, writer_proto);

  writer_ctor = JS_NewCFunction2(ctx, js_writer_constructor, "StreamWriter", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, writer_ctor, writer_proto);

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

  JS_NewClassID(&js_transform_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_transform_class_id, &js_transform_class);

  transform_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, transform_proto, js_transform_proto_funcs, countof(js_transform_proto_funcs));
  JS_SetClassProto(ctx, js_transform_class_id, transform_proto);

  transform_ctor = JS_NewCFunction2(ctx, js_transform_constructor, "TransformStream", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, transform_ctor, transform_proto);

  transform_controller = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, transform_controller, js_transform_controller_funcs, countof(js_transform_controller_funcs));
  JS_SetClassProto(ctx, js_transform_class_id, transform_controller);

  // JS_SetPropertyFunctionList(ctx, stream_ctor, js_stream_static_funcs, countof(js_stream_static_funcs));

  if(m) {
    JS_SetModuleExport(ctx, m, "StreamReader", reader_ctor);
    JS_SetModuleExport(ctx, m, "StreamWriter", writer_ctor);
    JS_SetModuleExport(ctx, m, "ReadableStream", readable_ctor);
    JS_SetModuleExport(ctx, m, "ReadableStreamDefaultController", readable_controller);
    JS_SetModuleExport(ctx, m, "WritableStream", writable_ctor);
    JS_SetModuleExport(ctx, m, "WritableStreamDefaultController", writable_controller);
    JS_SetModuleExport(ctx, m, "TransformStream", transform_ctor);
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
  m = JS_NewCModule(ctx, module_name, js_stream_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "StreamReader");
  JS_AddModuleExport(ctx, m, "StreamWriter");
  JS_AddModuleExport(ctx, m, "ReadableStream");
  JS_AddModuleExport(ctx, m, "ReadableStreamDefaultController");
  JS_AddModuleExport(ctx, m, "WritableStream");
  JS_AddModuleExport(ctx, m, "WritableStreamDefaultController");
  JS_AddModuleExport(ctx, m, "TransformStream");
  return m;
}

/**
 * @}
 */
