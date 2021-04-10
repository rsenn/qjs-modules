#define _GNU_SOURCE

#include "utils.h"

typedef struct ChildProcess {

} ChildProcess;

VISIBLE JSClassID js_child_process_class_id = 0;
static JSValue child_process_proto, child_process_ctor;

static JSValue
js_child_process_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  ChildProcess* cp;
  JSValue obj = JS_UNDEFINED, proto = JS_UNDEFINED;

  if(!(cp = js_mallocz(ctx, sizeof(ChildProcess))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_child_process_class_id);
  js_value_free(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, cp);

  return obj;
fail:
  js_free(ctx, cp);
  js_value_free(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_child_process_finalizer(JSRuntime* rt, JSValue val) {
  ChildProcess* cp = JS_GetOpaque(val, js_child_process_class_id);
  if(cp) {
    // js_free_rt(rt, cp);
  }
}

static JSValue
js_child_process_exec(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;

  return ret;
}
static JSValue
js_child_process_spawn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;

  return ret;
}

static JSClassDef js_child_process_class = {
    .class_name = "ChildProcess",
    .finalizer = js_child_process_finalizer,
};

static const JSCFunctionListEntry js_child_process_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ChildProcess", JS_PROP_C_W_E),

};

static const JSCFunctionListEntry js_child_process_funcs[] = {
    JS_CFUNC_DEF("exec", 1, js_child_process_exec),
    JS_CFUNC_DEF("spawn", 1, js_child_process_spawn),

};

static int
js_child_process_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_child_process_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_child_process_class_id, &js_child_process_class);

  child_process_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx,
                             child_process_proto,
                             js_child_process_proto_funcs,
                             countof(js_child_process_proto_funcs));
  JS_SetClassProto(ctx, js_child_process_class_id, child_process_proto);

  child_process_ctor = JS_NewCFunction2(ctx, js_child_process_constructor, "ChildProcess", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, child_process_ctor, child_process_proto);
  JS_SetPropertyFunctionList(ctx, child_process_ctor, js_child_process_funcs, countof(js_child_process_funcs));

  if(m) {
    JS_SetModuleExportList(ctx, m, js_child_process_funcs, countof(js_child_process_funcs));
    JS_SetModuleExport(ctx, m, "ChildProcess", child_process_ctor);
    JS_SetModuleExport(ctx, m, "default", child_process_ctor);
  }
  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_child_process
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_child_process_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_child_process_funcs, countof(js_child_process_funcs));
  JS_AddModuleExport(ctx, m, "ChildProcess");
  JS_AddModuleExport(ctx, m, "default");
  return m;
}
