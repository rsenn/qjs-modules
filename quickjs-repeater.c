#include "utils.h"
#include <string.h>

/**
 * \defgroup quickjs-repeater QuickJS module: repeater - Async Iterator Repeater
 * @{
 */

static JSValue js_repeater_push(JSContext*, JSValueConst, int, JSValueConst[], int);
static JSValue js_repeater_stop(JSContext*, JSValueConst, int, JSValueConst[], int);

thread_local VISIBLE JSClassID js_repeater_class_id = 0;
thread_local JSValue repeater_proto = {{JS_TAG_UNDEFINED}}, repeater_ctor = {{JS_TAG_UNDEFINED}};

enum repeater_functions {
  STATIC_RACE = 0,
  STATIC_MERGE,
  STATIC_ZIP,
};
enum repeater_getters {
  PROP_LENGTH = 0,
  PROP_PATH,
};
enum repeater_state { REPEATER_INITIAL = 0, REPEATER_STARTED, REPEATER_STOPPED, REPEATER_DONE, REPEATER_REJECTED };

struct resolvable_item {
  JSValue resolve, value;
};

struct repeater_item {
  struct list_head link;
  struct resolvable_item resolvable;
};

typedef struct {
  int ref_count;
  JSValue executor, buffer, err;
  enum repeater_state state;
  struct list_head pushes, nexts;
  JSValue pending, execution;
  JSValue onnext, onstop;
} Repeater;

static void
repeater_decrement_refcount(void* opaque) {
  Repeater* rpt = opaque;

  --rpt->ref_count;
}

static JSValue
repeater_consume(Repeater* rpt, JSContext* ctx) {
  JSValue execution, tmp, ret = JS_UNDEFINED;
  JSValueConst args[2];

  tmp = js_promise_resolve(ctx, rpt->execution);
  JS_FreeValue(ctx, rpt->execution);

  execution = js_promise_then(ctx, tmp, js_function_throw(ctx, rpt->err));
  JS_FreeValue(ctx, tmp);

  args[0] = js_function_return_undefined(ctx);
  args[1] = JS_DupValue(ctx, args[0]);

  JS_FreeValue(ctx, rpt->err);
  rpt->err = JS_UNDEFINED;
  rpt->execution = js_invoke(ctx, execution, "then", 2, args);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);

  if(JS_IsUndefined(rpt->pending))
    ret = JS_DupValue(ctx, rpt->execution);
  else
    ret = js_promise_then(ctx, rpt->pending, js_function_return_value(ctx, execution));
  JS_FreeValue(ctx, execution);
  return ret;
}

static JSValue
resolvable_value(JSContext* ctx, JSValueConst value, struct resolvable_item* rsva) {
  JSValue ret, resolving_funcs[2];
  ret = JS_NewPromiseCapability(ctx, resolving_funcs);
  rsva->resolve = resolving_funcs[0];
  rsva->value = JS_DupValue(ctx, value);
  JS_FreeValue(ctx, resolving_funcs[1]);
  return ret;
}

static JSValue
resolvable_resolve(JSContext* ctx, struct resolvable_item* rsva, JSValueConst value) {
  JSValue result;
  result = JS_Call(ctx, rsva->resolve, JS_UNDEFINED, 1, &value);
  JS_FreeValue(ctx, result);
  return JS_DupValue(ctx, rsva->value);
}

static void
resolvable_free(JSContext* ctx, struct resolvable_item* rsva) {
  JS_FreeValue(ctx, rsva->value);
  JS_FreeValue(ctx, rsva->resolve);
}

static struct repeater_item*
repeater_item(JSContext* ctx) {
  struct repeater_item* item;
  if((item = js_mallocz(ctx, sizeof(struct repeater_item)))) {
    item->resolvable.resolve = JS_UNDEFINED;
    item->resolvable.value = JS_UNDEFINED;
  }
  return item;
}
static void
queue_free(JSContext* ctx, struct repeater_item* item) {
  js_free(ctx, item);
}

static int
queue_add(struct list_head* q, struct repeater_item* item) {
  list_add_tail(&item->link, q);
}

static int
queue_length(struct list_head* q) {
  size_t i = 0;
  struct list_head* el;
  list_for_each(el, q) { ++i; }
  return i;
}

static int
queue_remove(struct repeater_item* item) {
  list_del(&item->link);
}

static struct repeater_item*
queue_shift(struct list_head* q) {
  struct list_head* el = 0;
  if(list_empty(q))
    return 0;
  list_for_each(el, q) { break; }
  list_del(el);
  return list_entry(el, struct repeater_item, link);
}

