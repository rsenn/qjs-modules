#include "stream-utils.h"
#include "utils.h"
#include "json.h"
#include "vector.h"
#define SJ_IMPL
#include "sj.h"

VISIBLE JSClassID js_json_parser_class_id = 0;
static JSValue json_parser_proto, json_parser_ctor;

struct js_json_parser_opaque {
  JSContext* ctx;
  JSObject *parser, *obj;
};

static JSValue
parse_val(JSContext* ctx, sj_Reader* r, sj_Value val) {
  sj_Value k, v;
  JSValue ret = JS_UNDEFINED;

  switch(val.type) {
    case SJ_ERROR: {
      goto error;
    }
    case SJ_ARRAY: {
      uint32_t count = 0;
      ret = JS_NewArray(ctx);
      while(sj_iter_array(r, val, &v))
        JS_SetPropertyUint32(ctx, ret, count++, parse_val(ctx, r, v));
      break;
    }
    case SJ_OBJECT: {
      ret = JS_NewObject(ctx);
      while(sj_iter_object(r, val, &k, &v)) {
        JSAtom key = JS_NewAtomLen(ctx, k.start, k.end - k.start);
        JS_SetProperty(ctx, ret, key, parse_val(ctx, r, v));
        JS_FreeAtom(ctx, key);
      }
      break;
    }
    case SJ_NUMBER: {
      double num;
      scan_double(val.start, &num);
      ret = JS_NewFloat64(ctx, num);
      break;
    }
    case SJ_STRING: {
      ret = JS_NewStringLen(ctx, val.start, val.end - val.start);
      break;
    }
    case SJ_NULL: {
      ret = JS_NULL;
      break;
    }
    case SJ_BOOL: {
      ret = val.start[0] == 't' ? JS_TRUE : JS_FALSE;
      break;
    }
  }

  if(!r->error)
    return ret;

error:
  int line, col;
  sj_location(r, &line, &col);
  return JS_ThrowInternalError(ctx, "error: %d:%d: %s\n", line, col, r->error);
}

static JSValue
js_json_parse(JSContext* ctx, const uint8_t* buf, size_t len, const char* input_name) {
  sj_Reader r = sj_reader(buf, len);
  JSValue ret = parse_val(ctx, &r, sj_read(&r));

  return ret;
}

static JSValue
js_json_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  InputBuffer input = js_input_chars(ctx, argv[0]);
  const char* input_name = 0;

  if(input.data == 0 || input.size == 0) {
    JS_ThrowReferenceError(ctx, "xml.read(): expecting buffer or string");
    return JS_EXCEPTION;
  }

  if(argc >= 2)
    input_name = JS_ToCString(ctx, argv[1]);

  ret = js_json_parse(ctx, input.data, input.size, input_name ? input_name : "<json>");

  if(input_name)
    JS_FreeCString(ctx, input_name);

  inputbuffer_free(&input, ctx);
  return ret;
}

static JSValue
js_json_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  return ret;
}

static const JSCFunctionListEntry js_json_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_json_read),
    JS_CFUNC_DEF("write", 2, js_json_write),
};

static JSValue
js_json_parser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  JsonParser* parser;

  if(!(parser = json_new(argc > 0 ? argv[0] : JS_UNDEFINED, ctx)))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, json_parser_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_json_parser_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, parser);

  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  JSON_PARSER_PARSE,
};

