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

static int reader_update(Reader*, JSContext*);
static BOOL reader_passthrough(Reader*, JSValueConst, JSContext*);
static int readable_unlock(Readable*, Reader*);
static int writable_unlock(Writable*, Writer*);

static JSValue
js_get_iterator_value(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  BOOL done = js_get_propertystr_bool(ctx, argv[0], "done");
  return JS_GetPropertyStr(ctx, argv[0], "value");
}

static JSValue
js_promise_iterator_value(JSContext* ctx, JSValueConst promise) {

  JSValue fn = JS_NewCFunction(ctx, &js_get_iterator_value, "getIteratorValue", 1);

  JSValue ret = promise_then(ctx, promise, fn);

  JS_FreeValue(ctx, fn);
  return ret;
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
 * @brief      Creates a new \ref Read
 *
 * @param      rd    Reader struct
 * @param      ctx   The JSContext
 *
 * @return     { description_of_the_return_value }
 */
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

/**
 * @brief      Reads the next value
 *
 * @param      rd    Reader struct
 * @param      ctx   The JSContext
 *
 * @return     A Promise that resolves to a value when reading done
 */
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

  if(!op)
    if(!(op = read_new(rd, ctx)))
      ret = JS_EXCEPTION;

  if(op) {
    // printf("read_next (%i/%zu)\n", op->seq, list_size(&rd->list));
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
read_done(Read* op) {
  return JS_IsUndefined(op->promise.value) && promise_done(&op->promise.funcs);
}

/**
 * @brief      Frees a \ref Read
 *
 * @param      op    The operation
 * @param      rt    The JSRuntime
 */
static void
read_free_rt(Read* op, JSRuntime* rt) {
  promise_free(rt, &op->promise);

  list_del(&op->link);
}

/**
 * @brief      Creates a new \ref Reader
 *
 * @param      ctx   The JSContext
 * @param      st    A readable stream
 *
 * @return     A Reader struct or NULL on error
 */
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

/**
 * @brief      Releases the \ref Reader from its \ref Readable stream
 *
 * @param      rd    Reader struct
 * @param      ctx   The JSContext
 *
 * @return     TRUE on success, FALSE if failed
 */
static BOOL
reader_release_lock(Reader* rd, JSContext* ctx) {
  BOOL ret = FALSE;
  Readable* r;

  if((r = atomic_load(&rd->stream)))
    if((ret = readable_unlock(r, rd)))
      atomic_store(&rd->stream, (Readable*)0);

  return ret;
}

/**
 * @brief      Clears all queued reads on the \ref Reader
 *
 * @param      rd    Reader struct
 * @param      ctx   The JSContext
 *
 * @return     How many reads have been cleared
 */
static int
reader_clear(Reader* rd, JSContext* ctx) {
  int ret = 0;
  Read *el, *next;

  list_for_each_prev_safe(el, next, &rd->reads) {
    promise_reject(ctx, &el->promise.funcs, JS_UNDEFINED);

    read_free_rt(el, JS_GetRuntime(ctx));

    ++ret;
  }

  return ret;
}

/**
 * @brief      Closes a \ref Reader
 *
 * @param      rd    Reader struct
 * @param      ctx   The JSContext
 *
 * @return     A Promise that resolves when the closing is done
 */
static JSValue
reader_close(Reader* rd, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  if(!rd->stream)
    return JS_ThrowInternalError(ctx, "no ReadableStream");

  ret = js_readable_callback(ctx, rd->stream, READABLE_CANCEL, 0, 0);

  // printf("reader_close (2) promise=%i\n", js_is_promise(ctx, ret));

  rd->stream->closed = TRUE;

  reader_update(rd, ctx);

  if(js_is_promise(ctx, ret))
    ret = promise_forward(ctx, ret, &rd->events[READER_CLOSED]);

  // printf("reader_close (2) promise=%i\n", js_is_promise(ctx, ret));

  return ret;
}

/**
 * @brief      Cancels a \ref Reader
 *
 * @param      rd      Reader struct
 * @param[in]  reason  The reason
 * @param      ctx     The JSContext
 *
 * @return     { return value }
 */
static JSValue
reader_cancel(Reader* rd, JSValueConst reason, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  if(!rd->stream)
    return JS_ThrowInternalError(ctx, "no WriteableStream");

  ret = js_readable_callback(ctx, rd->stream, READABLE_CANCEL, 1, &reason);

  if(js_is_promise(ctx, ret))
    ret = promise_forward(ctx, ret, &rd->events[READER_CANCELLED]);

  return ret;
}

/**
 * @brief      Reads from a \ref Reader
 *
 * @param      rd    Reader struct
 * @param      ctx   The JSContext
 *
 * @return     A Promise that resolves to data as soon as the read has completed
 */
static JSValue
reader_read(Reader* rd, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  Readable* st;

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
  // printf("Read (%i) q2[%zu]\n", op->seq, queue_size(&st->q));

  return js_promise_iterator_value(ctx, ret);
}

/**
 * @brief      Cleans all reads from a \ref Reader
 *
 * @param      rd    Reader struct
 * @param      ctx   The JSContext
 *
 * @return     How many reads that have been cleaned
 */
static size_t
reader_clean(Reader* rd, JSContext* ctx) {
  Read *el, *next;
  size_t ret = 0;

  list_for_each_prev_safe(el, next, (Read*)&rd->reads) {
    if(read_done(el)) {
      // printf("reader_clean() delete[%i]\n", el->seq);

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
 * @brief      Updates a \ref Reader state
 *
 * @param      rd    Reader struct
 * @param      ctx   The JSContext
 *
 * @return     How many chunks that have been processed
 */
static int
reader_update(Reader* rd, JSContext* ctx) {
  JSValue result;
  Chunk* ch;
  Readable* st = rd->stream;
  int ret = 0;

  reader_clean(rd, ctx);

  // printf("reader_update(1) [%zu] closed=%d queue.size=%zu\n", list_size(&rd->list), readable_closed(st), queue_size(&st->q));

  if(readable_closed(st)) {
    promise_resolve(ctx, &rd->events[READER_CLOSED].funcs, JS_UNDEFINED);

    // reader_clear(rd, ctx);

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

  // printf("reader_update(3) closed=%d queue.size=%zu result = %d\n", readable_closed(st), queue_size(&st->q), ret);

  return ret;
}

/**
 * @brief      { function_description }
 *
 * @param      rd      { parameter_description }
 * @param[in]  result  The result
 * @param      ctx     The JSContext
 *
 * @return     { description_of_the_return_value }
 */
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
    ret = promise_resolve(ctx, &op->promise.funcs, result);
    reader_clean(rd, ctx);
  }

  return ret;
}

/**
 * @brief      Creates a new \ref Readable stream
 *
 * @param      ctx   The JSContext
 *
 * @return     A new Readable stream
 */
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

/**
 * @brief      Duplicates a \ref Readable stream
 *
 * @param      st    A readable stream
 *
 * @return     The same readable stream (with incremented reference count)
 */
static Readable*
readable_dup(Readable* st) {
  ++st->ref_count;
  return st;
}

/**
 * @brief      Closes the \ref Readable stream
 *
 * @param      st    A readable stream
 * @param      ctx   The JSContext
 *
 * @return     JS_UNDEFINED
 */
static JSValue
readable_close(Readable* st, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  static BOOL expected = FALSE;

  // printf("readable_close(1) expected=%i, closed=%i\n", st->closed, expected);

  if(atomic_compare_exchange_weak(&st->closed, &expected, TRUE)) {
    if(readable_locked(st)) {
      promise_resolve(ctx, &st->reader->events[READER_CLOSED].funcs, JS_UNDEFINED);
      reader_close(st->reader, ctx);
    }
  }

  return ret;
}

/**
 * @brief      Cancels the \ref Readable stream
 *
 * @param      st      A readable stream
 * @param[in]  reason  The reason
 * @param      ctx     The JSContext
 *
 * @return     A Promise which is resolved when the cancellation has completed
 */
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
}

/**
 * @brief      Enqueues data on the \ref Readable stream
 *
 * @param      st     A readable stream
 * @param[in]  chunk  The chunk
 * @param      ctx    The JSContext
 *
 * @return     JSValue: Number of bytes written
 */
static JSValue
readable_enqueue(Readable* st, JSValueConst chunk, JSContext* ctx) {
  InputBuffer input;
  int64_t ret;
  Reader* rd;

  if(readable_locked(st) && (rd = st->reader)) {
    JSValue result = js_iterator_result(ctx, chunk, FALSE);
    BOOL ok = reader_passthrough(rd, result, ctx);

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

/**
 * @brief      Locks the \ref Readable stream
 *
 * @param      st    A readable stream
 * @param      rd    Reader struct
 *
 * @return     TRUE when locked, FALSE when failed
 */
static BOOL
readable_lock(Readable* st, Reader* rd) {
  Reader* expected = 0;

  return atomic_compare_exchange_weak(&st->reader, &expected, rd);
}

/**
 * @brief      Unlocks the \ref Readable stream
 *
 * @param      st    A readable stream
 * @param      rd    Reader struct
 *
 * @return     TRUE when unlocked, FALSE when failed
 */
static BOOL
readable_unlock(Readable* st, Reader* rd) {
  return atomic_compare_exchange_weak(&st->reader, &rd, 0);
}

/**
 * @brief      Gets the \ref Reader of the \ref Readable stream
 *
 * @param      st    A readable stream
 * @param      ctx   The JSContext
 *
 * @return     { description_of_the_return_value }
 */
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

/**
 * @brief      Frees the \ref Readable stream
 *
 * @param      st    A readable stream
 * @param      rt    The JSRuntime
 */
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

/**
 * @brief      Construct a JS Reader object
 *
 * @param      ctx         The JSContext
 * @param[in]  new_target  The constructor function
 * @param[in]  argc        Number of arguments
 * @param      argv        The arguments array
 *
 * @return     JS Reader object
 */
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

/**
 * @brief      Wraps a \ref Reader struct in a JS Reader object
 *
 * @param      ctx   The JSContext
 * @param      rd    Reader struct
 *
 * @return     JS Reader object
 */
JSValue
js_reader_wrap(JSContext* ctx, Reader* rd) {
  JSValue obj = JS_NewObjectProtoClass(ctx, reader_proto, js_reader_class_id);

  JS_SetOpaque(obj, rd);
  return obj;
}

enum {
  READER_CANCEL,
  READER_READ,
  READER_RELEASE_LOCK,
};

/**
 * @brief      JS Reader object method function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the method
 */
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

enum {
  READER_PROP_CLOSED,
};

/**
 * @brief      JS Reader object getter function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  magic     Magic number
 *
 * @return     { return value }
 */
JSValue
js_reader_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Reader* rd;
  JSValue ret = JS_UNDEFINED;

  if(!(rd = js_reader_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case READER_PROP_CLOSED: {
      ret = JS_DupValue(ctx, rd->events[READER_CLOSED].value);
      break;
    }
  }

  return ret;
}

/**
 * @brief      JS Reader object finalizer
 *
 * @param      rt    The JSRuntime
 * @param[in]  val   The value
 */
void
js_reader_finalizer(JSRuntime* rt, JSValue val) {
  Reader* rd;

  if((rd = JS_GetOpaque(val, js_reader_class_id)))
    js_free_rt(rt, rd);
}

JSClassDef js_reader_class = {
    .class_name = "StreamReader",
    .finalizer = js_reader_finalizer,
};

const JSCFunctionListEntry js_reader_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("read", 0, js_reader_method, READER_READ),
    JS_CFUNC_MAGIC_DEF("cancel", 0, js_reader_method, READER_CANCEL),
    JS_CFUNC_MAGIC_DEF("releaseLock", 0, js_reader_method, READER_RELEASE_LOCK),
    JS_CGETSET_MAGIC_DEF("closed", js_reader_get, 0, READER_PROP_CLOSED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StreamReader", JS_PROP_CONFIGURABLE),
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
JSValue
js_readable_callback(JSContext* ctx, Readable* st, ReadableEvent event, int argc, JSValueConst argv[]) {
  assert(event >= 0);
  assert(event < countof(st->on));

  if(JS_IsFunction(ctx, st->on[event]))
    return JS_Call(ctx, st->on[event], st->underlying_source, argc, argv);

  return JS_UNDEFINED;
}

/**
 * @brief      Constructs a JS Readable object
 *
 * @param      ctx         The JSContext
 * @param[in]  new_target  The constructor function
 * @param[in]  argc        Number of arguments
 * @param      argv        The arguments array
 *
 * @return     JS Readable object
 */
JSValue
js_readable_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Readable* st = 0;

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

    JS_SetOpaque(st->controller, readable_dup(st));
  }

  JS_SetOpaque(obj, st);
  return obj;

fail:
  js_free(ctx, st);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

/**
 * @brief      Wraps a Readable struct in a JS Readable object
 *
 * @param      ctx   The JSContext
 * @param      st    A readable stream
 *
 * @return     JS Readable object
 */
JSValue
js_readable_wrap(JSContext* ctx, Readable* st) {
  JSValue obj = JS_NewObjectProtoClass(ctx, readable_proto, js_readable_class_id);

  readable_dup(st);
  JS_SetOpaque(obj, st);
  return obj;
}

enum {
  READABLE_ABORT = 0,
  READABLE_GET_READER,
};

/**
 * @brief      JS Readable object method function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the method
 */
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
  READABLE_PROP_CLOSED = 0,
  READABLE_PROP_LOCKED,
};

/**
 * @brief      JS Readable object getter function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the getter
 */
JSValue
js_readable_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Readable* st;
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
 * @brief      JS Readable controller functions
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the controller function
 */
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

/**
 * @brief      Returns the desired size of a JS Readable object
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 *
 * @return     JS Number
 */
JSValue
js_readable_desired(JSContext* ctx, JSValueConst this_val) {
  Readable* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_readable_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(readable_locked(st)) {
    Reader* rd;

    if((rd = st->reader))
      ret = JS_NewUint32(ctx, rd->desired_size);
  }

  return ret;
}

/**
 * @brief      JS Readable object finalizer
 *
 * @param      rt    The JSRuntime
 * @param[in]  val   The value
 */
void
js_readable_finalizer(JSRuntime* rt, JSValue val) {
  Readable* st;

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
    JS_CFUNC_MAGIC_DEF("cancel", 0, js_readable_method, READABLE_ABORT),
    JS_CFUNC_MAGIC_DEF("getReader", 0, js_readable_method, READABLE_GET_READER),
    JS_CGETSET_MAGIC_FLAGS_DEF("closed", js_readable_get, 0, READABLE_PROP_CLOSED, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("locked", js_readable_get, 0, READABLE_PROP_LOCKED, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Readable", JS_PROP_CONFIGURABLE),
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

/**
 * @brief      Creates a new Writer struct
 *
 * @param      ctx   The JSContext
 * @param      st    A writable stream
 *
 * @return     Pointer to Writer struct or NULL on error
 */
static Writer*
writer_new(JSContext* ctx, Writable* st) {
  Writer* wr;

  if((wr = js_mallocz(ctx, sizeof(Writer)))) {
    atomic_store(&wr->stream, st);
    promise_init(ctx, &wr->events[WRITER_CLOSED]);
    promise_init(ctx, &wr->events[WRITER_READY]);

    JSValue ret = js_writable_callback(ctx, st, WRITABLE_START, 1, &st->controller);

    /*if(js_is_promise(ctx, ret))
      ret = promise_forward(ctx, ret, &wr->events[WRITER_READY]);
    else
      promise_resolve(ctx, &wr->events[WRITER_READY].funcs, JS_TRUE);*/

    JS_FreeValue(ctx, ret);
  }

  return wr;
}

/**
 * @brief      Releases the \ref Writer from its \ref Writable stream
 *
 * @param      wr    The writer
 * @param      ctx   The JSContext
 *
 * @return     TRUE on success, FALSE on error
 */
static BOOL
writer_release_lock(Writer* wr, JSContext* ctx) {
  BOOL ret = FALSE;
  Writable* r;

  if((r = atomic_load(&wr->stream)))
    if((ret = writable_unlock(r, wr)))
      atomic_store(&wr->stream, (Writable*)0);

  return ret;
}

/**
 * @brief      Writes a chunk to the \ref Writer
 *
 * @param      wr     The writer
 * @param[in]  chunk  The chunk
 * @param      ctx    The JSContext
 *
 * @return     A Promise that is resolved when the writing is done
 */
static JSValue
writer_write(Writer* wr, JSValueConst chunk, JSContext* ctx) {
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
writer_close(Writer* wr, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  if(!wr->stream)
    return JS_ThrowInternalError(ctx, "no WriteableStream");

  ret = js_writable_callback(ctx, wr->stream, WRITABLE_CLOSE, 0, 0);

  if(js_is_promise(ctx, ret))
    ret = promise_forward(ctx, ret, &wr->events[WRITER_CLOSED]);

  return ret;
}

/**
 * @brief      Abort a \ref Writer
 *
 * @param      wr      The writer
 * @param[in]  reason  The reason
 * @param      ctx     The JSContext
 *
 * @return     A promise that is resolved when the aborting is done
 */
static JSValue
writer_abort(Writer* wr, JSValueConst reason, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  if(!wr->stream)
    return JS_ThrowInternalError(ctx, "no WriteableStream");

  ret = js_writable_callback(ctx, wr->stream, WRITABLE_ABORT, 1, &reason);

  if(js_is_promise(ctx, ret))
    ret = promise_forward(ctx, ret, &wr->events[WRITER_CLOSED]);

  return ret;
}

/**
 * @brief      { function_description }
 *
 * @param      wr     The writer
 * @param[in]  event  The event
 * @param[in]  arg    The argument
 * @param      ctx    The JSContext
 *
 * @return     JS TRUE if successful
 */
static JSValue
writer_signal(Writer* wr, StreamEvent event, JSValueConst arg, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  assert(event <= EVENT_READ);
  assert(event >= EVENT_CLOSE);

  if(promise_resolve(ctx, &wr->events[event].funcs, arg))
    ret = JS_TRUE;

  return ret;
}

/**
 * @brief      Create a writable stream
 *
 * @param      ctx   The JSContext
 *
 * @return     a pointer to a Writable struct or NULL on error
 */
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

/**
 * @brief      Duplicate a writable stream
 *
 * @param      st    A writable stream
 *
 * @return     The same writable stream (with incremented reference count)
 */
static Writable*
writable_dup(Writable* st) {
  ++st->ref_count;
  return st;
}

/**
 * @brief      Abort a \ref Writable
 *
 * @param      st      A writable stream
 * @param[in]  reason  The reason
 * @param      ctx     The JSContext
 *
 * @return     A Promise that is resolved when the aborting is done
 */
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

/**
 * @brief      Close a writable stream
 *
 * @param      st    A writable stream
 * @param      ctx   The JSContext
 *
 * @return     a Promise which is resolved when the closing is done
 */
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

/**
 * @brief      Lock a writable stream
 *
 * @param      st    A writable stream
 * @param      wr    The writer
 *
 * @return     TRUE when locked, FALSE when failed
 */
static BOOL
writable_lock(Writable* st, Writer* wr) {
  Writer* expected = 0;

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
writable_unlock(Writable* st, Writer* wr) {
  return atomic_compare_exchange_weak(&st->writer, &wr, 0);
}

/**
 * @brief      Get the writer of a Writable Stream
 *
 * @param      st            A writable stream
 * @param[in]  desired_size  The desired size
 * @param      ctx           The JSContext
 *
 * @return     Pointer to Writer or NULL on error
 */
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

/**
 * @brief      Free a writable stream
 *
 * @param      st    A writable stream
 * @param      rt    The JSRuntime
 */
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

/**
 * @brief      Construct a Writer object
 *
 * @param      ctx         The JSContext
 * @param[in]  new_target  The constructor function
 * @param[in]  argc        Number of arguments
 * @param      argv        The arguments array
 *
 * @return     a JS Writer object
 */
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

/**
 * @brief      Wraps a \ref Writer struct in a JS Writer object
 *
 * @param      ctx   The JSContext
 * @param      wr    The writer
 *
 * @return     JS Writer object
 */
JSValue
js_writer_wrap(JSContext* ctx, Writer* wr) {
  JSValue obj = JS_NewObjectProtoClass(ctx, writer_proto, js_writer_class_id);

  JS_SetOpaque(obj, wr);
  return obj;
}

enum {
  WRITER_ABORT,
  WRITER_CLOSE,
  WRITER_WRITE,
  WRITER_RELEASE_LOCK,
};

/**
 * @brief      JS Writer object method function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the method
 */
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
      /*MemoryBlock b;
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

enum {
  WRITER_PROP_CLOSED = 0,
  WRITER_PROP_READY,
};

/**
 * @brief      JS Writer object getter function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  magic     Magic number
 *
 * @return     { return value }
 */
JSValue
js_writer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Writer* wr;
  JSValue ret = JS_UNDEFINED;

  if(!(wr = js_writer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITER_PROP_CLOSED: {
      ret = JS_DupValue(ctx, wr->events[WRITER_CLOSED].value);
      break;
    }

    case WRITER_PROP_READY: {
      ret = JS_DupValue(ctx, wr->events[WRITER_READY].value);
      break;
    }
  }

  return ret;
}

/**
 * @brief      JS Writer object finalizer
 *
 * @param      rt    The JSRuntime
 * @param[in]  val   The value
 */
void
js_writer_finalizer(JSRuntime* rt, JSValue val) {
  Writer* wr;

  if((wr = JS_GetOpaque(val, js_writer_class_id)))
    js_free_rt(rt, wr);
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
    JS_CGETSET_MAGIC_DEF("closed", js_writer_get, 0, WRITER_PROP_CLOSED),
    JS_CGETSET_MAGIC_DEF("ready", js_writer_get, 0, WRITER_PROP_READY),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StreamWriter", JS_PROP_CONFIGURABLE),
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
JSValue
js_writable_callback(JSContext* ctx, Writable* st, WritableEvent event, int argc, JSValueConst argv[]) {
  assert(event >= 0);
  assert(event < countof(st->on));

  if(JS_IsFunction(ctx, st->on[event]))
    return JS_Call(ctx, st->on[event], st->underlying_sink, argc, argv);

  return JS_UNDEFINED;
}

/**
 * @brief      Constructs a JS Writable object
 *
 * @param      ctx         The JSContext
 * @param[in]  new_target  The constructor function
 * @param[in]  argc        Number of arguments
 * @param      argv        The arguments array
 *
 * @return     JS Writable object
 */
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

/**
 * @brief      Wraps a Writable struct in a JS Writable object
 *
 * @param      ctx   The JSContext
 * @param      st    A writable stream
 *
 * @return     JS Writable object
 */
JSValue
js_writable_wrap(JSContext* ctx, Writable* st) {
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
 * @return     JS Writable object
 */
JSValue
js_writable_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

/**
 * @brief      { function_description }
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  magic     Magic number
 *
 * @return     { return value }
 */
/*JSValue
js_writable_handler(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue method = JS_NewCFunctionMagic(ctx, js_writable_method, "handler", 0, JS_CFUNC_generic_magic, magic);
  JSValue handler = js_function_bind_this(ctx, method, this_val);

  JS_FreeValue(ctx, method);

  return handler;
}*/

/**
 * @brief      JS Writable object method function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the method
 */
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
  WRITABLE_PROP_CLOSED,
  WRITABLE_PROP_LOCKED,
};

/**
 * @brief      JS Writable object getter function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the getter
 */
JSValue
js_writable_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Writable* st;
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
 * @brief      JS Writable controller functions
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the controller function
 */
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

/**
 * @brief      JS Writable object finalizer
 *
 * @param      rt    The JSRuntime
 * @param[in]  val   The value
 */
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
    JS_CGETSET_MAGIC_FLAGS_DEF("locked", js_writable_get, 0, WRITABLE_PROP_LOCKED, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WritableStream", JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_writable_iterator),
};

const JSCFunctionListEntry js_writable_controller_funcs[] = {
    JS_CFUNC_MAGIC_DEF("error", 0, js_writable_controller, WRITABLE_ERROR),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WritableStreamDefaultController", JS_PROP_CONFIGURABLE),
};

/**
 * @brief      Duplicates a \ref Transform stream
 *
 * @param      st    A transform stream
 *
 * @return     The same Transform stream (with incremented reference count)
 */
static Transform*
transform_dup(Transform* st) {
  ++st->ref_count;

  return st;
}

/**
 * @brief      Creates a new \ref Transform stream
 *
 * @param      ctx   The JSContext
 *
 * @return     A new Transform stream
 */
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

/**
 * @brief      Terminates a \ref Transform stream
 *
 * @param      st    A transform stream
 * @param      ctx   The JSContext
 */
static void
transform_terminate(Transform* st, JSContext* ctx) {
  readable_close(st->readable, ctx);
  writable_abort(st->writable, JS_UNDEFINED, ctx);
}

/**
 * @brief      Signals an error on a \ref Transform stream
 *
 * @param      st     A transform stream
 * @param[in]  error  The error
 * @param      ctx    The JSContext
 */
static void
transform_error(Transform* st, JSValueConst error, JSContext* ctx) {
  readable_cancel(st->readable, error, ctx);
  writable_abort(st->writable, error, ctx);
}

/**
 * @brief      Constructs a JS Transform object
 *
 * @param      ctx         The JSContext
 * @param[in]  new_target  The constructor function
 * @param[in]  argc        Number of arguments
 * @param      argv        The arguments array
 *
 * @return     JS Transform object
 */
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

enum {
  TRANSFORM_PROP_READABLE = 0,
  TRANSFORM_PROP_WRITABLE,
};

/**
 * @brief      JS Transform object getter function
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the getter
 */
JSValue
js_transform_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Transform* st;
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
 * @brief      JS Transform controller functions
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 * @param[in]  argc      Number of arguments
 * @param      argv      The arguments array
 * @param[in]  magic     Magic number
 *
 * @return     Return value of the controller function
 */
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

/**
 * @brief      Returns the desired size of a JS Transform object
 *
 * @param      ctx       The JSContext
 * @param[in]  this_val  The this object
 *
 * @return     JS Number
 */
JSValue
js_transform_desired(JSContext* ctx, JSValueConst this_val) {
  Transform* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = js_transform_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(readable_locked(st->readable)) {
    Reader* rd;

    if((rd = st->readable->reader))
      ret = JS_NewUint32(ctx, rd->desired_size);
  }

  return ret;
}

/**
 * @brief      JS Transform object finalizer
 *
 * @param      rt    The JSRuntime
 * @param[in]  val   The value
 */
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

  if(!(m = JS_NewCModule(ctx, module_name, js_stream_init)))
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
