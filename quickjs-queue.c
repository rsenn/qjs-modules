#include "defines.h"
#include "queue.h"
#include "utils.h"
#include "buffer-utils.h"

/**
 * \defgroup quickjs-queue quickjs-queue: Queue reader
 * @{
 */
VISIBLE JSClassID js_queue_class_id = 0, js_queue_iterator_class_id = 0;
VISIBLE JSValue queue_proto, queue_ctor, queue_iterator_proto;

JSValue chunk_arraybuffer(Chunk* ch, JSContext* ctx);

static inline Queue*
js_queue_data(JSValueConst value) {
  return JS_GetOpaque(value, js_queue_class_id);
}

static inline Queue*
js_queue_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_queue_class_id);
}

static JSValue
js_queue_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Queue* queue;

  if(!(queue = js_malloc(ctx, sizeof(Queue))))
    return JS_ThrowOutOfMemory(ctx);

  queue_init(queue);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_queue_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, queue);

  return obj;

fail:
  js_free(ctx, queue);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  QUEUE_WRITE,
  QUEUE_READ,
  QUEUE_PEEK,
  QUEUE_SKIP,
  QUEUE_CLEAR,
  QUEUE_NEXT,
  QUEUE_CHUNK,
  QUEUE_AT,
};

static JSValue
js_queue_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Queue* queue;
  JSValue ret = JS_UNDEFINED;

  if(!(queue = js_queue_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case QUEUE_WRITE: {
      InputBuffer input = js_input_args(ctx, argc, argv);
      int64_t r = queue_write(queue, input_buffer_data(&input), input_buffer_length(&input));

      ret = JS_NewInt64(ctx, r);
      input_buffer_free(&input, ctx);

      break;
    }
    case QUEUE_READ: {
      InputBuffer input = js_input_args(ctx, argc, argv);
      int64_t r = queue_read(queue, input_buffer_data(&input), input_buffer_length(&input));

      ret = JS_NewInt64(ctx, r);
      input_buffer_free(&input, ctx);
      break;
    }

    case QUEUE_PEEK: {
      InputBuffer input = js_input_args(ctx, argc, argv);
      int64_t r = queue_peek(queue, input_buffer_data(&input), input_buffer_length(&input));

      ret = JS_NewInt64(ctx, r);
      input_buffer_free(&input, ctx);
      break;
    }

    case QUEUE_SKIP: {
      uint32_t n = 0;

      JS_ToUint32(ctx, &n, argv[0]);

      int64_t r = queue_skip(queue, n);

      ret = JS_NewInt64(ctx, r);
      break;
    }

    case QUEUE_CLEAR: {
      queue_clear(queue);
      break;
    }

    case QUEUE_NEXT: {
      Chunk* chunk;

      if((chunk = queue_next(queue)))
        ret = chunk_arraybuffer(chunk, ctx);
      else
        ret = JS_NULL;

      break;
    }

    case QUEUE_CHUNK: {
      Chunk* chunk;
      int64_t pos = -1;
      BOOL returnPos = FALSE;

      if(argc > 1)
        returnPos = JS_ToBool(ctx, argv[1]);

      JS_ToInt64(ctx, &pos, argv[0]);

      if((chunk = queue_chunk(queue, pos))) {
        if(returnPos)
          ret = JS_NewInt64(ctx, pos < 0 ? chunk_tailpos(chunk, queue) : chunk_headpos(chunk, queue));
        else
          ret = chunk_arraybuffer(chunk, ctx);
      } else
        ret = JS_NULL;

      break;
    }

    case QUEUE_AT: {
      Chunk* chunk;
      int64_t offset = -1;
      size_t skip = 0;

      JS_ToInt64(ctx, &offset, argv[0]);

      if((chunk = queue_at(queue, offset, &skip))) {
        ret = JS_NewArray(ctx);
        JS_SetPropertyUint32(ctx, ret, 0, chunk_arraybuffer(chunk, ctx));
        JS_SetPropertyUint32(ctx, ret, 1, JS_NewUint32(ctx, skip));
      } else
        ret = JS_NULL;

      break;
    }
  }

  return ret;
}

enum {
  QUEUE_SIZE,
  QUEUE_EMPTY,
  QUEUE_HEAD,
  QUEUE_TAIL,
  QUEUE_CHUNKS,
};

static JSValue
js_queue_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Queue* queue;
  JSValue ret = JS_UNDEFINED;

  if(!(queue = js_queue_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case QUEUE_SIZE: {
      ret = JS_NewInt64(ctx, queue_size(queue));
      break;
    }

    case QUEUE_EMPTY: {
      ret = JS_NewBool(ctx, queue_size(queue) == 0);
      break;
    }
    case QUEUE_HEAD: {
      Chunk* head;

      if((head = queue_head(queue)))
        ret = chunk_arraybuffer(head, ctx);

      break;
    }
    case QUEUE_TAIL: {
      Chunk* head;

      if((head = queue_tail(queue)))
        ret = chunk_arraybuffer(head, ctx);

      break;
    }
    case QUEUE_CHUNKS: {
      ret = JS_NewUint32(ctx, queue->nchunks);
      break;
    }
  }

  return ret;
}

