#define _GNU_SOURCE

#include "quickjs.h"
#include "predicate.h"

JSClassID js_predicate_class_id = 0;
JSValue predicate_proto, predicate_constructor, predicate_ctor;

enum { METHOD_EVAL = 0, METHOD_TOSTRING };

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

int32_t
js_predicate_call(JSContext* ctx, JSValueConst value, int argc, JSValueConst* argv) {
  Predicate* pred;
  int32_t result = 0;
  if((pred = JS_GetOpaque2(ctx, value, js_predicate_class_id)))
    return predicate_eval(pred, ctx, argc, argv);
  if(JS_IsFunction(ctx, value)) {
    JSValue ret = JS_UNDEFINED;
    ret = JS_Call(ctx, value, JS_UNDEFINED, argc, argv);
    if(JS_IsException(ret)) {
      result = -1;
    } else {
      result = JS_ToBool(ctx, ret);
      JS_FreeValue(ctx, ret);
    }
    return result;
  }
  assert(0);
  return -1;
}

static JSValue
js_predicate_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  Predicate* pred;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  if(!(pred = js_mallocz(ctx, sizeof(Predicate))))
    return JS_EXCEPTION;
  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_predicate_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, pred);

  if(argc > 0) {
    int32_t id;
    JS_ToInt32(ctx, &id, argv[0]);

    switch(id) {
      case PREDICATE_CHARSET: {
        size_t len;
        const char* str = JS_ToCStringLen(ctx, &len, argv[1]);
        if(argc > 2 && JS_IsNumber(argv[2]))
          js_value_to_size(ctx, &len, argv[2]);

        *pred = predicate_charset(str, len);

        JS_FreeCString(ctx, str);
        break;
      }
      case PREDICATE_TYPE: {
        int32_t id = -1;
        JS_ToInt32(ctx, &id, argv[0]);
        *pred = predicate_type(id);
        break;
      }
      case PREDICATE_NOT: {
        *pred = predicate_not(argv[1]);
        break;
      }
      case PREDICATE_AND: {
        *pred = predicate_and(argv[1], argv[2]);
        break;
      }
      case PREDICATE_OR: {
        *pred = predicate_or(argv[1], argv[2]);
        break;
      }
      case PREDICATE_XOR: {
        *pred = predicate_xor(argv[1], argv[2]);
        break;
      }
    }
  }
  return obj;
fail:
  js_free(ctx, pred);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_predicate_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  Predicate* pred;
  JSValue ret = JS_UNDEFINED;
  if(!(pred = JS_GetOpaque2(ctx, this_val, js_predicate_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_EVAL: {
      int32_t r = predicate_eval(pred, ctx, argc, argv);

      // ret = JS_NewBool(ctx, r);
      ret = JS_NewInt32(ctx, r);

      //  printf("predicate_eval() = %i\n", r);
      break;
    }
  }
  return ret;
}

static JSValue
js_predicate_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Predicate* pred;
  DynBuf dbuf;
  JSValue ret;

  if(!(pred = JS_GetOpaque2(ctx, this_val, js_predicate_class_id)))
    return JS_EXCEPTION;

  dbuf_init2(&dbuf, JS_GetRuntime(ctx), (DynBufReallocFunc*)js_realloc_rt);
  predicate_tostring(pred, ctx, &dbuf);
  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
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

static JSValue
js_predicate_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  JSValue ret = JS_UNDEFINED;
  switch(magic) {
    case PREDICATE_AND: {
      ret = js_predicate_wrap(ctx, predicate_and(argv[0], argv[1]));

      break;
    }
    case PREDICATE_CHARSET: {
      const char* str;
      size_t size;
      str = JS_ToCStringLen(ctx, &size, argv[0]);
      ret = js_predicate_wrap(ctx, predicate_charset(str_ndup(str, size), size));
      JS_FreeCString(ctx, str);

      break;
    }
    case PREDICATE_NOT: {
      ret = js_predicate_wrap(ctx, predicate_not(argv[0]));
      break;
    }
    case PREDICATE_OR: {
      ret = js_predicate_wrap(ctx, predicate_or(argv[0], argv[1]));
      break;
    }
    case PREDICATE_TYPE: {
      int32_t type = 0;
      JS_ToInt32(ctx, &type, argv[0]);
      ret = js_predicate_wrap(ctx, predicate_type(type));
      break;
    }
  }
  return ret;
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
    JS_CFUNC_DEF("toString", 0, js_predicate_tostring),
    JS_ALIAS_DEF("call", "eval"),
    JS_CGETSET_MAGIC_DEF("id", js_predicate_get, 0, PROP_ID),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Predicate", JS_PROP_C_W_E)};

static const JSCFunctionListEntry js_predicate_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("oneOf", 1, js_predicate_funcs, PREDICATE_CHARSET),
    JS_CFUNC_MAGIC_DEF("not", 1, js_predicate_funcs, PREDICATE_NOT),
    JS_CFUNC_MAGIC_DEF("or", 2, js_predicate_funcs, PREDICATE_OR),
    JS_CFUNC_MAGIC_DEF("and", 2, js_predicate_funcs, PREDICATE_AND),
    JS_CFUNC_MAGIC_DEF("xor", 2, js_predicate_funcs, PREDICATE_XOR),
    JS_CFUNC_MAGIC_DEF("type", 1, js_predicate_funcs, PREDICATE_TYPE),
    JS_PROP_INT32_DEF("PREDICATE_AND", PREDICATE_AND, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("PREDICATE_CHARSET", PREDICATE_CHARSET, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("PREDICATE_NONE", PREDICATE_NONE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("PREDICATE_NOT", PREDICATE_NOT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("PREDICATE_OR", PREDICATE_OR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("PREDICATE_TYPE", PREDICATE_TYPE, JS_PROP_ENUMERABLE),
};

static int
js_predicate_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_predicate_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_predicate_class_id, &js_predicate_class);

  predicate_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, predicate_proto, js_predicate_proto_funcs, countof(js_predicate_proto_funcs));
  JS_SetClassProto(ctx, js_predicate_class_id, predicate_proto);

  predicate_ctor = JS_NewCFunction2(ctx, js_predicate_constructor, "Predicate", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, predicate_ctor, predicate_proto);
  JS_SetPropertyFunctionList(ctx, predicate_ctor, js_predicate_static_funcs, countof(js_predicate_static_funcs));

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
