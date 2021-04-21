#define _GNU_SOURCE

#include "quickjs.h"
#include "libregexp.h"
#include "quickjs-lexer.h"
#include "vector.h"
#include <string.h>
#include <ctype.h>
#include <stdint.h>

typedef struct {
  JSValue action;
  BOOL skip;
} JSLexerRule;

VISIBLE JSClassID js_syntaxerror_class_id = 0, js_token_class_id = 0, js_lexer_class_id = 0;
static JSValue location_ctor;
static JSValue syntaxerror_proto, syntaxerror_ctor;
static JSValue token_proto, token_ctor;
static JSValue lexer_proto, lexer_ctor;

static JSValue js_location_new(JSContext* ctx, const Location* loc);

static void
js_location_dump(JSContext* ctx, JSValueConst this_val, DynBuf* dbuf) {
  JSValue file;
  file = JS_GetPropertyStr(ctx, this_val, "file");
  if(JS_IsString(file)) {
    js_value_print(ctx, file, dbuf);
    dbuf_putc(dbuf, ':');
    js_value_free(ctx, file);
  }
  dbuf_printf(dbuf,
              "%" PRId32 ":%" PRId32,
              js_get_propertystr_int32(ctx, this_val, "line"),
              js_get_propertystr_int32(ctx, this_val, "column"));
}

static JSValue
js_location_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  DynBuf db;
  JSValue ret;
  js_dbuf_init(ctx, &db);
  js_location_dump(ctx, this_val, &db);

  ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);
  dbuf_free(&db);
  return ret;
}

static BOOL
js_is_location(JSContext* ctx, JSValueConst obj) {
  BOOL ret;
  JSAtom line, column;
  line = JS_NewAtom(ctx, "line");
  column = JS_NewAtom(ctx, "column");
  ret = JS_IsObject(obj) && JS_HasProperty(ctx, obj, line) && JS_HasProperty(ctx, obj, column);
  JS_FreeAtom(ctx, line);
  JS_FreeAtom(ctx, column);
  return ret;
}

static Location
js_location_get(JSContext* ctx, JSValueConst this_val) {
  Location loc = {0, 0, 0, -1};

  loc.line = js_get_propertystr_int32(ctx, this_val, "line");
  loc.column = js_get_propertystr_int32(ctx, this_val, "column");
  loc.file = js_get_propertystr_cstring(ctx, this_val, "file");
  if(js_has_propertystr(ctx, this_val, "pos"))
    loc.pos = js_get_propertystr_uint64(ctx, this_val, "pos");

  return loc;
}

static JSValue
js_location_constructor(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Location loc = {0, 0, 0, -1};
  JSValue ret = JS_EXCEPTION;

  /* From string */
  if(argc == 1 && js_is_input(ctx, argv[0])) {
    InputBuffer in = js_input_buffer(ctx, argv[0]);
    const uint8_t *p, *begin = input_buffer_begin(&in), *end = input_buffer_end(&in);
    unsigned long v, n[2];
    size_t ni = 2;

    while(end > begin) {
      for(p = end; p > begin && *(p - 1) != ':'; p--) {}
      if(ni > 0) {
        v = strtoul((const char*)p, (char**)&end, 10);
        if(end > p)
          n[--ni] = v;
      } else {
        loc.file = js_cstring_newlen(ctx, (const char*)p, end - p);
        break;
      }
      end = p - 1;
    }
    if(ni == 0) {
      loc.line = n[0];
      loc.column = n[1];
    }

    /* Dup from object */
  } else if(JS_IsObject(argv[0])) {

    loc = js_location_get(ctx, argv[0]);

    /* From arguments (line,column,pos,file) */
  } else if(argc > 1) {

    JS_ToUint32(ctx, &loc.line, argv[0]);
    JS_ToUint32(ctx, &loc.column, argv[1]);
    if(argc > 2)
      JS_ToIndex(ctx, (uint64_t*)&loc.pos, argv[2]);
    if(argc > 3)
      loc.file = JS_ToCString(ctx, argv[3]);

  } else {
    return ret;
  }
  loc.line--;
  loc.column--;
  ret = js_location_new(ctx, &loc);

  if(loc.file)
    JS_FreeCString(ctx, loc.file);

  return ret;
}

static const JSCFunctionListEntry js_location_funcs[] = {
    JS_CFUNC_DEF("toString", 0, js_location_tostring),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Location", JS_PROP_CONFIGURABLE),
};

static JSValue
js_location_new(JSContext* ctx, const Location* loc) {
  JSValue ret = JS_NewObject(ctx);
  if(loc->file)
    JS_SetPropertyStr(ctx, ret, "file", JS_NewString(ctx, loc->file));
  JS_SetPropertyStr(ctx, ret, "line", JS_NewUint32(ctx, loc->line + 1));
  JS_SetPropertyStr(ctx, ret, "column", JS_NewUint32(ctx, loc->column + 1));

  if(loc->pos >= 0)
    JS_SetPropertyStr(ctx, ret, "pos", JS_NewInt64(ctx, loc->pos));

  JS_SetPropertyFunctionList(ctx, ret, js_location_funcs, countof(js_location_funcs));
  return ret;
}

