#include "stream-utils.h"
#include "utils.h"
#include "virtual-properties.h"
#include "json.h"
#include "vector.h"
#include "property-enumeration.h"
#include <math.h>
#define SJ_IMPL
#include "sj.h"

#define REC_DEPTH(v) vector_size((v), sizeof(PropertyEnumeration))
#define REC_TOP(v) vector_back((v), sizeof(PropertyEnumeration))
#define REC_EMPLACE(v) vector_emplace((v), sizeof(PropertyEnumeration))
#define REC_POP(v) vector_pop((v), sizeof(PropertyEnumeration))

typedef struct {
  JSValue obj;
  sj_Value sj;
  uint32_t index;
  BOOL is_object;
} ParseFrame;

VISIBLE JSClassID js_json_parser_class_id = 0;
static JSValue json_parser_proto, json_parser_ctor;

struct js_json_parser_opaque {
  JSContext* ctx;
  JSObject *parser, *obj;
};

static JSValue
parse_primitive(JSContext* ctx, sj_Value val) {
  switch(val.type) {
    case SJ_NUMBER: {
      double num;
      scan_double(val.start, &num);
      return JS_NewFloat64(ctx, num);
    }

    case SJ_STRING: return JS_NewStringLen(ctx, val.start, val.end - val.start);
    case SJ_NULL: return JS_NULL;
    case SJ_BOOL: return val.start[0] == 't' ? JS_TRUE : JS_FALSE;
  }

  return JS_UNDEFINED;
}

static JSValue
parse_make_container(JSContext* ctx, int type) {
  return type == SJ_OBJECT ? JS_NewObjectProto(ctx, JS_NULL) : JS_NewArray(ctx);
}

static JSValue
parse_throw(JSContext* ctx, sj_Reader* r) {
  int line, col;
  sj_location(r, &line, &col);
  return JS_ThrowInternalError(ctx, "error: %d:%d: %s\n", line, col, r->error ? r->error : "parse error");
}

static void
parse_stack_free(JSContext* ctx, Vector* stack) {
  ParseFrame* it;

  vector_foreach_t(stack, it) {
    JS_FreeValue(ctx, it->obj);
  }

  vector_free(stack);
}

static JSValue
parse_val(JSContext* ctx, sj_Reader* r, sj_Value root) {
  Vector stack;
  JSValue ret = JS_UNDEFINED;

  if(root.type == SJ_ERROR)
    return parse_throw(ctx, r);

  if(root.type != SJ_ARRAY && root.type != SJ_OBJECT)
    return parse_primitive(ctx, root);

  vector_init(&stack, ctx);

  ParseFrame frame = (ParseFrame){parse_make_container(ctx, root.type), root, 0, root.type == SJ_OBJECT};

  if(!vector_put(&stack, &frame, sizeof(ParseFrame))) {
    JS_FreeValue(ctx, frame.obj);
    return JS_EXCEPTION;
  }

  while(!vector_empty(&stack)) {
    ParseFrame* top = vector_back(&stack, sizeof(ParseFrame));
    sj_Value k, v;
    BOOL more;

    if(top->is_object)
      more = sj_iter_object(r, top->sj, &k, &v);
    else
      more = sj_iter_array(r, top->sj, &v);

    if(!more) {
      if(r->error) {
        parse_stack_free(ctx, &stack);
        return parse_throw(ctx, r);
      }

      JSValue done = top->obj;
      vector_pop(&stack, sizeof(ParseFrame));

      if(vector_empty(&stack)) {
        ret = done;
        break;
      }

      JS_FreeValue(ctx, done);
      continue;
    }

    if(v.type == SJ_ERROR) {
      parse_stack_free(ctx, &stack);
      return parse_throw(ctx, r);
    }

    if(v.type == SJ_ARRAY || v.type == SJ_OBJECT) {
      JSValue child = parse_make_container(ctx, v.type);

      if(top->is_object) {
        JSAtom atom = JS_NewAtomLen(ctx, k.start, k.end - k.start);
        JS_SetProperty(ctx, top->obj, atom, JS_DupValue(ctx, child));
        JS_FreeAtom(ctx, atom);
      } else {
        JS_SetPropertyUint32(ctx, top->obj, top->index++, JS_DupValue(ctx, child));
      }

      frame = (ParseFrame){child, v, 0, v.type == SJ_OBJECT};

      if(!vector_put(&stack, &frame, sizeof(ParseFrame))) {
        JS_FreeValue(ctx, child);
        parse_stack_free(ctx, &stack);
        return JS_EXCEPTION;
      }

    } else {
      JSValue prim = parse_primitive(ctx, v);

      if(top->is_object) {
        JSAtom atom = JS_NewAtomLen(ctx, k.start, k.end - k.start);
        JS_SetProperty(ctx, top->obj, atom, prim);
        JS_FreeAtom(ctx, atom);
      } else {
        JS_SetPropertyUint32(ctx, top->obj, top->index++, prim);
      }
    }
  }

  vector_free(&stack);
  return ret;
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
    JS_ThrowReferenceError(ctx, "json.read(): expecting buffer or string");
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

static void
write_json_string(Writer* wr, const char* s, size_t len) {
  writer_putc(wr, '"');

  for(size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];

    switch(c) {
      case '"': writer_puts(wr, "\\\""); break;
      case '\\': writer_puts(wr, "\\\\"); break;
      case '\b': writer_puts(wr, "\\b"); break;
      case '\f': writer_puts(wr, "\\f"); break;
      case '\n': writer_puts(wr, "\\n"); break;
      case '\r': writer_puts(wr, "\\r"); break;
      case '\t': writer_puts(wr, "\\t"); break;
      default:
        if(c < 0x20) {
          char buf[32];
          writer_write(wr, buf, snprintf(buf, sizeof(buf), "\\u%04x", c));
        } else
          writer_putc(wr, c);
        break;
    }
  }

  writer_putc(wr, '"');
}

