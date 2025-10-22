#include "quickjs-stream.h"
#include "buffer-utils.h"
#include "utils.h"
#include "debug.h"
#include <list.h>
#include <assert.h>

extern const uint32_t qjsc_stream_size;
extern const uint8_t qjsc_stream[];

/**
 * \defgroup quickjs-stream quickjs-stream: Buffered stream
 * @{
 */

typedef enum {
  READABLE_START = 0,
  READABLE_PULL,
  READABLE_CANCEL,
} ReadableCallback;

typedef enum {
  WRITABLE_START = 0,
  WRITABLE_WRITE,
  WRITABLE_CLOSE,
  WRITABLE_ABORT,
} WritableCallback;

typedef enum {
  TRANSFORM_START = 0,
  TRANSFORM_TRANSFORM,
  TRANSFORM_FLUSH,
} TransformCallback;

/*VISIBLE*/ JSClassID js_readable_class_id = 0, js_writable_class_id = 0, js_reader_class_id = 0,
                      js_writer_class_id = 0, js_transform_class_id = 0;
/*VISIBLE*/ JSValue readable_proto, readable_default_controller, readable_bytestream_controller, readable_ctor,
    writable_proto, writable_controller, writable_ctor, transform_proto, transform_controller, transform_ctor,
    default_reader_proto, default_reader_ctor, byob_reader_proto, byob_reader_ctor, byob_request_proto, writer_proto,
    writer_ctor;

static int reader_update(ReadableStreamReader*, JSContext*);
static BOOL reader_passthrough(ReadableStreamReader*, JSValueConst, JSContext*);
static int readable_unlock(ReadableStream*, ReadableStreamReader*);
static int writable_unlock(WritableStream*, WritableStreamWriter*);
static JSValue js_readable_callback(JSContext*, ReadableStream*, ReadableCallback, int, JSValueConst[]);
static JSValue js_writable_callback(JSContext*, WritableStream*, WritableCallback, int, JSValueConst[]);
static JSValue js_reader_wrap(JSContext* ctx, ReadableStreamReader* rd);
static JSValue js_byob_request_new(JSContext* ctx, JSValueConst this_val);

/* clang-format off */
static inline ReadableStreamReader* js_reader_data(JSValueConst v) { return JS_GetOpaque(v, js_reader_class_id); }
static inline ReadableStreamReader* js_reader_data2(JSContext* ctx, JSValueConst v) { return JS_GetOpaque2(ctx, v, js_reader_class_id); }
static inline WritableStreamWriter* js_writer_data(JSValueConst v) { return JS_GetOpaque(v, js_writer_class_id); }
static inline WritableStreamWriter* js_writer_data2(JSContext* ctx, JSValueConst v) { return JS_GetOpaque2(ctx, v, js_writer_class_id); }
static inline ReadableStream* js_readable_data(JSValueConst v) { return JS_GetOpaque(v, js_readable_class_id); }
static inline ReadableStream* js_readable_data2(JSContext* ctx, JSValueConst v) { return JS_GetOpaque2(ctx, v, js_readable_class_id); }
static inline WritableStream* js_writable_data(JSValueConst v) { return JS_GetOpaque(v, js_writable_class_id); }
static inline WritableStream* js_writable_data2(JSContext* ctx, JSValueConst v) { return JS_GetOpaque2(ctx, v, js_writable_class_id); }
static inline TransformStream* js_transform_data(JSValueConst v) { return JS_GetOpaque(v, js_transform_class_id); }
static inline TransformStream* js_transform_data2(JSContext* ctx, JSValueConst v) { return JS_GetOpaque2(ctx, v, js_transform_class_id); }
/* clang-format on */

static JSValue
js_iterator_get_value(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  // BOOL done = js_get_propertystr_bool(ctx, argv[0], "done");
  return JS_GetPropertyStr(ctx, argv[0], "value");
}

static JSValue
js_iterator_promise_value(JSContext* ctx, JSValueConst promise) {
  JSValue fn = JS_NewCFunction(ctx, &js_iterator_get_value, "getIteratorValue", 1);
  JSValue ret = promise_then(ctx, promise, fn);

  JS_FreeValue(ctx, fn);
  return ret;
}

static JSValue
js_to_arraybuffer(JSContext* ctx, JSValueConst chunk) {
  if(JS_IsString(chunk)) {
    InputBuffer input = js_input_chars(ctx, chunk);
    JSValue buf = JS_NewArrayBufferCopy(ctx, input.data, input.size);
    input_buffer_free(&input, ctx);

    return buf;
  }

  return JS_DupValue(ctx, chunk);
}

static void
chunk_unref(JSRuntime* rt, void* opaque, void* ptr) {
  chunk_free(opaque);
}

/**
 * @brief      { function_description }
 *
 * @param      ch    { parameter_description }
 * @param      ctx   The JSContext
 *
 * @return     { return value }
 */
static JSValue
chunk_arraybuffer(Chunk* ch, JSContext* ctx) {
  chunk_dup(ch);

  return JS_NewArrayBuffer(ctx, ch->data + ch->pos, ch->size - ch->pos, chunk_unref, ch, FALSE);
}

/**
 * @brief      { function_description }
 *
 * @param      ch    { parameter_description }
 * @param      ctx   The JSContext
 *
 * @return     { return value }
 */
static JSValue
chunk_string(Chunk* ch, JSContext* ctx) {
  return JS_NewStringLen(ctx, (const char*)ch->data + ch->pos, ch->size - ch->pos);
}

/**
 * @brief      Creates a new \ref ReadRequest
 *
 * @param      rd    ReadableStreamReader struct
 * @param      ctx   The JSContext
 *
 * @return     { description_of_the_return_value }
 */
static ReadRequest*
read_new(ReadableStreamReader* rd, JSContext* ctx) {
  static int read_seq = 0;
  ReadRequest* op;

  if((op = js_mallocz(ctx, sizeof(struct read_next)))) {
    op->seq = ++read_seq;
    list_add((struct list_head*)op, &rd->list);

    promise_init(ctx, &op->promise);
  }

  return op;
}

/**
 * @brief      Reads the next value
 *
 * @param      rd    ReadableStreamReader struct
 * @param      ctx   The JSContext
 *
 * @return     A Promise that resolves to a value when reading done
 */
static JSValue
read_next(ReadableStreamReader* rd, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  ReadRequest *el, *op = 0;

  list_for_each_prev(el, &rd->reads) {
    if(!JS_IsUndefined(el->promise.value)) {
      op = el;
      break;
    }
  }

  if(!op)
    if(!(op = read_new(rd, ctx)))
      ret = JS_EXCEPTION;

  if(op) {
#ifdef DEBUG_OUTPUT_
    printf("%s(): (%i/%zu)\n", __func__, op->seq, list_size(&rd->list));
#endif

    ret = op->promise.value;
    op->promise.value = JS_UNDEFINED;
  }

  return ret;
}

/**
 * @brief      Indicates whether reading is done
 *
 * @param      op    The operation
 *
 * @return     TRUE if done, FALSE if not
 */
static BOOL
read_done(ReadRequest* op) {
  return JS_IsUndefined(op->promise.value) && promise_done(&op->promise.funcs);
}

/**
 * @brief      Frees a \ref ReadRequest
 *
 * @param      op    The operation
 * @param      rt    The JSRuntime
 */
static void
read_free_rt(ReadRequest* op, JSRuntime* rt) {
  promise_free(rt, &op->promise);

  list_del(&op->link);
}

/**
 * @brief      Creates a new \ref ReadableStreamReader
 *
 * @param      ctx   The JSContext
 * @param      st    A readable stream
 *
 * @return     A ReadableStreamReader struct or NULL on error
 */
static ReadableStreamReader*
reader_new(JSContext* ctx, ReadableStream* st) {
  ReadableStreamReader* rd;

  if((rd = js_mallocz(ctx, sizeof(ReadableStreamReader)))) {
    atomic_store(&rd->stream, st);

    promise_init(ctx, &rd->events.closed);
    promise_zero(&rd->events.cancelled);

    init_list_head(&rd->list);

    rd->ref_count = 1;

    JSValue ret = js_readable_callback(ctx, rd->stream, READABLE_START, 1, &rd->stream->controller);
    JS_FreeValue(ctx, ret);
  }

  return rd;
}

