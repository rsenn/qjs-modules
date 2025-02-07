#include "defines.h"
#include "utils.h"
#include <string.h>
#include "debug.h"

/**
 * \defgroup quickjs-repeater quickjs-repeater: Async Iterator Repeater
 * @{
 */

static JSValue js_repeater_push(JSContext*, JSValueConst, int, JSValueConst[]);
static JSValue js_repeater_stop(JSContext*, JSValueConst, int, JSValueConst[]);

VISIBLE JSClassID js_repeater_class_id = 0;
VISIBLE JSValue repeater_proto = {{0}, JS_TAG_UNDEFINED}, repeater_ctor = {{0}, JS_TAG_UNDEFINED};

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
  union {
    struct {
      struct block *prev, *next;
    };
    struct list_head link;
  };
  struct resolvable_item resolvable;
};

typedef struct repeater_object {
  int ref_count;
  JSValue executor, buffer, err;
  enum repeater_state state;
  struct list_head pushes, nexts;
  JSValue pending, execution;
  JSValue onnext, onstop;
} Repeater;

Repeater*
repeater_new(JSContext* ctx, JSValueConst executor) {
  Repeater* rpt;

  if((rpt = js_mallocz(ctx, sizeof(Repeater)))) {
    rpt->ref_count = 1;
    rpt->executor = JS_DupValue(ctx, executor);
    rpt->buffer = JS_UNDEFINED;
    rpt->err = JS_UNDEFINED;
    rpt->state = REPEATER_INITIAL;
    rpt->pending = JS_UNDEFINED;
    rpt->execution = JS_UNDEFINED;
    rpt->onnext = JS_UNDEFINED;
    rpt->onstop = JS_UNDEFINED;

    init_list_head(&rpt->pushes);
    init_list_head(&rpt->nexts);
  }

  return rpt;
}

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
  JSValue result = JS_Call(ctx, rsva->resolve, JS_UNDEFINED, 1, &value);
  JS_FreeValue(ctx, result);

  return JS_DupValue(ctx, rsva->value);
}

static void
resolvable_free(JSContext* ctx, struct resolvable_item* rsva) {
  JS_FreeValue(ctx, rsva->value);
  JS_FreeValue(ctx, rsva->resolve);
}

static struct repeater_item*
queue_alloc(JSContext* ctx) {
  struct repeater_item* item;

  if((item = js_mallocz(ctx, sizeof(struct repeater_item)))) {
    item->resolvable.resolve = JS_UNDEFINED;
    item->resolvable.value = JS_UNDEFINED;
  }

  return item;
}

static void
queue_free(JSContext* ctx, struct repeater_item* item) {
  resolvable_free(ctx, &item->resolvable);
  js_free(ctx, item);
}

static void
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

static void
queue_remove(struct repeater_item* item) {
  list_del(&item->link);
}

static struct repeater_item*
queue_head(struct list_head* q) {
  if(list_empty(q))
    return 0;

  return list_entry(q->next, struct repeater_item, link);
}

static struct repeater_item*
queue_shift(struct list_head* q) {
  struct repeater_item* item;

  if((item = queue_head(q)))
    list_del(&item->link);

  return item;
}

