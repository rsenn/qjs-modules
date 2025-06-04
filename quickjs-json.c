#include "stream-utils.h"
#include "buffer-utils.h"
#include "utils.h"

#include "include/json.h"
#include "include/jsonst.h"
#include "include/jsmn2.h"
#include "include/jsmn.h"
#include "include/jread.h"
#include "include/sjson.h"

typedef enum {
  PARSER_JSON = 1,
  PARSER_JSONST,
  PARSER_JSMN2,
  PARSER_JSMN,
  PARSER_JREAD,
  PARSER_SJSON,
} ParserType;

typedef struct {
  ParserType type;
  union {
    JsonParser* json;
    JsonSt* jsonst;
    jsmn2_parser* jsmn2;
    jsmn_parser* jsmn;
    sjson_ctx_t* sjson;
    void* generic;
  } impl;

} ParserContext;

VISIBLE JSClassID js_json_parser_class_id = 0;
static JSValue json_parser_proto, json_parser_ctor;

static JSValue
js_json_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  return ret;
}

static JSValue
js_json_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  return ret;
}

struct json_parser_opaque {
  JSContext* ctx;
  JSObject* obj;
};

static JSValue js_json_parser_wrap(JSContext*, JsonParser*);

static void
json_parser_callback(JsonParser* parser, JsonValueType type, void* ptr) {
  struct json_parser_opaque* op = parser->opaque;
  JSContext* ctx = op->ctx;
  JSValue fn = js_value_mkobj(op->obj);
  JSValue args[] = {
      js_json_parser_wrap(ctx, json_dup(parser)),
      JS_NewInt32(ctx, type),
      ptr ? JS_NewString(ctx, ptr) : JS_UNDEFINED,
  };

  JSValue ret = JS_Call(ctx, fn, JS_UNDEFINED, countof(args), args);
  JS_FreeValue(ctx, ret);

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, args[2]);
}

static int
sjson_callback(const char* buf, uint16_t len, sjson_type_t type, uint8_t depth, void* opaque) {
  JSCallback* cb = opaque;
  JSValueConst args[]={

  };


  JSValue ret = js_callback_call(cb, countof(args), args);

  JS_FreeValue(cb->ctx, ret);

  return 0;
}

static JSValue
js_json_parser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  ParserContext* pctx;

  if(!(pctx = js_malloc(ctx, sizeof(ParserContext))))
    return JS_EXCEPTION;

  pctx->impl.generic = NULL;
  pctx->type = argc > 0 ? js_toint32(ctx, argv[0]) : 0;

  // pctx->input = js_input_chars(ctx, argv[1]);

  switch(pctx->type) {
    case PARSER_JSON: {
      break;
    }
    case PARSER_JSONST: {
      break;
    }
    case PARSER_JSMN2: {
      break;
    }
    case PARSER_JSMN: {
      break;
    }
    case PARSER_JREAD: {
      break;
    }
    case PARSER_SJSON: {
      uint32_t bufsize = js_touint32(ctx, argv[1]);
      int64_t i = 0, len = argc > 2 ? js_array_length(ctx, argv[2]) : 0;

      if((pctx->impl.sjson =
              js_mallocz(ctx, sizeof(sjson_ctx_t) + sizeof(int) + sizeof(sjson_cb_t) * (len + 1) + bufsize))) {
        int* status_p = (void*)&pctx->impl.sjson[1];
        sjson_cb_t* cbs = (void*)&status_p[1];

        while(i < len) {
          JSValue entry = JS_GetPropertyUint32(ctx, argv[2], i);

          char* key = js_get_propertyint_string(ctx, entry, 0);
          JSValue fn = JS_GetPropertyUint32(ctx, entry, 1);
          struct js_callback* cb = js_callback(ctx, fn);
          JS_FreeValue(ctx, fn);
          JS_FreeValue(ctx, entry);

          cbs[i++] = (sjson_cb_t){key, sjson_callback, cb};
        }

        cbs[i] = (sjson_cb_t){NULL, NULL, NULL};

        int s = sjson_init(pctx->impl.sjson, (void*)&cbs[len + 1], bufsize, cbs);

        if(s != SJSON_STATUS_OK) {
          for(i = 0; i < len; i++) {
            js_free(ctx, (void*)cbs[i].key);
            js_callback_free(cbs[i].opaque);
          }

          js_free(ctx, pctx->impl.sjson);
          // input_buffer_free(&pctx->input, ctx);
          js_free(ctx, pctx);
          return JS_ThrowInternalError(ctx, "sjson_init failed %d", s);
        }
      }

      break;
    }
  }

  /* if(!(parser = json_new(argc > 0 ? argv[0] : JS_UNDEFINED, ctx)))
     return JS_EXCEPTION;*/

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, json_parser_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_json_parser_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, pctx);

  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_json_parser_wrap2(JSContext* ctx, JSValueConst proto, JsonParser* parser) {
  ParserContext* pctx;

  if(!(pctx = js_malloc(ctx, sizeof(ParserContext))))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectProtoClass(ctx, proto, js_json_parser_class_id);

  pctx->type = PARSER_JSON;
  pctx->impl.json = json_dup(parser);

  JS_SetOpaque(obj, pctx);

  return obj;
}

