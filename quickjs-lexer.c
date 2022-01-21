#include <quickjs.h>
#include <libregexp.h>
#include "quickjs-lexer.h"
#include "quickjs-location.h"
#include "vector.h"
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "buffer-utils.h"

/**
 * \addtogroup quickjs-lexer
 * @{
 */
typedef struct {
  JSValue action;
  BOOL skip;
} JSLexerRule;

thread_local VISIBLE JSClassID js_token_class_id = 0, js_lexer_class_id = 0;
thread_local JSValue token_proto = {{JS_TAG_UNDEFINED}}, token_ctor = {{JS_TAG_UNDEFINED}};
thread_local JSValue lexer_proto = {{JS_TAG_UNDEFINED}}, lexer_ctor = {{JS_TAG_UNDEFINED}};

static JSValue
js_lexer_rule_new(JSContext* ctx, Lexer* lex, LexerRule* rule) {

  JSValue ret, states;
  DynBuf dbuf;
  size_t i, j;
  js_dbuf_init(ctx, &dbuf);
  lexer_rule_dump(lex, rule, &dbuf);
  dbuf_0(&dbuf);

  if(rule->expr[0] == '<') {
    assert(!strncmp(rule->expr, (const char*)dbuf.buf, str_chr(rule->expr, '>')));
  }

  ret = JS_NewArray(ctx);
  js_set_propertyint_string(ctx, ret, 0, rule->name);
  js_set_propertyint_string(ctx, ret, 1, lexer_states_skip((char*)dbuf.buf));

  states = JS_NewArray(ctx);

  for(i = 0, j = 0; i < 32; i++) {
    if(rule->mask & (1 << i)) {
      char* name = lexer_state_name(lex, i);

      if(name)
        JS_SetPropertyUint32(ctx, states, j++, JS_NewString(ctx, name));
    }
  }
  JS_SetPropertyUint32(ctx, ret, 2, states);
  // JS_SetPropertyUint32(ctx, ret, 3, JS_NewInt64(ctx, rule->mask));
  dbuf_free(&dbuf);
  return ret;
}

enum token_methods {
  TO_STRING = 0,
};
enum token_getters {
  TOKEN_PROP_BYTELENGTH = 0,
  TOKEN_PROP_BYTEOFFSET,
  TOKEN_PROP_CHARLENGTH,
  TOKEN_PROP_START,
  TOKEN_PROP_END,
  TOKEN_PROP_CHARRANGE,
  TOKEN_PROP_LEXEME,
  TOKEN_PROP_LOC,
  TOKEN_PROP_ID,
  TOKEN_PROP_SEQ,
  TOKEN_PROP_TYPE,
  TOKEN_PROP_RULE,
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
  tok->loc = location_clone(loc, ctx);
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
js_token_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
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
    tok->loc = js_location_from(ctx, argv[2]);
  if(argc > 3)
    JS_ToInt64(ctx, &tok->loc.pos, argv[3]);

  return obj;
fail:
  js_free(ctx, tok);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_token_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Token* tok;
  if(!(tok = js_token_data2(ctx, this_val)))
    return JS_EXCEPTION;
  return JS_NewStringLen(ctx, (const char*)tok->lexeme, tok->byte_length);
}

JSValue
js_token_toprimitive(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Token* tok;
  const char* hint;
  JSValue ret;
  if(!(tok = js_token_data2(ctx, this_val)))
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

static const JSCFunctionListEntry js_token_inspect_funcs[1] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Token", JS_PROP_CONFIGURABLE),
};