static JSValue
js_queue_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_NewObjectProtoClass(ctx, queue_iterator_proto, js_queue_iterator_class_id);

  JS_DefinePropertyValueStr(ctx, ret, "queue", JS_DupValue(ctx, this_val), JS_PROP_CONFIGURABLE);

  return ret;
}

static void
js_queue_finalizer(JSRuntime* rt, JSValue val) {
  Queue* queue;

  if((queue = js_queue_data(val))) {
    queue_clear(queue);
    js_free_rt(rt, queue);
  }
}

static JSClassDef js_queue_class = {
    .class_name = "Queue",
    .finalizer = js_queue_finalizer,
};

static const JSCFunctionListEntry js_queue_funcs[] = {
    JS_CFUNC_MAGIC_DEF("write", 1, js_queue_method, QUEUE_WRITE),
    JS_CFUNC_MAGIC_DEF("read", 1, js_queue_method, QUEUE_READ),
    JS_CFUNC_MAGIC_DEF("peek", 1, js_queue_method, QUEUE_PEEK),
    JS_CFUNC_MAGIC_DEF("skip", 1, js_queue_method, QUEUE_SKIP),
    JS_CFUNC_MAGIC_DEF("clear", 0, js_queue_method, QUEUE_CLEAR),
    JS_CFUNC_MAGIC_DEF("next", 0, js_queue_method, QUEUE_NEXT),
    JS_CFUNC_MAGIC_DEF("chunk", 1, js_queue_method, QUEUE_CHUNK),
    JS_CFUNC_MAGIC_DEF("at", 1, js_queue_method, QUEUE_AT),
    JS_CGETSET_MAGIC_DEF("size", js_queue_get, 0, QUEUE_SIZE),
    JS_CGETSET_MAGIC_DEF("empty", js_queue_get, 0, QUEUE_EMPTY),
    JS_CGETSET_MAGIC_DEF("head", js_queue_get, 0, QUEUE_HEAD),
    JS_CGETSET_MAGIC_DEF("tail", js_queue_get, 0, QUEUE_TAIL),
    JS_CGETSET_MAGIC_DEF("chunks", js_queue_get, 0, QUEUE_CHUNKS),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_queue_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Queue", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_queue_iterator_class = {
    .class_name = "QueueIterator",
};

static JSValue
js_queue_iterator_next(JSContext* ctx, JSValueConst iter, int argc, JSValueConst argv[], BOOL* pdone, int magic) {

  Queue* queue;
  JSValue ret = JS_UNDEFINED, queue_obj = JS_GetPropertyStr(ctx, iter, "queue");

  if(!(queue = js_queue_data2(ctx, queue_obj)))
    return JS_EXCEPTION;

  Chunk* ch;

  *pdone = TRUE;

  if((ch = queue_next(queue))) {
    ret = chunk_arraybuffer(ch, ctx);
    *pdone = FALSE;
  }

  return ret;
}

static const JSCFunctionListEntry js_queue_iterator_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_queue_iterator_next, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "QueueIterator", JS_PROP_CONFIGURABLE),
};

int
js_queue_init(JSContext* ctx, JSModuleDef* m) {

  assert(js_queue_class_id == 0);

  JS_NewClassID(&js_queue_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_queue_class_id, &js_queue_class);

  queue_ctor = JS_NewCFunction2(ctx, js_queue_constructor, "Queue", 1, JS_CFUNC_constructor, 0);
  // JSValue generator_proto = js_generator_prototype(ctx);
  // queue_proto = JS_NewObjectProto(ctx, generator_proto);
  queue_proto = JS_NewObject(ctx);
  // JS_FreeValue(ctx, generator_proto);

  JS_SetPropertyFunctionList(ctx, queue_proto, js_queue_funcs, countof(js_queue_funcs));

  JS_SetClassProto(ctx, js_queue_class_id, queue_proto);
  JS_SetConstructor(ctx, queue_ctor, queue_proto);

  JS_NewClassID(&js_queue_iterator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_queue_iterator_class_id, &js_queue_iterator_class);

  queue_iterator_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, queue_iterator_proto, js_queue_iterator_funcs, countof(js_queue_iterator_funcs));

  JS_SetClassProto(ctx, js_queue_iterator_class_id, queue_iterator_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "Queue", queue_ctor);
  }

  return 0;
}

#ifdef JS_QUEUE_MODULE
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_queue
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_queue_init))) {
    JS_AddModuleExport(ctx, m, "Queue");
  }

  return m;
}

/**
 * @}
 */