static JSValue
js_lexer_rule_new(JSContext* ctx, Lexer* lex, LexerRule* rule) {

  DynBuf dbuf;
  js_dbuf_init(ctx, &dbuf);
  lexer_rule_expand(lex, rule, &dbuf);
  dbuf_0(&dbuf);

  JSValue ret = JS_NewArray(ctx);
  js_set_propertyint_string(ctx, ret, 0, rule->name);
  js_set_propertyint_string(ctx, ret, 1, (const char*)dbuf.buf);
  JS_SetPropertyUint32(ctx, ret, 2, JS_NewInt64(ctx, rule->mask));

  dbuf_free(&dbuf);
  return ret;
}

enum syntaxerror_getters { SYNTAXERROR_PROP_LOC = 0, SYNTAXERROR_PROP_MESSAGE };

JSValue
js_syntaxerror_new(JSContext* ctx, SyntaxError arg) {
  SyntaxError* err;
  JSValue obj = JS_UNDEFINED;

  if(!(err = js_mallocz(ctx, sizeof(SyntaxError))))
    return JS_EXCEPTION;

  memcpy(err, &arg, sizeof(SyntaxError));

  obj = JS_NewObjectProtoClass(ctx, syntaxerror_proto, js_syntaxerror_class_id);
  JS_SetOpaque(obj, err);
  JS_SetPropertyStr(ctx, obj, "message", JS_NewString(ctx, err->message));
  return obj;
}