/**
 * @brief      Duplicates a \ref ReadableStreamReader
 *
 * @param      rd    ReadableStreamReader
 *
 * @return     The same reader (with incremented reference count)
 */
static ReadableStreamReader*
reader_dup(ReadableStreamReader* rd) {
  ++rd->ref_count;
  return rd;
}

/**
 * @brief      Releases the \ref ReadableStreamReader from its \ref ReadableStream stream
 *
 * @param      rd    ReadableStreamReader struct
 * @param      ctx   The JSContext
 *
 * @return     TRUE on success, FALSE if failed
 */
static BOOL
reader_release_lock(ReadableStreamReader* rd, JSContext* ctx) {
  BOOL ret = FALSE;
  ReadableStream* r;

  if((r = atomic_load(&rd->stream)))
    if((ret = readable_unlock(r, rd)))
      atomic_store(&rd->stream, (ReadableStream*)0);

  return ret;
}

/**
 * @brief      Clears all queued reads on the \ref ReadableStreamReader
 *
 * @param      rd    ReadableStreamReader struct
 * @param      rt    The JSRuntime
 *
 * @return     How many reads have been cleared
 */
static int
reader_clear(ReadableStreamReader* rd, JSRuntime* rt) {
  int ret = 0;
  ReadRequest *el, *next;

  list_for_each_prev_safe(el, next, &rd->reads) {
    JS_FreeValueRT(rt, el->promise.funcs.resolve);
    JS_FreeValueRT(rt, el->promise.funcs.reject);
    JS_FreeValueRT(rt, el->promise.value);

    read_free_rt(el, rt);

    ++ret;
  }

  return ret;
}

/**
 * @brief      Frees reader
 *
 * @param      rd    ReadableStreamReader struct
 * @param      rt    The JSRuntime
 */
static void
reader_free(ReadableStreamReader* rd, JSRuntime* rt) {
  if(--rd->ref_count == 0) {
    reader_clear(rd, rt);
    js_free_rt(rt, rd);
  }
}

static JSValue
js_reader_close_forward(
    JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue func_data[]) {
  ReadableStreamReader* rd;

  if(!(rd = js_reader_data2(ctx, func_data[0])))
    return JS_EXCEPTION;

  promise_resolve(ctx, &rd->events.closed.funcs, argv[0]);

  return JS_UNDEFINED;
}

/**
 * @brief      Closes a \ref ReadableStreamReader
 *
 * @param      rd    ReadableStreamReader struct
 * @param      ctx   The JSContext
 *
 * @return     A Promise that resolves when the closing is done
 */
static JSValue
reader_close(ReadableStreamReader* rd, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  if(!rd->stream)
    return JS_ThrowInternalError(ctx, "no ReadableStream");

  ret = js_readable_callback(ctx, rd->stream, READABLE_CANCEL, 0, 0);

#ifdef DEBUG_OUTPUT_
  printf("%s(1): promise=%i\n", __func__, js_is_promise(ctx, ret));
#endif
  rd->stream->closed = TRUE;

  reader_update(rd, ctx);

  if(js_is_promise(ctx, ret)) {
    JSValue readerObj = js_reader_wrap(ctx, rd);
    JSValue readerCloseForward = JS_NewCFunctionData(ctx, js_reader_close_forward, 1, 0, 1, &readerObj);
    JS_FreeValue(ctx, readerObj);

    JSValue tmp = promise_then(ctx, ret, readerCloseForward);

    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, readerCloseForward);
    ret = tmp;

    //    ret = promise_forward(ctx, ret, &rd->events.closed);
  }

#ifdef DEBUG_OUTPUT_
  printf("%s(2): promise=%i\n", __func__, js_is_promise(ctx, ret));
#endif

  return ret;
}

/**
 * @brief      Cancels a \ref ReadableStreamReader
 *
 * @param      rd      ReadableStreamReader struct
 * @param[in]  reason  The reason
 * @param      ctx     The JSContext
 *
 * @return     { return value }
 */
static JSValue
reader_cancel(ReadableStreamReader* rd, JSValueConst reason, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  if(!rd->stream)
    return JS_ThrowInternalError(ctx, "no WriteableStream");

  ret = js_readable_callback(ctx, rd->stream, READABLE_CANCEL, 1, &reason);

  if(js_is_promise(ctx, ret))
    ret = promise_forward(ctx, ret, &rd->events.cancelled);

  return ret;
}

/**
 * @brief      Reads from a \ref ReadableStreamReader
 *
 * @param      rd    ReadableStreamReader struct
 * @param      ctx   The JSContext
 *
 * @return     A Promise that resolves to data as soon as the read has completed
 */
static JSValue
reader_read(ReadableStreamReader* rd, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  ReadableStream* st;

  ret = read_next(rd, ctx);

  if(JS_IsException(ret))
    return ret;

  if((st = rd->stream)) {
    if(queue_empty(&st->q)) {

      if(st->autoallocatechunksize) {
        JSValue byob_request = js_byob_request_new(ctx, st->controller);

        JS_SetPropertyStr(ctx, st->controller, "byobRequest", byob_request);
      }

      JSValue tmp = js_readable_callback(ctx, st, READABLE_PULL, 1, &st->controller);

      if(JS_IsException(tmp)) {
        JS_FreeValue(ctx, ret);
        return JS_EXCEPTION;
      }

      JS_FreeValue(ctx, tmp);
    }
  }

  reader_update(rd, ctx);

#ifdef DEBUG_OUTPUT_
  printf("%s(): (2) [%zu] closed=%i\n", __func__, list_size(&rd->list), rd->stream->closed);
#endif

#ifdef DEBUG_OUTPUT_
  printf("%s():ReadRequest q2[%zu]\n", __func__, queue_size(&st->q));
#endif

  return ret;
  // return js_iterator_promise_value(ctx, ret);
}

/**
 * @brief      Cleans all reads from a \ref ReadableStreamReader
 *
 * @param      rd    ReadableStreamReader struct
 * @param      ctx   The JSContext
 *
 * @return     How many reads that have been cleaned
 */
static size_t
reader_clean(ReadableStreamReader* rd, JSContext* ctx) {
  ReadRequest *el, *next;
  size_t ret = 0;

  list_for_each_prev_safe(el, next, (ReadRequest*)&rd->reads) {
    if(read_done(el)) {
#ifdef DEBUG_OUTPUT_
      printf("%s(): delete[%i]\n", __func__, el->seq);
#endif

      list_del(&el->link);
      js_free(ctx, el);
      ++ret;

      continue;
    }

    break;
  }

  return ret;
}

/**
 * @brief      Updates a \ref ReadableStreamReader state
 *
 * @param      rd    ReadableStreamReader struct
 * @param      ctx   The JSContext
 *
 * @return     How many chunks that have been processed
 */
static int
reader_update(ReadableStreamReader* rd, JSContext* ctx) {
  JSValue result;
  Chunk* ch;
  ReadableStream* st = rd->stream;
  int ret = 0;

  reader_clean(rd, ctx);

#ifdef DEBUG_OUTPUT_
  printf("%s(): [%zu] closed=%d queue.size=%zu\n",
         __func__,
         list_size(&rd->list),
         readable_closed(st),
         queue_size(&st->q));
#endif

  if(readable_closed(st)) {
    promise_resolve(ctx, &rd->events.closed.funcs, JS_UNDEFINED);

    // reader_clear(rd, ctx);

    result = js_iterator_result(ctx, JS_UNDEFINED, TRUE);

    if(reader_passthrough(rd, result, ctx))
      ++ret;
  } else {
    while(!list_empty(&rd->list) && (ch = queue_next(&st->q))) {
      JSValue chunk, value;

#ifdef DEBUG_OUTPUT_
      printf("%s(2): Chunk ptr=%p, size=%zu, pos=%zu\n", __func__, ch->data, ch->size, ch->pos);
#endif

      chunk = chunk_arraybuffer(ch, ctx);
      value = js_iterator_result(ctx, chunk, FALSE);
      JS_FreeValue(ctx, chunk);

      if(!reader_passthrough(rd, value, ctx))
        break;

      ++ret;

      JS_FreeValue(ctx, value);
    }
  }

#ifdef DEBUG_OUTPUT_
  printf("%s(3): closed=%d queue.size=%zu result = %d\n", __func__, readable_closed(st), queue_size(&st->q), ret);
#endif

  return ret;
}