static JSValue
js_repeater_execute(JSContext* ctx, JSValueConst this_val) {
  JSValue ret, args[2];
  Repeater* rpt;

  if(!(rpt = JS_GetOpaque2(ctx, this_val, js_repeater_class_id)))
    return JS_EXCEPTION;

  rpt->ref_count += 2;

  JSValue push = JS_NewCFunction(ctx, js_repeater_push, "push", 1);
  JSValue stop = JS_NewCFunction(ctx, js_repeater_stop, "stop", 0);

  args[0] = js_function_bind_this(ctx, push, this_val);
  args[1] = js_function_bind_this(ctx, stop, this_val);

  rpt->state = REPEATER_STARTED;

  ret = JS_Call(ctx, rpt->executor, JS_UNDEFINED, 2, args);

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);

  JS_FreeValue(ctx, push);
  JS_FreeValue(ctx, stop);

  return ret;
}

static JSValue
js_repeater_iteration(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValueConst data[]) {
  Repeater* rpt;
  JSValue ret = JS_UNDEFINED;
  JSValueConst value = argc >= 1 ? argv[0] : JS_UNDEFINED;

  if(!(rpt = JS_GetOpaque2(ctx, data[0], js_repeater_class_id)))
    return JS_EXCEPTION;

  BOOL done = JS_ToBool(ctx, data[1]);
  BOOL rejected = rpt->state >= REPEATER_REJECTED;

  if(!done && rejected) {
    JSValue execution = repeater_consume(rpt, ctx);
    JSValue then_done = js_iterator_then(ctx, TRUE);

    ret = js_promise_then(ctx, execution, then_done);
    JS_FreeValue(ctx, execution);
    JS_FreeValue(ctx, then_done);
  } else {
    ret = js_iterator_result(ctx, value, done);
  }
  return ret;
}

static JSValue
js_repeater_create_iteration(JSContext* ctx, JSValueConst this_val, JSValueConst value) {
  Repeater* rpt;
  JSValue ret = JS_UNDEFINED, iteration_fn;

  if(!(rpt = JS_GetOpaque2(ctx, this_val, js_repeater_class_id)))
    return JS_EXCEPTION;

  JSValueConst data[2] = {JS_DupValue(ctx, this_val), JS_NewBool(ctx, rpt->state >= REPEATER_DONE)};
  iteration_fn = JS_NewCFunctionData(ctx, js_repeater_iteration, 1, 0, 2, data);

  ret = js_promise_resolve_then(ctx, value, iteration_fn);

  JS_FreeValue(ctx, iteration_fn);
  return ret;
}

static JSValue
js_repeater_push(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Repeater* rpt;
  struct repeater_item* item;
  JSValue ret = JS_UNDEFINED;
  JSValueConst value = argc >= 1 ? argv[0] : JS_UNDEFINED;

  if(!(rpt = JS_GetOpaque2(ctx, this_val, js_repeater_class_id)))
    return JS_EXCEPTION;

  const char* v = JS_ToCString(ctx, value);
  printf("js_repeater_push value=%s pushes=%d nexts=%d\n", v, queue_length(&rpt->pushes), queue_length(&rpt->nexts));
  JS_FreeCString(ctx, v);

  if((item = queue_shift(&rpt->nexts))) {
    ret = resolvable_resolve(ctx, &item->resolvable, value);
    resolvable_free(ctx, &item->resolvable);
    queue_free(ctx, item);
  } else {
    if(!(item = repeater_item(ctx)))
      return JS_ThrowOutOfMemory(ctx);
    ret = resolvable_value(ctx, value, &item->resolvable);
    queue_add(&rpt->pushes, item);
  }

  return ret;
}

static JSValue
js_repeater_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Repeater* rpt;

  if(!(rpt = JS_GetOpaque2(ctx, this_val, js_repeater_class_id)))
    return JS_EXCEPTION;

  printf("js_repeater_stop\n");
  rpt->state = REPEATER_STOPPED;
  return JS_UNDEFINED;
}

JSValue
js_repeater_new(JSContext* ctx, JSValueConst proto, JSValueConst executor) {
  Repeater* rpt;
  JSValue obj = JS_UNDEFINED;
  if(!(rpt = js_mallocz(ctx, sizeof(Repeater))))
    return JS_EXCEPTION;

  obj = JS_NewObjectProtoClass(ctx, proto, js_repeater_class_id);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, rpt);

  rpt->ref_count = 1;
  rpt->executor = JS_DupValue(ctx, executor);
  rpt->buffer = JS_UNDEFINED;
  rpt->err = JS_UNDEFINED;
  rpt->state = REPEATER_INITIAL;

  init_list_head(&rpt->pushes);
  init_list_head(&rpt->nexts);

  if(JS_IsFunction(ctx, rpt->executor) && rpt->state <= REPEATER_INITIAL)
    rpt->execution = js_repeater_execute(ctx, obj);

  return obj;