static JSValue
js_syntaxerror_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  SyntaxError* err;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  int i;
  if(!(err = js_mallocz(ctx, sizeof(SyntaxError))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_syntaxerror_class_id);
  js_value_free(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, err);

  for(i = 0; i < argc; i++) {
    if(JS_IsString(argv[i]) && !err->message) {
      err->message = js_tostring(ctx, argv[i]);
    } else if(js_is_location(ctx, argv[i])) {
      err->loc = js_location_get(ctx, argv[i]);
    }
  }

  return obj;
fail:
  js_free(ctx, err);
  js_value_free(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_syntaxerror_get(JSContext* ctx, JSValueConst this_val, int magic) {
  SyntaxError* err;
  JSValue ret = JS_UNDEFINED;
  if(!(err = js_syntaxerror_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SYNTAXERROR_PROP_LOC: {
      ret = js_location_new(ctx, &err->loc);
      break;
    }
    case SYNTAXERROR_PROP_MESSAGE: {
      ret = JS_NewString(ctx, err->message);
      break;
    }
  }
  return ret;
}

static JSValue
js_syntaxerror_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  SyntaxError* err;
  DynBuf db;
  JSValue ret;

  if(!(err = js_syntaxerror_data(ctx, this_val)))
    return JS_EXCEPTION;

  js_dbuf_init(ctx, &db);
  location_print(&err->loc, &db);

  if(err->message) {
    dbuf_putc(&db, ' ');
    dbuf_putstr(&db, err->message);
  }

  ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);
  dbuf_free(&db);
  return ret;
}

static void
js_syntaxerror_finalizer(JSRuntime* rt, JSValue val) {
  SyntaxError* err = JS_GetOpaque(val, js_syntaxerror_class_id);
  if(err) {
    if(err->message)
      js_free_rt(rt, (char*)err->message);
    location_free(&err->loc, rt);
  }
  // js_value_free_rt(rt, val);
}

static JSClassDef js_syntaxerror_class = {
    .class_name = "SyntaxError",
    .finalizer = js_syntaxerror_finalizer,
};

static const JSCFunctionListEntry js_syntaxerror_proto_funcs[] = {
    JS_CGETSET_ENUMERABLE_DEF("loc", js_syntaxerror_get, 0, SYNTAXERROR_PROP_LOC),
    JS_CGETSET_ENUMERABLE_DEF("message", js_syntaxerror_get, 0, SYNTAXERROR_PROP_MESSAGE),
    JS_CFUNC_DEF("toString", 0, js_syntaxerror_tostring),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SyntaxError", JS_PROP_CONFIGURABLE),
};

enum token_methods { TO_STRING = 0 };
enum token_getters {
  TOKEN_PROP_BYTELENGTH = 0,
  TOKEN_PROP_CHARLENGTH,
  TOKEN_PROP_START,
  TOKEN_PROP_END,
  TOKEN_PROP_CHARRANGE,
  TOKEN_PROP_LEXEME,
  TOKEN_PROP_LOC,
  TOKEN_PROP_ID,
  TOKEN_PROP_TYPE,
  TOKEN_PROP_RULE
};

static void
token_free(Token* tok, JSRuntime* rt) {

  location_free(&tok->loc, rt);

  js_free_rt(rt, tok->lexeme);
  js_free_rt(rt, tok);
}

Token*
js_token_new(JSContext* ctx, int id, const char* lexeme, const Location* loc, uint64_t byte_offset) {
  Token* tok;

  if(!(tok = js_mallocz(ctx, sizeof(Token))))
    return 0;

  tok->id = id;
  tok->lexeme = js_strdup(ctx, lexeme);
  tok->loc = *loc;
  tok->byte_offset = byte_offset;

  return tok;
}

JSValue
js_token_wrap(JSContext* ctx, Token* tok) {
  JSValue obj = JS_UNDEFINED;

  obj = JS_NewObjectProtoClass(ctx, token_proto, js_token_class_id);
  JS_SetOpaque(obj, tok);
  return obj;
}

static JSValue
js_token_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  Token* tok;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  if(!(tok = js_mallocz(ctx, sizeof(Token))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_token_class_id);
  js_value_free(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, tok);

  if(argc > 0)
    JS_ToInt32(ctx, &tok->id, argv[0]);
  if(argc > 1)
    tok->lexeme = js_tostring(ctx, argv[1]);
  if(argc > 2)
    tok->loc = js_location_get(ctx, argv[2]);
  if(argc > 3)
    JS_ToInt64(ctx, &tok->loc.pos, argv[3]);

  return obj;
fail:
  js_free(ctx, tok);
  js_value_free(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_token_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Token* tok;

  if(!(tok = js_token_data(ctx, this_val)))
    return JS_EXCEPTION;

  return JS_NewStringLen(ctx, (const char*)tok->lexeme, tok->byte_length);
}

static JSValue
js_token_toprimitive(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Token* tok;
  const char* hint;
  JSValue ret;

  if(!(tok = js_token_data(ctx, this_val)))
    return JS_EXCEPTION;

  hint = JS_ToCString(ctx, argv[0]);

  if(!strcmp(hint, "number"))
    ret = JS_NewInt32(ctx, tok->id);
  else
    ret = js_token_tostring(ctx, this_val, argc, argv);

  js_cstring_free(ctx, hint);

  return ret;
}

static JSValue
js_token_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Token* tok;
  LexerRule* rule;
  DynBuf dbuf;
  JSValue ret;
  BOOL multiline = TRUE;

  if(!(tok = js_token_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(argc > 0)
    multiline = JS_ToBool(ctx, argv[0]);

  js_dbuf_init(ctx, &dbuf);

  rule = lexer_rule_at(tok->lexer, tok->id);

  dbuf_printf(&dbuf,
              multiline ? "Token {\n  id: %i, type: '%s' ,\n  lexeme: '" : "Token { id: %i, type: %s, lexeme: '",
              tok->id,
              rule->name);
  dbuf_put_escaped(&dbuf, (const char*)tok->lexeme, tok->byte_length);
  /*dbuf_printf(&dbuf,
              multiline ? "',\n  offset: %" PRIu32 ",\n  byte_length: %" PRIu32 ",\n  char_length: %" PRIu32 ",\n  loc:
     " : "', offset: %" PRIu32 ", byte_length: %" PRIu32 ", char_length: %" PRIu32 ", loc: ", tok->offset,
              tok->byte_length,
              tok->char_length);*/
  dbuf_printf(&dbuf,
              multiline ? "',\n  start: %zu,\n  end: %zu,\n  length: %" PRIu32 ",\n  loc: "
                        : "', start: %zu, end: %zu, length: %" PRIu32 ", loc: ",
              tok->loc.pos,
              tok->loc.pos + tok->char_length,
              tok->char_length);
  location_print(&tok->loc, &dbuf);
  dbuf_putstr(&dbuf, multiline ? "\n}" : " }");

  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
  return ret;
}

static JSValue
js_token_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Token* tok;
  JSValue ret = JS_UNDEFINED;
  if(!(tok = js_token_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case TOKEN_PROP_BYTELENGTH: {
      ret = JS_NewInt64(ctx, tok->byte_length);
      break;
    }
    case TOKEN_PROP_CHARLENGTH: {
      ret = JS_NewInt64(ctx, tok->char_length);
      break;
    }

    case TOKEN_PROP_START: {
      ret = JS_NewInt64(ctx, tok->loc.pos);
      break;
    }

    case TOKEN_PROP_END: {
      ret = JS_NewInt64(ctx, tok->loc.pos + tok->char_length);
      break;
    }

    case TOKEN_PROP_CHARRANGE: {
      ret = JS_NewArray(ctx);
      js_set_propertyint_int(ctx, ret, 0, tok->loc.pos);
      js_set_propertyint_int(ctx, ret, 1, tok->loc.pos + tok->char_length);
      break;
    }

    case TOKEN_PROP_LEXEME: {
      ret = JS_NewStringLen(ctx, (const char*)tok->lexeme, tok->byte_length);
      break;
    }

    case TOKEN_PROP_LOC: {
      ret = js_location_new(ctx, &tok->loc);
      break;
    }

    case TOKEN_PROP_ID: {
      ret = JS_NewInt32(ctx, tok->id);
      break;
    }
    case TOKEN_PROP_RULE: {
      LexerRule* rule = lexer_rule_at(tok->lexer, tok->id);

      ret = js_lexer_rule_new(ctx, tok->lexer, rule);
      break;
    }
    case TOKEN_PROP_TYPE: {
      LexerRule* rule = lexer_rule_at(tok->lexer, tok->id);

      ret = JS_NewString(ctx, rule->name);
      break;
    }
  }
  return ret;
}

static void
js_token_finalizer(JSRuntime* rt, JSValue val) {
  Token* tok = JS_GetOpaque(val, js_token_class_id);
  if(tok)
    token_free(tok, rt);
  // js_value_free_rt(rt, val);
}

static JSClassDef js_token_class = {
    .class_name = "Token",
    .finalizer = js_token_finalizer,
};

static const JSCFunctionListEntry js_token_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("byteLength", js_token_get, NULL, TOKEN_PROP_BYTELENGTH),
    JS_CGETSET_MAGIC_DEF("length", js_token_get, NULL, TOKEN_PROP_CHARLENGTH),
    JS_CGETSET_MAGIC_DEF("start", js_token_get, NULL, TOKEN_PROP_START),
    JS_CGETSET_MAGIC_DEF("end", js_token_get, NULL, TOKEN_PROP_END),
    JS_CGETSET_MAGIC_DEF("charRange", js_token_get, NULL, TOKEN_PROP_CHARRANGE),
    JS_CGETSET_MAGIC_DEF("loc", js_token_get, NULL, TOKEN_PROP_LOC),
    JS_CGETSET_MAGIC_DEF("id", js_token_get, NULL, TOKEN_PROP_ID),
    JS_CGETSET_MAGIC_DEF("type", js_token_get, NULL, TOKEN_PROP_TYPE),
    JS_CGETSET_MAGIC_DEF("rule", js_token_get, NULL, TOKEN_PROP_RULE),
    JS_CGETSET_MAGIC_DEF("lexeme", js_token_get, NULL, TOKEN_PROP_LEXEME),
    JS_CGETSET_MAGIC_DEF("value", js_token_get, NULL, TOKEN_PROP_LEXEME),
    JS_CFUNC_DEF("toString", 0, js_token_tostring),
    JS_CFUNC_DEF("[Symbol.toPrimitive]", 1, js_token_toprimitive),
    JS_CFUNC_DEF("inspect", 0, js_token_inspect),
    JS_ALIAS_DEF("position", "loc"),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Token", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_token_static_funcs[] = {
    JS_PROP_INT32_DEF("EOF", LEXER_EOF, JS_PROP_ENUMERABLE),
};

enum {
  LEXER_METHOD_SET_INPUT = 0,
  LEXER_METHOD_PEEKC,
  LEXER_METHOD_GETC,
  LEXER_METHOD_SKIPC,
  LEXER_METHOD_GET_RANGE,
  LEXER_METHOD_CURRENT_LINE,
  LEXER_METHOD_TOKEN_CLASS,
  LEXER_METHOD_GET_RULE,
  LEXER_METHOD_SKIPUNTIL,
  LEXER_METHOD_ERROR
};

enum lexer_getters {
  LEXER_PROP_SIZE = 0,
  LEXER_PROP_POS,
  LEXER_PROP_START,
  LEXER_PROP_EOF,
  LEXER_PROP_FILENAME,
  LEXER_PROP_LOC,
  LEXER_PROP_RULENAMES,
  LEXER_PROP_MODE,
  LEXER_PROP_BYTE_LENGTH
};

static Token*
lexer_token(Lexer* lex, int id, size_t bytelen, size_t charlen, Location loc, JSContext* ctx) {
  Token* tok;
  if((tok = js_mallocz(ctx, sizeof(Token)))) {
    tok->id = id;
    tok->loc = location_dup(&loc, ctx);
    tok->byte_length = bytelen;
    tok->char_length = charlen;
    tok->lexeme = js_strndup(ctx, (const char*)&lex->input.data[lex->start], tok->byte_length);
    tok->byte_offset = lex->start;
    tok->lexer = lex;
  }
  return tok;
}

JSValue
js_lexer_new(JSContext* ctx, JSValueConst proto, JSValueConst vinput, JSValueConst vmode) {
  Lexer* lex;
  int32_t mode = 0;
  JSValue obj = JS_UNDEFINED;
  if(!(lex = js_mallocz(ctx, sizeof(Lexer))))
    return JS_EXCEPTION;

  if(JS_IsNumber(vmode))
    JS_ToInt32(ctx, &mode, vmode);
  lexer_init(lex, mode, ctx);

  obj = JS_NewObjectProtoClass(ctx, proto, js_lexer_class_id);

  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, lex);

  lex->input = js_input_buffer(ctx, vinput);

  return obj;
fail:
  js_free(ctx, lex);
  js_value_free(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_lexer_wrap(JSContext* ctx, Lexer* lex) {
  JSValue obj;

  obj = JS_NewObjectProtoClass(ctx, lexer_proto, js_lexer_class_id);
  JS_SetOpaque(obj, lex);
  return obj;
}

static JSValue
js_lexer_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Lexer* lex;
  DynBuf dbuf;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  js_dbuf_init(ctx, &dbuf);
  lexer_dump(lex, &dbuf);

  return dbuf_tostring_free(&dbuf, ctx);
}

static JSValue
js_lexer_add_rule(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  Lexer* lex;
  char* name;
  int64_t mask = -1, skip = 0;
  RegExp expr;
  JSLexerRule* jsrule = 0;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  name = js_tostring(ctx, argv[0]);
  expr = regexp_from_argv(argc - 1, &argv[1], ctx);

  if(JS_IsNumber(argv[2]))
    JS_ToInt64(ctx, &mask, argv[2]);

  JSValue skipv = JS_GetPropertyStr(ctx, this_val, "skip");
  if(JS_IsNumber(skipv))
    JS_ToInt64(ctx, &skip, skipv);
  js_value_free(ctx, skipv);

  if(argc > 3) {
    jsrule = js_malloc(ctx, sizeof(JSLexerRule));

    jsrule->action = JS_DupValue(ctx, argv[3]);
    jsrule->skip = !!(mask & skip);
  }

  if(magic) {
    int index = lexer_rule_add(lex, name, expr.source);
    LexerRule* rule = lexer_rule_at(lex, index);

    rule->mask = jsrule ? mask & (~skip) : mask;
    rule->opaque = jsrule;

    return JS_NewInt32(ctx, index);
  } else {
    lexer_define(lex, name, expr.source);
  }

  return JS_UNDEFINED;
}

static JSValue
js_lexer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  JSValue proto, ret;
  Lexer* lex;
  int64_t mask = MASK_ALL;
  /* using new_target to get the prototype is necessary when the
      class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = lexer_proto; // JS_DupValue(ctx, lexer_proto);

  ret = js_lexer_new(ctx, proto, argc > 0 ? argv[0] : JS_UNDEFINED, argc > 1 ? argv[1] : JS_UNDEFINED);

  if((lex = js_lexer_data(ctx, ret))) {
    int i = 2;

    if(lex->loc.file == 0 && i < argc && JS_IsString(argv[i]))
      lex->loc.file = js_tostring(ctx, argv[i++]);

    if(i < argc && JS_IsNumber(argv[i])) {
      uint32_t mode = 0;
      JS_ToUint32(ctx, &mode, argv[i++]);
      lex->mode |= mode;
    }
    if(i < argc && JS_IsNumber(argv[i]))
      JS_ToInt64(ctx, &mask, argv[i++]);
  }

  JS_SetPropertyStr(ctx, ret, "mask", JS_NewInt64(ctx, mask));

  return ret;
}

static JSValue
js_lexer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  Lexer* lex;
  JSValue ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LEXER_METHOD_SET_INPUT: {
      Lexer* other;
      InputBuffer input;
      Location loc = {0, 0, 0, 0};

      if((other = JS_GetOpaque(argv[0], js_lexer_class_id))) {
        input = input_buffer_dup(&other->input, ctx);
        loc = other->loc;
        lex->start = other->start;
      } else {
        input = js_input_buffer(ctx, argv[0]);
      }

      input_buffer_free(&lex->input, ctx);
      lex->input = input;
      location_free(&lex->loc, JS_GetRuntime(ctx));
      lex->loc = loc;
      break;
    }
    case LEXER_METHOD_PEEKC: {
      if(!input_buffer_eof(&lex->input)) {
        size_t len;
        const uint8_t* buf = input_buffer_peek(&lex->input, &len);
        ret = JS_NewStringLen(ctx, (const char*)buf, len);
      }
      break;
    }

    case LEXER_METHOD_GETC: {
      if(!input_buffer_eof(&lex->input)) {
        size_t len;
        const uint8_t* buf = input_buffer_get(&lex->input, &len);
        ret = JS_NewStringLen(ctx, (const char*)buf, len);
      }
      break;
    }

    case LEXER_METHOD_SKIPC: {
      if(!input_buffer_eof(&lex->input)) {
        int32_t ntimes = 1;
        const uint8_t* p;
        size_t n;
        if(argc > 0)
          JS_ToInt32(ctx, &ntimes, argv[0]);
        while(ntimes-- > 0) { p = input_buffer_get(&lex->input, &n); }
        ret = JS_NewStringLen(ctx, (const char*)p, n);
      }
      break;
    }

    case LEXER_METHOD_SKIPUNTIL: {
      if(!input_buffer_eof(&lex->input)) {
        JSValueConst pred;
        if(!JS_IsFunction(ctx, argv[0]))
          return JS_ThrowTypeError(ctx, "argument 1 is not a function");
        pred = argv[0];
        while(lex->input.pos < lex->input.size) {
          size_t n;
          const uint8_t* p = input_buffer_peek(&lex->input, &n);
          JSValue str = JS_NewStringLen(ctx, (const char*)p, n);
          JSValue ret = JS_Call(ctx, pred, this_val, 1, &str);
          BOOL b = JS_ToBool(ctx, ret);
          js_value_free(ctx, ret);
          if(b) {
            ret = str;
            break;
          }
          js_value_free(ctx, str);
          input_buffer_getc(&lex->input);
        }
      }
      break;
    }

    case LEXER_METHOD_GET_RANGE: {
      size_t start, end;
      start = lex->start;
      end = lex->input.pos;
      if(argc > 0) {
        js_value_to_size(ctx, &start, argv[0]);
        if(argc > 1)
          js_value_to_size(ctx, &end, argv[1]);
      }
      ret = JS_NewStringLen(ctx, (const char*)&lex->input.data[start], end - start);
      break;
    }

    case LEXER_METHOD_CURRENT_LINE: {
      size_t start, end;
      start = lex->start;
      end = lex->input.pos;
      while(start > 0 && lex->input.data[start - 1] != '\n') start--;
      while(end < lex->input.size && lex->input.data[end] != '\n') end++;
      ret = JS_NewStringLen(ctx, (const char*)&lex->input.data[start], end - start);
      break;
    }

    case LEXER_METHOD_TOKEN_CLASS: {
      Token* tok;
      LexerRule* rule;
      int32_t id = -1;

      if((tok = JS_GetOpaque(argv[0], js_token_class_id)))
        id = tok->id;
      else
        JS_ToInt32(ctx, &id, argv[0]);

      if((rule = lexer_rule_at(lex, id)))
        ret = JS_NewString(ctx, rule->name);
      break;
    }

    case LEXER_METHOD_GET_RULE: {
      LexerRule* rule;

      if(JS_IsString(argv[0])) {
        size_t len;
        const char* name = JS_ToCStringLen(ctx, &len, argv[0]);
        if(!(rule = lexer_rule_find(lex, name)))
          rule = lexer_find_definition(lex, name, len);
        JS_FreeCString(ctx, name);
      } else {
        int32_t id = -1;
        JS_ToInt32(ctx, &id, argv[0]);
        rule = lexer_rule_at(lex, id);
      }

      if(rule)
        ret = js_lexer_rule_new(ctx, lex, rule);

      break;
    }

    case LEXER_METHOD_ERROR: {
      SyntaxError error;

      error.message = js_tostring(ctx, argv[0]);
      // error.byte_length = lex->input.pos - lex->start;
      error.loc = location_dup(&lex->loc, ctx);
      // error.loc.offset = lex->start;
      printf("lexer SyntaxError('%s', %u:%u)\n", error.message, lex->loc.line + 1, lex->loc.column + 1);
      ret = js_syntaxerror_new(ctx, error);
      break;
    }
  }
  return ret;
}

static JSValue
js_lexer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Lexer* lex;
  JSValue ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LEXER_PROP_POS: {
      ret = JS_NewInt64(ctx, lex->input.pos);
      break;
    }

    case LEXER_PROP_SIZE: {
      ret = JS_NewInt64(ctx, lex->input.size);
      break;
    }

    case LEXER_PROP_START: {
      ret = JS_NewInt64(ctx, lex->start);
      break;
    }

    case LEXER_PROP_EOF: {
      ret = JS_NewBool(ctx, input_buffer_eof(&lex->input));
      break;
    }

    case LEXER_PROP_FILENAME: {
      ret = lex->loc.file ? JS_NewString(ctx, lex->loc.file) : JS_UNDEFINED;
      break;
    }

    case LEXER_PROP_LOC: {
      ret = js_location_new(ctx, &lex->loc);
      break;
    }
    case LEXER_PROP_RULENAMES: {
      LexerRule* rule;
      uint32_t i = 0;
      ret = JS_NewArray(ctx);

      vector_foreach_t(&lex->rules, rule) { JS_SetPropertyUint32(ctx, ret, i++, JS_NewString(ctx, rule->name)); }
      break;
    }
    case LEXER_PROP_MODE: {
      ret = JS_NewInt32(ctx, lex->mode);
      break;
    }
    case LEXER_PROP_BYTE_LENGTH: {
      ret = JS_NewUint32(ctx, lex->bytelen);
      break;
    }
  }
  return ret;
}

static JSValue
js_lexer_tokens(JSContext* ctx, JSValueConst this_val) {
  Lexer* lex;
  LexerRule* rule;
  int i = 0;
  JSValue ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  ret = JS_NewObject(ctx);

  vector_foreach_t(&lex->rules, rule) { JS_SetPropertyStr(ctx, ret, rule->name, JS_NewInt32(ctx, i++)); }
  return ret;
}

static JSValue
js_lexer_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  return JS_DupValue(ctx, this_val);
}

static JSValue
js_lexer_escape(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  InputBuffer input = js_input_buffer(ctx, argv[0]);
  DynBuf output;
  js_dbuf_init(ctx, &output);
  dbuf_put_escaped_pred(&output, (const char*)input.data, input.size, iscntrl);
  return dbuf_tostring_free(&output, ctx);
}

static JSValue
js_lexer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Lexer* lex;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LEXER_PROP_POS: {
      js_value_to_size(ctx, &lex->input.pos, value);
      break;
    }

    case LEXER_PROP_SIZE: {
      js_value_to_size(ctx, &lex->input.size, value);
      break;
    }

      /*case LEXER_PROP_START: {
        js_value_to_size(ctx, &lex->start, value);
        break;
      }*/

    case LEXER_PROP_FILENAME: {
      if(lex->loc.file)
        js_free(ctx, (char*)lex->loc.file);
      lex->loc.file = js_tostring(ctx, value);
      break;
    }
    case LEXER_PROP_MODE: {
      int32_t m;
      JS_ToInt32(ctx, &m, value);
      lex->mode = m;
      break;
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_lexer_lex(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;
  Lexer* lex;
  int id;
  Location loc;
  int64_t state = MASK_ALL, skip = 0;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  loc = lex->loc;

  if(argc > 0 && JS_IsNumber(argv[0])) {
    JS_ToInt64(ctx, &state, argv[0]);
  } else {
    JSValue mask = JS_GetPropertyStr(ctx, this_val, "mask");

    if(JS_IsNumber(mask))
      JS_ToInt64(ctx, &state, mask);
    js_value_free(ctx, mask);
  }

  if(argc > 1 && JS_IsNumber(argv[1])) {
    JS_ToInt64(ctx, &skip, argv[1]);
  } else {
    JSValue mask = JS_GetPropertyStr(ctx, this_val, "skip");
    if(JS_IsNumber(mask))
      JS_ToInt64(ctx, &skip, mask);
    js_value_free(ctx, mask);
  }

  // printf("state = 0x%016" PRIx64 ", skip = 0x%016" PRIx64 "\n", state, skip);

  for(;;) {
    if((id = lexer_peek(lex, state | skip, ctx)) >= 0) {
      LexerRule* rule = lexer_rule_at(lex, id);
      JSLexerRule* jsrule = rule->opaque;
      Token* tok;
      size_t bytelen, charlen;

      bytelen = lex->bytelen;
      charlen = lexer_skip(lex);

      if((rule->mask & skip)) {
        loc = lex->loc;
        continue;
      }

      if(jsrule) {
        if(JS_IsFunction(ctx, jsrule->action)) {

          JSValue args[] = {JS_NewInt32(ctx, id),
                            JS_NewStringLen(ctx, (const char*)&lex->input.data[lex->start], bytelen),
                            JS_NewInt64(ctx, state)};
          ret = JS_Call(ctx, jsrule->action, this_val, 3, args);
          JS_FreeValue(ctx, args[0]);
          JS_FreeValue(ctx, args[1]);
          JS_FreeValue(ctx, args[2]);

          if(JS_IsNumber(ret)) {
            JS_ToInt64(ctx, &state, ret);
            // printf("new state: %" PRIi64 "\n", state);
            JS_SetPropertyStr(ctx, this_val, "mask", ret);
          }
        }

        if(jsrule->skip) {
          loc = lex->loc;
          continue;
        }
      }

      tok = lexer_token(lex, id, bytelen, charlen, loc, ctx);
      ret = js_token_wrap(ctx, tok);

    } else if(id == LEXER_ERROR_NOMATCH) {

      JSValue handler = JS_GetPropertyStr(ctx, this_val, "handler");

      if(JS_IsFunction(ctx, handler)) {
        JSValue args[] = {JS_NewInt64(ctx, state), JS_NewInt64(ctx, skip)};
        JSValue ret;
        int64_t newState;

        ret = JS_Call(ctx, handler, this_val, 2, args);

        js_value_free(ctx, args[0]);
        js_value_free(ctx, args[1]);

        if(JS_IsNumber(ret)) {
          JS_ToInt64(ctx, &newState, ret);

          if(newState != state) {
            state = newState;
            continue;
          }
        }
      }
      ret = JS_ThrowInternalError(
          ctx,
          "%s:%" PRIu32 ":%" PRIu32 ": No matching token at:\n%.*s\n%*s",
          loc.file,
          loc.line + 1,
          loc.column + 1,
          (int)(byte_chr((const char*)&lex->input.data[lex->start], lex->input.size - lex->start, '\n') + loc.column),
          &lex->input.data[lex->start - loc.column],
          loc.column + 1,
          "^");
      //)   ret = JS_EXCEPTION;
    } else if(id != LEXER_EOF) {
      ret = JS_EXCEPTION;
    } else if(id == LEXER_EOF) {
      break;
    }
    break;
  }

  return ret;
}

static JSValue
js_lexer_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, BOOL* pdone, int magic) {
  JSValue ret;
  Lexer* lex;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  ret = js_lexer_lex(ctx, this_val, argc, argv);

  *pdone = input_buffer_eof(&lex->input);

  return ret;
}

JSValue
js_lexer_call(JSContext* ctx, JSValueConst func_obj, JSValueConst this_val, int argc, JSValueConst* argv, int flags) {
  Lexer* lex;
  int32_t result;
  JSValue ret = JS_UNDEFINED;
  JSValueConst arg = argc > 0 ? argv[0] : JS_UNDEFINED;

  if(!(lex = JS_GetOpaque2(ctx, func_obj, js_lexer_class_id)))
    return JS_EXCEPTION;

  if(argc > 0 && JS_IsNumber(argv[0])) {
    JS_SetPropertyStr(ctx, func_obj, "mask", JS_DupValue(ctx, argv[0]));
  }

  if(argc > 1 && JS_IsNumber(argv[1])) {
    JS_SetPropertyStr(ctx, func_obj, "skip", JS_DupValue(ctx, argv[1]));
  }

  return JS_DupValue(ctx, func_obj);
}

static void
js_lexer_finalizer(JSRuntime* rt, JSValue val) {
  Lexer* lex;

  if((lex = JS_GetOpaque(val, js_lexer_class_id)))
    lexer_free(lex, 0);
  // js_value_free_rt(rt, val);
}

static JSClassDef js_lexer_class = {.class_name = "Lexer", .finalizer = js_lexer_finalizer, .call = js_lexer_call};

static const JSCFunctionListEntry js_lexer_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_lexer_next, 0),
    JS_CGETSET_MAGIC_DEF("size", js_lexer_get, js_lexer_set, LEXER_PROP_SIZE),
    JS_CGETSET_MAGIC_DEF("pos", js_lexer_get, js_lexer_set, LEXER_PROP_POS),
    JS_CGETSET_MAGIC_DEF("start", js_lexer_get, 0, LEXER_PROP_START),
    JS_CGETSET_MAGIC_DEF("loc", js_lexer_get, 0, LEXER_PROP_LOC),
    JS_CGETSET_MAGIC_DEF("eof", js_lexer_get, 0, LEXER_PROP_EOF),
    JS_CGETSET_MAGIC_DEF("mode", js_lexer_get, js_lexer_set, LEXER_PROP_MODE),
    JS_CGETSET_MAGIC_DEF("byteLength", js_lexer_get, 0, LEXER_PROP_BYTE_LENGTH),
    JS_CFUNC_MAGIC_DEF("setInput", 1, js_lexer_method, LEXER_METHOD_SET_INPUT),
    JS_CFUNC_MAGIC_DEF("currentLine", 0, js_lexer_method, LEXER_METHOD_CURRENT_LINE),
    JS_CFUNC_MAGIC_DEF("tokenClass", 1, js_lexer_method, LEXER_METHOD_TOKEN_CLASS),
    JS_CFUNC_MAGIC_DEF("define", 2, js_lexer_add_rule, 0),
    JS_CFUNC_MAGIC_DEF("addRule", 2, js_lexer_add_rule, 1),
    JS_CFUNC_MAGIC_DEF("getRule", 1, js_lexer_method, LEXER_METHOD_GET_RULE),
    JS_CGETSET_MAGIC_DEF("ruleNames", js_lexer_get, 0, LEXER_PROP_RULENAMES),
    JS_CFUNC_DEF("lex", 0, js_lexer_lex),
    JS_CFUNC_DEF("inspect", 0, js_lexer_inspect),
    JS_CGETSET_DEF("tokens", js_lexer_tokens, 0),
    JS_ALIAS_DEF("position", "loc"),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_lexer_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Lexer", JS_PROP_C_W_E),
};

