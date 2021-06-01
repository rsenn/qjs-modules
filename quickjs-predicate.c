#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "quickjs.h"
#include "predicate.h"

VISIBLE JSClassID js_predicate_class_id = 0;
static JSValue predicate_proto, predicate_ctor;

enum { METHOD_EVAL = 0, METHOD_TOSTRING };

enum { PROP_ID = 0, PROP_VALUES };

VISIBLE Predicate*
js_predicate_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque(value, js_predicate_class_id);
}

JSValue
js_predicate_new(JSContext* ctx, JSValueConst proto, JSValueConst value) {
  Predicate* pred;
  JSValue obj;
  if(!(pred = js_mallocz(ctx, sizeof(Predicate))))
    return JS_EXCEPTION;
  pred->id = -1;
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
js_predicate_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  Predicate* pred;
  JSValue obj = JS_UNDEFINED, proto = JS_UNDEFINED;

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

  if(argc > 0 && JS_IsString(argv[0])) {
    size_t len = 0;
    const char* str = js_tostringlen(ctx, &len, argv[0]);

    *pred = predicate_string(str, len);

  } else if(argc > 0 && js_is_regexp(ctx, argv[0])) {
    argc++;
    argv--;
    goto regexp;

  } else if(argc > 0 && JS_IsNumber(argv[0])) {
    int32_t id;
    JS_ToInt32(ctx, &id, argv[0]);

    switch(id) {
      case PREDICATE_TYPE: {
        id = -1;
        JS_ToInt32(ctx, &id, argv[1]);
        *pred = predicate_type(id);
        break;
      }

      case PREDICATE_CHARSET: {
        size_t len = 0;
        const char* str = js_tostringlen(ctx, &len, argv[1]);
        if(argc > 2 && JS_IsNumber(argv[2]))
          js_value_to_size(ctx, &len, argv[2]);
        *pred = predicate_charset(str, len);
        break;
      }

      case PREDICATE_STRING: {
        size_t len = 0;
        const char* str = js_tostringlen(ctx, &len, argv[1]);
        *pred = predicate_string(str, len);
        break;
      }

      case PREDICATE_NOTNOT: {
        *pred = predicate_notnot(JS_DupValue(ctx, argv[1]));
        break;
      }

      case PREDICATE_NOT: {
        *pred = predicate_not(JS_DupValue(ctx, argv[1]));
        break;
      }

      case PREDICATE_OR: {
        *pred = predicate_or(argc, js_values_dup(ctx, argc, argv));
        break;
      }

      case PREDICATE_AND: {
        *pred = predicate_and(argc, js_values_dup(ctx, argc, argv));
        break;
      }

      case PREDICATE_XOR: {
        *pred = predicate_xor(argc, js_values_dup(ctx, argc, argv));
        break;
      }

      case PREDICATE_REGEXP: {
        RegExp expr;
      regexp:
        expr = regexp_from_argv(argc - 1, argv + 1, ctx);
        *pred = predicate_regexp(expr.source, expr.len, expr.flags);
        break;
      }

      case PREDICATE_INSTANCEOF: {
        *pred = predicate_instanceof(JS_DupValue(ctx, argv[1]));
        break;
      }

      case PREDICATE_PROTOTYPEIS: {
        *pred = predicate_prototype(JS_DupValue(ctx, argv[1]));
        break;
      }

      case PREDICATE_EQUAL: {
        *pred = predicate_equal(JS_DupValue(ctx, argv[1]));
        break;
      }
      case PREDICATE_PROPERTY: {
        *pred = predicate_property(JS_ValueToAtom(ctx, argv[1]), argc > 2 ? JS_DupValue(ctx, argv[2]) : JS_UNDEFINED);
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
js_predicate_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
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
js_predicate_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Predicate* pred;
  DynBuf dbuf;
  JSValue ret;

  if(!(pred = JS_GetOpaque2(ctx, this_val, js_predicate_class_id)))
    return JS_EXCEPTION;

  js_dbuf_init(ctx, &dbuf);
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
    case PROP_ID: {
      ret = JS_NewInt32(ctx, pred->id);
      break;
    }

    case PROP_VALUES: {
      ret = predicate_values(pred, ctx);
      break;
    }
  }
  return ret;
}

static JSValue
js_predicate_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  switch(magic) {

    case PREDICATE_TYPE: {
      int32_t type = 0;
      JS_ToInt32(ctx, &type, argv[0]);
      ret = js_predicate_wrap(ctx, predicate_type(type));
      break;
    }

    case PREDICATE_CHARSET: {
      size_t size = 0;
      const char* str = js_tostringlen(ctx, &size, argv[0]);
      ret = js_predicate_wrap(ctx, predicate_charset(str, size));
      break;
    }

    case PREDICATE_STRING: {
      size_t size = 0;
      const char* str = js_tostringlen(ctx, &size, argv[0]);
      ret = js_predicate_wrap(ctx, predicate_string(str, size));
      break;
    }

    case PREDICATE_NOTNOT: {
      ret = js_predicate_wrap(ctx, predicate_notnot(JS_DupValue(ctx, argv[0])));
      break;
    }

    case PREDICATE_NOT: {
      ret = js_predicate_wrap(ctx, predicate_not(JS_DupValue(ctx, argv[0])));
      break;
    }

    case PREDICATE_OR: {
      ret = js_predicate_wrap(ctx, predicate_or(argc, js_values_dup(ctx, argc, argv)));
      break;
    }

    case PREDICATE_AND: {
      ret = js_predicate_wrap(ctx, predicate_and(argc, js_values_dup(ctx, argc, argv)));
      break;
    }

    case PREDICATE_XOR: {
      ret = js_predicate_wrap(ctx, predicate_xor(argc, js_values_dup(ctx, argc, argv)));
      break;
    }

    case PREDICATE_REGEXP: {
      RegExp expr = regexp_from_argv(argc, argv, ctx);
      Predicate pred = predicate_regexp(expr.source, expr.len, expr.flags);
      ret = js_predicate_wrap(ctx, pred);
      break;
    }

    case PREDICATE_INSTANCEOF: {
      ret = js_predicate_wrap(ctx, predicate_instanceof(JS_DupValue(ctx, argv[0])));
      break;
    }

    case PREDICATE_PROTOTYPEIS: {
      ret = js_predicate_wrap(ctx, predicate_prototype(JS_DupValue(ctx, argv[0])));
      break;
    }

    case PREDICATE_EQUAL: {
      ret = js_predicate_wrap(ctx, predicate_equal(JS_DupValue(ctx, argv[0])));
      break;
    }

    case PREDICATE_PROPERTY: {
      ret = js_predicate_wrap(ctx,
                              predicate_property(JS_ValueToAtom(ctx, argv[0]),
                                                 argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_UNDEFINED));
      break;
    }
  }
  return ret;
}

JSValue
js_predicate_call(
    JSContext* ctx, JSValueConst func_obj, JSValueConst this_val, int argc, JSValueConst argv[], int flags) {
  Predicate* pred;
  int32_t result;
  JSValue ret = JS_UNDEFINED;
  JSValueConst arg = argc > 0 ? argv[0] : JS_UNDEFINED;

  if(!(pred = JS_GetOpaque2(ctx, func_obj, js_predicate_class_id)))
    return JS_EXCEPTION;

  if(js_is_promise(ctx, arg)) {
    JSAtom then = JS_NewAtom(ctx, "then");
    ret = JS_Invoke(ctx, arg, then, 1, &func_obj);
    JS_FreeAtom(ctx, then);
  } else {
    result = predicate_eval(pred, ctx, argc, argv);

    switch(result) {
      case 0: ret = JS_NewBool(ctx, FALSE); break;
      case 1: ret = JS_NewBool(ctx, TRUE); break;
      default: ret = JS_NewInt32(ctx, result); break;
    }
  }
  return ret;
}

static void
js_predicate_finalizer(JSRuntime* rt, JSValue val) {
  Predicate* pred;

  if((pred = JS_GetOpaque(val, js_predicate_class_id))) {

    predicate_free_rt(pred, rt);

    //   js_free_rt(rt, pred);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_predicate_class = {.class_name = "Predicate",
                                        .finalizer = js_predicate_finalizer,
                                        .call = js_predicate_call};

static const JSCFunctionListEntry js_predicate_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("eval", 1, js_predicate_method, METHOD_EVAL),
    JS_CFUNC_DEF("toString", 0, js_predicate_tostring),
    JS_ALIAS_DEF("call", "eval"),
    JS_CGETSET_MAGIC_DEF("id", js_predicate_get, 0, PROP_ID),
    JS_CGETSET_MAGIC_DEF("values", js_predicate_get, 0, PROP_VALUES),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Predicate", JS_PROP_C_W_E),
};

static const JSCFunctionListEntry js_predicate_funcs[] = {
    JS_CFUNC_MAGIC_DEF("type", 1, js_predicate_function, PREDICATE_TYPE),
    JS_CFUNC_MAGIC_DEF("charset", 1, js_predicate_function, PREDICATE_CHARSET),
    JS_CFUNC_MAGIC_DEF("string", 1, js_predicate_function, PREDICATE_STRING),
    JS_CFUNC_MAGIC_DEF("not", 1, js_predicate_function, PREDICATE_NOT),
    JS_CFUNC_MAGIC_DEF("or", 2, js_predicate_function, PREDICATE_OR),
    JS_CFUNC_MAGIC_DEF("and", 2, js_predicate_function, PREDICATE_AND),
    JS_CFUNC_MAGIC_DEF("xor", 2, js_predicate_function, PREDICATE_XOR),
    JS_CFUNC_MAGIC_DEF("regexp", 1, js_predicate_function, PREDICATE_REGEXP),
    JS_CFUNC_MAGIC_DEF("instanceOf", 1, js_predicate_function, PREDICATE_INSTANCEOF),
    JS_CFUNC_MAGIC_DEF("prototypeIs", 1, js_predicate_function, PREDICATE_PROTOTYPEIS),
    JS_CFUNC_MAGIC_DEF("equal", 1, js_predicate_function, PREDICATE_EQUAL),
    JS_CFUNC_MAGIC_DEF("property", 1, js_predicate_function, PREDICATE_PROPERTY),
};

static const JSCFunctionListEntry js_predicate_ids[] = {
    JS_PROP_INT32_DEF("TYPE", PREDICATE_TYPE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("CHARSET", PREDICATE_CHARSET, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("STRING", PREDICATE_STRING, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("NOTNOT", PREDICATE_NOTNOT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("NOT", PREDICATE_NOT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("OR", PREDICATE_OR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("AND", PREDICATE_AND, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("XOR", PREDICATE_XOR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("REGEXP", PREDICATE_REGEXP, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("INSTANCEOF", PREDICATE_INSTANCEOF, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("PROTOTYPEIS", PREDICATE_PROTOTYPEIS, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("EQUAL", PREDICATE_EQUAL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("PROPERTY", PREDICATE_EQUAL, JS_PROP_ENUMERABLE),
};

static const JSCFunctionListEntry js_predicate_types[] = {
    JS_PROP_INT32_DEF("TYPE_UNDEFINED", TYPE_UNDEFINED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_NULL", TYPE_NULL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_BOOL", TYPE_BOOL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_INT", TYPE_INT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_OBJECT", TYPE_OBJECT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_STRING", TYPE_STRING, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_SYMBOL", TYPE_SYMBOL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_BIG_FLOAT", TYPE_BIG_FLOAT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_BIG_INT", TYPE_BIG_INT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_BIG_DECIMAL", TYPE_BIG_DECIMAL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_FLOAT64", TYPE_FLOAT64, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_NUMBER", TYPE_NUMBER, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_PRIMITIVE", TYPE_PRIMITIVE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ALL", TYPE_ALL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_FUNCTION", TYPE_FUNCTION, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ARRAY", TYPE_ARRAY, JS_PROP_ENUMERABLE),
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
  JS_SetPropertyFunctionList(ctx, predicate_ctor, js_predicate_funcs, countof(js_predicate_funcs));
  JS_SetPropertyFunctionList(ctx, predicate_ctor, js_predicate_ids, countof(js_predicate_ids));
  JS_SetPropertyFunctionList(ctx, predicate_ctor, js_predicate_types, countof(js_predicate_types));

  if(m) {
    JS_SetModuleExport(ctx, m, "Predicate", predicate_ctor);
    JS_SetModuleExportList(ctx, m, js_predicate_funcs, countof(js_predicate_funcs));
    JS_SetModuleExportList(ctx, m, js_predicate_types, countof(js_predicate_types));
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_predicate
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, &js_predicate_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "Predicate");
  JS_AddModuleExportList(ctx, m, js_predicate_funcs, countof(js_predicate_funcs));
  JS_AddModuleExportList(ctx, m, js_predicate_types, countof(js_predicate_types));
  return m;
}
