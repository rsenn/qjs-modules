#define _GNU_SOURCE

#include "quickjs.h"
#include "utils.h"
#include <string.h>

JSClassID js_lexer_class_id = 0;
JSValue lexer_proto, lexer_constructor, lexer_ctor;

typedef struct {
  uint32_t line;
  uint32_t column;
} Location;

typedef union Lexer {
  InputValue input;
  struct {
    const uint8_t* x;
    size_t n;
    size_t p;
    Location loc;
  };
} Lexer;

enum lexer_methods {
  METHOD_PEEK = 0,
  METHOD_NEXT,
  METHOD_SLICE,
  METHOD_TO_STRING,
  METHOD_TO_ARRAY,
  METHOD_INSPECT,
  METHOD_KEYS,
  METHOD_VALUES
};
enum lexer_functions { STATIC_FROM = 0, STATIC_OF };
enum lexer_getters { PROP_LENGTH = 0, PROP_PATH };

void
lexer_init(Lexer* lex) {
  lex->x = 0;
  lex->n = 0;
  lex->p = 0;
}

static Lexer
lexer_slice(Lexer* lex, int32_t start, int32_t end) {
  Lexer ret = {0, 0, 0, 0};

  ret.x = lex->x;
  ret.n = end;
  ret.p = start;

  return ret;
}

JSValue
js_lexer_new(JSContext* ctx, JSValueConst proto, JSValueConst value) {
  Lexer *ptr, *ptr2;
  JSValue obj = JS_UNDEFINED;
  if(!(ptr = js_mallocz(ctx, sizeof(Lexer))))
    return JS_EXCEPTION;
  lexer_init(ptr);
  obj = JS_NewObjectProtoClass(ctx, proto, js_lexer_class_id);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, ptr);

  ptr->input = js_value_to_bytes(ctx, value);

  return obj;
fail:
  js_free(ctx, ptr);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_lexer_wrap(JSContext* ctx, Lexer lex) {
  JSValue obj;
  Lexer* ptr;

  if(!(ptr = js_mallocz(ctx, sizeof(Lexer))))
    return JS_EXCEPTION;

  *ptr = lex;

  obj = JS_NewObjectProtoClass(ctx, lexer_proto, js_lexer_class_id);
  JS_SetOpaque(obj, ptr);
  return obj;
}

static JSValue
js_lexer_inspect(JSContext* ctx, JSValueConst this_val, BOOL color) {
  Lexer* ptr;
  DynBuf dbuf;
  JSValue ret;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_lexer_class_id)))
    return JS_EXCEPTION;

  dbuf_init2(&dbuf, JS_GetRuntime(ctx), (DynBufReallocFunc*)js_realloc_rt);

  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
  return ret;
}

static JSValue
js_lexer_toarray(JSContext* ctx, Lexer* ptr) {
  size_t i;
  JSValue array = JS_NewArray(ctx);
  return array;
}

static JSValue
js_lexer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  JSValue proto;
  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, lexer_proto);

  return js_lexer_new(ctx, proto, argc > 0 ? argv[0] : JS_UNDEFINED);
}

static JSValue
js_lexer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  Lexer* ptr;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_lexer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_PEEK: {
      return JS_NewStringLen(ctx, &ptr->x[ptr->p], 1);
    }
    case METHOD_NEXT: {
      return JS_NewStringLen(ctx, &ptr->x[ptr->p], 1);
    }
    case METHOD_TO_ARRAY: {
      return js_lexer_toarray(ctx, ptr);
    }
    case METHOD_INSPECT: {
      return js_lexer_inspect(ctx, this_val, TRUE);
    }
    case METHOD_SLICE: {
      int64_t s = js_int64_default(ctx, argv[0], 0);
      int64_t e = js_int64_default(ctx, argv[1], 0);

      return js_lexer_wrap(ctx, lexer_slice(ptr, s, e));
    }
    case METHOD_KEYS: {
      JSValue array = js_lexer_toarray(ctx, ptr);
      JSValue iter = js_iterator_new(ctx, array);
      JS_FreeValue(ctx, array);
      return iter;
    }
    case METHOD_VALUES: {
      JSValue array = js_lexer_toarray(ctx, ptr);
      JSValue iter = js_iterator_new(ctx, array);
      JS_FreeValue(ctx, array);
      return iter;
    }
  }
  return JS_EXCEPTION;
}