static JSValue
js_json_parser_wrap(JSContext* ctx, JsonParser* parser) {
  return js_json_parser_wrap2(ctx, json_parser_proto, parser);
}

enum {
  JSON_PARSER_PARSE,
};

static JSValue
js_json_parser_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  ParserContext* pctx;
  JSValue ret = JS_UNDEFINED;

  if(!(pctx = JS_GetOpaque2(ctx, this_val, js_json_parser_class_id)))
    return JS_EXCEPTION;

  switch(pctx->type) {
    case PARSER_JSON: {
      JsonParser* parser = pctx->impl.json;

      switch(magic) {
        case JSON_PARSER_PARSE: {
          JsonParseState state = json_parse(parser, ctx);

          ret = JS_NewString(ctx,
                             (const char*[]){
                                 "EOF",
                                 "error",
                                 "waitingfirstchar",
                                 "parsing",
                                 "parsing-object",
                                 "parsing-array",
                                 "parsing-string",
                                 "parsing-primitive",
                                 "expecting-comma-or-end",
                                 "expecting-colon",
                             }[state + 2]);
          break;
        }
      }

      break;
    }

    case PARSER_SJSON: {

      switch(magic) {
        case JSON_PARSER_PARSE: {
          InputBuffer input = js_input_args(ctx, argc, argv);
          int* status_p = (void*)&pctx->impl.sjson[1];

          *status_p = sjson_parse(pctx->impl.sjson, input_buffer_data(&input), input_buffer_length(&input));

          ret = JS_NewInt32(ctx, *status_p);

          input_buffer_free(&input, ctx);
          break;
        }
      }

      break;
    }
  }

  return ret;
}

enum {
  JSON_PARSER_TYPE,
  JSON_PARSER_STATE,
  JSON_PARSER_VALUETYPE,
  JSON_PARSER_BUF,
  JSON_PARSER_UNGET,
  JSON_PARSER_CALLBACK,
  JSON_PARSER_POS,
  JSON_PARSER_STACK,
};

static JSValue
js_json_parser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  ParserContext* pctx;
  JSValue ret = JS_UNDEFINED;

  if(!(pctx = JS_GetOpaque2(ctx, this_val, js_json_parser_class_id)))
    return JS_EXCEPTION;

  if(magic == JSON_PARSER_TYPE) {
    // ret = JS_NewInt32(ctx, pctx->type);
    ret = JS_NewString(ctx,
                       (const char*[]){
                           "<unknown>",
                           "json",
                           "jsonst",
                           "jsmn2",
                           "jsmn",
                           "jread",
                           "sjson",
                       }[pctx->type]);
  } else
    switch(pctx->type) {
      case PARSER_JSON: {
        JsonParser* parser = pctx->impl.json;

        switch(magic) {
          case JSON_PARSER_STATE: {

            ret = JS_NewString(ctx,
                               (const char*[]){
                                   "EOF",
                                   "error",
                                   "waitingfirstchar",
                                   "parsing",
                                   "parsing-object",
                                   "parsing-array",
                                   "parsing-string",
                                   "parsing-primitive",
                                   "expecting-comma-or-end",
                                   "expecting-colon",
                               }[parser->stack->state + 2]);

            break;
          }
          case JSON_PARSER_VALUETYPE: {
            ret = JS_NewString(ctx,
                               (const char*[]){
                                   "none",
                                   "object",
                                   "object-end",
                                   "array",
                                   "array-end",
                                   "key",
                                   "string",
                                   "true",
                                   "false",
                                   "null",
                                   "number",
                               }[parser->stack->type + 1]);

            break;
          }
          case JSON_PARSER_STACK: {
            if(parser->stack) {
              ret = JS_NewArray(ctx);

              struct JsonParserStack* st;
              uint32_t n = 0, i = 0;

              for(st = parser->stack; st; st = st->parent)
                ++n;

              for(st = parser->stack; st; st = st->parent) {
                ++i;

                JSValue item = JS_NewObjectProto(ctx, JS_NULL);

                JS_SetPropertyStr(ctx, item, "state", JS_NewInt32(ctx, st->state));
                JS_SetPropertyStr(ctx, item, "type", JS_NewInt32(ctx, st->type));

                JS_SetPropertyUint32(ctx, ret, n - i, item);

                st = st->parent;
              }
            }
            break;
          }
          case JSON_PARSER_BUF: {
            if(parser->unget)
              ret = JS_NewArrayBufferCopy(ctx, parser->buf.base, MIN_NUM(parser->unget, parser->buf.size));
            break;
          }
          case JSON_PARSER_UNGET: {
            ret = JS_NewUint32(ctx, parser->unget);
            break;
          }
          case JSON_PARSER_CALLBACK: {
            ret = parser->opaque ? JS_DupValue(ctx, js_value_mkobj(parser->opaque)) : JS_NULL;
            break;
          }
          case JSON_PARSER_POS: {
            ret = JS_NewUint32(ctx, parser->pos);
            break;
          }
        }
        break;
      }

      case PARSER_JSONST: {
        break;
      }
      case PARSER_JSMN2: {
        break;
      }
      case PARSER_JSMN: {
        break;
      }
      case PARSER_JREAD: {
        break;
      }
      case PARSER_SJSON: {
        switch(magic) {
          case JSON_PARSER_STATE: {
            int* status_p = (void*)&pctx->impl.sjson[1];

            ret = JS_NewString(ctx, sjson_status_to_str(*status_p));
            break;
          }
        }

        break;
      }
    }

  return ret;
}

