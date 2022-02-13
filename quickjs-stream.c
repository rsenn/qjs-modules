#include "quickjs-stream.h"
#include "buffer-utils.h"
#include "debug.h"
#include <assert.h>

/**
 * \defgroup quickjs-stream QuickJS module: stream - Buffered stream
 * @{
 */

thread_local VISIBLE JSClassID js_stream_class_id = 0, js_reader_class_id = 0, js_writer_class_id = 0;
thread_local JSValue stream_proto = {{JS_TAG_UNDEFINED}}, stream_ctor = {{JS_TAG_UNDEFINED}}, reader_proto = {{JS_TAG_UNDEFINED}},
                     reader_ctor = {{JS_TAG_UNDEFINED}}, writer_proto = {{JS_TAG_UNDEFINED}}, writer_ctor = {{JS_TAG_UNDEFINED}};

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
reader_new(JSContext* ctx, Stream* st) {
  Reader* rd;

  if((rd = js_mallocz(ctx, sizeof(Reader)))) {
    atomic_store(&rd->stream, st);

    promise_init(&rd->closed, ctx);
    promise_zero(&rd->cancelled);
    promise_zero(&rd->read);
  }

  return rd;
}

BOOL
reader_release_lock(Reader* rd, JSContext* ctx) {
  BOOL ret;

  if((ret = stream_unlock_rd(&rd->stream, rd))) {
    atomic_store(&rd->stream, (Stream*)0);
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

BOOL
reader_read(Reader* rd, JSContext* ctx) {
  BOOL ret = FALSE;

  if(JS_IsUndefined(rd->read.promise)) {
    ret = promise_init(&rd->read, ctx);
  }

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

Writer*
writer_new(JSContext* ctx, Stream* st) {
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

  if((ret = stream_unlock_rd(&wr->stream, wr))) {
    atomic_store(&wr->stream, (Stream*)0);
  }

  return ret;
}

BOOL
writer_close(Writer* wr, JSContext* ctx) {
  BOOL ret = FALSE;

  if(JS_IsUndefined(wr->closed.promise)) {
    ret = promise_init(&wr->closed, ctx);
  }

  return ret;
}

JSValue
writer_write(Writer* wr, const MemoryBlock* block, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  ssize_t bytes;
  /*  Chunk* chunk;

    if(JS_IsUndefined(wr->write.promise)) {
      ret = promise_init(&wr->write , ctx);
    }
  */
  if((bytes = queue_write(&wr->q, block->base, block->size)) == block->size) {
    Chunk* chunk = queue_tail(&wr->q);

    chunk->opaque = promise_new(ctx, &ret);
  }

  return ret;
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

Stream*
stream_new(JSContext* ctx) {
  Stream* st;

  if((st = js_mallocz(ctx, sizeof(Stream)))) {
    st->ref_count = 1;
    queue_init(&st->q);
  }

  return st;
}

size_t
stream_length(Stream* st) {
  return queue_size(&st->q);
}

void
stream_unref(void* opaque) {
  Stream* st = opaque;

  --st->ref_count;
}

JSValue
stream_next(Stream* st, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  Chunk* chunk;

  if((chunk = queue_next(&st->q))) {
    ret = chunk_arraybuf(chunk, ctx);
  }

  return ret;
}

int
stream_at(Stream* st, int64_t offset) {
  size_t length = stream_length(st);
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

int
stream_lock_rd(Stream* st, Reader* rd) {
  const Reader* expected = 0;
  return atomic_compare_exchange_weak(&st->reader, &expected, rd);
}

int
stream_unlock_rd(Stream* st, Reader* rd) {
  return atomic_compare_exchange_weak(&st->reader, &rd, 0);
}

Reader*
stream_get_reader(Stream* st, JSContext* ctx) {
  Reader* rd;

  if(!(rd = reader_new(ctx, st)))
    return 0;

  if(!stream_lock_rd(st, rd)) {
    js_free(ctx, rd);
    rd = 0;
  }

  return rd;
}

int
stream_lock_wr(Stream* st, Writer* wr) {
  const Reader* expected = 0;
  return atomic_compare_exchange_weak(&st->writer, &expected, wr);
}

int
stream_unlock_wr(Stream* st, Writer* wr) {
  return atomic_compare_exchange_weak(&st->writer, &wr, 0);
}

Writer*
stream_get_writer(Stream* st, size_t desired_size, JSContext* ctx) {
  Writer* wr;
  if(!(wr = writer_new(ctx, st)))
    return 0;
  wr->desired_size = desired_size;
  if(!stream_lock_wr(st, wr)) {
    js_free(ctx, wr);
    wr = 0;
  }
  return wr;
}

enum {
  FUNC_PEEK,

};

static JSValue
js_reader_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Reader* rd;
  Stream* st;

  if(argc < 1 || !(st = js_stream_data(argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a Stream");

  if(!(rd = reader_new(ctx, st)))
    return JS_ThrowOutOfMemory(ctx);

  if(!stream_lock_rd(st, rd)) {
    JS_ThrowInternalError(ctx, "unable to lock Stream");
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
  Stream* st;
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
js_reader_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
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
      reader_read(rd, ctx);
      ret = JS_DupValue(ctx, rd->read.promise);
      break;
    }
    case READER_RELEASE_LOCK: {
      reader_release_lock(rd, ctx);
      break;
    }
  }

  return ret;
}

enum { READER_CLOSED };
enum { PROP_READY, PROP_CLOSED, PROP_LOCKED, PROP_STATE, PROP_SIZE, PROP_EMPTY };

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
    JS_CFUNC_MAGIC_DEF("read", 0, js_reader_methods, READER_READ),
    // JS_CFUNC_MAGIC_DEF("peek", 1, js_reader_read, READER_PEEK),
    JS_CFUNC_MAGIC_DEF("cancel", 0, js_reader_methods, READER_CANCEL),
    JS_CFUNC_MAGIC_DEF("releaseLock", 0, js_reader_methods, READER_RELEASE_LOCK),
    JS_CGETSET_MAGIC_DEF("closed", js_reader_get, 0, READER_CLOSED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StreamReader", JS_PROP_C_W_E),
};

static JSValue
js_writer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Writer* wr;
  Stream* st;

  if(argc < 1 || !(st = js_stream_data(argv[0])))
    return JS_ThrowTypeError(ctx, "argument 1 must be a Stream");

  if(!(wr = writer_new(ctx, st)))
    return JS_ThrowOutOfMemory(ctx);

  if(!stream_lock_wr(st, wr)) {
    JS_ThrowInternalError(ctx, "unable to lock Stream");
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
js_writer_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  Writer* wr;

  if(!(wr = js_writer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case WRITER_ABORT: {
      writer_abort(wr, ctx);

      break;
    }
    case WRITER_CLOSE: {
      writer_abort(wr, ctx);

      break;
    }
    case WRITER_WRITE: {

      MemoryBlock b;
      InputBuffer input;

      input = js_input_args(ctx, argc, argv);
      b = block_range(input_buffer_blockptr(&input), &input.range);

      ret = writer_write(wr, &b, ctx);

      input_buffer_free(&input, ctx);

      /*    if(ret < 0)
            return JS_ThrowInternalError(ctx, "Error writing %zu bytes to queue", b.size);

          ret = JS_NewInt64(ctx, ret);*/
      break;
    }
    case WRITER_RELEASE_LOCK: {
      writer_release_lock(wr, ctx);
      break;
    }
  }

  return ret;
}

enum { WRITER_CLOSED, WRITER_READY };

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
    JS_CFUNC_MAGIC_DEF("write", 0, js_writer_methods, WRITER_WRITE),
    JS_CFUNC_MAGIC_DEF("abort", 0, js_writer_methods, WRITER_ABORT),
    JS_CFUNC_MAGIC_DEF("close", 0, js_writer_methods, WRITER_CLOSE),
    JS_CFUNC_MAGIC_DEF("releaseLock", 0, js_writer_methods, WRITER_RELEASE_LOCK),
    JS_CGETSET_MAGIC_DEF("closed", js_writer_get, 0, WRITER_CLOSED),
    JS_CGETSET_MAGIC_DEF("ready", js_writer_get, 0, WRITER_READY),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "StreamWriter", JS_PROP_C_W_E),
};

static JSValue
js_stream_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Stream* st;
  MemoryBlock b;
  InputBuffer input;
  ssize_t ret;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_stream_class_id)))
    return JS_EXCEPTION;

  input = js_input_args(ctx, argc, argv);
  b = block_range(input_buffer_blockptr(&input), &input.range);

  ret = queue_write(&st->q, b.base, b.size);

  input_buffer_free(&input, ctx);

  if(ret < 0)
    return JS_ThrowInternalError(ctx, "Error writing %zu bytes to queue", b.size);

  return JS_NewInt64(ctx, ret);
}

static JSValue
js_stream_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Stream* st;
  MemoryBlock b;
  InputBuffer output;
  ssize_t ret;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_stream_class_id)))
    return JS_EXCEPTION;

  output = js_output_args(ctx, argc, argv);
  b = block_range(input_buffer_blockptr(&output), &output.range);

  ret = magic ? queue_peek(&st->q, b.base, b.size) : queue_read(&st->q, b.base, b.size);

  input_buffer_free(&output, ctx);

  return JS_NewInt64(ctx, ret);
}

