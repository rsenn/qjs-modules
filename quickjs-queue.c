#include "defines.h"
#include "queue.h"
#include "utils.h"
#include "buffer-utils.h"

/**
 * \defgroup quickjs-queue quickjs-queue: Queue reader
 * @{
 */
VISIBLE JSClassID js_queue_class_id = 0;
VISIBLE JSValue queue_proto = {{0}, JS_TAG_UNDEFINED}, queue_ctor = {{0}, JS_TAG_UNDEFINED};

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
  }

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
    JS_CFUNC_MAGIC_DEF("write", 1, js_queue_funcs, QUEUE_WRITE),
    JS_CFUNC_MAGIC_DEF("read", 1, js_queue_funcs, QUEUE_READ),
    JS_CFUNC_MAGIC_DEF("peek", 1, js_queue_funcs, QUEUE_PEEK),
    JS_CFUNC_MAGIC_DEF("skip", 1, js_queue_funcs, QUEUE_SKIP),
    JS_CFUNC_MAGIC_DEF("clear", 0, js_queue_funcs, QUEUE_CLEAR),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Queue", JS_PROP_CONFIGURABLE),
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