static JSValue
js_json_parser_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  ParserContext* pctx;
  JSValue ret = JS_UNDEFINED;

  if(!(pctx = JS_GetOpaque2(ctx, this_val, js_json_parser_class_id)))
    return JS_EXCEPTION;

  switch(pctx->type) {
    case PARSER_JSON: {
      JsonParser* parser = pctx->impl.json;

      switch(magic) {
        case JSON_PARSER_CALLBACK: {
          struct json_parser_opaque* op;

          if(!JS_IsFunction(ctx, value))
            return JS_ThrowTypeError(ctx, "value must be a function");

          if(parser->opaque) {
            op = parser->opaque;
            JS_FreeValue(ctx, js_value_mkobj(op->obj));
          }

          op = parser->opaque ? parser->opaque : js_malloc(ctx, sizeof(struct json_parser_opaque));

          if(op) {
            *op = (struct json_parser_opaque){ctx, js_value_obj(JS_DupValue(ctx, value))};

            parser->callback = json_parser_callback;
            parser->opaque = op;
          }

          break;
        }
      }

      break;
    }
  }

  return ret;
}

static void
js_json_parser_finalizer(JSRuntime* rt, JSValue obj) {
  ParserContext* pctx;

  if((pctx = JS_GetOpaque(obj, js_json_parser_class_id))) {

    switch(pctx->type) {
      case PARSER_JSON: {
        json_free(pctx->impl.json, rt);
        break;
      }
    }
  }
}

static const JSCFunctionListEntry js_json_parser_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("parse", 0, js_json_parser_method, JSON_PARSER_PARSE),
    JS_CGETSET_MAGIC_DEF("type", js_json_parser_get, 0, JSON_PARSER_TYPE),
    JS_CGETSET_MAGIC_FLAGS_DEF("state", js_json_parser_get, 0, JSON_PARSER_STATE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("valueType", js_json_parser_get, 0, JSON_PARSER_VALUETYPE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("buf", js_json_parser_get, 0, JSON_PARSER_BUF),
    JS_CGETSET_MAGIC_DEF("unget", js_json_parser_get, 0, JSON_PARSER_UNGET),
    JS_CGETSET_MAGIC_FLAGS_DEF("pos", js_json_parser_get, 0, JSON_PARSER_POS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("callback", js_json_parser_get, js_json_parser_set, JSON_PARSER_CALLBACK),
    JS_CGETSET_MAGIC_DEF("stack", js_json_parser_get, 0, JSON_PARSER_STACK),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JsonParser", JS_PROP_CONFIGURABLE),
};

static JSClassDef js_json_parser_class = {
    .class_name = "JsonParser",
    .finalizer = js_json_parser_finalizer,
};

static const JSCFunctionListEntry js_json_funcs[] = {
    JS_CFUNC_DEF("read", 1, js_json_read),
    JS_CFUNC_DEF("write", 2, js_json_write),
    JS_CONSTANT(PARSER_JSON),
    JS_CONSTANT(PARSER_JSONST),
    JS_CONSTANT(PARSER_JSMN2),
    JS_CONSTANT(PARSER_JSMN),
    JS_CONSTANT(PARSER_JREAD),
    JS_CONSTANT(PARSER_SJSON),
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
