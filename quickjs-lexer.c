#include "defines.h"
#include <quickjs.h>
#include <libregexp.h>
#include "quickjs-lexer.h"
#include "quickjs-location.h"
#include "vector.h"
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "buffer-utils.h"
#include "debug.h"
#include "token.h"

/**
 * \addtogroup quickjs-lexer
 * @{
 */
typedef struct {
  JSValue action;
  BOOL skip;
} JSLexerRule;

thread_local VISIBLE JSClassID js_token_class_id = 0, js_lexer_class_id = 0;
thread_local JSValue token_proto = {{0}, JS_TAG_UNDEFINED}, token_ctor = {{0}, JS_TAG_UNDEFINED};
thread_local JSValue lexer_proto = {{0}, JS_TAG_UNDEFINED}, lexer_ctor = {{0}, JS_TAG_UNDEFINED};

static JSValue
offset_toarray(OffsetLength offs_len, JSContext* ctx) {
  JSValue ret = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, ret, 0, JS_NewInt64(ctx, offs_len.offset));
  JS_SetPropertyUint32(ctx, ret, 1, JS_NewInt64(ctx, offs_len.offset + offs_len.length));
  return ret;
}

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
  if(rule->opaque) {
    JS_SetPropertyUint32(ctx, ret, 3, JS_DupValue(ctx, ((JSLexerRule*)rule->opaque)->action));
    JS_SetPropertyUint32(ctx, ret, 4, JS_NewBool(ctx, ((JSLexerRule*)rule->opaque)->skip));
  }
  // JS_SetPropertyUint32(ctx, ret, 3, JS_NewInt64(ctx, rule->mask));

  dbuf_free(&dbuf);
  return ret;
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
  Lexer* lex = 0;
  Token* tok;
  JSValue proto, obj = JS_UNDEFINED;

  if(!(tok = js_mallocz(ctx, sizeof(Token))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_token_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, tok);

  if(argc >= 1 && (lex = js_lexer_data(argv[0]))) {
    argc--;
    argv++;
  }

  if(argc > 0) {
    if(JS_IsNumber(argv[0])) {
      JS_ToInt32(ctx, &tok->id, argv[0]);
    } else if(JS_IsString(argv[0])) {
    }
  }

  if(argc > 1)
    tok->lexeme = (uint8_t*)js_tostring(ctx, argv[1]);

  if(argc > 2) {
    Location* loc;

    if((loc = js_location_data2(ctx, argv[2])))
      tok->loc = location_dup(loc);
    else
      tok->loc = js_location_from(ctx, argv[2]);
  }

  if(argc > 3)
    JS_ToInt64(ctx, &tok->loc->char_offset, argv[3]);

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
  JSValue obj;
  Token* tok;
  LexerRule* rule;
  int argi = 0;
  int32_t d = 0;

  if(!(tok = js_token_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(argi < argc && JS_IsNumber(argv[argi]))
    JS_ToInt32(ctx, &d, argv[argi++]);

  obj = JS_NewObjectProtoClass(ctx, token_proto, js_token_class_id);
  rule = lexer_rule_at(tok->lexer, tok->id);

  JS_DefinePropertyValueStr(ctx, obj, "id", JS_NewUint32(ctx, tok->id), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "seq", JS_NewUint32(ctx, tok->seq), JS_PROP_ENUMERABLE);

  JS_DefinePropertyValueStr(ctx, obj, "type", rule ? JS_NewString(ctx, rule->name) : JS_NULL, JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "lexeme", JS_NewString(ctx, (const char*)tok->lexeme), JS_PROP_ENUMERABLE);

  if(tok->loc) {
    if(tok->loc->char_offset != -1LL)
      JS_DefinePropertyValueStr(ctx, obj, "charOffset", JS_NewInt64(ctx, tok->loc->char_offset), JS_PROP_ENUMERABLE);

    JS_DefinePropertyValueStr(ctx, obj, "loc", js_location_wrap(ctx, tok->loc), 0);
  }

  JS_DefinePropertyValueStr(ctx, obj, "charLength", JS_NewUint32(ctx, tok->char_length), JS_PROP_ENUMERABLE);

  JS_DefinePropertyValueStr(ctx, obj, "charRange", offset_toarray(token_char_range(tok), ctx), JS_PROP_ENUMERABLE);

  return obj;
}

enum {
  TOKEN_BYTELENGTH = 0,
  TOKEN_CHARLENGTH,
  TOKEN_BYTERANGE,
  TOKEN_CHARRANGE,
  TOKEN_LEXEME,
  TOKEN_LOC,
  TOKEN_ID,
  TOKEN_SEQ,
  TOKEN_RULE,
  TOKEN_TYPE,
};

JSValue
js_token_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Token* tok;
  JSValue ret = JS_UNDEFINED;

  if(!(tok = js_token_data(this_val)))
    return ret;

  switch(magic) {
    case TOKEN_BYTELENGTH: {
      ret = JS_NewInt64(ctx, tok->byte_length);
      break;
    }

    case TOKEN_CHARLENGTH: {
      ret = JS_NewInt64(ctx, tok->char_length);
      break;
    }

    case TOKEN_BYTERANGE: {
      Location* loc;
      if((loc = tok->loc))
        ret = offset_toarray(token_byte_range(tok), ctx);
      break;
    }

    case TOKEN_CHARRANGE: {
      Location* loc;
      if((loc = tok->loc))
        ret = offset_toarray(token_char_range(tok), ctx);
      break;
    }

    case TOKEN_LEXEME: {
      ret = JS_NewStringLen(ctx, (const char*)tok->lexeme, tok->byte_length);
      break;
    }

    case TOKEN_LOC: {
      Location* loc;

      if((loc = location_dup(tok->loc)))
        ret = js_location_wrap(ctx, loc);

      break;
    }

    case TOKEN_ID: {
      ret = JS_NewInt32(ctx, tok->id);
      break;
    }

    case TOKEN_SEQ: {
      ret = JS_NewUint32(ctx, tok->seq);
      break;
    }

    case TOKEN_RULE: {
      LexerRule* rule = lexer_rule_at(tok->lexer, tok->id);
      ret = rule ? js_lexer_rule_new(ctx, tok->lexer, rule) : JS_NULL;
      break;
    }

    case TOKEN_TYPE: {
      LexerRule* rule = lexer_rule_at(tok->lexer, tok->id);
      ret = rule ? JS_NewString(ctx, rule->name) : JS_NULL;
      break;
    }
  }
  return ret;
}

void
js_token_finalizer(JSRuntime* rt, JSValue val) {
  Token* tok;

  if((tok = js_token_data(val))) {
    token_free(tok, rt);
    // JS_SetOpaque(val, 0);
  }
}

static JSClassDef js_token_class = {
    .class_name = "Token",
    .finalizer = js_token_finalizer,
};

static const JSCFunctionListEntry js_token_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("charLength", js_token_get, NULL, TOKEN_CHARLENGTH),
    JS_CGETSET_MAGIC_DEF("byteLength", js_token_get, NULL, TOKEN_BYTELENGTH),
    JS_CGETSET_MAGIC_DEF("charRange", js_token_get, NULL, TOKEN_CHARRANGE),
    JS_CGETSET_MAGIC_DEF("byteRange", js_token_get, NULL, TOKEN_BYTERANGE),
    JS_CGETSET_MAGIC_DEF("loc", js_token_get, NULL, TOKEN_LOC),
    JS_CGETSET_MAGIC_DEF("id", js_token_get, NULL, TOKEN_ID),
    JS_CGETSET_MAGIC_DEF("seq", js_token_get, NULL, TOKEN_SEQ),
    JS_CGETSET_MAGIC_DEF("type", js_token_get, NULL, TOKEN_TYPE),
    JS_CGETSET_MAGIC_DEF("rule", js_token_get, NULL, TOKEN_RULE),
    JS_CGETSET_MAGIC_DEF("lexeme", js_token_get, NULL, TOKEN_LEXEME),
    JS_CGETSET_MAGIC_DEF("value", js_token_get, NULL, TOKEN_LEXEME),
    // JS_CFUNC_DEF("toString", 0, js_token_tostring),
    // JS_CFUNC_DEF("[Symbol.toPrimitive]", 1, js_token_toprimitive),
    JS_ALIAS_DEF("position", "loc"),
    JS_ALIAS_DEF("length", "charLength"),
    JS_ALIAS_DEF("range", "charRange"),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Token", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_token_static_funcs[] = {
    JS_PROP_INT32_DEF("EOF", LEXER_EOF, JS_PROP_ENUMERABLE),
};

