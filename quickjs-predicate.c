#define _GNU_SOURCE

#include "quickjs.h"
#include "predicate.h"

JSClassID js_predicate_class_id = 0;
JSValue predicate_proto, predicate_constructor, predicate_ctor;

enum { METHOD_EVAL = 0 };

enum { PROP_ID = 0 };

JSValue
js_predicate_new(JSContext* ctx, JSValueConst proto, JSValueConst value) {
  Predicate* pred;
  JSValue obj = JS_UNDEFINED;
  if(!(pred = js_mallocz(ctx, sizeof(Predicate))))
    return JS_EXCEPTION;

  pred->id = PREDICATE_NONE;

  obj = JS_NewObjectProtoClass(ctx, proto, js_predicate_class_id);

  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, pred);

  return obj;
fail:
  js_free(ctx, pred);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_predicate_wrap(JSContext* ctx, Predicate pred) {
  JSValue obj;
  Predicate* ret;

  if(!(ret = js_mallocz(ctx, sizeof(Predicate))))
    return JS_EXCEPTION;

  *ret = pred;

  obj = JS_NewObjectProtoClass(ctx, predicate_proto, js_predicate_class_id);
  JS_SetOpaque(obj, ret);
  return obj;
}

static JSValue
js_predicate_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  Predicate* pred;
  JSValue ret = JS_UNDEFINED;
  if(!(pred = JS_GetOpaque2(ctx, this_val, js_predicate_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_EVAL: {
      BOOL fulfilled = predicate_eval(pred, ctx, argv[0]);
      ret = JS_NewBool(ctx, fulfilled);
      break;
    }
  }
  return ret;
}

static JSValue
js_predicate_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Predicate* pred;
  JSValue ret = JS_UNDEFINED;
  if(!(pred = JS_GetOpaque2(ctx, this_val, js_predicate_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_ID: ret = JS_NewInt32(ctx, pred->id); break;
  }
  return ret;
}

static JSValue
js_predicate_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Predicate* pred;

  if(!(pred = JS_GetOpaque2(ctx, this_val, js_predicate_class_id)))
    return JS_EXCEPTION;

  switch(magic) {}
  return JS_UNDEFINED;
}
void
js_predicate_finalizer(JSRuntime* rt, JSValue val) {
  Predicate* pred;

  if((pred = JS_GetOpaque(val, js_predicate_class_id))) {

    js_free_rt(rt, pred);
  }
  // JS_FreeValueRT(rt, val);
}

JSClassDef js_predicate_class = {.class_name = "Predicate", js_predicate_finalizer};

static const JSCFunctionListEntry js_predicate_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("eval", 1, js_predicate_method, METHOD_EVAL),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Predicate", JS_PROP_C_W_E)};

static int
js_predicate_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_predicate_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_predicate_class_id, &js_predicate_class);

  predicate_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx,
                             predicate_proto,
                             js_predicate_proto_funcs,
                             countof(js_predicate_proto_funcs));
  JS_SetClassProto(ctx, js_predicate_class_id, predicate_proto);

  /*predicate_ctor = JS_NewCFunction2(ctx, js_predicate_constructor, "Predicate", 1, JS_CFUNC_constructor,
  0);

  JS_SetConstructor(ctx, predicate_ctor, predicate_proto);*/

  if(m) {
    JS_SetModuleExport(ctx, m, "Predicate", predicate_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_predicate
#endif

JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, &js_predicate_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "Predicate");
  return m;
}