JSValue
js_stream_new(JSContext* ctx, JSValueConst proto) {
  Stream* st;
  JSValue obj = JS_UNDEFINED;

  if(!(st = stream_new(ctx)))
    return JS_EXCEPTION;

  obj = JS_NewObjectProtoClass(ctx, proto, js_stream_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, st);

  return obj;
fail:
  js_free(ctx, st);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_stream_wrap(JSContext* ctx, Stream* st) {
  JSValue obj;
  obj = JS_NewObjectProtoClass(ctx, stream_proto, js_stream_class_id);
  JS_SetOpaque(obj, st);
  return obj;
}

static JSValue
js_stream_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, stream_proto);

  return js_stream_new(ctx, proto);
}

static JSValue
js_stream_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  Stream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_stream_class_id)))
    return JS_EXCEPTION;

  *pdone = queue_empty(&st->q);

  if(!*pdone)
    ret = stream_next(st, ctx);

  return ret;
}

static JSValue
js_stream_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

enum {
  FUNC_CLEAR = 0,
  FUNC_GET_READER,
};

static JSValue
js_stream_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Stream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_stream_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case FUNC_CLEAR: {
      queue_clear(&st->q);
      break;
    }
    case FUNC_GET_READER: {
      Reader* rd;

      if((rd = stream_get_reader(st, ctx)))
        ret = js_reader_wrap(ctx, rd);
      break;
    }
  }

  return ret;
}