static JSValue
lexer_continue(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue data[]) {
  JSValue val = argc >= 1 ? JS_DupValue(ctx, argv[0]) : JS_NewBool(ctx, TRUE);

  JS_SetPropertyUint32(ctx, data[0], 0, val);

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
  BOOL result = FALSE;
  JSValue ret, do_resume = JS_FALSE, data[1] = {JS_NewArray(ctx)};
  JSValueConst args[2] = {
      JS_DupValue(ctx, this_val),
      JS_NewCFunctionData(ctx, lexer_continue, 0, 0, 1, data),
  };

  ret = JS_Call(ctx, handler, this_val, countof(args), args);

  JS_FreeValue(ctx, ret);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);

  do_resume = JS_GetPropertyUint32(ctx, data[0], 0);

  if(JS_IsBool(do_resume))
    result = JS_ToBool(ctx, do_resume);

  JS_FreeValue(ctx, data[0]);

  return result;
}

static int
lexer_lex(Lexer* lex, JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int64_t flags = 0;
  int id = 0;

  if(argc >= 1 && (flags = lexer_to_state(lex, ctx, argv[0]))) {
  }

  if(lex->byte_length > 0 && lex->token_id != -1)
    lexer_skip(lex);

  for(;;) {
    if((id = lexer_peek(lex, id, ctx)) >= 0) {
      LexerRule* rule = lexer_rule_at(lex, id);
      JSLexerRule* jsrule;

      // printf("state %i rule %s\n", lex->state, rule->name);

      if((rule->mask & flags)) {
        lexer_skip(lex);
        continue;
      }

      if((jsrule = rule->opaque)) {
        BOOL skip = FALSE;

        if(JS_IsFunction(ctx, jsrule->action))
          skip = lexer_handle(lex, ctx, this_val, jsrule->action);

        if(skip || jsrule->skip) {
          lexer_skip(lex);
          continue;
        }
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
  size_t size, start = lex->pos;

  while(start > 0 && lex->data[start - 1] != '\n')
    start--;

  size = byte_chr((const char*)&lex->data[start], lex->size - start, '\n');

  return js_strndup(ctx, (const char*)&lex->data[start], size);
}

static char*
lexer_lexeme_s(Lexer* lex, JSContext* ctx) {
  size_t len;
  char* s;
  DynBuf output;

  js_dbuf_init(ctx, &output);

  s = lexer_lexeme(lex, &len);

  dbuf_put_escaped_pred(&output, s, len, lexer_escape_pred);
  dbuf_0(&output);

  return (char*)output.buf;
}

static Token*
lexer_token(Lexer* lex, int32_t id, JSContext* ctx) {
  size_t len;
  const char* lexeme;
  Token* tok;

  if(!(lexeme = lexer_lexeme(lex, &len)))
    return 0;

  if(!(tok = token_create(id, lexeme, len, ctx)))
    return 0;

  // lexer_skip_n(lex, len);

  tok->lexer = lexer_dup(lex);
  tok->seq = lex->seq;

  *tok->loc = lexer_get_location(lex, ctx);
  /*  len = lex->pos - tok->loc->byte_offset;
  tok->loc->byte_offset += len;*/

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
  }

  lexer_define(lex, name, expr.source);

  return JS_UNDEFINED;
}

JSValue
js_lexer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, ret;
  Lexer* lex;
  int64_t mask = MASK_ALL;

  /* using new_target to get the prototype is necessary when the class is extended. */
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
  LEXER_SET_INPUT = 0,
  LEXER_SKIP_BYTES,
  LEXER_SKIP_TOKEN,
  LEXER_BACK,
  LEXER_PEEKC,
  LEXER_GETC,
  LEXER_SKIP_CHARS,
  LEXER_GET_RANGE,
  LEXER_CURRENT_LINE,
  LEXER_TOKEN_CLASS,
  LEXER_GET_RULE,
  LEXER_SKIP_UNTIL,
  LEXER_ERROR,
  LEXER_PUSH_STATE,
  LEXER_POP_STATE,
  LEXER_TOP_STATE,
  LEXER_PEEK,
};

JSValue
js_lexer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Lexer* lex;
  JSValue ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LEXER_SET_INPUT: {
      Lexer* other;
      InputBuffer input;
      Location loc = LOCATION();

      if((other = JS_GetOpaque(argv[0], js_lexer_class_id))) {
        input = input_buffer_clone(&other->input, ctx);
        loc = other->loc;
        // lex->start = other->start;
      } else {
        input = js_input_chars(ctx, argv[0]);
      }

      input_buffer_free(&lex->input, ctx);
      lex->input = input;
      location_release(&lex->loc, JS_GetRuntime(ctx));
      lex->loc = loc;

      if(argc > 1 && JS_IsString(argv[1])) {
        if(lex->loc.file > -1)
          JS_FreeAtom(ctx, lex->loc.file);
        lex->loc.file = JS_ValueToAtom(ctx, argv[1]);
      }

      break;
    }

    case LEXER_SKIP_BYTES: {
      uint32_t n = 0;
      JS_ToUint32(ctx, &n, argv[0]);

      if(n > lex->byte_length)
        ret = JS_ThrowInternalError(ctx, "skipBytes(): count n > %lu", (unsigned long)lex->byte_length);
      else
        ret = JS_NewUint32(ctx, lexer_skip_n(lex, n));
      break;
    }

    case LEXER_SKIP_TOKEN: {
      if(lex->token_id == -1)
        ret = JS_ThrowInternalError(ctx, "skipToken(): lexer token id is -1");
      else if(lex->byte_length == 0)
        ret = JS_ThrowInternalError(ctx, "skipToken(): lexer token length is 0");
      else
        ret = JS_NewUint32(ctx, lexer_skip(lex));
      break;
    }

    case LEXER_BACK: {
      int i;
      for(i = 0; i < argc; i++) {
        Token* tok;
        Location* loc;
        if((loc = js_location_data2(ctx, argv[i]))) {
          lexer_set_location(lex, loc, ctx);
          if(lex->byte_length > 0 && lex->token_id != -1)
            lexer_clear_token(lex);
          ret = JS_NewInt32(ctx, lexer_peek(lex, /*1 << lex->state,*/ 0, ctx));
        } else if((tok = js_token_data(argv[i]))) {
          lexer_set_location(lex, tok->loc, ctx);
          lex->byte_length = tok->byte_length;
          lex->seq = tok->seq;
          ret = JS_NewInt32(ctx, tok->id);
        } /*else if(JS_IsString(argv[i])) {
          size_t len;
          const char* str = JS_ToCStringLen(ctx, &len, argv[i]);
          if(lex->pos >= len && !memcmp(&lex->data[lex->pos - len], str, len)) {
            Location diff;
            location_zero(&diff);
            location_count(&diff, (const uint8_t*)&lex->data[lex->pos - len], len);
            location_sub(&lex->loc, &diff);
            ret = JS_NewInt32(ctx, lexer_peek(lex, 1 << lex->state, 0, ctx));
          } else {
            char* buf = byte_escape((const char*)&lex->data[lex->pos - len], len);
            ret = JS_ThrowInternalError(ctx, "Lexer.prototype.back('%s') `%s` ...", str, buf);
            free(buf);
          }
          JS_FreeCString(ctx, str);
        }*/
        else {
          ret = JS_ThrowTypeError(ctx, "Lexer.prototype.back() needs token or location");
        }
      }

      break;
    }

    case LEXER_PEEKC: {
      if(!input_buffer_eof(&lex->input)) {
        size_t len;
        const uint8_t* buf = input_buffer_peek(&lex->input, &len);
        ret = JS_NewStringLen(ctx, (const char*)buf, len);
      }
      break;
    }

    case LEXER_GETC: {
      if(!input_buffer_eof(&lex->input)) {
        size_t len;
        const uint8_t* buf = input_buffer_get(&lex->input, &len);
        ret = JS_NewStringLen(ctx, (const char*)buf, len);
      }
      break;
    }

    case LEXER_SKIP_CHARS: {
      if(!input_buffer_eof(&lex->input)) {
        int32_t ntimes = 1;
        const uint8_t* p = 0;
        size_t n;
        if(argc > 0)
          JS_ToInt32(ctx, &ntimes, argv[0]);
        while(ntimes-- > 0) {
          p = input_buffer_get(&lex->input, &n);
        }
        if(p)
          ret = JS_NewStringLen(ctx, (const char*)p, n);
      }
      break;
    }

    case LEXER_SKIP_UNTIL: {
      if(!input_buffer_eof(&lex->input)) {
        JSValueConst pred;
        if(!JS_IsFunction(ctx, argv[0]))
          return JS_ThrowTypeError(ctx, "argument 1 is not a function");
        pred = argv[0];
        while(lex->pos < lex->size) {
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
          // lex->start = lex->pos;
        }
      }
      break;
    }

    case LEXER_GET_RANGE: {
      size_t start, end;
      start = lex->pos;
      end = start + lex->byte_length;
      if(argc > 0) {
        js_value_tosize(ctx, &start, argv[0]);
        if(argc > 1)
          js_value_tosize(ctx, &end, argv[1]);
      }
      ret = JS_NewStringLen(ctx, (const char*)&lex->data[start], end - start);
      break;
    }

    case LEXER_CURRENT_LINE: {
      ret = JS_NewString(ctx, lexer_current_line(lex, ctx));
      break;
    }

    case LEXER_TOKEN_CLASS: {
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

    case LEXER_GET_RULE: {
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

      if(rule) {
        ret = js_lexer_rule_new(ctx, lex, rule);
      }

      break;
    }

    case LEXER_ERROR: {
      const char* message = JS_ToCString(ctx, argv[0]);
      char* location = location_tostring(&lex->loc, ctx);

      ret = JS_ThrowSyntaxError(ctx, "%s at %s", message, location);

      JS_FreeCString(ctx, message);
      js_free(ctx, location);
      break;
    }

    case LEXER_PUSH_STATE: {
      const char* state = JS_ToCString(ctx, argv[0]);
      int id;

      id = lexer_state_push(lex, state);
      // printf("[%zu] pushState('%s')\n", lexer_state_depth(lex), state);
      ret = JS_NewInt32(ctx, id);
      JS_FreeCString(ctx, state);
      break;
    }

    case LEXER_POP_STATE: {
      int id;
      if(lexer_state_depth(lex) > 0) {
        id = lexer_state_pop(lex);
        // printf("[%zu] popState() = '%s'\n", lexer_state_depth(lex),
        // lexer_state_name(lex, id));
        ret = JS_NewInt32(ctx, id);
      } else {
        ret = JS_ThrowInternalError(ctx,
                                    "lexer (%s) depth %lu",
                                    lexer_state_topname(lex),
                                    (unsigned long)lexer_state_depth(lex));
      }

      break;
    }

    case LEXER_TOP_STATE: {
      int32_t index = 0, id;
      if(argc > 0 && JS_IsNumber(argv[0]))
        JS_ToInt32(ctx, &index, argv[0]);

      if((id = index > 0 ? lexer_state_top(lex, index) : lex->state) >= 0)
        ret = JS_NewString(ctx, lexer_state_name(lex, id));
      break;
    }
    case LEXER_PEEK: {
      ret = JS_NewInt32(ctx, lexer_peek(lex, 0, ctx));
      break;
    }
  }
  return ret;
}

