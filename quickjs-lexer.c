#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "quickjs.h"
#include "libregexp.h"
#include "quickjs-lexer.h"
#include "vector.h"
#include <string.h>
#include <ctype.h>
#include <stdint.h>

enum {
  LEXER_METHOD_SET_INPUT = 0,
  LEXER_METHOD_SKIP,
  LEXER_METHOD_BACK,
  LEXER_METHOD_PEEKC,
  LEXER_METHOD_GETC,
  LEXER_METHOD_SKIPC,
  LEXER_METHOD_GET_RANGE,
  LEXER_METHOD_CURRENT_LINE,
  LEXER_METHOD_TOKEN_CLASS,
  LEXER_METHOD_GET_RULE,
  LEXER_METHOD_SKIPUNTIL,
  LEXER_METHOD_ERROR,
  LEXER_METHOD_PUSH_STATE,
  LEXER_METHOD_POP_STATE,
  LEXER_METHOD_TOP_STATE
};

enum {
  LEXER_PROP_SIZE = 0,
  LEXER_PROP_POS,
  LEXER_PROP_START,
  LEXER_PROP_EOF,
  LEXER_PROP_FILENAME,
  LEXER_PROP_LOC,
  LEXER_PROP_RULENAMES,
  LEXER_PROP_MODE,
  LEXER_PROP_BYTE_LENGTH,
  LEXER_PROP_STATE,
  LEXER_PROP_STATES,
  LEXER_PROP_STATE_DEPTH,
  LEXER_PROP_STATE_STACK,
  LEXER_PROP_SOURCE,
  LEXER_PROP_LEXEME
};

typedef struct {
  JSValue action;
  BOOL skip;
} JSLexerRule;

VISIBLE JSClassID js_location_class_id = 0, js_syntaxerror_class_id = 0, js_token_class_id = 0, js_lexer_class_id = 0;
static JSValue location_proto, location_ctor;
static JSValue syntaxerror_proto, syntaxerror_ctor;
static JSValue token_proto, token_ctor;
static JSValue lexer_proto, lexer_ctor;

enum { LOCATION_PROP_LINE, LOCATION_PROP_COLUMN, LOCATION_PROP_POS, LOCATION_PROP_FILE };

static JSValue js_location_new(JSContext* ctx, const Location* loc);

static JSValue
js_location_new(JSContext* ctx, const Location* location) {
  JSValue obj;
  Location* loc;

  if(!(loc = js_mallocz(ctx, sizeof(Location))))
    return JS_EXCEPTION;

  *loc = location_dup(location, ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */

  obj = JS_NewObjectProtoClass(ctx, location_proto, js_location_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, loc);

  return obj;
fail:
  js_free(ctx, loc);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_location_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Location* loc;
  JSValue ret = JS_UNDEFINED;
  size_t len;
  if(!(loc = js_location_data(ctx, this_val)))
    return ret;

  if(!loc->str) {
    len = loc->file ? strlen(loc->file) : 0;
    len += 46;
    loc->str = js_malloc(ctx, len);

    if(loc->file)
      snprintf(loc->str, len, "%s:%" PRIi32 ":%" PRIi32 "", loc->file, loc->line + 1, loc->column + 1);
    else
      snprintf(loc->str, len, "%" PRIi32 ":%" PRIi32 "", loc->line + 1, loc->column + 1);
  }

  ret = JS_NewString(ctx, loc->str);
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

static JSValue
js_location_getter(JSContext* ctx, JSValueConst this_val, int magic) {
  Location* loc;
  JSValue ret = JS_UNDEFINED;

  if(!(loc = js_location_data(ctx, this_val)))
    return ret;

  switch(magic) {
    case LOCATION_PROP_LINE: {
      ret = JS_NewUint32(ctx, loc->line + 1);
      break;
    }
    case LOCATION_PROP_COLUMN: {
      ret = JS_NewUint32(ctx, loc->column + 1);
      break;
    }
    case LOCATION_PROP_POS: {
      ret = JS_NewInt64(ctx, loc->pos);
      break;
    }
    case LOCATION_PROP_FILE: {
      ret = JS_NewString(ctx, loc->file);
      break;
    }
  }
  return ret;
}

static Location
js_location_get(JSContext* ctx, JSValueConst this_val) {
  Location loc = {0, 0, 0, -1, 0};

  loc.line = js_get_propertystr_int32(ctx, this_val, "line") - 1;
  loc.column = js_get_propertystr_int32(ctx, this_val, "column") - 1;
  loc.file = js_get_propertystr_string(ctx, this_val, "file");
  if(js_has_propertystr(ctx, this_val, "pos"))
    loc.pos = js_get_propertystr_uint64(ctx, this_val, "pos");

  return loc;
}

JSValue
js_location_toprimitive(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Location* loc;
  const char* hint;
  JSValue ret;

  if(!(loc = js_location_data(ctx, this_val)))
    return JS_EXCEPTION;

  hint = argc > 0 ? JS_ToCString(ctx, argv[0]) : 0;

  if(hint && !strcmp(hint, "number"))
    ret = JS_NewInt64(ctx, loc->pos);
  else
    ret = js_location_tostring(ctx, this_val, argc, argv);

  if(hint)
    js_cstring_free(ctx, hint);
  return ret;
}

JSValue
js_location_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  Location *loc, *other;

  if(!(loc = js_mallocz(ctx, sizeof(Location))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_location_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, loc);

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
        loc->file = js_strndup(ctx, (const char*)p, end - p);
        break;
      }
      end = p - 1;
    }
    if(ni == 0) {
      loc->line = n[0];
      loc->column = n[1];
    }
    loc->line--;
    loc->column--;
    /* Dup from object */
  } else if(JS_IsObject(argv[0]) && (other = js_location_data(ctx, argv[0]))) {

    *loc = location_dup(other, ctx);

    /* From arguments (line,column,pos,file) */
  } else if(argc > 1) {

    JS_ToUint32(ctx, &loc->line, argv[0]);
    JS_ToUint32(ctx, &loc->column, argv[1]);

    loc->line--;
    loc->column--;

    if(argc > 2)
      JS_ToIndex(ctx, (uint64_t*)&loc->pos, argv[2]);
    if(argc > 3)
      loc->file = js_tostring(ctx, argv[3]);
  }

  return obj;