fail:
  js_free(ctx, rpt);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_repeater_wrap(JSContext* ctx, Repeater* rpt) {
  JSValue obj;
  obj = JS_NewObjectProtoClass(ctx, repeater_proto, js_repeater_class_id);
  JS_SetOpaque(obj, rpt);
  return obj;
}

static JSValue
js_repeater_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto;
  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, repeater_proto);

  return js_repeater_new(ctx, proto, argv[0]);
}

static JSValue
js_repeater_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  Repeater* rpt;
  struct repeater_item* item;
  JSValue ret = JS_UNDEFINED;
  JSValue value = argc >= 1 ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;

  if(!(rpt = JS_GetOpaque2(ctx, this_val, js_repeater_class_id)))
    return JS_EXCEPTION;

  if(JS_IsFunction(ctx, rpt->executor) && rpt->state <= REPEATER_INITIAL)
    rpt->execution = js_repeater_execute(ctx, this_val);

  if(JS_IsFunction(ctx, rpt->onnext)) {
    JSValue result = JS_Call(ctx, rpt->onnext, this_val, argc, argv);
    JS_FreeValue(ctx, result);
  }

  *pdone = rpt->state >= REPEATER_STOPPED;

  printf("js_repeater_next done=%d pushes=%d nexts=%d\n", *pdone, queue_length(&rpt->pushes), queue_length(&rpt->nexts));

  if(rpt->state >= REPEATER_STOPPED) {

  } else if((item = queue_shift(&rpt->pushes))) {

    ret = js_repeater_create_iteration(ctx, this_val, resolvable_resolve(ctx, &item->resolvable, value));
    //    ret = js_iterator_result(ctx, item->resolvable.value, FALSE);

  } else {
    if(!(item = repeater_item(ctx)))
      return JS_ThrowOutOfMemory(ctx);

    ret = resolvable_value(ctx, value, &item->resolvable);
    queue_add(&rpt->nexts, item);
  }
  JS_FreeValue(ctx, value);

  return ret;
}

static JSValue
js_repeater_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

static JSValue
js_repeater_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Repeater* rpt;
  JSValue ret = JS_UNDEFINED;

  if(!(rpt = JS_GetOpaque2(ctx, this_val, js_repeater_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case STATIC_RACE: break;
    case STATIC_MERGE: break;
    case STATIC_ZIP: break;
  }
  return ret;
}

static void
repeater_finalizer(JSRuntime* rt, Repeater* rpt) {
  if(--rpt->ref_count == 0) {
    JS_FreeValueRT(rt, rpt->executor);
    JS_FreeValueRT(rt, rpt->buffer);
    JS_FreeValueRT(rt, rpt->err);

    js_free_rt(rt, rpt);
  }
}

static void
js_repeater_finalizer(JSRuntime* rt, JSValue val) {
  Repeater* rpt;

  if((rpt = JS_GetOpaque(val, js_repeater_class_id)))
    repeater_finalizer(rt, rpt);

  JS_FreeValueRT(rt, val);
}
static JSClassDef js_repeater_class = {
    .class_name = "Repeater",
    .finalizer = js_repeater_finalizer,
};

static const JSCFunctionListEntry js_repeater_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_repeater_next, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Repeater", JS_PROP_C_W_E),
    JS_CFUNC_DEF("[Symbol.asyncIterator]", 0, js_repeater_iterator),
};

static const JSCFunctionListEntry js_repeater_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("race", 1, js_repeater_funcs, STATIC_RACE),
    JS_CFUNC_MAGIC_DEF("merge", 1, js_repeater_funcs, STATIC_MERGE),
    JS_CFUNC_MAGIC_DEF("zip", 1, js_repeater_funcs, STATIC_ZIP),
};

static int
js_repeater_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_repeater_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_repeater_class_id, &js_repeater_class);

  repeater_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, repeater_proto, js_repeater_proto_funcs, countof(js_repeater_proto_funcs));
  JS_SetClassProto(ctx, js_repeater_class_id, repeater_proto);

  repeater_ctor = JS_NewCFunction2(ctx, js_repeater_constructor, "Repeater", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, repeater_ctor, repeater_proto);
  JS_SetPropertyFunctionList(ctx, repeater_ctor, js_repeater_static_funcs, countof(js_repeater_static_funcs));

  if(m) {
    JS_SetModuleExport(ctx, m, "Repeater", repeater_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_repeater
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, &js_repeater_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "Repeater");
  return m;
}

/**
 * @}
 */