enum {
  LEXER_SIZE = 0,
  LEXER_POSITION,
  LEXER_ENDOFFILE,
  LEXER_FILENAME,
  LEXER_LOCATION,
  LEXER_RULENAMES,
  LEXER_RULES,
  LEXER_MODE,
  LEXER_SEQUENCE,
  LEXER_BYTE_LENGTH,
  LEXER_CHAR_LENGTH,
  LEXER_STATE,
  LEXER_STATES,
  LEXER_STATE_DEPTH,
  LEXER_STATE_STACK,
  LEXER_SOURCE,
  LEXER_LEXEME,
  LEXER_TOKEN,
};

JSValue
js_lexer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Lexer* lex;
  JSValue ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LEXER_POSITION: {
      ret = JS_NewInt64(ctx, lex->pos);
      break;
    }

    case LEXER_SIZE: {
      ret = JS_NewInt64(ctx, lex->size);
      break;
    }

      /*    case PROP_START: {
            ret = JS_NewInt64(ctx, lex->start);
            break;
          }*/

    case LEXER_ENDOFFILE: {
      ret = JS_NewBool(ctx, input_buffer_eof(&lex->input));
      break;
    }

    case LEXER_FILENAME: {
      ret = lex->loc.file > -1 ? JS_AtomToValue(ctx, lex->loc.file) : JS_UNDEFINED;
      break;
    }

    case LEXER_LOCATION: {
      Location* loc;

      if((loc = location_new(ctx))) {
        location_copy(loc, &lex->loc, ctx);
        ret = js_location_wrap(ctx, loc);
      }
      break;
    }

    case LEXER_RULENAMES: {
      LexerRule* rule;
      uint32_t i = 0;
      ret = JS_NewArray(ctx);

      vector_foreach_t(&lex->rules, rule) { JS_SetPropertyUint32(ctx, ret, i++, JS_NewString(ctx, rule->name)); }
      break;
    }

    case LEXER_RULES: {
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

    case LEXER_MODE: {
      ret = JS_NewInt32(ctx, lex->mode);
      break;
    }

    case LEXER_SEQUENCE: {
      ret = JS_NewInt64(ctx, lex->seq);
      break;
    }

    case LEXER_BYTE_LENGTH: {
      ret = JS_NewUint32(ctx, lex->byte_length);
      break;
    }

    case LEXER_CHAR_LENGTH: {
      ret = JS_NewUint32(ctx, lexer_charlen(lex));
      break;
    }

    case LEXER_STATE: {
      ret = JS_NewInt32(ctx, lex->state);
      break;
    }

    case LEXER_STATES: {
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

    case LEXER_STATE_DEPTH: {
      ret = JS_NewUint32(ctx, lexer_state_depth(lex));
      break;
    }

    case LEXER_STATE_STACK: {
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

    case LEXER_SOURCE: {
      ret = JS_NewStringLen(ctx, (const char*)lex->data, lex->size);
      break;
    }

    case LEXER_LEXEME: {
      ret = JS_NewStringLen(ctx, (const char*)lex->data + lex->pos, lex->byte_length);
      break;
    }

    case LEXER_TOKEN: {
      Token* tok;

      if(lex->token_id < 0)
        ret = JS_NULL;
      else if((tok = lexer_token(lex, lex->token_id, ctx)))
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
    case LEXER_POSITION: {
      Token* tok;

      if((tok = js_token_data(value))) {
        lex->pos = tok->loc->char_offset;

        location_release(&lex->loc, JS_GetRuntime(ctx));
        location_copy(&lex->loc, tok->loc, ctx);
        //        lex->loc = tok->loc;
      } /* else if(JS_IsNumber(value)) {
         uint64_t newpos = lex->pos;
         JS_ToIndex(ctx, &newpos, value);
         lex->pos = newpos;
       } */
      else {
        return JS_ThrowTypeError(ctx, "lexer.pos must be a Location object");
      }
      break;
    }

    case LEXER_BYTE_LENGTH: {
      Token* tok;
      if(JS_IsNumber(value)) {
        js_value_tosize(ctx, &lex->byte_length, value);
      } else if((tok = js_token_data(value))) {
        lex->byte_length = tok->byte_length;
      }
      break;
    }

    case LEXER_FILENAME: {
      if(lex->loc.file > -1)
        JS_FreeAtom(ctx, lex->loc.file);
      lex->loc.file = JS_ValueToAtom(ctx, value);
      break;
    }

    case LEXER_MODE: {
      int32_t m;
      JS_ToInt32(ctx, &m, value);
      lex->mode = m;
      break;
    }

    case LEXER_SEQUENCE: {
      uint64_t s;
      JS_ToIndex(ctx, &s, value);
      lex->seq = s;
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

  buf = JS_NewArrayBuffer(
      ctx, (void*)stack, sizeof(int32_t) * size, (JSFreeArrayBufferDataFunc*)&js_free_rt, stack, FALSE);

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
                                  "%s:%" PRIu32 ":%" PRIu32 ": No matching token (%d: %s)\n%.*s\n%*s",
                                  file,
                                  lex->loc.line + 1,
                                  lex->loc.column + 1,
                                  lexer_state_top(lex, 0),
                                  lexer_state_name(lex, lexer_state_top(lex, 0)),
                                  /*   lexeme,*/
                                  (int)(byte_chr((const char*)&lex->data[lex->pos], lex->size - lex->pos, '\n') +
                                        lex->loc.column),
                                  &lex->data[lex->pos - lex->loc.column],
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

    case LEXER_EXCEPTION: {
      ret = JS_EXCEPTION;
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
      Token* tok = lexer_token(lex, id, ctx);
      value = js_token_wrap(ctx, tok);
    } else {
      value = JS_NewInt32(ctx, id);
    }
  } else {
    value = JS_DupValue(ctx, ret);
  }
  JS_FreeValue(ctx, ret);

  if(magic & YIELD_DONE_VALUE) {
    JSValue ret = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, ret, "value", value);
    JS_SetPropertyStr(ctx, ret, "done", JS_NewBool(ctx, js_is_null_or_undefined(value)));
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

  /*if(argc > 1 && JS_IsNumber(argv[1])) {
    JS_SetPropertyStr(ctx, func_obj, "skip", JS_DupValue(ctx, argv[1]));
  }*/

  return JS_DupValue(ctx, func_obj);
}

JSValue
js_lexer_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue next, ret = JS_NewObject(ctx);
  JSAtom symbol = js_symbol_static_atom(ctx, "iterator");

  next = JS_NewCFunction2(ctx, (JSCFunction*)&js_lexer_nextfn, "next", 0, JS_CFUNC_generic_magic, magic);

  JS_DefinePropertyValue(ctx,
                         ret,
                         symbol,
                         JS_NewCFunction2(ctx, (JSCFunction*)&JS_DupValue, "[Symbol.iterator]", 0, JS_CFUNC_generic, 0),
                         JS_PROP_CONFIGURABLE);
  JS_FreeAtom(ctx, symbol);

  JS_DefinePropertyValueStr(ctx, ret, "next", js_function_bind_this(ctx, next, this_val), JS_PROP_CONFIGURABLE);
  return ret;

  return JS_DupValue(ctx, this_val);
}

/*static JSValue
js_lexer_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Lexer* lex;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectClass(ctx, js_lexer_class_id);

  JS_DefinePropertyValueStr(ctx, obj, "bytelen", JS_NewUint32(ctx, lex->byte_length), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "tokid", JS_NewInt32(ctx, lex->token_id), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "state", JS_NewInt32(ctx, lex->state), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "eof", JS_NewBool(ctx, input_buffer_eof(&lex->input)), JS_PROP_ENUMERABLE);
  // JS_DefinePropertyValueStr(ctx, obj, "loc", js_location_new(ctx, &lex->loc), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "pos", JS_NewUint32(ctx, lex->pos), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "size", JS_NewUint32(ctx, lex->size), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "seq", JS_NewInt64(ctx, lex->seq), JS_PROP_ENUMERABLE);

  return obj;
}*/

void
js_lexer_finalizer(JSRuntime* rt, JSValue val) {
  Lexer* lex;

  if((lex = JS_GetOpaque(val, js_lexer_class_id))) {
    lexer_free(lex, rt);
  }
}

static JSClassDef js_lexer_class = {
    .class_name = "Lexer",
    .finalizer = js_lexer_finalizer,
    .call = js_lexer_call,
};

static const JSCFunctionListEntry js_lexer_proto_funcs[] = {
    // JS_ITERATOR_NEXT_DEF("next", 0, js_lexer_next, YIELD_OBJ),
    JS_CFUNC_MAGIC_DEF("peek", 0, js_lexer_method, LEXER_PEEK),
    JS_CFUNC_MAGIC_DEF("next", 0, js_lexer_nextfn, YIELD_ID),
    JS_CFUNC_MAGIC_DEF("nextToken", 0, js_lexer_nextfn, YIELD_OBJ),
    JS_CGETSET_MAGIC_DEF("size", js_lexer_get, js_lexer_set, LEXER_SIZE),
    JS_CGETSET_MAGIC_DEF("pos", js_lexer_get, js_lexer_set, LEXER_POSITION),
    // JS_CGETSET_MAGIC_DEF("start", js_lexer_get, 0, PROP_START),
    JS_CGETSET_MAGIC_DEF("loc", js_lexer_get, 0, LEXER_LOCATION),
    JS_CGETSET_MAGIC_DEF("eof", js_lexer_get, 0, LEXER_ENDOFFILE),
    JS_CGETSET_MAGIC_DEF("mode", js_lexer_get, js_lexer_set, LEXER_MODE),
    JS_CGETSET_MAGIC_DEF("seq", js_lexer_get, js_lexer_set, LEXER_SEQUENCE),
    JS_CGETSET_MAGIC_DEF("byteLength", js_lexer_get, 0, LEXER_BYTE_LENGTH),
    JS_CGETSET_MAGIC_DEF("charLength", js_lexer_get, 0, LEXER_CHAR_LENGTH),
    JS_CGETSET_MAGIC_DEF("state", js_lexer_get, 0, LEXER_STATE),
    JS_CGETSET_MAGIC_DEF("states", js_lexer_get, 0, LEXER_STATES),
    JS_CGETSET_MAGIC_DEF("stateDepth", js_lexer_get, 0, LEXER_STATE_DEPTH),
    JS_CGETSET_MAGIC_DEF("stateStack", js_lexer_get, 0, LEXER_STATE_STACK),
    JS_CGETSET_MAGIC_DEF("source", js_lexer_get, 0, LEXER_SOURCE),
    JS_CGETSET_MAGIC_DEF("lexeme", js_lexer_get, 0, LEXER_LEXEME),
    JS_CGETSET_MAGIC_DEF("token", js_lexer_get, 0, LEXER_TOKEN),
    JS_CGETSET_MAGIC_DEF("fileName", js_lexer_get, js_lexer_set, LEXER_FILENAME),
    JS_CFUNC_MAGIC_DEF("setInput", 1, js_lexer_method, LEXER_SET_INPUT),
    JS_CFUNC_MAGIC_DEF("skipBytes", 0, js_lexer_method, LEXER_SKIP_BYTES),
    JS_CFUNC_MAGIC_DEF("skipToken", 0, js_lexer_method, LEXER_SKIP_TOKEN),
    JS_CFUNC_MAGIC_DEF("skipChars", 0, js_lexer_method, LEXER_SKIP_CHARS),
    JS_CFUNC_MAGIC_DEF("skipUntil", 1, js_lexer_method, LEXER_SKIP_UNTIL),
    JS_CFUNC_MAGIC_DEF("tokenClass", 1, js_lexer_method, LEXER_TOKEN_CLASS),
    JS_CFUNC_MAGIC_DEF("define", 2, js_lexer_add_rule, 0),
    JS_CFUNC_MAGIC_DEF("addRule", 2, js_lexer_add_rule, 1),
    JS_CFUNC_MAGIC_DEF("getRule", 1, js_lexer_method, LEXER_GET_RULE),
    JS_CFUNC_MAGIC_DEF("pushState", 1, js_lexer_method, LEXER_PUSH_STATE),
    JS_ALIAS_DEF("begin", "pushState"),
    JS_CFUNC_MAGIC_DEF("popState", 0, js_lexer_method, LEXER_POP_STATE),
    JS_CFUNC_MAGIC_DEF("topState", 0, js_lexer_method, LEXER_TOP_STATE),
    JS_CFUNC_MAGIC_DEF("currentLine", 0, js_lexer_method, LEXER_CURRENT_LINE),
    JS_CFUNC_MAGIC_DEF("back", 0, js_lexer_method, LEXER_BACK),
    JS_CGETSET_MAGIC_DEF("ruleNames", js_lexer_get, 0, LEXER_RULENAMES),
    JS_CGETSET_MAGIC_DEF("rules", js_lexer_get, 0, LEXER_RULES),
    JS_CFUNC_DEF("lex", 0, js_lexer_lex),
    // JS_CFUNC_DEF("inspect", 0, js_lexer_inspect),
    JS_CGETSET_DEF("tokens", js_lexer_tokens, 0),
    JS_CGETSET_DEF("states", js_lexer_states, 0),
    // JS_CGETSET_DEF("stateStack", js_lexer_statestack, 0),
    // JS_ALIAS_DEF("position", "loc"),
    JS_CFUNC_MAGIC_DEF("[Symbol.iterator]", 0, js_lexer_iterator, YIELD_ID | YIELD_DONE_VALUE),
    JS_CFUNC_MAGIC_DEF("values", 0, js_lexer_iterator, YIELD_OBJ | YIELD_DONE_VALUE),
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

  // js_set_inspect_method(ctx, lexer_proto, js_lexer_inspect);

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

  if((m = JS_NewCModule(ctx, module_name, js_lexer_init))) {
    JS_AddModuleExport(ctx, m, "Location");
    JS_AddModuleExport(ctx, m, "Token");
    JS_AddModuleExport(ctx, m, "Lexer");
  }

  return m;
}

/**
 * @}
 */