static void
clear_pending_exception(JSContext* ctx) {
  JSValue exc = JS_GetException(ctx);

  if(!JS_IsNull(exc) && !JS_IsUndefined(exc))
    JS_FreeValue(ctx, exc);
}

static void
write_json_primitive(JSContext* ctx, Writer* wr, JSValueConst val) {
  if(JS_IsNull(val)) {
    writer_puts(wr, "null");
    return;
  }

  if(JS_IsUndefined(val) || JS_IsSymbol(val) || JS_IsFunction(ctx, val)) {
    writer_puts(wr, "null");
    return;
  }

  if(JS_IsBool(val)) {
    writer_puts(wr, JS_ToBool(ctx, val) ? "true" : "false");
    return;
  }

  if(JS_IsString(val)) {
    size_t len;
    const char* s = JS_ToCStringLen(ctx, &len, val);

    if(s) {
      write_json_string(wr, s, len);
      JS_FreeCString(ctx, s);
    } else {
      writer_puts(wr, "null");
      clear_pending_exception(ctx);
    }
    return;
  }

  if(JS_IsNumber(val)) {
    double d;

    JS_ToFloat64(ctx, &d, val);

    if(isnan(d) || isinf(d)) {
      writer_puts(wr, "null");
      return;
    }
  }

  if(js_is_numeric(ctx, val)) {
    size_t len;
    const char* s = JS_ToCStringLen(ctx, &len, val);

    if(s) {
      writer_write(wr, (const uint8_t*)s, len);
      JS_FreeCString(ctx, s);
    } else {
      writer_puts(wr, "null");
      clear_pending_exception(ctx);
    }
    return;
  }

  /* Fallback: object-typed value that reached here (e.g. a circular
   * container that we refuse to recurse into). JS_ToCString invokes
   * the value's toString, which for arrays calls Array.prototype.join.
   * That can throw — typically InternalError "stack overflow" when the
   * referenced structure is deep, or TypeError when an element is a
   * Symbol. We write "null" and discard the pending exception so it
   * doesn't leak past the writer. */
  size_t len;
  const char* s = JS_ToCStringLen(ctx, &len, val);

  if(s) {
    write_json_string(wr, s, len);
    JS_FreeCString(ctx, s);
  } else {
    writer_puts(wr, "null");
    clear_pending_exception(ctx);
  }
}

static int
write_push(Vector* stack, JSContext* ctx, JSValue obj, int flags) {
  PropertyEnumeration* it;
  JSPropertyEnum* tmp;
  uint32_t len = 0;

  if(JS_GetOwnPropertyNames(ctx, &tmp, &len, obj, flags)) {
    JS_FreeValue(ctx, obj);
    return -1;
  }

  if(!(it = REC_EMPLACE(stack))) {
    js_propertyenums_free(ctx, tmp, len);
    JS_FreeValue(ctx, obj);
    return -1;
  }

  *it = (PropertyEnumeration)PROPENUM_INIT();
  it->obj = obj;
  it->tab_atom_len = len;

  if(len > 0) {
    if(!(it->tab_atom = js_malloc(ctx, sizeof(JSAtom) * len))) {
      js_propertyenums_free(ctx, tmp, len);
      JS_FreeValue(ctx, obj);
      REC_POP(stack);
      return -1;
    }

    for(uint32_t i = 0; i < len; i++)
      it->tab_atom[i] = JS_DupAtom(ctx, tmp[i].atom);
  }

  js_propertyenums_free(ctx, tmp, len);
  return 0;
}

