#define _GNU_SOURCE

#include "utils.h"
#include <string.h>

VISIBLE JSClassID js_repeater_class_id = 0;
JSValue repeater_proto, repeater_constructor, repeater_ctor;

enum repeater_functions { STATIC_RACE = 0, STATIC_MERGE, STATIC_ZIP };
enum repeater_getters { PROP_LENGTH = 0, PROP_PATH };

typedef union {
  JSValue promise;
  JSValue push, stop;
} Repeater;

static JSValue
js_repeater_push(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
}

static JSValue
js_repeater_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
}

JSValue
js_repeater_new(JSContext* ctx, JSValueConst proto, JSValueConst value) {
  Repeater *rpt, *ptr2;
  JSValue obj = JS_UNDEFINED;
  if(!(rpt = js_mallocz(ctx, sizeof(Repeater))))
    return JS_EXCEPTION;

  obj = JS_NewObjectProtoClass(ctx, proto, js_repeater_class_id);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, rpt);

  rpt->promise = JS_UNDEFINED;
  rpt->push = JS_NewCFunctionData(ctx, &js_repeater_push, 0, 0, 1, (JSValueConst*)&obj);
  rpt->stop = JS_NewCFunctionData(ctx, &js_repeater_stop, 0, 0, 1, (JSValueConst*)&obj);

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
js_repeater_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  JSValue proto;
  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, repeater_proto);

  return js_repeater_new(ctx, proto, argc > 0 ? argv[0] : JS_UNDEFINED);
}

static JSValue
js_repeater_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, BOOL* pdone, int magic) {
  Repeater* rpt;

  if(!(rpt = JS_GetOpaque2(ctx, this_val, js_repeater_class_id)))
    return JS_EXCEPTION;

  return JS_UNDEFINED;
}

static JSValue
js_repeater_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
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

void
js_repeater_finalizer(JSRuntime* rt, JSValue val) {
  Repeater* rpt;

  if((rpt = JS_GetOpaque(val, js_repeater_class_id))) {

    JS_FreeValueRT(rt, rpt->promise);
    JS_FreeValueRT(rt, rpt->push);
    JS_FreeValueRT(rt, rpt->stop);

    js_free_rt(rt, rpt);
  }
  // JS_FreeValueRT(rt, val);
}

JSClassDef js_repeater_class = {
    .class_name = "Repeater",
    .finalizer = js_repeater_finalizer,
};

static const JSCFunctionListEntry js_repeater_proto_funcs[] = {JS_ITERATOR_NEXT_DEF("next", 0, js_repeater_next, 0),
                                                               JS_PROP_STRING_DEF("[Symbol.toStringTag]",
                                                                                  "Repeater",
                                                                                  JS_PROP_C_W_E)};

static const JSCFunctionListEntry js_repeater_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("race", 1, js_repeater_funcs, STATIC_RACE),
    JS_CFUNC_MAGIC_DEF("merge", 1, js_repeater_funcs, STATIC_MERGE),
    JS_CFUNC_MAGIC_DEF("zip", 1, js_repeater_funcs, STATIC_ZIP)};

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