static JSValue
js_json_parser_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JsonParser* parser;
  JSValue ret = JS_UNDEFINED;

  if(!(parser = JS_GetOpaque2(ctx, this_val, js_json_parser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_PARSER_PARSE: {
      JsonValueType type = json_parse(parser, ctx);
      ret =
          JS_NewString(ctx, CONST_STRARRAY("NONE", "OBJECT", "OBJECT_END", "ARRAY", "ARRAY_END", "KEY", "STRING", "TRUE", "FALSE", "NULL", "NUMBER")[type + 1]);
      break;
    }
  }

  return ret;
}

enum {
  JSON_PARSER_CALLBACK,
  JSON_PARSER_POS,
  JSON_PARSER_TOKEN,
  JSON_PARSER_STATE,
  JSON_PARSER_DEPTH,
};

static JSValue
js_json_parser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JsonParser* parser;
  JSValue ret = JS_UNDEFINED;

  if(!(parser = JS_GetOpaque2(ctx, this_val, js_json_parser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_PARSER_CALLBACK: {
      ret = js_value_mkobj2(ctx, parser->opaque);
      break;
    }

    case JSON_PARSER_POS: {
      ret = JS_NewUint32(ctx, parser->pos);
      break;
    }

    case JSON_PARSER_TOKEN: {
      ret = JS_NewStringLen(ctx, (const char*)parser->token.buf, parser->token.size);
      break;
    }

    case JSON_PARSER_STATE: {
      ret = JS_NewInt32(ctx, parser->state);
      break;
    }

    case JSON_PARSER_DEPTH: {
      ret = JS_NewUint32(ctx, parser->stack.len);
      break;
    }
  }

  return ret;
}

static void
js_json_parser_callback(JsonParser* parser, JsonValueType type, void* ptr) {
  struct js_json_parser_opaque* op = parser->opaque;
  JSContext* ctx = op->ctx;
  JSValue fn = js_value_mkobj(op->obj);
  JSValue args[] = {
      js_value_mkobj(op->parser),
      JS_NewInt32(ctx, type),
      ptr ? JS_NewString(ctx, ptr) : JS_UNDEFINED,
  };

  JSValue ret = JS_Call(ctx, fn, JS_UNDEFINED, countof(args), args);
  JS_FreeValue(ctx, ret);

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, args[2]);
}

static JSValue
js_json_parser_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  JsonParser* parser;
  JSValue ret = JS_UNDEFINED;

  if(!(parser = JS_GetOpaque2(ctx, this_val, js_json_parser_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case JSON_PARSER_CALLBACK: {
      struct js_json_parser_opaque* op;

      if(!JS_IsFunction(ctx, value))
        return JS_ThrowTypeError(ctx, "value must be a function");

      if(parser->opaque) {
        op = parser->opaque;
        js_freeobj(ctx, op->obj);
      }

      op = parser->opaque ? parser->opaque : js_malloc(ctx, sizeof(struct js_json_parser_opaque));

      if(op) {
        *op = (struct js_json_parser_opaque){ctx, js_value_obj(this_val), js_value_obj2(ctx, value)};

        parser->callback = js_json_parser_callback;
        parser->opaque = op;
      }

      break;
    }
  }

  return ret;
}

static void
js_json_parser_finalizer(JSRuntime* rt, JSValue obj) {
  JsonParser* parser;

  if((parser = JS_GetOpaque(obj, js_json_parser_class_id)))
    json_free(parser, rt);
}

static const JSCFunctionListEntry js_json_parser_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("parse", 0, js_json_parser_method, JSON_PARSER_PARSE),
    JS_CGETSET_MAGIC_FLAGS_DEF("pos", js_json_parser_get, 0, JSON_PARSER_POS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("token", js_json_parser_get, 0, JSON_PARSER_TOKEN, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("state", js_json_parser_get, 0, JSON_PARSER_STATE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("depth", js_json_parser_get, 0, JSON_PARSER_DEPTH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("callback", js_json_parser_get, js_json_parser_set, JSON_PARSER_CALLBACK),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_json_parser_class = {
    .class_name = "JsonParser",
    .finalizer = js_json_parser_finalizer,
};

static int
js_json_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_json_parser_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_json_parser_class_id, &js_json_parser_class);

  json_parser_proto = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyFunctionList(ctx, json_parser_proto, js_json_parser_proto_funcs, countof(js_json_parser_proto_funcs));

  json_parser_ctor = JS_NewCFunction2(ctx, js_json_parser_constructor, "JsonParser", 1, JS_CFUNC_constructor, 0);

  JS_SetClassProto(ctx, js_json_parser_class_id, json_parser_proto);
  JS_SetConstructor(ctx, json_parser_ctor, json_parser_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "JsonParser", json_parser_ctor);

  JS_SetModuleExportList(ctx, m, js_json_funcs, countof(js_json_funcs));

  /*JSValue defaultObj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, defaultObj, "read", JS_NewCFunction(ctx, js_json_read, "read", 1));
  JS_SetPropertyStr(ctx, defaultObj, "write", JS_NewCFunction(ctx, js_json_write, "write", 2));
  JS_SetModuleExport(ctx, m, "default", defaultObj);*/

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_json
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_json_init))) {
    JS_AddModuleExport(ctx, m, "JsonParser");
    JS_AddModuleExportList(ctx, m, js_json_funcs, countof(js_json_funcs));
    // JS_AddModuleExport(ctx, m, "default");
  }

  return m;
}
