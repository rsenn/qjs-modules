#include "js-utils.h"
#include "defines.h"
#include "quickjs-internal.h"

static inline void
js_resolve_functions_zero(ResolveFunctions* funcs) {
  funcs->array[0] = JS_NULL;
  funcs->array[1] = JS_NULL;
}

static inline BOOL
js_resolve_functions_is_null(ResolveFunctions* funcs) {
  return JS_IsNull(funcs->array[0]) && JS_IsNull(funcs->array[1]);
}

static void
js_resolve_functions_free(JSContext* ctx, ResolveFunctions* funcs) {
  JS_FreeValue(ctx, funcs->array[0]);
  JS_FreeValue(ctx, funcs->array[1]);
  js_resolve_functions_zero(funcs);
}

static void
js_resolve_functions_free_rt(JSRuntime* rt, ResolveFunctions* funcs) {
  JS_FreeValueRT(rt, funcs->array[0]);
  JS_FreeValueRT(rt, funcs->array[1]);
  js_resolve_functions_zero(funcs);
}

static inline BOOL
js_resolve_functions_call(JSContext* ctx, ResolveFunctions* funcs, int index, JSValueConst arg) {
  JSValue ret = JS_UNDEFINED;
  if(!JS_IsNull(funcs->array[index])) {
    ret = JS_Call(ctx, funcs->array[index], JS_UNDEFINED, 1, &arg);
    js_resolve_functions_free(ctx, funcs);
    JS_FreeValue(ctx, ret);
    return TRUE;
  }
  return FALSE;
}

ResolveFunctions*
promise_new(JSContext* ctx, JSValue* promise) {
  ResolveFunctions* funcs;

  if(!(funcs = js_mallocz(ctx, sizeof(ResolveFunctions)))) {
    *promise = JS_ThrowOutOfMemory(ctx);
    return 0;
  }

  *promise = JS_NewPromiseCapability(ctx, funcs->array);

  return funcs;
}

JSValue
promise_create(JSContext* ctx, ResolveFunctions* funcs) {
  JSValue ret;

  ret = JS_NewPromiseCapability(ctx, funcs->array);
  return ret;
}

void
promise_free(JSContext* ctx, Promise* pr) {
  JS_FreeValue(ctx, pr->value);
  pr->value = JS_UNDEFINED;
  js_resolve_functions_free(ctx, &pr->funcs);
}

void
promise_free_rt(JSRuntime* rt, Promise* pr) {
  JS_FreeValueRT(rt, pr->value);
  pr->value = JS_UNDEFINED;
  js_resolve_functions_free_rt(rt, &pr->funcs);
}

BOOL
promise_init(JSContext* ctx, Promise* pr) {
  pr->value = JS_NewPromiseCapability(ctx, pr->funcs.array);
  return !JS_IsException(pr->value);
}

BOOL
promise_resolve(JSContext* ctx, ResolveFunctions* funcs, JSValueConst value) {
  return js_resolve_functions_call(ctx, funcs, 0, value);
}

BOOL
promise_reject(JSContext* ctx, ResolveFunctions* funcs, JSValueConst value) {
  return js_resolve_functions_call(ctx, funcs, 1, value);
}

void
promise_zero(Promise* pr) {
  pr->value = JS_UNDEFINED;
  js_resolve_functions_zero(&pr->funcs);
}

BOOL
promise_pending(Promise* pr) {
  return /*!JS_IsUndefined(pr->value) &&*/ !js_resolve_functions_is_null(&pr->funcs);
}

BOOL
promise_done(Promise* pr) {
  return /*!JS_IsUndefined(pr->value) &&*/ js_resolve_functions_is_null(&pr->funcs);
}

JSValue
promise_then(JSContext* ctx, JSValueConst promise, JSValueConst handler) {
  JSValue fn, ret;

  fn = JS_GetPropertyStr(ctx, promise, "then");
  ret = JS_Call(ctx, fn, promise, 1, &handler);
  JS_FreeValue(ctx, fn);

  return ret;
}