static JSValue
js_stream_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Stream* st;
  JSValue ret = JS_UNDEFINED;

  if(!(st = JS_GetOpaque2(ctx, this_val, js_stream_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_READY: ret = JS_NewBool(ctx, !st->closed); break;
    case PROP_CLOSED: ret = JS_NewBool(ctx, st->closed); break;
    case PROP_LOCKED: ret = JS_NewBool(ctx, st->reader); break;
    case PROP_SIZE: ret = JS_NewInt64(ctx, queue_size(&st->q)); break;
    case PROP_EMPTY: ret = JS_NewBool(ctx, queue_size(&st->q) == 0); break;
  }
  return ret;
}

static void
stream_finalizer(JSRuntime* rt, Stream* st) {
  if(--st->ref_count == 0) {
    queue_clear(&st->q);

    js_free_rt(rt, st);
  }
}

static void
js_stream_finalizer(JSRuntime* rt, JSValue val) {
  Stream* st;

  if((st = JS_GetOpaque(val, js_stream_class_id)))
    stream_finalizer(rt, st);

  // JS_FreeValueRT(rt, val);
}

static int
js_stream_get_own_property(JSContext* ctx, JSPropertyDescriptor* pdesc, JSValueConst obj, JSAtom prop) {
  Stream* st = js_stream_data2(ctx, obj);
  JSValue value = JS_UNDEFINED;
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    size_t length = stream_length(st);

    if(index < 0 && ABS_NUM(index) < (int64_t)length)
      index = MOD_NUM(index, (int64_t)length);

    if(index >= 0 && index < (int64_t)length)
      value = JS_NewInt32(ctx, stream_at(st, index));

  } else if(js_atom_is_string(ctx, prop, "size")) {
    value = JS_NewInt64(ctx, stream_length(st));
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
js_stream_has_property(JSContext* ctx, JSValueConst obj, JSAtom prop) {
  Stream* st = js_stream_data2(ctx, obj);
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    size_t length = stream_length(st);

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
js_stream_get_property(JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst receiver) {
  Stream* st = js_stream_data2(ctx, obj);
  JSValue value = JS_UNDEFINED;
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    size_t length = stream_length(st);

    if(index < 0 && ABS_NUM(index) < (int64_t)length)
      index = MOD_NUM(index, (int64_t)length);

    if(index >= 0 && index < (int64_t)length)
      value = JS_NewUint32(ctx, stream_at(st, index));

  } else if(js_atom_is_string(ctx, prop, "size")) {
    value = JS_NewInt64(ctx, stream_length(st));

  } else {
    JSValue proto = JS_IsUndefined(stream_proto) ? JS_GetPrototype(ctx, obj) : stream_proto;
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

static JSClassExoticMethods js_stream_exotic_methods = {
    .has_property = js_stream_has_property,
    .get_property = js_stream_get_property,
    .get_own_property = js_stream_get_own_property,

};

static JSClassDef js_stream_class = {
    .class_name = "Stream",
    .finalizer = js_stream_finalizer,
    .exotic = &js_stream_exotic_methods,
};

static const JSCFunctionListEntry js_stream_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_stream_next, 0),
    JS_CFUNC_DEF("write", 1, js_stream_write),
    JS_CFUNC_MAGIC_DEF("read", 1, js_stream_read, 0),
    JS_CFUNC_MAGIC_DEF("peek", 1, js_stream_read, 1),
    JS_CFUNC_MAGIC_DEF("clear", 0, js_stream_funcs, FUNC_CLEAR),
    JS_CFUNC_MAGIC_DEF("getReader", 0, js_stream_funcs, FUNC_GET_READER),
    JS_CGETSET_MAGIC_FLAGS_DEF("ready", js_stream_get, 0, PROP_READY, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("closed", js_stream_get, 0, PROP_CLOSED, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("locked", js_stream_get, 0, PROP_LOCKED, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("state", js_stream_get, 0, PROP_STATE, JS_PROP_CONFIGURABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("empty", js_stream_get, 0, PROP_EMPTY, JS_PROP_CONFIGURABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("size", js_stream_get, 0, PROP_SIZE, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Stream", JS_PROP_C_W_E),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_stream_iterator),
};

static const JSCFunctionListEntry js_stream_static_funcs[] = {
    JS_PROP_INT32_DEF("READY", STREAM_READY, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("CLOSED", STREAM_CLOSED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("LOCKED", STREAM_LOCKED, JS_PROP_ENUMERABLE),
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

  JS_NewClassID(&js_stream_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stream_class_id, &js_stream_class);

  stream_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, stream_proto, js_stream_proto_funcs, countof(js_stream_proto_funcs));
  JS_SetClassProto(ctx, js_stream_class_id, stream_proto);

  stream_ctor = JS_NewCFunction2(ctx, js_stream_constructor, "Stream", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, stream_ctor, stream_proto);
  JS_SetPropertyFunctionList(ctx, stream_ctor, js_stream_static_funcs, countof(js_stream_static_funcs));

  if(m) {
    JS_SetModuleExport(ctx, m, "StreamReader", reader_ctor);
    JS_SetModuleExport(ctx, m, "Stream", stream_ctor);
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
  JS_AddModuleExport(ctx, m, "Stream");
  return m;
}

/**
 * @}
 */