static const JSCFunctionListEntry js_lexer_static_funcs[] = {
    JS_CFUNC_DEF("escape", 1, js_lexer_escape),
    JS_PROP_INT32_DEF("FIRST", LEXER_FIRST, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("LONGEST", LEXER_LONGEST, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("LAST", LEXER_LAST, JS_PROP_ENUMERABLE),
};

static int
js_lexer_init(JSContext* ctx, JSModuleDef* m) {

  location_ctor = JS_NewCFunction2(ctx, js_location_constructor, "Location", 1, JS_CFUNC_constructor, 0);

  JS_NewClassID(&js_syntaxerror_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_syntaxerror_class_id, &js_syntaxerror_class);

  syntaxerror_proto = JS_NewError(ctx);
  JS_SetPropertyFunctionList(ctx, syntaxerror_proto, js_syntaxerror_proto_funcs, countof(js_syntaxerror_proto_funcs));
  JS_SetClassProto(ctx, js_syntaxerror_class_id, syntaxerror_proto);

  syntaxerror_ctor = JS_NewCFunction2(ctx, js_syntaxerror_constructor, "SyntaxError", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, syntaxerror_ctor, syntaxerror_proto);

  JS_NewClassID(&js_token_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_token_class_id, &js_token_class);

  token_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, token_proto, js_token_proto_funcs, countof(js_token_proto_funcs));
  JS_SetClassProto(ctx, js_token_class_id, token_proto);

  token_ctor = JS_NewCFunction2(ctx, js_token_constructor, "Token", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, token_ctor, token_proto);
  JS_SetPropertyFunctionList(ctx, token_ctor, js_token_static_funcs, countof(js_token_static_funcs));

  JS_NewClassID(&js_lexer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_lexer_class_id, &js_lexer_class);

  lexer_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, lexer_proto, js_lexer_proto_funcs, countof(js_lexer_proto_funcs));
  JS_SetClassProto(ctx, js_lexer_class_id, lexer_proto);

  lexer_ctor = JS_NewCFunction2(ctx, js_lexer_constructor, "Lexer", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, lexer_ctor, lexer_proto);
  JS_SetPropertyFunctionList(ctx, lexer_ctor, js_lexer_static_funcs, countof(js_lexer_static_funcs));

  if(m) {
    JS_SetModuleExport(ctx, m, "Location", location_ctor);
    JS_SetModuleExport(ctx, m, "SyntaxError", syntaxerror_ctor);
    JS_SetModuleExport(ctx, m, "Token", token_ctor);
    JS_SetModuleExport(ctx, m, "Lexer", lexer_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_lexer
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, &js_lexer_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "Location");
  JS_AddModuleExport(ctx, m, "SyntaxError");
  JS_AddModuleExport(ctx, m, "Token");
  JS_AddModuleExport(ctx, m, "Lexer");
  return m;
}