JSValue
promise_catch(JSContext* ctx, JSValueConst promise, JSValueConst handler) {
  JSValue fn, ret;

  fn = JS_GetPropertyStr(ctx, promise, "catch");
  ret = JS_Call(ctx, fn, promise, 1, &handler);
  JS_FreeValue(ctx, fn);

  return ret;
}

JSValue
promise_forward(JSContext* ctx, JSValueConst promise, Promise* receiver) {
  JSValue ret, ret2;

  ret = promise_then(ctx, promise, receiver->funcs.array[0]);
  ret2 = promise_catch(ctx, ret, receiver->funcs.array[1]);
  JS_FreeValue(ctx, ret);
  return ret2;
}

typedef struct {
  CFunctionClosure* func;
  uint16_t length;
  uint16_t magic;
  void* opaque;
  void (*opaque_finalize)(void*);
} CFunctionClosureRecord;

static thread_local JSClassID js_cclosure_class_id;

static inline CFunctionClosureRecord*
js_cclosure_data(JSValueConst value) {
  return JS_GetOpaque(value, js_cclosure_class_id);
}

static inline CFunctionClosureRecord*
js_cclosure_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_cclosure_class_id);
}

static JSValue
js_cclosure_call(JSContext* ctx, JSValueConst func_obj, JSValueConst this_val, int argc, JSValueConst argv[], int flags) {
  CFunctionClosureRecord* cfcr;

  if(!(cfcr = js_cclosure_data2(ctx, func_obj)))
    return JS_EXCEPTION;

  JSValueConst* arg_buf;
  int i;

  /* XXX: could add the function on the stack for debug */
  if(unlikely(argc < cfcr->length)) {
    arg_buf = alloca(sizeof(arg_buf[0]) * cfcr->length);
    for(i = 0; i < argc; i++) arg_buf[i] = argv[i];
    for(i = argc; i < cfcr->length; i++) arg_buf[i] = JS_UNDEFINED;
  } else {
    arg_buf = argv;
  }

  return cfcr->func(ctx, this_val, argc, arg_buf, cfcr->magic, cfcr->opaque);
}

static void
js_cclosure_finalizer(JSRuntime* rt, JSValue val) {
  CFunctionClosureRecord* cfcr;

  if((cfcr = js_cclosure_data(val))) {

    if(cfcr->opaque_finalize)
      cfcr->opaque_finalize(cfcr->opaque);

    js_free_rt(rt, cfcr);
  }
}

static JSClassDef js_cclosure_class = {
    .class_name = "CFunctionClosure",
    .finalizer = js_cclosure_finalizer,
    .call = js_cclosure_call,
};

JSValue
JS_NewCFunctionClosure(JSContext* ctx, CFunctionClosure* func, int length, int magic, void* opaque, void (*opaque_finalize)(void*)) {
  CFunctionClosureRecord* cfcr;
  JSValue func_obj;

  if(js_cclosure_class_id) {
    JS_NewClassID(&js_cclosure_class_id);
  }

  func_obj = JS_NewObjectProtoClass(ctx, ctx->function_proto, js_cclosure_class_id);
  if(JS_IsException(func_obj))
    return func_obj;

  if(!(cfcr = js_malloc(ctx, sizeof(CFunctionClosureRecord)))) {
    JS_FreeValue(ctx, func_obj);
    return JS_EXCEPTION;
  }

  cfcr->func = func;
  cfcr->length = length;
  cfcr->magic = magic;
  cfcr->opaque = opaque;
  cfcr->opaque_finalize = opaque_finalize;

  JS_SetOpaque(func_obj, cfcr);

  JS_DefinePropertyValueStr(ctx, func_obj, "length", JS_NewUint32(ctx, length), JS_PROP_CONFIGURABLE);

  return func_obj;
}