static JSValue
js_lexer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Lexer* ptr;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_lexer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_LENGTH: {
      return JS_NewUint32(ctx, ptr->n);
    }
    case PROP_PATH: {
      return js_lexer_toarray(ctx, ptr);
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_lexer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Lexer* ptr;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_lexer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_PATH: {
      break;
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_lexer_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  JSValue ret;

  switch(magic) {
    case STATIC_FROM: {
      return js_lexer_new(ctx, lexer_proto, argc > 0 ? argv[0] : JS_UNDEFINED);
    }
    case STATIC_OF: {
      int i;
      Lexer* ptr;

      if(!(ptr = js_mallocz(ctx, sizeof(Lexer))))
        return JS_ThrowOutOfMemory(ctx);

      ret = js_lexer_wrap(ctx, *ptr);
      break;
    }
  }
  return ret;
}

void
js_lexer_finalizer(JSRuntime* rt, JSValue val) {
  Lexer* ptr;

  if((ptr = JS_GetOpaque(val, js_lexer_class_id))) {

    js_free_rt(rt, ptr);
  }
  // JS_FreeValueRT(rt, val);
}

JSClassDef js_lexer_class = {
    .class_name = "Lexer",
    .finalizer = js_lexer_finalizer,
};

static const JSCFunctionListEntry js_lexer_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("toArray", 0, js_lexer_method, METHOD_TO_ARRAY),
    JS_CFUNC_MAGIC_DEF("inspect", 0, js_lexer_method, METHOD_INSPECT),
    JS_CFUNC_MAGIC_DEF("next", 0, js_lexer_method, METHOD_NEXT),
    JS_CFUNC_MAGIC_DEF("peek", 0, js_lexer_method, METHOD_PEEK),
    JS_CFUNC_MAGIC_DEF("slice", 2, js_lexer_method, METHOD_SLICE),
    JS_CFUNC_MAGIC_DEF("keys", 0, js_lexer_method, METHOD_KEYS),
    JS_CFUNC_MAGIC_DEF("values", 0, js_lexer_method, METHOD_VALUES),
    JS_ALIAS_DEF("toPrimitive", "toString"),
    JS_ALIAS_DEF("[Symbol.iterator]", "keys"),
    JS_CGETSET_MAGIC_DEF("length", js_lexer_get, 0, PROP_LENGTH),
    JS_CGETSET_MAGIC_DEF("path", js_lexer_get, js_lexer_set, PROP_PATH),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Lexer", JS_PROP_C_W_E)};

static const JSCFunctionListEntry js_lexer_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("from", 1, js_lexer_funcs, STATIC_FROM),
    JS_CFUNC_MAGIC_DEF("of", 0, js_lexer_funcs, STATIC_OF)};

static int
js_lexer_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_lexer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_lexer_class_id, &js_lexer_class);

  lexer_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, lexer_proto, js_lexer_proto_funcs, countof(js_lexer_proto_funcs));
  JS_SetClassProto(ctx, js_lexer_class_id, lexer_proto);

  lexer_ctor = JS_NewCFunction2(ctx, js_lexer_constructor, "Lexer", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, lexer_ctor, lexer_proto);
  JS_SetPropertyFunctionList(ctx, lexer_ctor, js_lexer_static_funcs, countof(js_lexer_static_funcs));

  if(m) {
    JS_SetModuleExport(ctx, m, "Lexer", lexer_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_lexer
#endif

JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, &js_lexer_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "Lexer");
  return m;
}
