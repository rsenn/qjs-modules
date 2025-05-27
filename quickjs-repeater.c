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

typedef struct {
  JSValue resolve, value;
} Resolvable;

typedef struct {
  struct list_head link;
  Resolvable resolvable;
  BOOL stop;
} RepeaterItem;

typedef struct {
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

static void
repeater_free(JSRuntime* rt, Repeater* rpt) {
  if(--rpt->ref_count == 0) {
    JS_FreeValueRT(rt, rpt->executor);
    JS_FreeValueRT(rt, rpt->buffer);
    JS_FreeValueRT(rt, rpt->err);

    js_free_rt(rt, rpt);
  }
}

static JSValue
resolvable_value(JSContext* ctx, JSValueConst value, Resolvable* rsva) {
  JSValue ret, resolving_funcs[2];

  ret = JS_NewPromiseCapability(ctx, resolving_funcs);
  rsva->resolve = resolving_funcs[0];
  rsva->value = JS_DupValue(ctx, value);
  JS_FreeValue(ctx, resolving_funcs[1]);

  return ret;
}

static void
resolvable_call(JSContext* ctx, Resolvable* rsva, JSValueConst value) {
  JSValue result = JS_Call(ctx, rsva->resolve, JS_UNDEFINED, 1, &value);
  JS_FreeValue(ctx, result);
}

static JSValue
resolvable_deferred(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  Resolvable* rsva = opaque;

  JSValue riter = js_iterator_result(ctx, argv[0], FALSE);
  resolvable_call(ctx, rsva, riter);
  JS_FreeValue(ctx, riter);

  return JS_DupValue(ctx, rsva->value);
}

static Resolvable*
resolvable_dup(Resolvable* rsva, JSContext* ctx) {
  Resolvable* r;

  if((r = js_malloc(ctx, sizeof(Resolvable)))) {
    r->resolve = JS_DupValue(ctx, rsva->resolve);
    r->value = JS_DupValue(ctx, rsva->value);
  }
  return r;
}

static void
resolvable_closure_free(JSRuntime* rt, void*ptr) {
  Resolvable* r=ptr;

  JS_FreeValueRT(rt, r->resolve);
  JS_FreeValueRT(rt, r->value);
  js_free_rt(rt, r);
}

static JSValue
resolvable_resolve(JSContext* ctx, Resolvable* rsva, JSValueConst value, BOOL as_promise) {
  if(js_is_promise(ctx, value)) {
    Resolvable* r;

    if(!(r = resolvable_dup(rsva, ctx)))
      return JS_EXCEPTION;

    JSValue func = js_function_cclosure(ctx, resolvable_deferred, 1, 0, r,  &resolvable_closure_free);
    JSValue result = js_promise_resolve_then(ctx, value, func);
    JS_FreeValue(ctx, func);
    return result;
  } else {
    resolvable_call(ctx, rsva, value);

    return as_promise ? js_promise_resolve(ctx, rsva->value) : JS_DupValue(ctx, rsva->value);
  }
}

static void
resolvable_free(JSContext* ctx, Resolvable* rsva) {
  JS_FreeValue(ctx, rsva->value);
  JS_FreeValue(ctx, rsva->resolve);
}

static RepeaterItem*
item_new(JSContext* ctx) {
  RepeaterItem* item;

  if((item = js_mallocz(ctx, sizeof(RepeaterItem)))) {
    item->resolvable.resolve = JS_UNDEFINED;
    item->resolvable.value = JS_UNDEFINED;
  }

  return item;
}

static void
item_free(RepeaterItem* item, JSContext* ctx) {
  resolvable_free(ctx, &item->resolvable);
  js_free(ctx, item);
}

static RepeaterItem*
list_shift(struct list_head* list) {
  struct list_head* item;

  if((item = list_front(list)))
    list_del(item);

  return list_entry(item, RepeaterItem, link);
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
js_repeater_iteration(
    JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValueConst data[]) {
  Repeater* rpt = JS_GetOpaque(data[0], js_repeater_class_id);
  JSValue ret = JS_UNDEFINED;
  JSValueConst value = argc >= 1 ? argv[0] : JS_UNDEFINED;
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
  Repeater* rpt = JS_GetOpaque(this_val, js_repeater_class_id);
  JSValueConst data[2] = {
      JS_DupValue(ctx, this_val),
      JS_NewBool(ctx, rpt->state >= REPEATER_DONE),
  };
  JSValue iteration_fn = JS_NewCFunctionData(ctx, js_repeater_iteration, 1, 0, 2, data);
  JSValue promise = js_is_promise(ctx, value) ? JS_DupValue(ctx, value) : js_promise_resolve(ctx, value);
  JSValue ret = js_promise_resolve_then(ctx, promise, iteration_fn);

  JS_FreeValue(ctx, promise);
  JS_FreeValue(ctx, iteration_fn);
  return ret;
}

static JSValue
js_repeater_push(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Repeater* rpt;
  RepeaterItem* item;
  JSValue ret = JS_UNDEFINED;
  JSValueConst value = argc >= 1 ? argv[0] : JS_UNDEFINED;

  if(!(rpt = JS_GetOpaque2(ctx, this_val, js_repeater_class_id)))
    return JS_EXCEPTION;

  if((item = list_shift(&rpt->nexts))) {
    JSValue result = js_is_promise(ctx, value) ? JS_DupValue(ctx, value) : js_iterator_result(ctx, value, FALSE);

    ret = resolvable_resolve(ctx, &item->resolvable, result, TRUE);
    JS_FreeValue(ctx, result);
    item_free(item, ctx);
  } else if(rpt->state < REPEATER_STOPPED) {
    if(!(item = item_new(ctx)))
      return JS_EXCEPTION;

    ret = resolvable_value(ctx, value, &item->resolvable);
    list_add_tail(&item->link, &rpt->pushes);
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
    rpt->err = argc >= 1 ? JS_DupValue(ctx, argv[0]) : JS_NewBool(ctx, TRUE);

  if(list_empty(&rpt->nexts)) {
    RepeaterItem* item;

    if(!(item = item_new(ctx)))
      return JS_EXCEPTION;

    item->stop = TRUE;
    ret = resolvable_value(ctx, argc >= 1 ? argv[0] : JS_UNDEFINED, &item->resolvable);

    list_add_tail(&item->link, &rpt->pushes);
  } else {
    list_for_each_safe(el, next, &rpt->nexts) {
      RepeaterItem* next = list_entry(el, RepeaterItem, link);

      JSValue result = js_iterator_result(ctx, argc >= 1 ? argv[0] : JS_UNDEFINED, TRUE);
      resolvable_call(ctx, &next->resolvable, result);
      JS_FreeValue(ctx, result);

      list_del(&next->link);
      item_free(next, ctx);
    }

    rpt->state = REPEATER_DONE;
  }

  return ret;
}

static JSValue
js_repeater_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  Repeater* rpt;
  JSValue proto, obj = JS_UNDEFINED;

  if(argc < 1 || !JS_IsFunction(ctx, argv[0]))
    return JS_ThrowInternalError(ctx, "argument 1 must be executor function");

  if(!(rpt = repeater_new(ctx, argv[0])))
    goto fail;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_repeater_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, rpt);
  return obj;

fail:
  if(rpt)
    js_free(ctx, rpt);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_repeater_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Repeater* rpt;
  RepeaterItem* item;
  JSValue ret = JS_UNDEFINED, value = argc >= 1 ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;

  if(!(rpt = JS_GetOpaque2(ctx, this_val, js_repeater_class_id)))
    return JS_EXCEPTION;

  if(JS_IsFunction(ctx, rpt->executor) && rpt->state <= REPEATER_INITIAL)
    rpt->execution = js_repeater_execute(ctx, this_val);

  if(JS_IsFunction(ctx, rpt->onnext)) {
    JSValue result = JS_Call(ctx, rpt->onnext, this_val, argc, argv);
    JS_FreeValue(ctx, result);
  }

  // printf("js_repeater_next done=%d pushes=%d nexts=%d\n", done, list_length(&rpt->pushes),
  // list_length(&rpt->nexts));

  if((item = list_shift(&rpt->pushes))) {
    JSValue it = resolvable_resolve(ctx, &item->resolvable, value, FALSE);

    if(item->stop)
      rpt->state = REPEATER_DONE;

    ret = js_repeater_create_iteration(ctx, this_val, it);
    JS_FreeValue(ctx, it);

    item_free(item, ctx);
  } else if(rpt->state >= REPEATER_STOPPED) {
    JSValue it = repeater_consume(rpt, ctx);

    ret = js_repeater_create_iteration(ctx, this_val, it);
    JS_FreeValue(ctx, it);
  } else {
    if(!(item = item_new(ctx)))
      return JS_EXCEPTION;

    ret = resolvable_value(ctx, value, &item->resolvable);
    list_add_tail(&item->link, &rpt->nexts);
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
js_repeater_finalizer(JSRuntime* rt, JSValue val) {
  Repeater* rpt;

  if((rpt = JS_GetOpaque(val, js_repeater_class_id)))
    repeater_free(rt, rpt);
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

  if(m)
    JS_SetModuleExport(ctx, m, "Repeater", repeater_ctor);

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

  if((m = JS_NewCModule(ctx, module_name, js_repeater_init)))
    JS_AddModuleExport(ctx, m, "Repeater");

  return m;
}

/**
 * @}
 */