static JSValue
get_iterators(JSContext* ctx, JSValueConst arg) {
  JSValue ret, *items;
  size_t n_items, i, j = 0;

  if(!(items = js_values_fromarray(ctx, &n_items, arg)))
    return JS_EXCEPTION;

  ret = JS_NewArray(ctx);

  for(i = 0; i < n_items; i++) {
    JSValue meth = js_iterator_method(ctx, items[i]);

    if(JS_IsFunction(ctx, meth)) {
      JSValue tmp = JS_Call(ctx, meth, items[i], 0, 0);

      JS_SetPropertyUint32(ctx, ret, j++, tmp);
    } else {
    }

    JS_FreeValue(ctx, meth);
  }
  return ret;
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
  Repeater* rpt = JS_GetOpaque(data[0], js_repeater_class_id);
  JSValue ret = JS_UNDEFINED;
  JSValueConst value = argc >= 1 ? argv[0] : JS_UNDEFINED;

  bool done = JS_ToBool(ctx, data[1]);
  bool rejected = rpt->state >= REPEATER_REJECTED;

  if(!done && rejected) {
    JSValue execution = repeater_consume(rpt, ctx);
    JSValue then_done = js_iterator_then(ctx, true);

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
  Repeater* rpt = JS_GetOpaque(this_val, js_repeater_class_id);
  JSValue ret = JS_UNDEFINED, iteration_fn;

  JSValueConst data[2] = {JS_DupValue(ctx, this_val), JS_NewBool(ctx, rpt->state >= REPEATER_DONE)};
  iteration_fn = JS_NewCFunctionData(ctx, js_repeater_iteration, 1, 0, 2, data);

  JSValue promise = js_is_promise(ctx, value) ? JS_DupValue(ctx, value) : js_promise_resolve(ctx, value);

  // ret = js_promise_resolve(ctx, promise);
  ret = js_promise_resolve_then(ctx, promise, iteration_fn);

  JS_FreeValue(ctx, promise);
  JS_FreeValue(ctx, iteration_fn);

  return ret;
}

static JSValue
js_repeater_push(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Repeater* rpt;
  struct repeater_item* item;
  JSValue ret = JS_UNDEFINED;
  JSValueConst value = argc >= 1 ? argv[0] : JS_UNDEFINED;

  if(!(rpt = JS_GetOpaque2(ctx, this_val, js_repeater_class_id)))
    return JS_EXCEPTION;

  const char* v = JS_ToCString(ctx, value);

  JS_FreeCString(ctx, v);

  if((item = queue_shift(&rpt->nexts))) {
    ret = resolvable_resolve(ctx, &item->resolvable, value);
    queue_free(ctx, item);
  } else {
    if(!(item = queue_alloc(ctx)))
      return JS_EXCEPTION;

    ret = resolvable_value(ctx, value, &item->resolvable);
    queue_add(&rpt->pushes, item);
  }

  return ret;
}

static JSValue
js_repeater_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Repeater* rpt;
  JSValue ret = JS_UNDEFINED;
  struct list_head *el, *next;

  if(!(rpt = JS_GetOpaque2(ctx, this_val, js_repeater_class_id)))
    return JS_EXCEPTION;

  if(rpt->state >= REPEATER_STOPPED)
    return JS_UNDEFINED;

  rpt->state = REPEATER_STOPPED;

  if(JS_IsFunction(ctx, rpt->onnext))
    JS_Call(ctx, rpt->onnext, this_val, 0, 0);

  if(JS_IsFunction(ctx, rpt->onstop))
    JS_Call(ctx, rpt->onstop, this_val, 0, 0);

  if(js_is_null_or_undefined(rpt->err))
    rpt->err = argc >= 1 ? JS_DupValue(ctx, argv[0]) : JS_NewBool(ctx, true);

  list_for_each_safe(el, next, &rpt->pushes) {
    struct repeater_item* item = list_entry(el, struct repeater_item, link);

    JS_FreeValue(ctx, resolvable_resolve(ctx, &item->resolvable, JS_UNDEFINED));
    list_del(&item->link);
    queue_free(ctx, item);
  }

  return ret;
}

static void
js_repeater_finish(JSContext* ctx, JSValueConst this_val) {
  Repeater* rpt = JS_GetOpaque(this_val, js_repeater_class_id);
  struct list_head *el, *el1;

  assert(rpt);

  if(rpt->state >= REPEATER_DONE)
    return;

  if(rpt->state < REPEATER_STOPPED)
    js_repeater_stop(ctx, this_val, 0, 0);

  rpt->state = REPEATER_DONE;

  list_for_each_safe(el, el1, &rpt->nexts) {
    struct repeater_item* next = list_entry(el, struct repeater_item, link);
    JSValue value = resolvable_resolve(ctx, &next->resolvable, JS_UNDEFINED);

    list_del(&next->link);
    queue_free(ctx, next);

    JS_FreeValue(ctx, value);
  }
}