/**
 * @brief      Passes through a chunk/result to an active \ref ReadableStreamReader
 *
 * @param      rd      The ReadableStreamReader
 * @param[in]  result  JS iterator object
 * @param      ctx     The JSContext
 *
 * @return     TRUE if succeeded, FALSE if not
 */
static BOOL
reader_passthrough(ReadableStreamReader* rd, JSValueConst result, JSContext* ctx) {
  ReadRequest *op = 0, *el, *next;
  BOOL ret = FALSE;

  list_for_each_prev_safe(el, next, &rd->reads) {
#ifdef DEBUG_OUTPUT_
    printf("%s(1): el[%i]\n", __func__, el->seq);
#endif

    if(promise_pending(&el->promise.funcs)) {
      op = el;
      break;
    }
  }

#ifdef DEBUG_OUTPUT_
  printf("%s(2): result=%s\n", __func__, JS_ToCString(ctx, result));
#endif

  if(op) {
    ret = promise_resolve(ctx, &op->promise.funcs, result);
    reader_clean(rd, ctx);
  }

  return ret;
}

/**
 * @brief      Creates a new \ref ReadableStream stream
 *
 * @param      ctx   The JSContext
 *
 * @return     A new ReadableStream stream
 */
static ReadableStream*
readable_new(JSContext* ctx) {
  ReadableStream* st;

  if((st = js_mallocz(ctx, sizeof(ReadableStream)))) {
    st->ref_count = 1;
    st->controller = JS_NULL;

    queue_init(&st->q);
  }

  return st;
}

/**
 * @brief      Duplicates a \ref ReadableStream stream
 *
 * @param      st    A readable stream
 *
 * @return     The same readable stream (with incremented reference count)
 */
static ReadableStream*
readable_dup(ReadableStream* st) {
  ++st->ref_count;
  return st;
}

/**
 * @brief      Closes the \ref ReadableStream stream
 *
 * @param      st    A readable stream
 * @param      ctx   The JSContext
 *
 * @return     JS_UNDEFINED
 */
static JSValue
readable_close(ReadableStream* st, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  static BOOL expected = FALSE;

#ifdef DEBUG_OUTPUT_
  printf("%s(1): expected=%i, closed=%i\n", __func__, st->closed, expected);
#endif

  if(atomic_compare_exchange_weak(&st->closed, &expected, TRUE)) {
    if(readable_locked(st)) {
      promise_resolve(ctx, &st->reader->events.closed.funcs, JS_UNDEFINED);
      reader_close(st->reader, ctx);
    }
  }

  return ret;
}

/**
 * @brief      Cancels the \ref ReadableStream stream
 *
 * @param      st      A readable stream
 * @param[in]  reason  The reason
 * @param      ctx     The JSContext
 *
 * @return     A Promise which is resolved when the cancellation has completed
 */
static JSValue
readable_cancel(ReadableStream* st, JSValueConst reason, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  if(st->closed)
    return ret;

  /* static const BOOL expected = FALSE;

    if(!atomic_compare_exchange_weak(&st->closed, &expected, TRUE))
      JS_ThrowInternalError(ctx, "No locked ReadableStream associated");*/

  if(readable_locked(st)) {
    promise_resolve(ctx, &st->reader->events.closed.funcs, JS_UNDEFINED);

    ret = reader_cancel(st->reader, reason, ctx);
  }

  return ret;
}

/**
 * @brief      Enqueues data on the \ref ReadableStream stream
 *
 * @param      st     A readable stream
 * @param[in]  chunk  The chunk
 * @param      ctx    The JSContext
 *
 * @return     JSValue: Number of bytes written
 */
static JSValue
readable_enqueue(ReadableStream* st, JSValueConst chunk, BOOL binary, JSContext* ctx) {
  ReadableStreamReader* rd;
  JSValue ret = JS_UNDEFINED;
  InputBuffer input = js_input_chars(ctx, chunk);
  BOOL ok = FALSE;

  if(readable_locked(st) && (rd = st->reader)) {
    JSValue buf = (!binary || js_is_arraybuffer(ctx, chunk)) ? JS_DupValue(ctx, chunk)
                                                             : JS_NewArrayBufferCopy(ctx, input.data, input.size);

    JSValue result = js_iterator_result(ctx, buf, FALSE);
    JS_FreeValue(ctx, buf);

    if((ok = reader_passthrough(rd, result, ctx)))
      ret = JS_NewInt64(ctx, input.size);

    JS_FreeValue(ctx, result);
  }

  if(!ok) {
    int64_t r = queue_write(&st->q, input.data, input.size);

    ret = r < 0 ? JS_ThrowInternalError(ctx, "enqueue() returned %lu", (unsigned long)r) : JS_NewInt64(ctx, r);
  }

  input_buffer_free(&input, ctx);
  return ret;
}

/**
 * @brief      Locks the \ref ReadableStream stream
 *
 * @param      st    A readable stream
 * @param      rd    ReadableStreamReader struct
 *
 * @return     TRUE when locked, FALSE when failed
 */
static BOOL
readable_lock(ReadableStream* st, ReadableStreamReader* rd) {
  ReadableStreamReader* expected = 0;

  return atomic_compare_exchange_weak(&st->reader, &expected, rd);
}

/**
 * @brief      Unlocks the \ref ReadableStream stream
 *
 * @param      st    A readable stream
 * @param      rd    ReadableStreamReader struct
 *
 * @return     TRUE when unlocked, FALSE when failed
 */
static BOOL
readable_unlock(ReadableStream* st, ReadableStreamReader* rd) {
  return atomic_compare_exchange_weak(&st->reader, &rd, 0);
}

/**
 * @brief      Gets the \ref ReadableStreamReader of the \ref ReadableStream stream
 *
 * @param      st    A readable stream
 * @param      ctx   The JSContext
 *
 * @return     { description_of_the_return_value }
 */
static ReadableStreamReader*
readable_get_reader(ReadableStream* st, JSContext* ctx) {
  ReadableStreamReader* rd;

  if(!(rd = reader_new(ctx, st)))
    return 0;

  if(!readable_lock(st, rd)) {
    js_free(ctx, rd);
    rd = 0;
  }

  return rd;
}

/**
 * @brief      Frees the \ref ReadableStream stream
 *
 * @param      st    A readable stream
 * @param      rt    The JSRuntime
 */