static void
write_indent(Writer* wr, int indent, int n, DynBuf* ws) {
  if(indent) {
    int count = indent * n;
    size_t oldsize = ws->size;

    writer_putc(wr, '\n');

    if(oldsize != count) {
      dbuf_realloc(ws, count);

      if(count > oldsize)
        memset(&ws->buf[oldsize], ' ', ws->allocated_size - oldsize);

      ws->size = count;
    }

    writer_write(wr, ws->buf, count);
  }
}

static JSValue
js_json_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  DynBuf out, space;
  Vector stack;
  const int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
  int32_t indent = 0;

  if(argc > 1)
    JS_ToInt32(ctx, &indent, argv[1]);

  dbuf_init2(&out, 0, 0);
  dbuf_init2(&space, 0, 0);

  Writer wr = writer_from_dynbuf(&out);

  if(!JS_IsObject(argv[0]) || JS_IsFunction(ctx, argv[0])) {
    write_json_primitive(ctx, &wr, argv[0]);
    JSValue ret = dbuf_tostring_free(&out, ctx);
    writer_free(&wr);
    return ret;
  }

  vector_init(&stack, ctx);

  if(write_push(&stack, ctx, JS_DupValue(ctx, argv[0]), flags)) {
    writer_free(&wr);
    vector_free(&stack);
    return JS_EXCEPTION;
  }

  writer_putc(&wr, JS_IsArray(ctx, argv[0]) ? '[' : '{');

  write_indent(&wr, indent, REC_DEPTH(&stack), &space);

  while(!vector_empty(&stack)) {
    PropertyEnumeration* top = REC_TOP(&stack);
    BOOL is_array = JS_IsArray(ctx, top->obj);

    if(top->idx >= top->tab_atom_len) {
      write_indent(&wr, indent, REC_DEPTH(&stack) - 1, &space);

      writer_putc(&wr, is_array ? ']' : '}');
      property_enumeration_reset(top, JS_GetRuntime(ctx));
      REC_POP(&stack);
      continue;
    }

    if(top->idx > 0) {
      writer_putc(&wr, ',');

      if(indent)
        write_indent(&wr, indent, REC_DEPTH(&stack), &space);
    } else {
    }

    if(!is_array) {
      size_t klen;
      const char* kstr = js_atom_to_cstringlen(ctx, &klen, top->tab_atom[top->idx]);

      if(kstr) {
        write_json_string(&wr, kstr, klen);
        JS_FreeCString(ctx, kstr);
      } else {
        writer_puts(&wr, "\"\"");
      }

      writer_putc(&wr, ':');
      if(indent)
        writer_putc(&wr, ' ');
    }

    JSValue val = property_enumeration_value(top, ctx);
    BOOL is_container = JS_IsObject(val) && !JS_IsFunction(ctx, val);

    if(is_container && !property_recursion_circular(&stack, val)) {
      writer_putc(&wr, JS_IsArray(ctx, val) ? '[' : '{');

      top->idx++;

      if(write_push(&stack, ctx, val, flags)) {
        property_recursion_free(&stack, JS_GetRuntime(ctx));
        writer_free(&wr);
        return JS_EXCEPTION;
      }

      write_indent(&wr, indent, REC_DEPTH(&stack), &space);

    } else {
      write_json_primitive(ctx, &wr, val);
      JS_FreeValue(ctx, val);
      top->idx++;
    }
  }

  JSValue ret = dbuf_tostring_free(&out, ctx);
  writer_free(&wr);
  dbuf_free(&space);
  vector_free(&stack);
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
      ret = JS_NewString(ctx,
                         (const char* const[]){
                             "NONE",
                             "OBJECT",
                             "OBJECT_END",
                             "ARRAY",
                             "ARRAY_END",
                             "KEY",
                             "STRING",
                             "TRUE",
                             "FALSE",
                             "NULL",
                             "NUMBER",
                         }[type + 1]);
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
      ret = dbuf_tostring(&parser->token, ctx);
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
  }

  return m;
}