JSValue
js_token_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Token* tok;
  LexerRule* rule;

  if(!(tok = js_token_data2(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectClass(ctx, js_token_class_id);
  rule = lexer_rule_at(tok->lexer, tok->id);

  // JS_SetPropertyFunctionList(ctx, obj, js_token_inspect_funcs, 1);
  // JS_DefinePropertyValueStr(ctx, obj, "constructor", JS_GetPropertyStr(ctx, this_val, "constructor"), 0);
  // JS_DefinePropertyValueStr(ctx, obj, "[Symbol.toStringTag]", JS_NewString(ctx, "Token"), 0);

  JS_DefinePropertyValueStr(ctx, obj, "id", JS_NewUint32(ctx, tok->id), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "seq", JS_NewUint32(ctx, tok->seq), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "type", JS_NewString(ctx, rule->name), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "lexeme", JS_NewString(ctx, tok->lexeme), JS_PROP_ENUMERABLE);

  JS_DefinePropertyValueStr(ctx, obj, "byteOffset", JS_NewUint32(ctx, tok->byte_offset), 0);
  JS_DefinePropertyValueStr(ctx, obj, "byteLength", JS_NewUint32(ctx, tok->byte_length), 0);
  JS_DefinePropertyValueStr(ctx, obj, "length", JS_NewUint32(ctx, tok->char_length), JS_PROP_ENUMERABLE);

  JS_DefinePropertyValueStr(ctx, obj, "loc", location_tovalue(&tok->loc, ctx), 0);
  return obj;
}

JSValue
js_token_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Token* tok;
  JSValue ret = JS_UNDEFINED;

  if(!(tok = js_token_data(this_val)))
    return ret;

  switch(magic) {
    case TOKEN_PROP_BYTELENGTH: {
      ret = JS_NewInt64(ctx, tok->byte_length);
      break;
    }
    case TOKEN_PROP_BYTEOFFSET: {
      ret = JS_NewInt64(ctx, tok->byte_offset);
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
    case TOKEN_PROP_SEQ: {
      ret = JS_NewUint32(ctx, tok->seq);
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
    location_free_rt(&tok->loc, rt);
    token_free_rt(tok, rt);
  }
  JS_FreeValueRT(rt, val);
}

static JSClassDef js_token_class = {
    .class_name = "Token",
    .finalizer = js_token_finalizer,
};

static const JSCFunctionListEntry js_token_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("byteLength", js_token_get, NULL, TOKEN_PROP_BYTELENGTH),
    JS_CGETSET_MAGIC_DEF("byteOffset", js_token_get, NULL, TOKEN_PROP_BYTEOFFSET),
    JS_CGETSET_MAGIC_DEF("length", js_token_get, NULL, TOKEN_PROP_CHARLENGTH),
    JS_CGETSET_MAGIC_DEF("start", js_token_get, NULL, TOKEN_PROP_START),
    JS_CGETSET_MAGIC_DEF("end", js_token_get, NULL, TOKEN_PROP_END),
    JS_CGETSET_MAGIC_DEF("charRange", js_token_get, NULL, TOKEN_PROP_CHARRANGE),
    JS_CGETSET_MAGIC_DEF("loc", js_token_get, NULL, TOKEN_PROP_LOC),
    JS_CGETSET_MAGIC_DEF("id", js_token_get, NULL, TOKEN_PROP_ID),
    JS_CGETSET_MAGIC_DEF("seq", js_token_get, NULL, TOKEN_PROP_SEQ),
    JS_CGETSET_MAGIC_DEF("type", js_token_get, NULL, TOKEN_PROP_TYPE),
    JS_CGETSET_MAGIC_DEF("rule", js_token_get, NULL, TOKEN_PROP_RULE),
    JS_CGETSET_MAGIC_DEF("lexeme", js_token_get, NULL, TOKEN_PROP_LEXEME),
    JS_CGETSET_MAGIC_DEF("value", js_token_get, NULL, TOKEN_PROP_LEXEME),
    // JS_CFUNC_DEF("toString", 0, js_token_tostring),
    // JS_CFUNC_DEF("[Symbol.toPrimitive]", 1, js_token_toprimitive),
    JS_ALIAS_DEF("position", "loc"),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Token", JS_PROP_CONFIGURABLE),
};
static const JSCFunctionListEntry js_token_static_funcs[] = {
    JS_PROP_INT32_DEF("EOF", LEXER_EOF, JS_PROP_ENUMERABLE),
};

static Token*
lexer_token(Lexer* lex, JSContext* ctx) {
  Token* tok;
  if((tok = js_mallocz(ctx, sizeof(Token)))) {
    tok->id = lex->token_id;
    tok->loc = location_clone(&lex->loc, ctx);
    tok->loc_val = JS_UNDEFINED;
    tok->byte_length = lex->byte_length;
    tok->char_length = lexer_charlen(lex);
    tok->lexeme = js_strndup(ctx, (const char*)&lex->input.data[lex->start], tok->byte_length);
    tok->byte_offset = lex->start;
    tok->lexer = lex;
    tok->seq = lex->seq;
  }
  return tok;
}

static JSValue
lexer_continue(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue* data) {
  JS_SetPropertyUint32(ctx, data[0], 0, JS_NewBool(ctx, TRUE));
  return JS_UNDEFINED;
}

static int32_t
lexer_to_state(Lexer* lex, JSContext* ctx, JSValueConst value) {
  int32_t num;
  if(JS_IsNumber(value)) {
    JS_ToInt32(ctx, &num, value);
  } else {
    const char* str = JS_ToCString(ctx, value);
    num = lexer_state_find(lex, str);
  }
  if(num >= 0 && (size_t)num < lexer_num_states(lex))
    return num;
  return -1;
}

static BOOL
lexer_handle(Lexer* lex, JSContext* ctx, JSValueConst this_val, JSValueConst handler) {
  JSValue data[1] = {JS_NewArray(ctx)};
  JSValue args[] = {JS_DupValue(ctx, this_val), JS_NewCFunctionData(ctx, lexer_continue, 0, 0, 1, data)};
  JSValue do_resume = JS_FALSE;

  /*   ret = */ JS_Call(ctx, handler, this_val, 2, args);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);

  do_resume = JS_GetPropertyUint32(ctx, data[0], 0);

  if(JS_IsBool(do_resume))
    if(JS_ToBool(ctx, do_resume))
      return TRUE;

  return FALSE;
}

static int
lexer_lex(Lexer* lex, JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int id = -1;
  int64_t skip = 0;
  JSValue callback = JS_UNDEFINED;

  if(argc >= 1 && (skip = lexer_to_state(lex, ctx, argv[0]))) {}

  callback = JS_GetPropertyStr(ctx, this_val, "callback");

  if(lex->byte_length)
    lexer_skip(lex);

  for(;;) {
    if((id = lexer_peek(lex, skip == -1 ? 0 : skip, ctx)) >= 0) {
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
          JSValue data[] = {JS_NewArray(ctx)};
          JSValue args[] = {this_val, JS_NewCFunctionData(ctx, lexer_continue, 0, 0, countof(data), data)};
          JSValue do_skip;
          /*ret =*/JS_Call(ctx, jsrule->action, this_val, 2, args);
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

      if(JS_IsFunction(ctx, callback)) {
        lexer_handle(lex, ctx, this_val, callback);
      }

    } else if(id == LEXER_ERROR_NOMATCH) {
      JSValue handler = JS_GetPropertyStr(ctx, this_val, "handler");
      if(JS_IsFunction(ctx, handler)) {

        if(lexer_handle(lex, ctx, this_val, handler) == TRUE)
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

  lex->input = js_input_chars(ctx, vinput);

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
js_lexer_add_rule(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Lexer* lex;
  char* name;
  int64_t mask = -1, skip = 0;
  RegExp expr;
  JSLexerRule* jsrule = 0;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  name = (magic || JS_IsString(argv[0])) ? js_tostring(ctx, argv[0]) : 0;
  expr = regexp_from_argv(argc - 1, &argv[1], ctx);

  if(argc >= 3 && JS_IsNumber(argv[2]))
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
    int index;
    LexerRule* rule;

    if((index = lexer_rule_add(lex, name, expr.source)) == -1)
      return JS_ThrowInternalError(ctx, "Lexer rule '%s' not unique", name);

    rule = lexer_rule_at(lex, index);
    rule->opaque = jsrule;

    return JS_NewInt32(ctx, index);
  } else {
    lexer_define(lex, name, expr.source);
  }

  return JS_UNDEFINED;
}

JSValue
js_lexer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
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

    if(lex->loc.file == -1 && i < argc && JS_IsString(argv[i]))
      lex->loc.file = JS_ValueToAtom(ctx, argv[i++]);

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

enum {
  METHOD_SET_INPUT = 0,
  METHOD_SKIP,
  METHOD_BACK,
  METHOD_PEEKC,
  METHOD_GETC,
  METHOD_SKIPC,
  METHOD_GET_RANGE,
  METHOD_CURRENT_LINE,
  METHOD_TOKEN_CLASS,
  METHOD_GET_RULE,
  METHOD_SKIPUNTIL,
  METHOD_ERROR,
  METHOD_PUSH_STATE,
  METHOD_POP_STATE,
  METHOD_TOP_STATE,
};

JSValue
js_lexer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Lexer* lex;
  JSValue ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_SET_INPUT: {
      Lexer* other;
      InputBuffer input;
      Location loc = {0, 0, 0, -1, 0};

      if((other = JS_GetOpaque(argv[0], js_lexer_class_id))) {
        input = input_buffer_clone(&other->input, ctx);
        loc = other->loc;
        lex->start = other->start;
      } else {
        input = js_input_chars(ctx, argv[0]);
      }

      input_buffer_free(&lex->input, ctx);
      lex->input = input;
      location_free_rt(&lex->loc, JS_GetRuntime(ctx));
      lex->loc = loc;

      if(argc > 1 && JS_IsString(argv[1])) {
        if(lex->loc.file > -1)
          JS_FreeAtom(ctx, lex->loc.file);
        lex->loc.file = JS_ValueToAtom(ctx, argv[1]);
      }

      break;
    }

    case METHOD_SKIP: {
      ret = JS_NewUint32(ctx, lexer_skip(lex));
      break;
    }

    case METHOD_BACK: {
      int i;
      for(i = 0; i < argc; i++) {
        Token* tok;
        Location* loc;
        if((loc = js_location_data(ctx, argv[i]))) {
          lexer_set_location(lex, loc, ctx);
          ret = JS_NewInt32(ctx, lexer_peek(lex, 1 << lex->state, ctx));
        } else if((tok = js_token_data(argv[i]))) {
          lexer_set_location(lex, &tok->loc, ctx);
          lex->byte_length = tok->byte_length;
          lex->seq = tok->seq;
          ret = JS_NewInt32(ctx, tok->id);
        } else if(JS_IsString(argv[i])) {
          size_t len;
          const char* str = JS_ToCStringLen(ctx, &len, argv[i]);
          if(lex->input.pos >= len && !memcmp(&lex->input.data[lex->input.pos - len], str, len)) {
            Location diff;
            location_zero(&diff);
            location_count(&diff, (const char*)&lex->input.data[lex->input.pos - len], len);
            location_sub(&lex->loc, &diff);
            ret = JS_NewInt32(ctx, lexer_peek(lex, 1 << lex->state, ctx));
          } else {
            char* buf = byte_escape((const char*)&lex->input.data[lex->input.pos - len], len);
            ret = JS_ThrowInternalError(ctx, "Lexer.prototype.back('%s') `%s` ...", str, buf);
            free(buf);
          }
          JS_FreeCString(ctx, str);
        }
      }

      break;
    }

    case METHOD_PEEKC: {
      if(!input_buffer_eof(&lex->input)) {
        size_t len;
        const uint8_t* buf = input_buffer_peek(&lex->input, &len);
        ret = JS_NewStringLen(ctx, (const char*)buf, len);
      }
      break;
    }

    case METHOD_GETC: {
      if(!input_buffer_eof(&lex->input)) {
        size_t len;
        const uint8_t* buf = input_buffer_get(&lex->input, &len);
        ret = JS_NewStringLen(ctx, (const char*)buf, len);
      }
      break;
    }

    case METHOD_SKIPC: {
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

    case METHOD_SKIPUNTIL: {
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

    case METHOD_GET_RANGE: {
      size_t start, end;
      start = lex->start;
      end = lex->input.pos;
      if(argc > 0) {
        js_value_tosize(ctx, &start, argv[0]);
        if(argc > 1)
          js_value_tosize(ctx, &end, argv[1]);
      }
      ret = JS_NewStringLen(ctx, (const char*)&lex->input.data[start], end - start);
      break;
    }

    case METHOD_CURRENT_LINE: {
      ret = JS_NewString(ctx, lexer_current_line(lex, ctx));
      break;
    }

    case METHOD_TOKEN_CLASS: {
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

    case METHOD_GET_RULE: {
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

    case METHOD_ERROR: {
      const char* message = JS_ToCString(ctx, argv[0]);
      char* location = location_tostring(&lex->loc, ctx);

      ret = JS_ThrowSyntaxError(ctx, "%s at %s", message, location);

      JS_FreeCString(ctx, message);
      js_free(ctx, location);
      break;
    }

    case METHOD_PUSH_STATE: {
      const char* state = JS_ToCString(ctx, argv[0]);
      int id;

      id = lexer_state_push(lex, state);
      // printf("[%zu] pushState('%s')\n", lexer_state_depth(lex), state);
      ret = JS_NewInt32(ctx, id);
      JS_FreeCString(ctx, state);
      break;
    }

    case METHOD_POP_STATE: {
      int id;
      if(lexer_state_depth(lex) > 0) {
        id = lexer_state_pop(lex);
        // printf("[%zu] popState() = '%s'\n", lexer_state_depth(lex),
        // lexer_state_name(lex, id));
        ret = JS_NewInt32(ctx, id);
      } else {
        ret = JS_ThrowInternalError(ctx, "lexer (%s) depth %zu", lexer_state_topname(lex), lexer_state_depth(lex));
      }

      break;
    }

    case METHOD_TOP_STATE: {
      int32_t index = 0, id;
      if(argc > 0 && JS_IsNumber(argv[0]))
        JS_ToInt32(ctx, &index, argv[0]);

      if((id = index > 0 ? lexer_state_top(lex, index) : lex->state) >= 0)
        ret = JS_NewString(ctx, lexer_state_name(lex, id));
      break;
    }
  }
  return ret;
}

enum {
  PROP_SIZE = 0,
  PROP_POS,
  PROP_START,
  PROP_EOF,
  PROP_FILENAME,
  PROP_LOC,
  PROP_RULENAMES,
  PROP_RULES,
  PROP_MODE,
  PROP_BYTE_LENGTH,
  PROP_CHAR_LENGTH,
  PROP_STATE,
  PROP_STATES,
  PROP_STATE_DEPTH,
  PROP_STATE_STACK,
  PROP_SOURCE,
  PROP_LEXEME,
  PROP_TOKEN,
};

JSValue
js_lexer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Lexer* lex;
  JSValue ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_POS: {
      ret = JS_NewInt64(ctx, lex->input.pos);
      break;
    }

    case PROP_SIZE: {
      ret = JS_NewInt64(ctx, lex->input.size);
      break;
    }

    case PROP_START: {
      ret = JS_NewInt64(ctx, lex->start);
      break;
    }

    case PROP_EOF: {
      ret = JS_NewBool(ctx, input_buffer_eof(&lex->input));
      break;
    }

    case PROP_FILENAME: {
      ret = lex->loc.file > -1 ? JS_AtomToValue(ctx, lex->loc.file) : JS_UNDEFINED;
      break;
    }

    case PROP_LOC: {
      ret = js_location_new(ctx, &lex->loc);
      break;
    }

    case PROP_RULENAMES: {
      LexerRule* rule;
      uint32_t i = 0;
      ret = JS_NewArray(ctx);

      vector_foreach_t(&lex->rules, rule) { JS_SetPropertyUint32(ctx, ret, i++, JS_NewString(ctx, rule->name)); }
      break;
    }

    case PROP_RULES: {
      LexerRule* rule;
      uint32_t i = 0;
      ret = JS_NewObject(ctx);

      vector_foreach_t(&lex->rules, rule) {
        // printf("rule #%" PRIu32 " '%s' '%s'\n", i, rule->name, rule->expr);
        JS_SetPropertyStr(ctx, ret, rule->name, JS_NewUint32(ctx, i));
        i++;
      }
      break;
    }

    case PROP_MODE: {
      ret = JS_NewInt32(ctx, lex->mode);
      break;
    }

    case PROP_BYTE_LENGTH: {
      ret = JS_NewUint32(ctx, lex->byte_length);
      break;
    }

    case PROP_CHAR_LENGTH: {
      ret = JS_NewUint32(ctx, lexer_charlen(lex));
      break;
    }

    case PROP_STATE: {
      ret = JS_NewInt32(ctx, lex->state);
      break;
    }

    case PROP_STATES: {
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

    case PROP_STATE_DEPTH: {
      ret = JS_NewUint32(ctx, lexer_state_depth(lex));
      break;
    }

    case PROP_STATE_STACK: {
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

    case PROP_SOURCE: {
      ret = JS_NewStringLen(ctx, (const char*)lex->input.data, lex->input.size);
      break;
    }
    case PROP_LEXEME: {
      ret = JS_NewStringLen(ctx, (const char*)lex->input.data + lex->start, lex->input.pos - lex->start);
      break;
    }
    case PROP_TOKEN: {
      Token* tok;

      if((tok = lexer_token(lex, ctx)))
        ret = js_token_wrap(ctx, tok);

      break;
    }
  }
  return ret;
}

JSValue
js_lexer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Lexer* lex;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_POS: {
      Token* tok;
      if((tok = js_token_data(value))) {
        lex->input.pos = tok->byte_offset;
        lex->loc = tok->loc;
      }
      break;
    }

    case PROP_BYTE_LENGTH: {
      Token* tok;
      if(JS_IsNumber(value)) {
        js_value_tosize(ctx, &lex->byte_length, value);
      } else if((tok = js_token_data(value))) {
        lex->byte_length = tok->byte_length;
      }
      break;
    }

    case PROP_FILENAME: {
      if(lex->loc.file > -1)
        JS_FreeAtom(ctx, lex->loc.file);
      lex->loc.file = JS_ValueToAtom(ctx, value);
      break;
    }

    case PROP_MODE: {
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

  if(!(lex = js_lexer_data2(ctx, this_val)))
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

  if(!(lex = js_lexer_data2(ctx, this_val)))
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

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  size = vector_size(&lex->state_stack, sizeof(int32_t)) + 1;
  stack = js_malloc_rt(JS_GetRuntime(ctx), sizeof(int32_t) * size);

  memcpy(stack, vector_begin(&lex->state_stack), sizeof(int32_t) * (size - 1));

  stack[size - 1] = lex->state;

  buf = JS_NewArrayBuffer(ctx, (void*)stack, sizeof(int32_t) * size, (JSFreeArrayBufferDataFunc*)&js_free_rt, stack, FALSE);

  ctor = js_global_get_str(ctx, "Int32Array");

  ret = JS_CallConstructor(ctx, ctor, 1, &buf);

  JS_FreeValue(ctx, ctor);
  JS_FreeValue(ctx, buf);

  return ret;
}

JSValue
js_lexer_escape(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  InputBuffer input = js_input_chars(ctx, argv[0]);
  DynBuf output;
  js_dbuf_init(ctx, &output);
  magic ? dbuf_put_unescaped_pred(&output, (const char*)input.data, input.size, lexer_unescape_pred)
        : dbuf_put_escaped_pred(&output, (const char*)input.data, input.size, lexer_escape_pred);
  return dbuf_tostring_free(&output, ctx);
}

JSValue
js_lexer_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  if(js_is_regexp(ctx, argv[0]) || JS_IsString(argv[0])) {
    RegExp re;
    re = regexp_from_argv(argc, argv, ctx);
    ret = JS_NewString(ctx, re.source);
  } else {
    InputBuffer input;
    input = js_input_chars(ctx, argv[0]);
    ret = JS_NewStringLen(ctx, (const char*)input.data, input.size);
    input_buffer_free(&input, ctx);
  }
  return ret;
}

JSValue
js_lexer_lex(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  Lexer* lex;
  int id, state = -1;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(argc > 0 && JS_IsString(argv[0])) {
    const char* name = JS_ToCString(ctx, argv[0]);
    state = lexer_state_push(lex, name);
    JS_FreeCString(ctx, name);
  }

  id = lexer_lex(lex, ctx, this_val, argc, argv);

  if(state > -1)
    lexer_state_pop(lex);

  switch(id) {
    case LEXER_ERROR_NOMATCH: {
      char* lexeme = lexer_lexeme_s(lex, ctx);
      char* file = location_file(&lex->loc, ctx);

      ret = JS_ThrowInternalError(ctx,
                                  "%s:%" PRIu32 ":%" PRIu32 ": No matching token (%d: %s) '%s'\n%.*s\n%*s",
                                  file,
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
      if(file)
        js_free(ctx, file);
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

enum {
  YIELD_ID = 0,
  YIELD_OBJ = 1,
  YIELD_DONE_VALUE = 2,
};

JSValue
js_lexer_nextfn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret, value = JS_UNDEFINED;
  Lexer* lex;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  ret = js_lexer_lex(ctx, this_val, argc, argv);

  if(JS_IsNumber(ret)) {
    int32_t id;

    JS_ToInt32(ctx, &id, ret);

    if(magic & YIELD_OBJ) {
      Token* tok = lexer_token(lex, ctx);
      value = js_token_wrap(ctx, tok);
    } else {
      value = JS_NewInt32(ctx, id);
    }
  }

  if(magic & YIELD_DONE_VALUE) {
    JSValue ret = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, ret, "value", value);
    JS_SetPropertyStr(ctx, ret, "done", JS_NewBool(ctx, JS_IsUndefined(value)));
    return ret;
  }

  return value;
}

JSValue
js_lexer_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  JSValue ret = js_lexer_nextfn(ctx, this_val, argc, argv, magic);

  *pdone = JS_IsUndefined(ret);

  return ret;
}

JSValue
js_lexer_call(JSContext* ctx, JSValueConst func_obj, JSValueConst this_val, int argc, JSValueConst argv[], int flags) {
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

JSValue
js_lexer_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue next, ret = JS_NewObject(ctx);

  next = JS_NewCFunction2(ctx, js_lexer_nextfn, "next", 0, JS_CFUNC_generic_magic, YIELD_ID | YIELD_DONE_VALUE);

  JS_SetPropertyStr(ctx, ret, "next", js_function_bind_this(ctx, next, this_val));
  return ret;

  return JS_DupValue(ctx, this_val);
}

static JSValue
js_lexer_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Lexer* lex;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectClass(ctx, js_lexer_class_id);

  JS_DefinePropertyValueStr(ctx, obj, "start", JS_NewUint32(ctx, lex->start), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "bytelen", JS_NewUint32(ctx, lex->byte_length), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "tokid", JS_NewInt32(ctx, lex->token_id), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "state", JS_NewInt32(ctx, lex->state), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "eof", JS_NewBool(ctx, input_buffer_eof(&lex->input)), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "loc", js_location_new(ctx, &lex->loc), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "pos", JS_NewUint32(ctx, lex->input.pos), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "size", JS_NewUint32(ctx, lex->input.size), JS_PROP_ENUMERABLE);

  return obj;
}

void
js_lexer_finalizer(JSRuntime* rt, JSValue val) {
  Lexer* lex;

  if((lex = JS_GetOpaque(val, js_lexer_class_id))) {
    location_free_rt(&lex->loc, rt);
    lexer_free_rt(lex, rt);
  }
  JS_FreeValueRT(rt, val);
}

static JSClassDef js_lexer_class = {.class_name = "Lexer", .finalizer = js_lexer_finalizer, .call = js_lexer_call};

static const JSCFunctionListEntry js_lexer_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_lexer_next, YIELD_OBJ),
    JS_CFUNC_MAGIC_DEF("nextId", 0, js_lexer_nextfn, YIELD_ID),
    JS_CFUNC_MAGIC_DEF("nextObj", 0, js_lexer_nextfn, YIELD_OBJ),
    JS_CGETSET_MAGIC_DEF("size", js_lexer_get, js_lexer_set, PROP_SIZE),
    JS_CGETSET_MAGIC_DEF("pos", js_lexer_get, js_lexer_set, PROP_POS),
    JS_CGETSET_MAGIC_DEF("start", js_lexer_get, 0, PROP_START),
    JS_CGETSET_MAGIC_DEF("loc", js_lexer_get, 0, PROP_LOC),
    JS_CGETSET_MAGIC_DEF("eof", js_lexer_get, 0, PROP_EOF),
    JS_CGETSET_MAGIC_DEF("mode", js_lexer_get, js_lexer_set, PROP_MODE),
    JS_CGETSET_MAGIC_DEF("byteLength", js_lexer_get, 0, PROP_BYTE_LENGTH),
    JS_CGETSET_MAGIC_DEF("charLength", js_lexer_get, 0, PROP_CHAR_LENGTH),
    JS_CGETSET_MAGIC_DEF("state", js_lexer_get, 0, PROP_STATE),
    JS_CGETSET_MAGIC_DEF("states", js_lexer_get, 0, PROP_STATES),
    JS_CGETSET_MAGIC_DEF("stateDepth", js_lexer_get, 0, PROP_STATE_DEPTH),
    JS_CGETSET_MAGIC_DEF("stateStack", js_lexer_get, 0, PROP_STATE_STACK),
    JS_CGETSET_MAGIC_DEF("source", js_lexer_get, 0, PROP_SOURCE),
    JS_CGETSET_MAGIC_DEF("lexeme", js_lexer_get, 0, PROP_LEXEME),
    JS_CGETSET_MAGIC_DEF("token", js_lexer_get, 0, PROP_TOKEN),
    JS_CGETSET_MAGIC_DEF("fileName", js_lexer_get, js_lexer_set, PROP_FILENAME),
    JS_CFUNC_MAGIC_DEF("setInput", 1, js_lexer_method, METHOD_SET_INPUT),
    JS_CFUNC_MAGIC_DEF("skip", 0, js_lexer_method, METHOD_SKIP),
    JS_CFUNC_MAGIC_DEF("skipUntil", 1, js_lexer_method, METHOD_SKIPUNTIL),
    JS_CFUNC_MAGIC_DEF("tokenClass", 1, js_lexer_method, METHOD_TOKEN_CLASS),
    JS_CFUNC_MAGIC_DEF("define", 2, js_lexer_add_rule, 0),
    JS_CFUNC_MAGIC_DEF("addRule", 2, js_lexer_add_rule, 1),
    JS_CFUNC_MAGIC_DEF("getRule", 1, js_lexer_method, METHOD_GET_RULE),
    JS_CFUNC_MAGIC_DEF("pushState", 1, js_lexer_method, METHOD_PUSH_STATE),
    JS_ALIAS_DEF("begin", "pushState"),
    JS_CFUNC_MAGIC_DEF("popState", 0, js_lexer_method, METHOD_POP_STATE),
    JS_CFUNC_MAGIC_DEF("topState", 0, js_lexer_method, METHOD_TOP_STATE),
    JS_CFUNC_MAGIC_DEF("currentLine", 0, js_lexer_method, METHOD_CURRENT_LINE),
    JS_CFUNC_MAGIC_DEF("back", 0, js_lexer_method, METHOD_BACK),
    JS_CGETSET_MAGIC_DEF("ruleNames", js_lexer_get, 0, PROP_RULENAMES),
    JS_CGETSET_MAGIC_DEF("rules", js_lexer_get, 0, PROP_RULES),
    JS_CFUNC_DEF("lex", 0, js_lexer_lex),
    JS_CFUNC_DEF("inspect", 0, js_lexer_inspect),
    JS_CGETSET_DEF("tokens", js_lexer_tokens, 0),
    JS_CGETSET_DEF("states", js_lexer_states, 0),
    // JS_CGETSET_DEF("stateStack", js_lexer_statestack, 0),
    // JS_ALIAS_DEF("position", "loc"),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_lexer_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Lexer", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_lexer_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("escape", 1, js_lexer_escape, 0),
    JS_CFUNC_MAGIC_DEF("unescape", 1, js_lexer_escape, 1),
    JS_CFUNC_DEF("toString", 1, js_lexer_tostring),
    JS_PROP_INT32_DEF("FIRST", LEXER_FIRST, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("LONGEST", LEXER_LONGEST, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("LAST", LEXER_LAST, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("YIELD_ID", YIELD_ID, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("YIELD_OBJ", YIELD_OBJ, JS_PROP_ENUMERABLE),
};

int
js_lexer_init(JSContext* ctx, JSModuleDef* m) {

  js_location_init(ctx, m);

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
  if(!(m = JS_NewCModule(ctx, module_name, &js_lexer_init)))
    return m;

  JS_AddModuleExport(ctx, m, "Location");
  JS_AddModuleExport(ctx, m, "Token");
  JS_AddModuleExport(ctx, m, "Lexer");
  return m;
}

/**
 * @}
 */