fail:
  js_free(ctx, loc);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_location_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Location* loc;

  if(!(loc = js_location_data(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectProto(ctx, location_proto);

  JS_DefinePropertyValueStr(ctx, obj, "line", JS_NewUint32(ctx, loc->line), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "column", JS_NewUint32(ctx, loc->column), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "pos", JS_NewInt64(ctx, loc->pos), JS_PROP_ENUMERABLE);
  if(loc->file)
    JS_DefinePropertyValueStr(ctx, obj, "file", JS_NewString(ctx, loc->file), JS_PROP_ENUMERABLE);
  if(loc->str)
    JS_DefinePropertyValueStr(ctx, obj, "str", JS_NewString(ctx, loc->str), JS_PROP_ENUMERABLE);

  return obj;
}

void
js_location_finalizer(JSRuntime* rt, JSValue val) {
  Location* loc = JS_GetOpaque(val, js_location_class_id);
  if(loc) {
    location_free_rt(loc, rt);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_location_class = {
    .class_name = "Location",
    .finalizer = js_location_finalizer,
};

static const JSCFunctionListEntry js_location_funcs[] = {
    JS_CGETSET_MAGIC_DEF("line", js_location_getter, 0, LOCATION_PROP_LINE),
    JS_CGETSET_MAGIC_DEF("column", js_location_getter, 0, LOCATION_PROP_COLUMN),
    JS_CGETSET_MAGIC_DEF("pos", js_location_getter, 0, LOCATION_PROP_POS),
    JS_CGETSET_MAGIC_DEF("file", js_location_getter, 0, LOCATION_PROP_FILE),
    JS_CFUNC_DEF("[Symbol.toPrimitive]", 0, js_location_toprimitive),
    JS_CFUNC_DEF("toString", 0, js_location_tostring),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Location", JS_PROP_CONFIGURABLE),
};

static JSValue
js_lexer_rule_new(JSContext* ctx, Lexer* lex, LexerRule* rule) {

  JSValue ret, states;
  DynBuf dbuf;
  size_t i, j;
  js_dbuf_init(ctx, &dbuf);
  lexer_rule_dump(lex, rule, &dbuf);
  dbuf_0(&dbuf);

  ret = JS_NewArray(ctx);
  js_set_propertyint_string(ctx, ret, 0, rule->name);
  js_set_propertyint_string(ctx, ret, 1, (const char*)dbuf.buf);

  states = JS_NewArray(ctx);

  for(i = 0, j = 0; i < 32; i++) {
    if(rule->mask & (1 << i)) {
      char* name = lexer_state_name(lex, i);

      if(name)
        JS_SetPropertyUint32(ctx, states, j++, JS_NewString(ctx, name));
    }
  }
  JS_SetPropertyUint32(ctx, ret, 2, states);
  dbuf_free(&dbuf);
  return ret;
}

enum syntaxerror_getters { SYNTAXERROR_PROP_LOC = 0, SYNTAXERROR_PROP_MESSAGE, SYNTAXERROR_PROP_LINE };

JSValue
js_syntaxerror_new(JSContext* ctx, SyntaxError arg) {
  SyntaxError* err;
  JSValue obj = JS_UNDEFINED;

  if(!(err = js_mallocz(ctx, sizeof(SyntaxError))))
    return JS_EXCEPTION;

  memcpy(err, &arg, sizeof(SyntaxError));

  obj = JS_NewObjectProtoClass(ctx, syntaxerror_proto, js_syntaxerror_class_id);
  JS_SetOpaque(obj, err);
  if(!location_isnull(&err->loc))
    JS_SetPropertyStr(ctx, obj, "loc", js_location_new(ctx, &err->loc));
  if(err->message)
    JS_SetPropertyStr(ctx, obj, "message", JS_NewString(ctx, err->message));
  if(err->line)
    JS_SetPropertyStr(ctx, obj, "line", JS_NewString(ctx, err->line));
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
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, err);

  for(i = 0; i < argc; i++) {
    if(JS_IsString(argv[i]) && !err->message) {
      err->message = js_tostring(ctx, argv[i]);
    } else if(JS_IsString(argv[i]) && !err->line) {
      err->line = js_tostring(ctx, argv[i]);
    } else if(js_is_location(ctx, argv[i])) {
      err->loc = js_location_get(ctx, argv[i]);
    }
  }

  return obj;
fail:
  js_free(ctx, err);
  JS_FreeValue(ctx, obj);
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
    case SYNTAXERROR_PROP_LINE: {
      ret = JS_NewString(ctx, err->line);
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

static JSValue
js_syntaxerror_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  SyntaxError* err;

  if(!(err = js_syntaxerror_data(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectProto(ctx, syntaxerror_proto);

  if(!location_isnull(&err->loc))
    JS_DefinePropertyValueStr(ctx, obj, "loc", js_location_new(ctx, &err->loc), JS_PROP_ENUMERABLE);

  if(err->line)
    JS_DefinePropertyValueStr(ctx, obj, "line", JS_NewString(ctx, err->line), JS_PROP_ENUMERABLE);
  if(err->message)
    JS_DefinePropertyValueStr(ctx, obj, "message", JS_NewString(ctx, err->message), JS_PROP_ENUMERABLE);

  return obj;
}

static void
js_syntaxerror_finalizer(JSRuntime* rt, JSValue val) {
  SyntaxError* err = JS_GetOpaque(val, js_syntaxerror_class_id);
  if(err) {
    if(err->message)
      js_free_rt(rt, (char*)err->message);
    if(err->line)
      js_free_rt(rt, (char*)err->line);

    location_free_rt(&err->loc, rt);
  }
  // JS_FreeValueRT(rt, val);
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
token_free(Token* tok, JSContext* ctx) {

  location_free(&tok->loc, ctx);

  if(!JS_IsUndefined(tok->loc_val))
    js_value_free(ctx, tok->loc_val);

  js_free(ctx, tok->lexeme);
  js_free(ctx, tok);
}

static void
token_free_rt(Token* tok, JSRuntime* rt) {

  location_free_rt(&tok->loc, rt);

  if(!JS_IsUndefined(tok->loc_val))
    js_value_free_rt(rt, tok->loc_val);

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
  tok->loc_val = JS_UNDEFINED;
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

JSValue
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
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, tok);

  tok->loc_val = JS_UNDEFINED;

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
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_token_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Token* tok;
  if(!(tok = js_token_data(ctx, this_val)))
    return JS_EXCEPTION;
  return JS_NewStringLen(ctx, (const char*)tok->lexeme, tok->byte_length);
}

JSValue
js_token_toprimitive(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Token* tok;
  const char* hint;
  JSValue ret;
  if(!(tok = js_token_data(ctx, this_val)))
    return JS_EXCEPTION;
  hint = argc > 0 ? JS_ToCString(ctx, argv[0]) : 0;

  if(hint && !strcmp(hint, "number"))
    ret = JS_NewInt32(ctx, tok->id);
  else
    ret = JS_NewStringLen(ctx, (const char*)tok->lexeme, tok->byte_length);

  if(hint)
    js_cstring_free(ctx, hint);
  return ret;
}

JSValue
js_token_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Token* tok;
  LexerRule* rule;

  if(!(tok = js_token_data(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectProto(ctx, token_proto);
  rule = lexer_rule_at(tok->lexer, tok->id);

  JS_DefinePropertyValueStr(ctx, obj, "id", JS_NewUint32(ctx, tok->id), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "type", JS_NewString(ctx, rule->name), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "lexeme", JS_NewString(ctx, tok->lexeme), JS_PROP_ENUMERABLE);

  JS_DefinePropertyValueStr(ctx, obj, "end", JS_NewUint32(ctx, tok->loc.pos + tok->char_length), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "byte_offset", JS_NewUint32(ctx, tok->byte_offset), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "char_length", JS_NewUint32(ctx, tok->char_length), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "byte_length", JS_NewUint32(ctx, tok->byte_length), JS_PROP_ENUMERABLE);

  JS_DefinePropertyValueStr(ctx, obj, "loc", js_location_new(ctx, &tok->loc), JS_PROP_ENUMERABLE);

  return obj;
}

JSValue
js_token_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Token* tok;
  JSValue ret = JS_UNDEFINED;

  if(!(tok = js_token_data(ctx, this_val)))
    return ret;

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
      if(JS_IsUndefined(tok->loc_val))
        tok->loc_val = js_location_new(ctx, &tok->loc);

      ret = JS_DupValue(ctx, tok->loc_val);
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

void
js_token_finalizer(JSRuntime* rt, JSValue val) {
  Token* tok = JS_GetOpaque(val, js_token_class_id);
  if(tok) {
    token_free_rt(tok, rt);
  }
  // JS_FreeValueRT(rt, val);
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
    // JS_CFUNC_DEF("inspect", 0, js_token_inspect),
    JS_ALIAS_DEF("position", "loc"),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Token", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_token_static_funcs[] = {
    JS_PROP_INT32_DEF("EOF", LEXER_EOF, JS_PROP_ENUMERABLE),
};

static Token*
lexer_token(Lexer* lex, int id, size_t charlen, Location loc, JSContext* ctx) {
  Token* tok;
  if((tok = js_mallocz(ctx, sizeof(Token)))) {
    tok->id = id;
    tok->loc = location_dup(&loc, ctx);
    tok->loc_val = JS_UNDEFINED;
    tok->byte_length = lex->bytelen;
    tok->char_length = charlen;
    tok->lexeme = js_strndup(ctx, (const char*)&lex->input.data[lex->start], tok->byte_length);
    tok->byte_offset = lex->start;
    tok->lexer = lex;
  }
  return tok;
}

static JSValue
lexer_continue(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) {
  JS_SetPropertyUint32(ctx, data[0], 0, JS_NewBool(ctx, TRUE));
  return JS_UNDEFINED;
}

static int
lexer_lex(Lexer* lex, JSContext* ctx, JSValueConst this_val) {
  int id = -1;
  int64_t state = MASK_ALL, skip = 0;
  {
    JSValue mask = JS_GetPropertyStr(ctx, this_val, "mask");
    if(JS_IsNumber(mask))
      JS_ToInt64(ctx, &state, mask);
    JS_FreeValue(ctx, mask);
  }
  {
    JSValue mask = JS_GetPropertyStr(ctx, this_val, "skip");
    if(JS_IsNumber(mask))
      JS_ToInt64(ctx, &skip, mask);
    JS_FreeValue(ctx, mask);
  }
  for(;;) {
    if((id = lexer_peek(lex, state | skip, ctx)) >= 0) {
      LexerRule* rule = lexer_rule_at(lex, id);

      // printf("state %i rule %s\n", lex->state, rule->name);

      JSLexerRule* jsrule = rule->opaque;
      if((rule->mask & skip)) {
        lexer_skip(lex);
        continue;
      }
      if(jsrule) {
        BOOL skip = FALSE;
        if(JS_IsFunction(ctx, jsrule->action)) {
          JSValue data[1] = {JS_NewArray(ctx)};
          JSValue args[] = {JS_DupValue(ctx, this_val), JS_NewCFunctionData(ctx, lexer_continue, 0, 0, 1, data)};
          JSValue do_skip;
          /*ret =*/JS_Call(ctx, jsrule->action, this_val, 2, args);
          JS_FreeValue(ctx, args[0]);
          JS_FreeValue(ctx, args[1]);

          do_skip = JS_GetPropertyUint32(ctx, data[0], 0);

          if(JS_IsBool(do_skip))
            skip = JS_ToBool(ctx, do_skip);

          JS_FreeValue(ctx, data[0]);
        }
        if(skip || jsrule->skip) {
          lexer_skip(lex);
          continue;
        }
      }
    } else if(id == LEXER_ERROR_NOMATCH) {
      JSValue handler = JS_GetPropertyStr(ctx, this_val, "handler");
      if(JS_IsFunction(ctx, handler)) {
        JSValue data[1] = {JS_NewArray(ctx)};
        JSValue args[] = {JS_DupValue(ctx, this_val), JS_NewCFunctionData(ctx, lexer_continue, 0, 0, 1, data)};
        JSValue do_resume = JS_FALSE;

        /*   ret = */ JS_Call(ctx, handler, this_val, 2, args);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);

        do_resume = JS_GetPropertyUint32(ctx, data[0], 0);

        if(JS_IsBool(do_resume))
          if(JS_ToBool(ctx, do_resume))
            continue;

        id = LEXER_ERROR_NOMATCH;
      }
    }
    break;
  }
  return id;
}

static BOOL
lexer_escape_pred(int c) {
  switch(c) {
    case '*':
    case '?':
    case '+':
    case '[':
    case ']':
    case '(':
    case ')':
    case '.':
    case '^':
    case '$':
    case '|':
    case '\r':
    case '\n':
    case '\t':
    case '\v':
    case '\f':
    case '\\': return TRUE;
  }
  return FALSE;
}
static BOOL
lexer_unescape_pred(int c) {
  switch(c) {
    case 'r': return '\r';
    case 'n': return '\n';
    case 't': return '\t';
    case 'v': return '\v';
    case 'f': return '\f';
    case '/':
      return '/';
      // case '\\': return '\\';
  }
  return 0;
}

static char*
lexer_current_line(Lexer* lex, JSContext* ctx) {
  size_t start, end, size;
  start = lex->start;
  end = lex->input.pos;
  while(start > 0 && lex->input.data[start - 1] != '\n') start--;
  size = byte_chr((const char*)&lex->input.data[start], lex->input.size - start, '\n');
  // while(end < lex->input.size && lex->input.data[end] != '\n') end++;
  return js_strndup(ctx, (const char*)&lex->input.data[start], size);
}

static char*
lexer_lexeme_s(Lexer* lex, JSContext* ctx) {
  size_t len;
  char* s;

  s = lexer_lexeme(lex, &len);

  return js_strndup(ctx, s, len);
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
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_lexer_wrap(JSContext* ctx, Lexer* lex) {
  JSValue obj;

  obj = JS_NewObjectProtoClass(ctx, lexer_proto, js_lexer_class_id);
  JS_SetOpaque(obj, lex);
  return obj;
}

JSValue
js_lexer_add_rule(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  Lexer* lex;
  char* name;
  int64_t mask = -1, skip = 0;
  RegExp expr;
  JSLexerRule* jsrule = 0;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  name = (magic || JS_IsString(argv[0])) ? js_tostring(ctx, argv[0]) : 0;
  expr = regexp_from_argv(argc - 1, &argv[1], ctx);

  if(JS_IsNumber(argv[2]))
    JS_ToInt64(ctx, &mask, argv[2]);

  JSValue skipv = JS_GetPropertyStr(ctx, this_val, "skip");
  if(JS_IsNumber(skipv))
    JS_ToInt64(ctx, &skip, skipv);
  JS_FreeValue(ctx, skipv);

  if(argc > 3 || JS_IsFunction(ctx, argv[argc - 1])) {
    jsrule = js_malloc(ctx, sizeof(JSLexerRule));

    jsrule->action = JS_DupValue(ctx, argv[argc - 1]);
    jsrule->skip = !!(mask & skip);
  }

  if(magic) {
    int index = lexer_rule_add(lex, name, expr.source);
    LexerRule* rule = lexer_rule_at(lex, index);

    rule->opaque = jsrule;

    return JS_NewInt32(ctx, index);
  } else {
    lexer_define(lex, name, expr.source);
  }

  return JS_UNDEFINED;
}

JSValue
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

  if((lex = JS_GetOpaque(ret, js_lexer_class_id))) {
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

JSValue
js_lexer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  Lexer* lex;
  JSValue ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LEXER_METHOD_SET_INPUT: {
      Lexer* other;
      InputBuffer input;
      Location loc = {0, 0, 0, -1, 0};

      if((other = JS_GetOpaque(argv[0], js_lexer_class_id))) {
        input = input_buffer_dup(&other->input, ctx);
        loc = other->loc;
        lex->start = other->start;
      } else {
        input = js_input_buffer(ctx, argv[0]);
      }

      input_buffer_free(&lex->input, ctx);
      lex->input = input;
      location_free_rt(&lex->loc, JS_GetRuntime(ctx));
      lex->loc = loc;

      if(argc > 1 && JS_IsString(argv[1])) {
        if(lex->loc.file)
          js_free(ctx, (char*)lex->loc.file);
        lex->loc.file = js_tostring(ctx, argv[1]);
      }

      break;
    }

    case LEXER_METHOD_SKIP: {
      ret = JS_NewUint32(ctx, lexer_skip(lex));
      break;
    }

    case LEXER_METHOD_BACK: {
      Token* tok;
      if(argc >= 1 && (tok = js_token_data(ctx, argv[0]))) {
        lexer_set_location(lex, &tok->loc, ctx);
        lex->bytelen = tok->byte_length;

        ret = JS_NewInt32(ctx, tok->id);
      }
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
        const uint8_t* p = 0;
        size_t n;
        if(argc > 0)
          JS_ToInt32(ctx, &ntimes, argv[0]);
        while(ntimes-- > 0) { p = input_buffer_get(&lex->input, &n); }
        if(p)
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
          JS_FreeValue(ctx, ret);
          if(b) {
            ret = str;
            break;
          }
          JS_FreeValue(ctx, str);
          input_buffer_getc(&lex->input);
          lex->start = lex->input.pos;
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
      ret = JS_NewString(ctx, lexer_current_line(lex, ctx));
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
      error.loc = location_dup(&lex->loc, ctx);
      error.line = lexer_current_line(lex, ctx);
      // printf("lexer SyntaxError('%s', %u:%u)\n", error.message, lex->loc.line + 1, lex->loc.column + 1);
      ret = js_syntaxerror_new(ctx, error);
      break;
    }

    case LEXER_METHOD_PUSH_STATE: {
      const char* state = JS_ToCString(ctx, argv[0]);
      int id;

      id = lexer_state_push(lex, state);
      // printf("[%zu] pushState('%s')\n", lexer_state_depth(lex), state);
      ret = JS_NewInt32(ctx, id);
      JS_FreeCString(ctx, state);
      break;
    }

    case LEXER_METHOD_POP_STATE: {
      int id;
      if(lexer_state_depth(lex) > 0) {
        id = lexer_state_pop(lex);
        // printf("[%zu] popState() = '%s'\n", lexer_state_depth(lex), lexer_state_name(lex, id));
        ret = JS_NewInt32(ctx, id);
      } else {
        ret = JS_ThrowInternalError(ctx, "lexer (%s) depth %zu", lexer_state_topname(lex), lexer_state_depth(lex));
      }

      break;
    }

    case LEXER_METHOD_TOP_STATE: {
      int32_t index = 0, id;
      if(argc > 0 && JS_IsNumber(argv[0]))
        JS_ToInt32(ctx, &index, argv[0]);

      if((id = lexer_state_top(lex, index)) >= 0)
        ret = JS_NewString(ctx, lexer_state_name(lex, id));
      break;
    }
  }
  return ret;
}

JSValue
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

    case LEXER_PROP_STATE: {
      ret = JS_NewInt32(ctx, lex->state);
      break;
    }

    case LEXER_PROP_STATES: {
      size_t i = 0;
      ret = JS_NewArray(ctx);
      for(;;) {
        char* name;
        if((name = lexer_state_name(lex, i)) == 0)
          break;
        JS_SetPropertyUint32(ctx, ret, i++, JS_NewString(ctx, name));
      }
      break;
    }

    case LEXER_PROP_STATE_DEPTH: {
      ret = JS_NewUint32(ctx, lexer_state_depth(lex));
      break;
    }

    case LEXER_PROP_STATE_STACK: {
      size_t i = 0, n = vector_size(&lex->state_stack, sizeof(int32_t));
      ret = JS_NewArray(ctx);
      for(; i < n; i++) {
        int32_t state = *(int32_t*)vector_at(&lex->state_stack, sizeof(int32_t), i);
        char* name;
        if((name = lexer_state_name(lex, state)) == 0)
          break;
        JS_SetPropertyUint32(ctx, ret, i, JS_NewString(ctx, name));
      }
      JS_SetPropertyUint32(ctx, ret, i, JS_NewString(ctx, lexer_state_name(lex, lex->state)));
      break;
    }

    case LEXER_PROP_SOURCE: {
      ret = JS_NewStringLen(ctx, (const char*)lex->input.data, lex->input.size);
      break;
    }
    case LEXER_PROP_LEXEME: {
      ret = JS_NewStringLen(ctx, (const char*)lex->input.data + lex->start, lex->input.pos - lex->start);
      break;
    }
  }
  return ret;
}

JSValue
js_lexer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Lexer* lex;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LEXER_PROP_POS: {
      Token* tok;
      if((tok = js_token_data(ctx, value))) {
        lex->input.pos = tok->byte_offset;
        lex->loc = tok->loc;
      }
      break;
    }

    case LEXER_PROP_BYTE_LENGTH: {
      Token* tok;
      if(JS_IsNumber(value)) {
        js_value_to_size(ctx, &lex->bytelen, value);
      } else if((tok = js_token_data(ctx, value))) {
        lex->bytelen = tok->byte_length;
      }
      break;
    }

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
JSValue
js_lexer_tokens(JSContext* ctx, JSValueConst this_val) {
  Lexer* lex;
  LexerRule* rule;
  int i = 0;
  JSValue ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  ret = JS_NewArray(ctx);

  vector_foreach_t(&lex->rules, rule) {
    const char* name = rule->name;

    JS_SetPropertyUint32(ctx, ret, i++, JS_NewString(ctx, name));
  }
  return ret;
}

JSValue
js_lexer_states(JSContext* ctx, JSValueConst this_val) {
  Lexer* lex;
  char** cond;
  uint32_t i = 0;
  JSValue ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  ret = JS_NewArray(ctx);

  vector_foreach_t(&lex->states, cond) { JS_SetPropertyUint32(ctx, ret, i++, JS_NewString(ctx, *cond)); }

  return ret;
}

JSValue
js_lexer_statestack(JSContext* ctx, JSValueConst this_val) {
  Lexer* lex;
  int32_t* stack;
  size_t size;
  JSValue ctor, buf, ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  size = vector_size(&lex->state_stack, sizeof(int32_t)) + 1;
  stack = js_malloc_rt(JS_GetRuntime(ctx), sizeof(int32_t) * size);

  memcpy(stack, vector_begin(&lex->state_stack), sizeof(int32_t) * (size - 1));

  stack[size - 1] = lex->state;

  buf = JS_NewArrayBuffer(
      ctx, (void*)stack, sizeof(int32_t) * size, (JSFreeArrayBufferDataFunc*)&js_free_rt, stack, FALSE);

  ctor = js_global_get(ctx, "Int32Array");

  ret = JS_CallConstructor(ctx, ctor, 1, &buf);

  JS_FreeValue(ctx, ctor);
  JS_FreeValue(ctx, buf);

  return ret;
}

JSValue
js_lexer_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  return JS_DupValue(ctx, this_val);
}

JSValue
js_lexer_escape(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  InputBuffer input = js_input_buffer(ctx, argv[0]);
  DynBuf output;
  js_dbuf_init(ctx, &output);
  magic ? dbuf_put_unescaped_pred(&output, (const char*)input.data, input.size, lexer_unescape_pred)
        : dbuf_put_escaped_pred(&output, (const char*)input.data, input.size, lexer_escape_pred);
  return dbuf_tostring_free(&output, ctx);
}

JSValue
js_lexer_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;

  if(js_is_regexp(ctx, argv[0]) || JS_IsString(argv[0])) {
    RegExp re;
    re = regexp_from_argv(argc, argv, ctx);
    ret = JS_NewString(ctx, re.source);
  } else {
    InputBuffer input;
    input = js_input_buffer(ctx, argv[0]);
    ret = JS_NewStringLen(ctx, (const char*)input.data, input.size);
    input_buffer_free(&input, ctx);
  }
  return ret;
}

JSValue
js_lexer_lex(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;
  Lexer* lex;
  int id, state = -1;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(argc > 0 && JS_IsString(argv[0])) {
    const char* name = JS_ToCString(ctx, argv[0]);

    state = lexer_state_push(lex, name);

    JS_FreeCString(ctx, name);
  }

  id = lexer_lex(lex, ctx, this_val);

  if(state > -1)
    lexer_state_pop(lex);

  switch(id) {
    case LEXER_ERROR_NOMATCH: {
      char* lexeme = lexer_lexeme_s(lex, ctx);

      ret = JS_ThrowInternalError(
          ctx,
          "%s:%" PRIu32 ":%" PRIu32 ": No matching token (%d: %s) '%s'\n%.*s\n%*s",
          lex->loc.file,
          lex->loc.line + 1,
          lex->loc.column + 1,
          lexer_state_top(lex, 0),
          lexer_state_name(lex, lexer_state_top(lex, 0)),
          lexeme,
          (int)(byte_chr((const char*)&lex->input.data[lex->start], lex->input.size - lex->start, '\n') +
                lex->loc.column),
          &lex->input.data[lex->start - lex->loc.column],
          lex->loc.column + 1,
          "^");
      js_free(ctx, lexeme);
      break;
    }
    case LEXER_EOF: {
      ret = JS_NULL;
      break;
    }
    default: {

      ret = JS_NewInt32(ctx, id);

      break;
    }
  }

  return ret;
}

JSValue
js_lexer_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, BOOL* pdone, int magic) {
  JSValue ret;
  Lexer* lex;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  ret = js_lexer_lex(ctx, this_val, argc, argv);

  if(JS_IsNumber(ret)) {
    int32_t id;
    Token* tok;
    size_t charlen;
    Location loc;

    JS_ToInt32(ctx, &id, ret);
    loc = lex->loc;

    charlen = lexer_skip(lex);

    tok = lexer_token(lex, id, charlen, loc, ctx);
    ret = js_token_wrap(ctx, tok);
  }

  *pdone = JS_IsNull(ret); // input_buffer_eof(&lex->input);

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

static JSValue
js_lexer_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Lexer* lex;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectProto(ctx, lexer_proto);

  JS_DefinePropertyValueStr(ctx, obj, "start", JS_NewUint32(ctx, lex->start), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "bytelen", JS_NewUint32(ctx, lex->bytelen), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "tokid", JS_NewInt32(ctx, lex->tokid), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "state", JS_NewInt32(ctx, lex->state), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "loc", js_location_new(ctx, &lex->loc), JS_PROP_ENUMERABLE);

  return obj;
}

void
js_lexer_finalizer(JSRuntime* rt, JSValue val) {
  Lexer* lex;

  if((lex = JS_GetOpaque(val, js_lexer_class_id)))
    lexer_free_rt(lex, rt);
  // JS_FreeValueRT(rt, val);
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
    JS_CGETSET_MAGIC_DEF("state", js_lexer_get, 0, LEXER_PROP_STATE),
    JS_CGETSET_MAGIC_DEF("states", js_lexer_get, 0, LEXER_PROP_STATES),
    JS_CGETSET_MAGIC_DEF("stateDepth", js_lexer_get, 0, LEXER_PROP_STATE_DEPTH),
    JS_CGETSET_MAGIC_DEF("stateStack", js_lexer_get, 0, LEXER_PROP_STATE_STACK),
    JS_CGETSET_MAGIC_DEF("source", js_lexer_get, 0, LEXER_PROP_SOURCE),
    JS_CGETSET_MAGIC_DEF("lexeme", js_lexer_get, 0, LEXER_PROP_LEXEME),
    JS_CFUNC_MAGIC_DEF("setInput", 1, js_lexer_method, LEXER_METHOD_SET_INPUT),
    JS_CFUNC_MAGIC_DEF("skip", 0, js_lexer_method, LEXER_METHOD_SKIP),
    JS_CFUNC_MAGIC_DEF("skipUntil", 1, js_lexer_method, LEXER_METHOD_SKIPUNTIL),
    JS_CFUNC_MAGIC_DEF("tokenClass", 1, js_lexer_method, LEXER_METHOD_TOKEN_CLASS),
    JS_CFUNC_MAGIC_DEF("define", 2, js_lexer_add_rule, 0),
    JS_CFUNC_MAGIC_DEF("addRule", 2, js_lexer_add_rule, 1),
    JS_CFUNC_MAGIC_DEF("getRule", 1, js_lexer_method, LEXER_METHOD_GET_RULE),
    JS_CFUNC_MAGIC_DEF("pushState", 1, js_lexer_method, LEXER_METHOD_PUSH_STATE),
    JS_ALIAS_DEF("begin", "pushState"),
    JS_CFUNC_MAGIC_DEF("popState", 0, js_lexer_method, LEXER_METHOD_POP_STATE),
    JS_CFUNC_MAGIC_DEF("topState", 0, js_lexer_method, LEXER_METHOD_TOP_STATE),
    JS_CFUNC_MAGIC_DEF("currentLine", 0, js_lexer_method, LEXER_METHOD_CURRENT_LINE),
    JS_CFUNC_MAGIC_DEF("back", 0, js_lexer_method, LEXER_METHOD_BACK),
    JS_CGETSET_MAGIC_DEF("ruleNames", js_lexer_get, 0, LEXER_PROP_RULENAMES),
    JS_CFUNC_DEF("lex", 0, js_lexer_lex),
    JS_CFUNC_DEF("inspect", 0, js_lexer_inspect),
    JS_CGETSET_DEF("tokens", js_lexer_tokens, 0),
    JS_CGETSET_DEF("states", js_lexer_states, 0),
    // JS_CGETSET_DEF("stateStack", js_lexer_statestack, 0),
    // JS_ALIAS_DEF("position", "loc"),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_lexer_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Lexer", JS_PROP_C_W_E),
};

static const JSCFunctionListEntry js_lexer_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("escape", 1, js_lexer_escape, 0),
    JS_CFUNC_MAGIC_DEF("unescape", 1, js_lexer_escape, 1),
    JS_CFUNC_DEF("toString", 1, js_lexer_tostring),
    JS_PROP_INT32_DEF("FIRST", LEXER_FIRST, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("LONGEST", LEXER_LONGEST, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("LAST", LEXER_LAST, JS_PROP_ENUMERABLE),
};

int
js_lexer_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_location_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_location_class_id, &js_location_class);

  location_ctor = JS_NewCFunction2(ctx, js_location_constructor, "Location", 1, JS_CFUNC_constructor, 0);
  location_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, location_proto, js_location_funcs, countof(js_location_funcs));
  js_set_inspect_method(ctx, location_proto, js_location_inspect);
  JS_SetClassProto(ctx, js_location_class_id, location_proto);

  JS_NewClassID(&js_syntaxerror_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_syntaxerror_class_id, &js_syntaxerror_class);

  syntaxerror_proto = JS_NewError(ctx);
  JS_SetPropertyFunctionList(ctx, syntaxerror_proto, js_syntaxerror_proto_funcs, countof(js_syntaxerror_proto_funcs));

  js_set_inspect_method(ctx, syntaxerror_proto, js_syntaxerror_inspect);

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

  js_set_inspect_method(ctx, token_proto, js_token_inspect);

  JS_NewClassID(&js_lexer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_lexer_class_id, &js_lexer_class);

  lexer_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, lexer_proto, js_lexer_proto_funcs, countof(js_lexer_proto_funcs));
  JS_SetClassProto(ctx, js_lexer_class_id, lexer_proto);

  js_set_inspect_method(ctx, lexer_proto, js_lexer_inspect);

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