static void
readable_free(ReadableStream* st, JSRuntime* rt) {
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

/**
 * @brief      Construct a JS ReadableStreamReader object
 *
 * @param      ctx         The JSContext
 * @param[in]  new_target  The constructor function
 * @param[in]  argc        Number of arguments
 * @param      argv        The arguments array
 *
 * @return     JS ReadableStreamReader object
 */
static JSValue
js_reader_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  ReadableStreamReader* rd;
  ReadableStream* st;

  if(argc < 1 || !(st = js_readable_data(argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a ReadableStream");

  if(!(rd = reader_new(ctx, st)))
    return JS_EXCEPTION;

  if(!readable_unlock(st, rd)) {
    JS_ThrowInternalError(ctx, "unable to lock ReadableStream");
    goto fail;
  }

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

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

/**
 * @brief      Wraps a \ref ReadableStreamReader struct in a JS ReadableStreamReader object
 *
 * @param      ctx   The JSContext
 * @param      rd    ReadableStreamReader struct
 *
 * @return     JS ReadableStreamReader object
 */
static JSValue
js_reader_wrap(JSContext* ctx, ReadableStreamReader* rd) {
  JSValue obj = JS_NewObjectProtoClass(ctx, default_reader_proto, js_reader_class_id);

  reader_dup(rd);
  JS_SetOpaque(obj, rd);
  return obj;
}

enum {
  READER_METHOD_CANCEL,
  READER_METHOD_READ,
  READER_RELEASE_LOCK,
};

/**
 * @brief      JS ReadableStreamReader object method function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the method
 */
static JSValue
js_reader_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  ReadableStreamReader* rd;

  if(!(rd = js_reader_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case READER_METHOD_CANCEL: {
      reader_close(rd, ctx);
      ret = JS_DupValue(ctx, rd->events.cancelled.value);
      break;
    }

    case READER_METHOD_READ: {
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

enum {
  READER_PROP_CLOSED,
};

/**
 * @brief      JS ReadableStreamReader object getter function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  magic     Magic number
 *
 * @return     { return value }
 */
static JSValue
js_reader_get(JSContext* ctx, JSValueConst this_val, int magic) {
  ReadableStreamReader* rd;
  JSValue ret = JS_UNDEFINED;

  if(!(rd = js_reader_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case READER_PROP_CLOSED: {
      // ret = JS_NewBool(ctx, promise_done(&rd->events.closed.funcs));
      ret = JS_DupValue(ctx, rd->events.closed.value);
      break;
    }
  }

  return ret;
}

/**
 * @brief      JS ReadableStreamReader object finalizer
 *
 * @param      rt    The JSRuntime
 * @param[in]  val   The value
 */
static void
js_reader_finalizer(JSRuntime* rt, JSValue val) {
  ReadableStreamReader* rd;

  if((rd = JS_GetOpaque(val, js_reader_class_id)))
    reader_free(rd, rt);
}

JSClassDef js_default_reader_class = {
    .class_name = "ReadableStreamDefaultReader",
    .finalizer = js_reader_finalizer,
};

const JSCFunctionListEntry js_default_reader_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("read", 0, js_reader_method, READER_METHOD_READ),
    JS_CFUNC_MAGIC_DEF("cancel", 0, js_reader_method, READER_METHOD_CANCEL),
    JS_CFUNC_MAGIC_DEF("releaseLock", 0, js_reader_method, READER_RELEASE_LOCK),
    JS_CGETSET_MAGIC_DEF("closed", js_reader_get, 0, READER_PROP_CLOSED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ReadableStreamDefaultReader", JS_PROP_CONFIGURABLE),
};

JSClassDef js_byob_reader_class = {
    .class_name = "ReadableStreamBYOBReader",
    .finalizer = js_reader_finalizer,
};

const JSCFunctionListEntry js_byob_reader_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("read", 0, js_reader_method, READER_METHOD_READ),
    JS_CFUNC_MAGIC_DEF("cancel", 0, js_reader_method, READER_METHOD_CANCEL),
    JS_CFUNC_MAGIC_DEF("releaseLock", 0, js_reader_method, READER_RELEASE_LOCK),
    JS_CGETSET_MAGIC_DEF("closed", js_reader_get, 0, READER_PROP_CLOSED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ReadableStreamBYOBReader", JS_PROP_CONFIGURABLE),
};

/**
 * @brief      { function_description }
 *
 * @param      ctx    The JSContext
 * @param      st     A readable stream
 * @param[in]  event  The event
 * @param[in]  argc   Number of arguments
 * @param      argv   The arguments array
 *
 * @return     { return value }
 */
static JSValue
js_readable_callback(JSContext* ctx, ReadableStream* st, ReadableCallback cb, int argc, JSValueConst argv[]) {
  assert(cb >= 0);
  assert(cb < countof(st->on));

  if(JS_IsFunction(ctx, st->on[cb]))
    return JS_Call(ctx, st->on[cb], st->underlying_source, argc, argv);

  return JS_UNDEFINED;
}

/**
 * @brief      Constructs a JS ReadableStream object
 *
 * @param      ctx         The JSContext
 * @param[in]  new_target  The constructor function
 * @param[in]  argc        Number of arguments
 * @param      argv        The arguments array
 *
 * @return     JS ReadableStream object
 */
static JSValue
js_readable_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  ReadableStream* st = 0;
  BOOL bytestream = FALSE;

  if(!(st = readable_new(ctx)))
    return JS_EXCEPTION;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_readable_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  if(argc >= 1 && !JS_IsNull(argv[0]) && JS_IsObject(argv[0])) {
    char* typestr;

    st->on[READABLE_START] = JS_GetPropertyStr(ctx, argv[0], "start");
    st->on[READABLE_PULL] = JS_GetPropertyStr(ctx, argv[0], "pull");
    st->on[READABLE_CANCEL] = JS_GetPropertyStr(ctx, argv[0], "cancel");
    st->underlying_source = JS_DupValue(ctx, argv[0]);

    if((typestr = js_get_propertystr_string(ctx, argv[0], "type"))) {
      bytestream = !strcmp(typestr, "bytes");
      js_free(ctx, typestr);
    }

    st->controller = JS_NewObjectProtoClass(ctx,
                                            bytestream ? readable_bytestream_controller : readable_default_controller,
                                            js_readable_class_id);

    JS_SetOpaque(st->controller, readable_dup(st));

    if(bytestream) {
      st->autoallocatechunksize = js_get_propertystr_uint64(ctx, argv[0], "autoAllocateChunkSize");

      /* XXX: right? */
      JS_SetPropertyStr(ctx, st->controller, "desiredSize", JS_NewInt64(ctx, st->autoallocatechunksize));
    }
  }

  JS_SetOpaque(obj, st);
  return obj;

fail:
  if(st)
    js_free(ctx, st);

  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

/**
 * @brief      Wraps a ReadableStream struct in a JS ReadableStream object
 *
 * @param      ctx   The JSContext
 * @param      st    A readable stream
 *
 * @return     JS ReadableStream object
 */
static JSValue
js_readable_wrap(JSContext* ctx, ReadableStream* st) {
  JSValue obj = JS_NewObjectProtoClass(ctx, readable_proto, js_readable_class_id);

  readable_dup(st);
  JS_SetOpaque(obj, st);
  return obj;
}

enum {
  READABLE_METHOD_ABORT = 0,
  READABLE_METHOD_GET_READER,
};

/**
 * @brief      JS ReadableStream object method function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the method
 */
static JSValue
js_readable_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  ReadableStream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_readable_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case READABLE_METHOD_ABORT: {
      if(argc >= 1)
        readable_cancel(st, argv[0], ctx);
      else
        readable_close(st, ctx);

      break;
    }

    case READABLE_METHOD_GET_READER: {
      ReadableStreamReader* rd;

      if((rd = readable_get_reader(st, ctx)))
        ret = js_reader_wrap(ctx, rd);
      else
        ret = JS_ThrowTypeError(ctx,
                                "Failed to execute 'getReader' on 'ReadableStream': ReadableStreamDefaultReader "
                                "constructor can only accept readable streams that are not yet locked to a reader");

      break;
    }
  }

  return ret;
}

enum {
  READABLE_PROP_CLOSED = 0,
  READABLE_PROP_LOCKED,
};

/**
 * @brief      JS ReadableStream object getter function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the getter
 */
static JSValue
js_readable_get(JSContext* ctx, JSValueConst this_val, int magic) {
  ReadableStream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_readable_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case READABLE_PROP_CLOSED: {
      ret = JS_NewBool(ctx, readable_closed(st));
      break;
    }

    case READABLE_PROP_LOCKED: {
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

/**
 * @brief      JS ReadableStream controller functions
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the controller function
 */
static JSValue
js_readable_controller(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  ReadableStream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_readable_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case READABLE_CLOSE: {
      readable_close(st, ctx);
      break;
    }

    case READABLE_ENQUEUE: {
      BOOL binary = FALSE;

      if(argc > 1)
        binary = JS_ToBool(ctx, argv[1]);

      ret = readable_enqueue(st, argv[0], binary, ctx);
      break;
    }

    case READABLE_ERROR: {
      readable_cancel(st, argc >= 1 ? argv[0] : JS_UNDEFINED, ctx);
      break;
    }
  }

  return ret;
}

enum {
  BYOB_REQUEST_METHOD_RESPOND = 0,
  BYOB_REQUEST_METHOD_RESPONDWITHNEWVIEW,
};

static JSValue
js_byob_request_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  ReadableStream* st;
  ReadableStreamReader* rd;
  JSValue ret = JS_UNDEFINED;
  BOOL success = FALSE;

  if(!(st = js_readable_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(rd = readable_locked(st)))
    return JS_ThrowInternalError(ctx, "ReadableStreamBYOBRequest: ReadableStream is not locked");

  switch(magic) {
    case BYOB_REQUEST_METHOD_RESPOND: {
      JSValue view = JS_GetPropertyStr(ctx, this_val, "view");
      uint64_t length = js_get_propertystr_uint64(ctx, view, "length");
      int64_t bytes = -1;
      JSValue newa = JS_UNDEFINED;

      JS_ToInt64Ext(ctx, &bytes, argv[0]);

      if(bytes >= 0 && (size_t)bytes > length) {
        ret = JS_ThrowRangeError(ctx,
                                 "Supplied bytesWritten value (%ld) is bigger than view length (%lu).",
                                 (long)bytes,
                                 (unsigned long)length);
      } else if(bytes >= 0 && (size_t)bytes == length) {
        newa = JS_DupValue(ctx, view);
      } else {
        uint64_t offset = js_get_propertystr_uint64(ctx, view, "byteOffset");
        JSValue buf = JS_GetPropertyStr(ctx, view, "buffer");

        newa = js_typedarray_new3(ctx, 8, FALSE, FALSE, buf, offset, MIN_NUM(bytes, (int64_t)length));
        JS_FreeValue(ctx, buf);
      }

      JS_FreeValue(ctx, view);

      if(!JS_IsUndefined(newa)) {
        success = reader_passthrough(rd, newa, ctx);

        JS_FreeValue(ctx, newa);
      }

      break;
    }

    case BYOB_REQUEST_METHOD_RESPONDWITHNEWVIEW: {

      /* XXX: Safety checks:
       *
       * - same underlying ArrayBuffer
       * - same byteOffset
       * - smaller or equal length
       */
      success = reader_passthrough(rd, argv[0], ctx);
      break;
    }
  }

  if(!success) {
    ret = JS_ThrowInternalError(ctx, "Passing through BYOB request failed because no pending read");
  } else {
    JSAtom va = JS_NewAtom(ctx, "view");
    JS_DeleteProperty(ctx, this_val, va, 0);
    JS_FreeAtom(ctx, va);
  }

  return ret;
}

/**
 * @brief      Returns the desired size of a JS ReadableStream object
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 *
 * @return     JS Number
 */
static JSValue
js_readable_desired(JSContext* ctx, JSValueConst this_val) {
  ReadableStream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_readable_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(readable_locked(st)) {
    ReadableStreamReader* rd;

    if((rd = st->reader))
      ret = JS_NewUint32(ctx, rd->desired_size);
  }

  return ret;
}

/**
 * @brief
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 *
 * @return     JS Number
 */
static JSValue
js_byob_request_new(JSContext* ctx, JSValueConst controller) {
  ReadableStream* st;
  JSValue byob_request = JS_UNDEFINED;

  if(!(st = js_readable_data2(ctx, controller)))
    return JS_EXCEPTION;

  byob_request = JS_NewObjectProtoClass(ctx, byob_request_proto, js_readable_class_id);
  JS_SetOpaque(byob_request, readable_dup(st));

  JSValue size = JS_NewInt64(ctx, st->autoallocatechunksize);
  JSValue view = js_typedarray_new(ctx, 8, FALSE, FALSE, size);
  JS_FreeValue(ctx, size);

  JS_DefinePropertyValueStr(ctx, byob_request, "view", view, JS_PROP_CONFIGURABLE);
  /* XXX: Todo */

  return byob_request;
}

static JSValue
js_readable_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  ReadableStream* st;

  if(!(st = js_readable_data2(ctx, this_val)))
    return JS_EXCEPTION;

  return JS_UNDEFINED;
}

/**
 * @brief      JS ReadableStream object finalizer
 *
 * @param      rt    The JSRuntime
 * @param[in]  val   The value
 */
static void
js_readable_finalizer(JSRuntime* rt, JSValue val) {
  ReadableStream* st;

  if((st = js_readable_data(val))) {
    readable_free(st, rt);
    JS_SetOpaque(val, 0);
  }
}

JSClassDef js_readable_class = {
    .class_name = "ReadableStream",
    .finalizer = js_readable_finalizer,
};

const JSCFunctionListEntry js_readable_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("cancel", 0, js_readable_method, READABLE_METHOD_ABORT),
    JS_CFUNC_MAGIC_DEF("getReader", 0, js_readable_method, READABLE_METHOD_GET_READER),
    JS_CGETSET_MAGIC_FLAGS_DEF("closed", js_readable_get, 0, READABLE_PROP_CLOSED, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("locked", js_readable_get, 0, READABLE_PROP_LOCKED, JS_PROP_ENUMERABLE),
    // JS_CFUNC_DEF("[Symbol.asyncIterator]", 0, js_readable_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ReadableStream", JS_PROP_CONFIGURABLE),
};

const JSCFunctionListEntry js_readable_default_controller_funcs[] = {
    JS_CFUNC_MAGIC_DEF("close", 0, js_readable_controller, READABLE_CLOSE),
    JS_CFUNC_MAGIC_DEF("enqueue", 1, js_readable_controller, READABLE_ENQUEUE),
    JS_CFUNC_MAGIC_DEF("error", 1, js_readable_controller, READABLE_ERROR),
    JS_CGETSET_DEF("desiredSize", js_readable_desired, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ReadableStreamDefaultController", JS_PROP_CONFIGURABLE),
};

const JSCFunctionListEntry js_readable_bytestream_controller_funcs[] = {
    JS_CFUNC_MAGIC_DEF("close", 0, js_readable_controller, READABLE_CLOSE),
    JS_CFUNC_MAGIC_DEF("enqueue", 1, js_readable_controller, READABLE_ENQUEUE),
    JS_CFUNC_MAGIC_DEF("error", 1, js_readable_controller, READABLE_ERROR),
    JS_CGETSET_DEF("desiredSize", js_readable_desired, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ReadableByteStreamController", JS_PROP_CONFIGURABLE),
};

const JSCFunctionListEntry js_byob_request_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("respond", 1, js_byob_request_method, BYOB_REQUEST_METHOD_RESPOND),
    JS_CFUNC_MAGIC_DEF("respondWithNewView", 1, js_byob_request_method, BYOB_REQUEST_METHOD_RESPONDWITHNEWVIEW),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ReadableStreamBYOBRequest", JS_PROP_CONFIGURABLE),
};

/**
 * @brief      Creates a new WritableStreamWriter struct
 *
 * @param      ctx   The JSContext
 * @param      st    A writable stream
 *
 * @return     Pointer to WritableStreamWriter struct or NULL on error
 */
static WritableStreamWriter*
writer_new(JSContext* ctx, WritableStream* st) {
  WritableStreamWriter* wr;

  if((wr = js_mallocz(ctx, sizeof(WritableStreamWriter)))) {
    atomic_store(&wr->stream, st);
    promise_init(ctx, &wr->events.closed);
    promise_init(ctx, &wr->events.ready);

    JSValue ret = js_writable_callback(ctx, st, WRITABLE_START, 1, &st->controller);

    /*if(js_is_promise(ctx, ret))
      ret = promise_forward(ctx, ret, &wr->events.ready);
    else
      promise_resolve(ctx, &wr->events.ready.funcs, JS_TRUE);*/

    JS_FreeValue(ctx, ret);
  }

  return wr;
}

/**
 * @brief      Releases the \ref WritableStreamWriter from its \ref WritableStream stream
 *
 * @param      wr    The writer
 * @param      ctx   The JSContext
 *
 * @return     TRUE on success, FALSE on error
 */
static BOOL
writer_release_lock(WritableStreamWriter* wr, JSContext* ctx) {
  BOOL ret = FALSE;
  WritableStream* r;

  if((r = atomic_load(&wr->stream)))
    if((ret = writable_unlock(r, wr)))
      atomic_store(&wr->stream, (WritableStream*)0);

  return ret;
}

/**
 * @brief      Writes a chunk to the \ref WritableStreamWriter
 *
 * @param      wr     The writer
 * @param[in]  chunk  The chunk
 * @param      ctx    The JSContext
 *
 * @return     A Promise that is resolved when the writing is done
 */
static JSValue
writer_write(WritableStreamWriter* wr, JSValueConst chunk, JSContext* ctx) {
  /*JSValue ret = JS_UNDEFINED;
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

/**
 * @brief      Close a writer
 *
 * @param      wr    The writer
 * @param      ctx   The JSContext
 *
 * @return     A promise that is resolved when the closing is done
 */
static JSValue
writer_close(WritableStreamWriter* wr, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  if(!wr->stream)
    return JS_ThrowInternalError(ctx, "no WriteableStream");

  ret = js_writable_callback(ctx, wr->stream, WRITABLE_CLOSE, 0, 0);

  if(js_is_promise(ctx, ret))
    ret = promise_forward(ctx, ret, &wr->events.closed);

  return ret;
}

/**
 * @brief      Abort a \ref WritableStreamWriter
 *
 * @param      wr      The writer
 * @param[in]  reason  The reason
 * @param      ctx     The JSContext
 *
 * @return     A promise that is resolved when the aborting is done
 */
static JSValue
writer_abort(WritableStreamWriter* wr, JSValueConst reason, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  if(!wr->stream)
    return JS_ThrowInternalError(ctx, "no WriteableStream");

  ret = js_writable_callback(ctx, wr->stream, WRITABLE_ABORT, 1, &reason);

  if(js_is_promise(ctx, ret))
    ret = promise_forward(ctx, ret, &wr->events.closed);

  return ret;
}

/**
 * @brief      Create a writable stream
 *
 * @param      ctx   The JSContext
 *
 * @return     a pointer to a WritableStream struct or NULL on error
 */
static WritableStream*
writable_new(JSContext* ctx) {
  WritableStream* st;

  if((st = js_mallocz(ctx, sizeof(WritableStream)))) {
    st->ref_count = 1;
    st->controller = JS_NULL;
    queue_init(&st->q);

    st->on[3] = st->on[2] = st->on[1] = st->on[0] = JS_NULL;
    st->underlying_sink = JS_NULL;
    st->controller = JS_NULL;
  }

  return st;
}

/**
 * @brief      Duplicate a writable stream
 *
 * @param      st    A writable stream
 *
 * @return     The same writable stream (with incremented reference count)
 */
static WritableStream*
writable_dup(WritableStream* st) {
  ++st->ref_count;
  return st;
}

/**
 * @brief      Abort a \ref WritableStream
 *
 * @param      st      A writable stream
 * @param[in]  reason  The reason
 * @param      ctx     The JSContext
 *
 * @return     A Promise that is resolved when the aborting is done
 */
static JSValue
writable_abort(WritableStream* st, JSValueConst reason, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  static BOOL expected = FALSE;

  if(atomic_compare_exchange_weak(&st->closed, &expected, TRUE)) {
    st->reason = js_tostring(ctx, reason);

    if(writable_locked(st)) {
      promise_resolve(ctx, &st->writer->events.closed.funcs, JS_UNDEFINED);
      ret = writer_abort(st->writer, reason, ctx);
    }
  }

  return ret;
}

/**
 * @brief      Close a writable stream
 *
 * @param      st    A writable stream
 * @param      ctx   The JSContext
 *
 * @return     a Promise which is resolved when the closing is done
 */
static JSValue
writable_close(WritableStream* st, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  static BOOL expected = FALSE;

  if(atomic_compare_exchange_weak(&st->closed, &expected, TRUE)) {
    if(writable_locked(st)) {
      promise_resolve(ctx, &st->writer->events.closed.funcs, JS_UNDEFINED);
      ret = writer_close(st->writer, ctx);
    }
  }

  return ret;
}

/**
 * @brief      Lock a writable stream
 *
 * @param      st    A writable stream
 * @param      wr    The writer
 *
 * @return     TRUE when locked, FALSE when failed
 */
static BOOL
writable_lock(WritableStream* st, WritableStreamWriter* wr) {
  WritableStreamWriter* expected = 0;

  return atomic_compare_exchange_weak(&st->writer, &expected, wr);
}

/**
 * @brief      Unlock a writable stream
 *
 * @param      st    A writable stream
 * @param      wr    The writer
 *
 * @return     TRUE when unlocked, FALSE when failed
 */
static BOOL
writable_unlock(WritableStream* st, WritableStreamWriter* wr) {
  return atomic_compare_exchange_weak(&st->writer, &wr, 0);
}

/**
 * @brief      Get the writer of a WritableStream Stream
 *
 * @param      st            A writable stream
 * @param[in]  desired_size  The desired size
 * @param      ctx           The JSContext
 *
 * @return     Pointer to WritableStreamWriter or NULL on error
 */
static WritableStreamWriter*
writable_get_writer(WritableStream* st, size_t desired_size, JSContext* ctx) {
  WritableStreamWriter* wr;

  if(!(wr = writer_new(ctx, st)))
    return 0;

  wr->desired_size = desired_size;

  if(!writable_lock(st, wr)) {
    js_free(ctx, wr);
    wr = 0;
  }

  return wr;
}

/**
 * @brief      Free a writable stream
 *
 * @param      st    A writable stream
 * @param      rt    The JSRuntime
 */
static void
writable_free(WritableStream* st, JSRuntime* rt) {
  if(--st->ref_count == 0) {
    JS_FreeValueRT(rt, st->underlying_sink);
    JS_FreeValueRT(rt, st->controller);

    for(size_t i = 0; i < countof(st->on); i++)
      JS_FreeValueRT(rt, st->on[i]);

    queue_clear(&st->q);
    js_free_rt(rt, st);
  }
}

/**
 * @brief      Construct a WritableStreamWriter object
 *
 * @param      ctx         The JSContext
 * @param[in]  new_target  The constructor function
 * @param[in]  argc        Number of arguments
 * @param      argv        The arguments array
 *
 * @return     a JS WritableStreamWriter object
 */
static JSValue
js_writer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  WritableStreamWriter* wr = 0;
  WritableStream* st;

  if(argc < 1 || !(st = js_writable_data(argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a WritableStream");

  if(!(wr = writer_new(ctx, st)))
    return JS_EXCEPTION;

  if(!writable_lock(st, wr)) {
    JS_ThrowInternalError(ctx, "unable to lock WritableStream");
    goto fail;
  }

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_writer_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, wr);
  return obj;

fail:
  if(wr)
    js_free(ctx, wr);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

/**
 * @brief      Wraps a \ref WritableStreamWriter struct in a JS WritableStreamWriter object
 *
 * @param      ctx   The JSContext
 * @param      wr    The writer
 *
 * @return     JS WritableStreamWriter object
 */
static JSValue
js_writer_wrap(JSContext* ctx, WritableStreamWriter* wr) {
  JSValue obj = JS_NewObjectProtoClass(ctx, writer_proto, js_writer_class_id);

  JS_SetOpaque(obj, wr);
  return obj;
}

enum {
  WRITER_METHOD_ABORT,
  WRITER_METHOD_CLOSE,
  WRITER_METHOD_WRITE,
  WRITER_METHOD_RELEASE_LOCK,
};

/**
 * @brief      JS WritableStreamWriter object method function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the method
 */
static JSValue
js_writer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  WritableStreamWriter* wr;

  if(!(wr = js_writer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITER_METHOD_ABORT: {
      ret = writer_abort(wr, argv[0], ctx);
      break;
    }

    case WRITER_METHOD_CLOSE: {
      ret = writer_close(wr, ctx);
      break;
    }

    case WRITER_METHOD_WRITE: {
      /*MemoryBlock b;
      InputBuffer input;
      input = js_input_args(ctx, argc, argv);
      b = block_range(input_buffer_blockptr(&input), &input.range);
      ret = writer_write(wr, &b, ctx);
      input_buffer_free(&input, ctx);*/
      ret = writer_write(wr, argv[0], ctx);
      break;
    }

    case WRITER_METHOD_RELEASE_LOCK: {
      writer_release_lock(wr, ctx);
      break;
    }
  }

  return ret;
}

enum {
  WRITER_PROP_CLOSED = 0,
  WRITER_PROP_READY,
};

/**
 * @brief      JS WritableStreamWriter object getter function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  magic     Magic number
 *
 * @return     { return value }
 */
static JSValue
js_writer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  WritableStreamWriter* wr;
  JSValue ret = JS_UNDEFINED;

  if(!(wr = js_writer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITER_PROP_CLOSED: {
      // ret= promise_done(&wr->events.closed.funcs);
      ret = JS_DupValue(ctx, wr->events.closed.value);
      break;
    }

    case WRITER_PROP_READY: {
      // ret= promise_done(&wr->events.ready.funcs);
      ret = JS_DupValue(ctx, wr->events.ready.value);
      break;
    }
  }

  return ret;
}

/**
 * @brief      JS WritableStreamWriter object finalizer
 *
 * @param      rt    The JSRuntime
 * @param[in]  val   The value
 */
static void
js_writer_finalizer(JSRuntime* rt, JSValue val) {
  WritableStreamWriter* wr;

  if((wr = JS_GetOpaque(val, js_writer_class_id)))
    js_free_rt(rt, wr);
}

JSClassDef js_writer_class = {
    .class_name = "WritableStreamDefaultWriter",
    .finalizer = js_writer_finalizer,
};

const JSCFunctionListEntry js_writer_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("write", 0, js_writer_method, WRITER_METHOD_WRITE),
    JS_CFUNC_MAGIC_DEF("abort", 0, js_writer_method, WRITER_METHOD_ABORT),
    JS_CFUNC_MAGIC_DEF("close", 0, js_writer_method, WRITER_METHOD_CLOSE),
    JS_CFUNC_MAGIC_DEF("releaseLock", 0, js_writer_method, WRITER_METHOD_RELEASE_LOCK),
    JS_CGETSET_MAGIC_DEF("closed", js_writer_get, 0, WRITER_PROP_CLOSED),
    JS_CGETSET_MAGIC_DEF("ready", js_writer_get, 0, WRITER_PROP_READY),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WritableStreamDefaultWriter", JS_PROP_CONFIGURABLE),
};

/**
 * @brief      { function_description }
 *
 * @param      ctx    The JSContext
 * @param      st     A writable stream
 * @param[in]  event  The event
 * @param[in]  argc   Number of arguments
 * @param      argv   The arguments array
 *
 * @return     { return value }
 */
static JSValue
js_writable_callback(JSContext* ctx, WritableStream* st, WritableCallback cb, int argc, JSValueConst argv[]) {
  assert(cb >= 0);
  assert(cb < countof(st->on));

  if(JS_IsFunction(ctx, st->on[cb]))
    return JS_Call(ctx, st->on[cb], st->underlying_sink, argc, argv);

  return JS_UNDEFINED;
}

/**
 * @brief      Constructs a JS WritableStream object
 *
 * @param      ctx         The JSContext
 * @param[in]  new_target  The constructor function
 * @param[in]  argc        Number of arguments
 * @param      argv        The arguments array
 *
 * @return     JS WritableStream object
 */
static JSValue
js_writable_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  WritableStream* st;

  if(!(st = writable_new(ctx)))
    return JS_EXCEPTION;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

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
  if(st)
    js_free(ctx, st);
  JS_FreeValue(ctx, obj);

  return JS_EXCEPTION;
}

/**
 * @brief      Wraps a WritableStream struct in a JS WritableStream object
 *
 * @param      ctx   The JSContext
 * @param      st    A writable stream
 *
 * @return     JS WritableStream object
 */
static JSValue
js_writable_wrap(JSContext* ctx, WritableStream* st) {
  JSValue obj = JS_NewObjectProtoClass(ctx, writable_proto, js_writable_class_id);

  writable_dup(st);
  JS_SetOpaque(obj, st);
  return obj;
}

/**
 * @brief      Returns the JS object itself
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 *
 * @return     JS WritableStream object
 */
static JSValue
js_writable_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

enum {
  WRITABLE_METHOD_CLOSE = 0,
  WRITABLE_METHOD_ABORT,
  WRITABLE_METHOD_GET_WRITER,
};

/**
 * @brief      JS WritableStream object method function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the method
 */
static JSValue
js_writable_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  WritableStream* st;
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

    case WRITABLE_METHOD_GET_WRITER: {
      WritableStreamWriter* wr;

      if((wr = writable_get_writer(st, 0, ctx)))
        ret = js_writer_wrap(ctx, wr);
      else
        ret = JS_ThrowTypeError(ctx,
                                "Failed to execute 'getWriter' on 'WritableStream': WritableStreamDefaultWriter "
                                "constructor can only accept writable streams that are not yet locked to a writer");

      break;
    }
  }

  return ret;
}

enum {
  WRITABLE_PROP_CLOSED,
  WRITABLE_PROP_LOCKED,
};

/**
 * @brief      JS WritableStream object getter function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the getter
 */
static JSValue
js_writable_get(JSContext* ctx, JSValueConst this_val, int magic) {
  WritableStream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_writable_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITABLE_PROP_CLOSED: {
      ret = JS_NewBool(ctx, writable_closed(st));
      break;
    }

    case WRITABLE_PROP_LOCKED: {
      ret = JS_NewBool(ctx, writable_locked(st) || writable_locked(st));
      break;
    }
  }

  return ret;
}

enum {
  WRITABLE_ERROR = 0,
};

/**
 * @brief      JS WritableStream controller functions
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the controller function
 */
static JSValue
js_writable_controller(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  WritableStream* st;
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

/**
 * @brief      JS WritableStream object finalizer
 *
 * @param      rt    The JSRuntime
 * @param[in]  val   The value
 */
static void
js_writable_finalizer(JSRuntime* rt, JSValue val) {
  WritableStream* st;

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
    JS_CFUNC_MAGIC_DEF("getWriter", 0, js_writable_method, WRITABLE_METHOD_GET_WRITER),
    JS_CGETSET_MAGIC_FLAGS_DEF("locked", js_writable_get, 0, WRITABLE_PROP_LOCKED, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WritableStream", JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_writable_iterator),
};

const JSCFunctionListEntry js_writable_controller_funcs[] = {
    JS_CFUNC_MAGIC_DEF("error", 0, js_writable_controller, WRITABLE_ERROR),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WritableStreamDefaultController", JS_PROP_CONFIGURABLE),
};

/**
 * @brief      Duplicates a \ref TransformStream stream
 *
 * @param      st    A transform stream
 *
 * @return     The same TransformStream stream (with incremented reference count)
 */
static TransformStream*
transform_dup(TransformStream* st) {
  ++st->ref_count;

  return st;
}

/**
 * @brief      Creates a new \ref TransformStream stream
 *
 * @param      ctx   The JSContext
 *
 * @return     A new TransformStream stream
 */
static TransformStream*
transform_new(JSContext* ctx) {
  TransformStream* st;

  if((st = js_mallocz(ctx, sizeof(TransformStream)))) {
    st->ref_count = 1;
    st->readable = readable_new(ctx);
    st->writable = writable_new(ctx);

    st->controller = JS_NewObjectProtoClass(ctx, transform_controller, js_transform_class_id);

    JS_SetOpaque(st->controller, transform_dup(st));
  }

  return st;
}

/**
 * @brief      Terminates a \ref TransformStream stream
 *
 * @param      st    A transform stream
 * @param      ctx   The JSContext
 */
static void
transform_terminate(TransformStream* st, JSContext* ctx) {
  readable_close(st->readable, ctx);
  writable_abort(st->writable, JS_UNDEFINED, ctx);
}

/**
 * @brief      Signals an error on a \ref TransformStream stream
 *
 * @param      st     A transform stream
 * @param[in]  error  The error
 * @param      ctx    The JSContext
 */
static void
transform_error(TransformStream* st, JSValueConst error, JSContext* ctx) {
  readable_cancel(st->readable, error, ctx);
  writable_abort(st->writable, error, ctx);
}

/**
 * @brief      Constructs a JS TransformStream object
 *
 * @param      ctx         The JSContext
 * @param[in]  new_target  The constructor function
 * @param[in]  argc        Number of arguments
 * @param      argv        The arguments array
 *
 * @return     JS TransformStream object
 */
static JSValue
js_transform_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  TransformStream* st;

  if(!(st = transform_new(ctx)))
    return JS_EXCEPTION;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

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
  if(st)
    js_free(ctx, st);
  JS_FreeValue(ctx, obj);

  return JS_EXCEPTION;
}

enum {
  TRANSFORM_PROP_READABLE = 0,
  TRANSFORM_PROP_WRITABLE,
};

/**
 * @brief      JS TransformStream object getter function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the getter
 */
static JSValue
js_transform_get(JSContext* ctx, JSValueConst this_val, int magic) {
  TransformStream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_transform_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case TRANSFORM_PROP_READABLE: {
      ret = js_readable_wrap(ctx, st->readable);
      break;
    }

    case TRANSFORM_PROP_WRITABLE: {
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

/**
 * @brief      JS TransformStream controller functions
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the controller function
 */
static JSValue
js_transform_controller(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  TransformStream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_transform_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case TRANSFORM_ENQUEUE: {
      BOOL binary = FALSE;

      if(argc > 1)
        binary = JS_ToBool(ctx, argv[1]);

      ret = readable_enqueue(st->readable, argv[0], binary, ctx);
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

/**
 * @brief      Returns the desired size of a JS TransformStream object
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 *
 * @return     JS Number
 */
static JSValue
js_transform_desired(JSContext* ctx, JSValueConst this_val) {
  TransformStream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_transform_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(readable_locked(st->readable)) {
    ReadableStreamReader* rd;

    if((rd = st->readable->reader))
      ret = JS_NewUint32(ctx, rd->desired_size);
  }

  return ret;
}

/**
 * @brief      JS TransformStream object finalizer
 *
 * @param      rt    The JSRuntime
 * @param[in]  val   The value
 */
static void
js_transform_finalizer(JSRuntime* rt, JSValue val) {
  TransformStream* st;

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
    JS_CGETSET_MAGIC_FLAGS_DEF("readable", js_transform_get, 0, TRANSFORM_PROP_READABLE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("writable", js_transform_get, 0, TRANSFORM_PROP_WRITABLE, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TransformStream", JS_PROP_CONFIGURABLE),
};

const JSCFunctionListEntry js_transform_controller_funcs[] = {
    JS_CFUNC_MAGIC_DEF("terminate", 0, js_transform_controller, TRANSFORM_TERMINATE),
    JS_CFUNC_MAGIC_DEF("enqueue", 1, js_transform_controller, TRANSFORM_ENQUEUE),
    JS_CFUNC_MAGIC_DEF("error", 1, js_transform_controller, TRANSFORM_ERROR),
    JS_CGETSET_DEF("desiredSize", js_transform_desired, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TransformStreamDefaultController", JS_PROP_CONFIGURABLE),
};

/**
 * @brief      Initialize all stream classes
 *
 * @param      ctx   The JSContext
 * @param      m     Module definition
 *
 * @return     0 on success
 */
int
js_stream_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_reader_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_reader_class_id, &js_default_reader_class);
  JS_NewClass(JS_GetRuntime(ctx), js_reader_class_id, &js_byob_reader_class);

  default_reader_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx,
                             default_reader_proto,
                             js_default_reader_proto_funcs,
                             countof(js_default_reader_proto_funcs));
  JS_SetClassProto(ctx, js_reader_class_id, default_reader_proto);

  default_reader_ctor =
      JS_NewCFunction2(ctx, js_reader_constructor, "ReadableStreamDefaultReader", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, default_reader_ctor, default_reader_proto);

  byob_reader_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, byob_reader_proto, js_byob_reader_proto_funcs, countof(js_byob_reader_proto_funcs));
  JS_SetClassProto(ctx, js_reader_class_id, byob_reader_proto);

  byob_reader_ctor =
      JS_NewCFunction2(ctx, js_reader_constructor, "ReadableStreamBYOBReader", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, byob_reader_ctor, byob_reader_proto);

  JS_NewClassID(&js_readable_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_readable_class_id, &js_readable_class);

  readable_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, readable_proto, js_readable_proto_funcs, countof(js_readable_proto_funcs));
  JS_SetClassProto(ctx, js_readable_class_id, readable_proto);

  readable_ctor = JS_NewCFunction2(ctx, js_readable_constructor, "ReadableStream", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, readable_ctor, readable_proto);

  readable_default_controller = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx,
                             readable_default_controller,
                             js_readable_default_controller_funcs,
                             countof(js_readable_default_controller_funcs));
  JS_SetClassProto(ctx, js_readable_class_id, readable_default_controller);

  readable_bytestream_controller = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx,
                             readable_bytestream_controller,
                             js_readable_bytestream_controller_funcs,
                             countof(js_readable_bytestream_controller_funcs));
  JS_SetClassProto(ctx, js_readable_class_id, readable_bytestream_controller);

  byob_request_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx,
                             byob_request_proto,
                             js_byob_request_proto_funcs,
                             countof(js_byob_request_proto_funcs));
  JS_SetClassProto(ctx, js_readable_class_id, byob_request_proto);

  JS_NewClassID(&js_writer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_writer_class_id, &js_writer_class);

  writer_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, writer_proto, js_writer_proto_funcs, countof(js_writer_proto_funcs));
  JS_SetClassProto(ctx, js_writer_class_id, writer_proto);

  writer_ctor = JS_NewCFunction2(ctx, js_writer_constructor, "WritableStreamDefaultWriter", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, writer_ctor, writer_proto);

  JS_NewClassID(&js_writable_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_writable_class_id, &js_writable_class);

  writable_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, writable_proto, js_writable_proto_funcs, countof(js_writable_proto_funcs));
  JS_SetClassProto(ctx, js_writable_class_id, writable_proto);

  writable_ctor = JS_NewCFunction2(ctx, js_writable_constructor, "WritableStream", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, writable_ctor, writable_proto);

  writable_controller = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx,
                             writable_controller,
                             js_writable_controller_funcs,
                             countof(js_writable_controller_funcs));
  JS_SetClassProto(ctx, js_writable_class_id, writable_controller);

  JS_NewClassID(&js_transform_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_transform_class_id, &js_transform_class);

  transform_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, transform_proto, js_transform_proto_funcs, countof(js_transform_proto_funcs));
  JS_SetClassProto(ctx, js_transform_class_id, transform_proto);

  transform_ctor = JS_NewCFunction2(ctx, js_transform_constructor, "TransformStream", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, transform_ctor, transform_proto);

  transform_controller = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx,
                             transform_controller,
                             js_transform_controller_funcs,
                             countof(js_transform_controller_funcs));
  JS_SetClassProto(ctx, js_transform_class_id, transform_controller);

  // JS_SetPropertyFunctionList(ctx, stream_ctor, js_stream_static_funcs,
  // countof(js_stream_static_funcs));

  if(m) {
    JS_SetModuleExport(ctx, m, "ReadableStreamDefaultReader", default_reader_ctor);
    JS_SetModuleExport(ctx, m, "ReadableStreamBYOBReader", byob_reader_ctor);
    JS_SetModuleExport(ctx, m, "WritableStreamDefaultWriter", writer_ctor);
    JS_SetModuleExport(ctx, m, "ReadableStream", readable_ctor);
    JS_SetModuleExport(ctx, m, "ReadableStreamDefaultController", readable_default_controller);
    JS_SetModuleExport(ctx, m, "ReadableByteStreamController", readable_bytestream_controller);
    JS_SetModuleExport(ctx, m, "WritableStream", writable_ctor);
    JS_SetModuleExport(ctx, m, "WritableStreamDefaultController", writable_controller);
    JS_SetModuleExport(ctx, m, "TransformStream", transform_ctor);
  }

  // js_eval_binary(ctx, qjsc_stream, qjsc_stream_size, FALSE);

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

  if(!(m = JS_NewCModule(ctx, module_name, js_stream_init)))
    return NULL;

  JS_AddModuleExport(ctx, m, "ReadableStreamDefaultReader");
  JS_AddModuleExport(ctx, m, "ReadableStreamBYOBReader");
  JS_AddModuleExport(ctx, m, "WritableStreamDefaultWriter");
  JS_AddModuleExport(ctx, m, "ReadableStream");
  JS_AddModuleExport(ctx, m, "ReadableStreamDefaultController");
  JS_AddModuleExport(ctx, m, "ReadableByteStreamController");
  JS_AddModuleExport(ctx, m, "WritableStream");
  JS_AddModuleExport(ctx, m, "WritableStreamDefaultController");
  JS_AddModuleExport(ctx, m, "TransformStream");
  return m;
}

/**
 * @}
 */