JSValue
js_repeater_new(JSContext* ctx, JSValueConst proto, JSValueConst executor) {
  Repeater* rpt;
  JSValue obj = JS_UNDEFINED;

  if(!(rpt = repeater_new(ctx, executor)))
    return JS_EXCEPTION;

  obj = JS_NewObjectProtoClass(ctx, proto, js_repeater_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, rpt);

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
  JSValue obj = JS_NewObjectProtoClass(ctx, repeater_proto, js_repeater_class_id);
  JS_SetOpaque(obj, rpt);

  return obj;
}

static JSValue
js_repeater_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, repeater_proto);

  obj = js_repeater_new(ctx, proto, argv[0]);

  JS_FreeValue(ctx, proto);
  return obj;
}

static JSValue
js_repeater_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Repeater* rpt;
  struct repeater_item* item;
  JSValue ret = JS_UNDEFINED, value = argc >= 1 ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;

  if(!(rpt = JS_GetOpaque2(ctx, this_val, js_repeater_class_id)))
    return JS_EXCEPTION;

  if(JS_IsFunction(ctx, rpt->executor) && rpt->state <= REPEATER_INITIAL)
    rpt->execution = js_repeater_execute(ctx, this_val);

  if(JS_IsFunction(ctx, rpt->onnext)) {
    JSValue result = JS_Call(ctx, rpt->onnext, this_val, argc, argv);
    JS_FreeValue(ctx, result);
  }

  // printf("js_repeater_next done=%d pushes=%d nexts=%d\n", done, queue_length(&rpt->pushes),
  // queue_length(&rpt->nexts));

  if((item = queue_shift(&rpt->pushes))) {
    JSValue it = resolvable_resolve(ctx, &item->resolvable, value);

    ret = js_repeater_create_iteration(ctx, this_val, it);
    JS_FreeValue(ctx, it);

  } else if(rpt->state >= REPEATER_STOPPED) {
    js_repeater_finish(ctx, this_val);

    JSValue it = repeater_consume(rpt, ctx);

    ret = js_repeater_create_iteration(ctx, this_val, it);
    JS_FreeValue(ctx, it);

    /*
      JSValue it = js_iterator_result(ctx, JS_UNDEFINED, done);
        ret = js_promise_resolve(ctx, it);
        JS_FreeValue(ctx, it);
    */

  } else {
    if(!(item = queue_alloc(ctx)))
      return JS_EXCEPTION;

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

enum { PROP_STATE };

static JSValue
js_repeater_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Repeater* rpt;
  JSValue ret = JS_UNDEFINED;

  if(!(rpt = JS_GetOpaque2(ctx, this_val, js_repeater_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_STATE: {
      ret = JS_NewInt32(ctx, rpt->state);
      break;
    }
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

  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_repeater_class = {
    .class_name = "Repeater",
    .finalizer = js_repeater_finalizer,
};

static const JSCFunctionListEntry js_repeater_proto_funcs[] = {
    JS_CFUNC_DEF("next", 0, js_repeater_next),
    JS_CGETSET_MAGIC_DEF("state", js_repeater_get, 0, PROP_STATE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Repeater", JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("[Symbol.asyncIterator]", 0, js_repeater_iterator),
};

static const JSCFunctionListEntry js_repeater_static_funcs[] = {
    JS_PROP_INT32_DEF("INITIAL", REPEATER_INITIAL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("STARTED", REPEATER_STARTED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("STOPPED", REPEATER_STOPPED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("REJECTED", REPEATER_REJECTED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("DONE", REPEATER_DONE, JS_PROP_ENUMERABLE),
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

  if((m = JS_NewCModule(ctx, module_name, js_repeater_init))) {
    JS_AddModuleExport(ctx, m, "Repeater");
  }

  return m;
}

/**
 * @}
 */
