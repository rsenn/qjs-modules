#include "defines.h"
#include <quickjs.h>
#include <cutils.h>
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

VISIBLE JSClassID js_token_class_id = 0, js_lexer_class_id = 0;
static JSValue token_proto, token_ctor, lexer_proto, lexer_ctor;

static int
escape_pred(int c) {
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

static int
unescape_pred(const char* s, size_t* n) {
  switch(*s) {
    case 'r': return '\r';
    case 'n': return '\n';
    case 't': return '\t';
    case 'v': return '\v';
    case 'f': return '\f';
    case '/': return '/';
  }

  return 0;
}

static JSValue
js_lexer_rule_new(JSContext* ctx, Lexer* lex, LexerRule* rule) {
  JSValue ret, states;
  DynBuf dbuf;

  js_dbuf_init(ctx, &dbuf);
  lexer_rule_dump(lex, rule, &dbuf);
  dbuf_0(&dbuf);

  if(rule->expr[0] == '<')
    assert(!strncmp(rule->expr, (const char*)dbuf.buf, str_chr(rule->expr, '>')));

  ret = JS_NewArray(ctx);
  js_set_propertyint_string(ctx, ret, 0, rule->name);
  js_set_propertyint_string(ctx, ret, 1, lexer_states_skip((char*)dbuf.buf));

  states = JS_NewArray(ctx);

  for(size_t i = 0, j = 0; i < 32; i++) {
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

  dbuf_free(&dbuf);
  return ret;
}

static inline Lexer*
token_lexer(Token* tok) {
  if(tok->opaque)
    return js_lexer_data(JS_MKPTR(JS_TAG_OBJECT, tok->opaque));

  return 0;
}

static int64_t
token_byte_pos(Token* tok) {
  Lexer* lex;

  if((tok->lexeme && (lex = token_lexer(tok)))) {
    PointerRange r = inputbuffer_range(&lex->input);

    if(range_in(r, tok->lexeme))
      return range_index(r, tok->lexeme);
  }

  return location_byteoffset(tok->loc);
}

static int64_t
token_char_pos(Token* tok) {
  Lexer* lex;

  if((tok->lexeme && (lex = token_lexer(tok))))
    return utf8_strlen(lex->data, token_byte_pos(tok));

  return location_charoffset(tok->loc);
}

JSValue
js_token_new(JSContext* ctx, JSValueConst new_target) {
  Token* tok;

  if(!(tok = token_new(ctx)))
    return JS_EXCEPTION;

  return js_token_wrap(ctx, new_target, tok);
}

JSValue
js_token_wrap(JSContext* ctx, JSValueConst new_target, Token* tok) {
  JSValue proto, obj = JS_UNDEFINED;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_token_class_id);
  JS_FreeValue(ctx, proto);

  JS_SetOpaque(obj, tok);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_token_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  Lexer* lex = 0;
  Location* loc = 0;
  Token* tok;
  int32_t id = -1;
  JSValue obj = js_token_new(ctx, new_target);

  if(JS_IsException(obj))
    goto fail;

  if(!(tok = js_token_data(obj)))
    goto fail;

  int index = 0;
  int64_t char_offset = -1;
  InputBuffer in = INPUTBUFFER();

  while(index < argc) {
    int r = 0;

    if(id == -1 && JS_IsNumber(argv[index])) {
      id = tok->id = js_toint32(ctx, argv[index]);
    } else if(!lex && (lex = js_lexer_data(argv[index]))) {
      tok->opaque = js_value_obj2(ctx, argv[index]);
    } else if(!loc && (loc = js_location_data(argv[index]))) {
      if(tok->loc) {
        location_free(tok->loc, JS_GetRuntime(ctx));
        tok->loc = 0;
      }
      tok->loc = location_dup(loc);

    } else if((r = inputbuffer_from_argv(&in, argc - index, argv + index, ctx)) > 0) {

      index += r;
      continue;

    } else if(char_offset == -1) {
      JS_ToInt64Ext(ctx, &char_offset, argv[index]);
    }

    index++;
  }

  if(tok->loc == 0 && tok->opaque) {
    tok->loc = location_new(ctx);

    if((lex = token_lexer(tok)))
      location_copy(tok->loc, &lex->loc, ctx);
  }

  if(loc == 0)
    loc = tok->loc;

  if(char_offset != -1)
    if(loc && lex) {
      loc->byte_offset = utf8_byteoffset(lex->data, lex->size, char_offset);
      loc->char_offset = char_offset;
    }

  return obj;

fail:
  if(tok)
    js_free(ctx, tok);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_token_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Token* tok;

  if(!(tok = js_token_data2(ctx, this_val)))
    return JS_EXCEPTION;

  return JS_NewStringLen(ctx, (const char*)tok->lexeme, tok->byte_length);
}

static JSValue
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
    JS_FreeCString(ctx, hint);

  return ret;
}

enum {
  TOKEN_BYTELENGTH = 0,
  TOKEN_CHARLENGTH,
  TOKEN_BYTERANGE,
  TOKEN_CHARRANGE,
  TOKEN_BYTEPOS,
  TOKEN_CHARPOS,
  TOKEN_LEXEME,
  TOKEN_LOC,
  TOKEN_ID,
  TOKEN_SEQ,
  TOKEN_RULE,
  TOKEN_TYPE,
  TOKEN_LEXER,
};

static JSValue
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
      ret = JS_NewInt64(ctx, token_char_length(tok));
      break;
    }

    case TOKEN_BYTERANGE: {
      const uint8_t* base = tok->lexeme - token_byte_pos(tok);

      PointerRange pr = range_from_block(block_new(tok->lexeme, tok->byte_length));

      ret = indexrange_toarray(range_to_indexrange(pr, base), ctx);
      break;
    }

    case TOKEN_CHARRANGE: {
      const OffsetLength ol = {token_char_pos(tok), token_char_length(tok)};
      ret = indexrange_toarray(indexrange_from_offsetlength(ol), ctx);
      break;
    }

    case TOKEN_BYTEPOS: {
      ret = JS_NewInt64(ctx, token_byte_pos(tok));
      break;
    }

    case TOKEN_CHARPOS: {
      ret = JS_NewInt64(ctx, token_char_pos(tok));
      break;
    }

    case TOKEN_LEXEME: {
      ret = tok->lexeme ? JS_NewStringLen(ctx, (const char*)tok->lexeme, tok->byte_length) : JS_NULL;
      break;
    }

    case TOKEN_LOC: {
      ret = tok->loc ? js_location_wrap(ctx, location_dup(tok->loc)) : JS_NULL;
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
      Lexer* lex;

      if((lex = token_lexer(tok))) {
        LexerRule* rule = lexer_rule_at(lex, tok->id);

        ret = rule ? js_lexer_rule_new(ctx, lex, rule) : JS_NULL;
      }

      break;
    }

    case TOKEN_TYPE: {
      Lexer* lex;

      if((lex = token_lexer(tok))) {
        LexerRule* rule = lexer_rule_at(lex, tok->id);

        ret = rule ? JS_NewString(ctx, rule->name) : JS_NULL;
      }
      break;
    }

    case TOKEN_LEXER: {
      ret = js_value_mkobj2(ctx, tok->opaque);
      break;
    }
  }

  return ret;
}

static JSValue
js_token_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Token* tok;
  JSValue ret = JS_UNDEFINED;

  if(!(tok = js_token_data(this_val)))
    return ret;

  switch(magic) {
    case TOKEN_LOC: {
      if(tok->loc)
        js_location_from2(ctx, value, tok->loc);
      else
        tok->loc = js_location_from(ctx, value);
      break;
    }
  }

  return ret;
}

static JSValue
js_token_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Token* tok;

  if(!(tok = js_token_data2(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue str = tok->lexeme ? JS_NewStringLen(ctx, (const char*)tok->lexeme, tok->byte_length) : JS_UNDEFINED;
  JSValue ret = js_global_new(ctx, "String", 1, &str);

  JS_FreeValue(ctx, str);

  js_set_tostringtag_value(ctx, ret, js_get_tostringtag_value(ctx, this_val));

  return ret;
}

static void
js_token_finalizer(JSRuntime* rt, JSValue val) {
  Token* tok;

  if((tok = js_token_data(val))) {
    if(tok->opaque) {
      js_freeobj_rt(rt, tok->opaque);
      tok->opaque = 0;
    }

    token_free(tok, rt);
  }
}

static JSClassDef js_token_class = {
    .class_name = "Token",
    .finalizer = js_token_finalizer,
};

static const JSCFunctionListEntry js_token_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("charLength", js_token_get, 0, TOKEN_CHARLENGTH),
    JS_CGETSET_MAGIC_DEF("byteLength", js_token_get, 0, TOKEN_BYTELENGTH),
    JS_CGETSET_MAGIC_DEF("charRange", js_token_get, 0, TOKEN_CHARRANGE),
    JS_CGETSET_MAGIC_DEF("byteRange", js_token_get, 0, TOKEN_BYTERANGE),
    JS_CGETSET_MAGIC_DEF("charPos", js_token_get, 0, TOKEN_CHARPOS),
    JS_CGETSET_MAGIC_DEF("bytePos", js_token_get, 0, TOKEN_BYTEPOS),
    JS_CGETSET_MAGIC_DEF("loc", js_token_get, js_token_set, TOKEN_LOC),
    JS_CGETSET_MAGIC_DEF("id", js_token_get, 0, TOKEN_ID),
    JS_CGETSET_MAGIC_FLAGS_DEF("seq", js_token_get, 0, TOKEN_SEQ, JS_PROP_CONFIGURABLE),
    JS_CGETSET_MAGIC_DEF("type", js_token_get, 0, TOKEN_TYPE),
    JS_CGETSET_MAGIC_DEF("lexer", js_token_get, 0, TOKEN_LEXER),
    JS_CGETSET_MAGIC_DEF("rule", js_token_get, 0, TOKEN_RULE),
    JS_CGETSET_MAGIC_DEF("lexeme", js_token_get, 0, TOKEN_LEXEME),
    JS_CFUNC_DEF("toString", 0, js_token_tostring),
    JS_CFUNC_DEF("[Symbol.toPrimitive]", 0, js_token_toprimitive),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Token", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_token_static_funcs[] = {
    JS_PROP_INT32_DEF("EOF", LEXER_EOF, JS_PROP_ENUMERABLE),
};

static JSValue
lexer_continue(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue data[]) {
  JS_SetPropertyUint32(ctx, data[0], 0, argc >= 1 ? JS_DupValue(ctx, argv[0]) : JS_NewBool(ctx, TRUE));

  return JS_UNDEFINED;
}

static int
lexer_to_state(Lexer* lex, JSValueConst value, JSContext* ctx) {
  int num;

  if(JS_IsNumber(value)) {
    num = js_toint32(ctx, value);
  } else {
    const char* str = JS_ToCString(ctx, value);
    num = lexer_state_find(lex, str);
  }

  if(num >= 0 && (size_t)num < lexer_num_states(lex))
    return num;

  return -1;
}

static int64_t
lexer_to_mask(Lexer* lex, JSValueConst value, JSContext* ctx) {
  size_t len;
  char* str = js_tostringlen(ctx, &len, value);
  size_t p = 0, n;
  int64_t mask = 0;

  while(p < len) {
    n = byte_chr(&str[p], len - p, ',');

    if(n < (len - p))
      str[p + n++] = '\0';

    int num = lexer_state_find(lex, &str[p]);

    if(num >= 0)
      mask |= 1ll << num;

    p += n;
  }

  js_free(ctx, str);
  return mask;
}

static BOOL
lexer_handle(Lexer* lex, JSValueConst this_val, JSValueConst handler, JSContext* ctx) {
  BOOL result = FALSE;
  JSValue ret, do_resume = JS_FALSE, data[1] = {JS_NewArray(ctx)};
  JSValueConst args[] = {
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
lexer_lex(Lexer* lex, JSValueConst this_val, int argc, JSValueConst argv[], JSContext* ctx) {
  uint64_t flags = 0;
  int id = 0;

  if(argc > 0) {
    int64_t i;

    if(!JS_IsString(argv[0]) && !JS_IsArray(ctx, argv[0]) && !JS_ToInt64Ext(ctx, &i, argv[0]))
      flags = i;
    else
      flags = lexer_to_mask(lex, argv[0], ctx);
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
          skip = lexer_handle(lex, this_val, jsrule->action, ctx);

        if(skip || jsrule->skip) {
          lexer_skip(lex);
          continue;
        }
      }

    } else if(id == LEXER_ERROR_NOMATCH) {
      JSValue handler = JS_GetPropertyStr(ctx, this_val, "handler");

      if(JS_IsFunction(ctx, handler)) {
        if(lexer_handle(lex, this_val, handler, ctx) == TRUE)
          continue;

        id = LEXER_ERROR_NOMATCH;
      }
    } else if(id == LEXER_EOF) {
    }

    break;
  }

  return id;
}

JSValue
js_lexer_new(JSContext* ctx, JSValueConst new_target) {
  Lexer* lex;
  int32_t mode = 0;
  JSValue obj = JS_UNDEFINED;

  if(!(lex = lexer_new(ctx)))
    return JS_EXCEPTION;

  return js_lexer_wrap(ctx, new_target, lex);
}

JSValue
js_lexer_wrap(JSContext* ctx, JSValueConst new_target, Lexer* lex) {
  JSValue proto, obj;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, lexer_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_lexer_class_id);
  JS_FreeValue(ctx, proto);

  JS_SetOpaque(obj, lex);
  return obj;
}

static JSValue
js_lexer_add_rule(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Lexer* lex;
  char* name;
  int index = 1;
  int64_t mask = -1, skip = 0;
  int do_skip = -1;
  RegExp expr;
  JSLexerRule* jsrule = 0;
  JSValue fn = JS_UNDEFINED;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  name = (magic || JS_IsString(argv[0])) ? js_tostring(ctx, argv[0]) : 0;
  index += regexp_from_argv(&expr, argc - index, &argv[index], ctx);

  while(index < argc) {
    if(JS_IsNumber(argv[index])) {
      JS_ToInt64(ctx, &mask, argv[index]);

    } else if(JS_IsBool(argv[index])) {
      do_skip = JS_ToBool(ctx, argv[index]);

    } else if(JS_IsFunction(ctx, argv[index])) {
      fn = JS_DupValue(ctx, argv[index]);

    } else {
      break;
    }

    index++;
  }

  JSValue skipv = JS_GetPropertyStr(ctx, this_val, "skip");

  if(JS_IsNumber(skipv))
    JS_ToInt64(ctx, &skip, skipv);

  JS_FreeValue(ctx, skipv);

  if(JS_IsFunction(ctx, fn) || (do_skip > 0 || (mask & skip))) {
    jsrule = js_malloc(ctx, sizeof(JSLexerRule));
    jsrule->action = fn;
    jsrule->skip = do_skip > 0 || !!(mask & skip);
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
  Lexer* lex;
  int64_t mask = MASK_ALL;
  JSValue ret = js_lexer_new(ctx, new_target);

  if(!(lex = JS_GetOpaque(ret, js_lexer_class_id)))
    return JS_EXCEPTION;

  if(!js_is_null_or_undefined(argv[0])) {
    InputBuffer input = js_input_chars(ctx, argv[0]);

    if(input.data)
      lex->input = input;
  }

  int i = 1;

  if(i < argc && JS_IsNumber(argv[i])) {
    uint32_t mode = 0;

    JS_ToUint32(ctx, &mode, argv[i++]);
    lex->mode |= mode;
  }

  if(i < argc && lex->loc.file == -1 && JS_IsString(argv[i]))
    lex->loc.file = JS_ValueToAtom(ctx, argv[i++]);

  if(i < argc && JS_IsNumber(argv[i]))
    JS_ToInt64(ctx, &mask, argv[i++]);

  JS_SetPropertyStr(ctx, ret, "mask", JS_NewInt64(ctx, mask));
  return ret;
}

enum {
  LEXER_SKIP_BYTES = 0,
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
  LEXER_PEEKTOKEN,
};

static JSValue
js_lexer_set_input(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Lexer *lex, *other;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  inputbuffer_free(&lex->input, ctx);
  location_release(&lex->loc, JS_GetRuntime(ctx));

  if((other = JS_GetOpaque(argv[0], js_lexer_class_id))) {
    lex->input = inputbuffer_clone(&other->input, ctx);
    location_copy(&lex->loc, &other->loc, ctx);
  } else if(argc > 1 && js_is_null_or_undefined(argv[0])) {
    const char* file = JS_ToCString(ctx, argv[1]);
    lex->input = inputbuffer_from_file(file, ctx);
    JS_FreeCString(ctx, file);
  } else {
    lex->input = js_input_chars(ctx, argv[0]);
  }

  if(argc > 1 && !js_is_null_or_undefined(argv[1]))
    location_set_file(&lex->loc, JS_ValueToAtom(ctx, argv[1]), ctx);

  return JS_UNDEFINED;
}

static JSValue
js_lexer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Lexer* lex;
  JSValue ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LEXER_SKIP_BYTES: {
      uint32_t n = argc > 0 ? js_touint32(ctx, argv[0]) : 0;

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
      for(int i = 0; i < argc; i++) {
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
          if(lex->byte_offset >= len && !memcmp(&lex->data[lex->byte_offset - len], str, len)) {
            Location diff;
            location_zero(&diff);
            location_count(&diff, (const uint8_t*)&lex->data[lex->byte_offset - len], len);
            location_sub(&lex->loc, &diff);
            ret = JS_NewInt32(ctx, lexer_peek(lex, 1 << lex->state, 0, ctx));
        } else {
            char* buf = byte_escape((const char*)&lex->data[lex->byte_offset - len], len);
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
      if(!inputbuffer_eof(&lex->input)) {
        size_t len;
        const uint8_t* buf;

        if((buf = inputbuffer_peek(&lex->input, &len)))
          ret = JS_NewStringLen(ctx, (const char*)buf, len);
      }

      break;
    }

    case LEXER_GETC: {
      if(!inputbuffer_eof(&lex->input)) {
        size_t len;
        const uint8_t* buf;

        if((buf = inputbuffer_get(&lex->input, &len)))
          ret = JS_NewStringLen(ctx, (const char*)buf, len);
      }

      break;
    }

    case LEXER_SKIP_CHARS: {
      if(!inputbuffer_eof(&lex->input)) {
        int32_t ntimes = 1;
        const uint8_t* p = 0;
        size_t n;

        if(argc > 0)
          JS_ToInt32(ctx, &ntimes, argv[0]);

        while(ntimes-- > 0)
          p = inputbuffer_get(&lex->input, &n);

        if(p)
          ret = JS_NewStringLen(ctx, (const char*)p, n);
      }

      break;
    }

    case LEXER_SKIP_UNTIL: {
      if(!inputbuffer_eof(&lex->input)) {
        JSValueConst pred = argv[0];

        if(!JS_IsFunction(ctx, pred))
          return JS_ThrowTypeError(ctx, "argument 1 is not a function");

        while(lex->byte_offset < lex->size) {
          size_t n;
          const uint8_t* p;

          if((p = inputbuffer_peek(&lex->input, &n))) {
            JSValue str = JS_NewStringLen(ctx, (const char*)p, n);
            JSValue result = JS_Call(ctx, pred, this_val, 1, &str);
            BOOL b = JS_ToBool(ctx, result);

            JS_FreeValue(ctx, result);

            if(b) {
              result = str;
              break;
            }

            JS_FreeValue(ctx, str);
          }

          inputbuffer_getc(&lex->input);
        }
      }

      break;
    }

    case LEXER_GET_RANGE: {
      size_t start, end;

      start = lex->byte_offset;
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

      if(rule)
        ret = js_lexer_rule_new(ctx, lex, rule);

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
      int id = lexer_state_push(lex, state);

      ret = JS_NewInt32(ctx, id);
      JS_FreeCString(ctx, state);
      break;
    }

    case LEXER_POP_STATE: {
      int id;

      if(lexer_state_depth(lex) > 0) {
        id = lexer_state_pop(lex);
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

    case LEXER_PEEK:
    case LEXER_PEEKTOKEN: {
      int32_t id = lexer_peek(lex, 0, ctx);

      if(magic == LEXER_PEEKTOKEN) {
        Token* tok;

        if(id == LEXER_EOF) {
          ret = JS_NULL;
        } else if(id == LEXER_EXCEPTION) {
          ret = JS_ThrowInternalError(ctx, "Lexer exception");
        } else if(id < 0) {
          ret = JS_ThrowInternalError(ctx, "Lexer error %" PRId32, id);
        } else if((tok = lexer_token(lex, id, ctx)) >= 0) {
          ret = js_token_wrap(ctx, token_ctor, tok);

          tok->opaque = js_value_obj2(ctx, this_val);
        }

      } else {
        ret = JS_NewInt32(ctx, id);
      }

      break;
    }
  }

  return ret;
}

enum {
  LEXER_SIZE = 0,
  LEXER_POSITION,
  LEXER_BYTEPOSITION,
  LEXER_ENDOFFILE,
  LEXER_FILENAME,
  LEXER_LOCATION,
  LEXER_RULENAMES,
  LEXER_RULES,
  LEXER_MODE,
  LEXER_SEQUENCE,
  LEXER_BYTELENGTH,
  LEXER_LENGTH,
  LEXER_STATE,
  LEXER_STATES,
  LEXER_STATE_DEPTH,
  LEXER_STATE_STACK,
  LEXER_INPUT,
  LEXER_LEXEME,
};

static JSValue
js_lexer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Lexer* lex;
  JSValue ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LEXER_POSITION: {
      ret = JS_NewInt64(ctx, lex->char_offset);
      break;
    }
    case LEXER_BYTEPOSITION: {
      ret = JS_NewInt64(ctx, lex->byte_offset);
      break;
    }

    case LEXER_SIZE: {
      ret = JS_NewInt64(ctx, lex->size);
      break;
    }

    case LEXER_ENDOFFILE: {
      ret = JS_NewBool(ctx, inputbuffer_eof(&lex->input));
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

      vector_foreach_t(&lex->rules, rule) {
        JS_SetPropertyUint32(ctx, ret, i, JS_NewString(ctx, rule->name));
        ++i;
      }

      break;
    }

    case LEXER_RULES: {
      LexerRule* rule;
      uint32_t i = 0;

      ret = JS_NewObject(ctx);

      vector_foreach_t(&lex->rules, rule) {
        // printf("rule #%" PRIu32 " '%s' '%s'\n", i, rule->name, rule->expr);
        JS_SetPropertyStr(ctx, ret, rule->name, JS_NewUint32(ctx, i));
        ++i;
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

    case LEXER_BYTELENGTH: {
      ret = JS_NewUint32(ctx, lex->byte_length);
      break;
    }

    case LEXER_LENGTH: {
      ret = JS_NewUint32(ctx, lexer_charlen(lex));
      break;
    }

    case LEXER_STATE: {
      ret = JS_NewInt32(ctx, lex->state);
      break;
    }

    case LEXER_STATES: {
      ret = JS_NewArray(ctx);

      for(size_t i = 0;; ++i) {
        char* name;

        if((name = lexer_state_name(lex, i)) == 0)
          break;

        JS_SetPropertyUint32(ctx, ret, i, JS_NewString(ctx, name));
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

    case LEXER_INPUT: {
      ret = block_toarraybuffer(&lex->input.block, ctx);
      break;
    }

    case LEXER_LEXEME: {
      ret = JS_NewStringLen(ctx, (const char*)lex->data + lex->byte_offset, lex->byte_length);
      break;
    }
  }

  return ret;
}

static JSValue
js_lexer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Lexer* lex;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LEXER_POSITION: {
      uint64_t newpos = lex->byte_offset;
      Token* tok;
      Location* loc;

      if(((tok = js_token_data(value)) && (loc = tok->loc)) || (loc = js_location_data(value))) {
        lex->byte_offset = loc->byte_offset;
        lex->char_offset = loc->char_offset;
      } else {
        JS_ToIndex(ctx, &newpos, value);

        size_t offset = utf8_byteoffset(lex->data, lex->size, newpos);

        if(offset > lex->size)
          return JS_ThrowRangeError(ctx, "new .byte_offset not within range 0 - %" PRIu64, lex->size);

        lex->byte_offset = offset;
        lex->char_offset = newpos;
      }

      break;
    }

    case LEXER_BYTELENGTH: {
      Token* tok;

      if((tok = js_token_data(value)))
        lex->byte_length = tok->byte_length;
      else
        JS_ToIndex(ctx, &lex->byte_length, value);

      break;
    }

    case LEXER_FILENAME: {
      if(lex->loc.file > -1)
        JS_FreeAtom(ctx, lex->loc.file);

      lex->loc.file = JS_ValueToAtom(ctx, value);
      break;
    }

    case LEXER_MODE: {
      int64_t m;

      JS_ToInt64Ext(ctx, &m, value);
      lex->mode = m;
      break;
    }

    case LEXER_STATE: {
      int32_t state = lexer_to_state(lex, value, ctx);
      uint32_t num_states = vector_size(&lex->states, sizeof(int32_t));

      if(!(state >= 0 && state < num_states))
        return JS_ThrowRangeError(ctx, "state is not in range from 0 - %" PRIu32, num_states);

      lex->state = state;
      break;
    }
  }

  return JS_UNDEFINED;
}

static JSValue
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

static JSValue
js_lexer_states(JSContext* ctx, JSValueConst this_val) {
  Lexer* lex;
  char** cond;
  uint32_t i = 0;
  JSValue ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  ret = JS_NewArray(ctx);

  vector_foreach_t(&lex->states, cond) {
    JS_SetPropertyUint32(ctx, ret, i++, JS_NewString(ctx, *cond));
  }

  return ret;
}

static JSValue
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
      ctx, (void*)stack, sizeof(int32_t) * size, (JSFreeArrayBufferDataFunc*)(void*)&orig_js_free_rt, stack, FALSE);

  ctor = js_global_get_str(ctx, "Int32Array");

  ret = JS_CallConstructor(ctx, ctor, 1, &buf);

  JS_FreeValue(ctx, ctor);
  JS_FreeValue(ctx, buf);

  return ret;
}

static JSValue
js_lexer_escape(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  InputBuffer in = js_input_chars(ctx, argv[0]);
  DynBuf out;
  js_dbuf_init(ctx, &out);

  magic ? dbuf_put_unescaped_pred(&out, (const char*)in.data, in.size, unescape_pred)
        : dbuf_put_escaped_pred(&out, (const char*)in.data, in.size, escape_pred);

  return dbuf_tostring_free(&out, ctx);
}

static JSValue
js_lexer_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  if(argc > 0) {
    if(js_is_regexp(ctx, argv[0]) || JS_IsString(argv[0])) {
      RegExp re = {0};

      regexp_from_argv(&re, argc, argv, ctx);

      ret = JS_NewString(ctx, re.source);
    } else {
      InputBuffer in = js_input_chars(ctx, argv[0]);

      ret = inputbuffer_tostring_free(&in, ctx);
    }
  }

  return ret;
}

static JSValue
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

  id = lexer_lex(lex, this_val, argc, argv, ctx);

  if(state > -1)
    lexer_state_pop(lex);

  switch(id) {
    case LEXER_ERROR_NOMATCH: {
      char* lexeme = lexer_lexeme_s(lex, ctx, escape_pred);
      char* file = location_file(&lex->loc, ctx);

      ret = JS_ThrowInternalError(
          ctx,
          "%s:%" PRIu32 ":%" PRIu32 ": No matching token (%d: %s)\n%.*s\n%*s",
          file,
          lex->loc.line + 1,
          lex->loc.column + 1,
          lexer_state_top(lex, 0),
          lexer_state_name(lex, lexer_state_top(lex, 0)),
          /*   lexeme,*/
          (int)(byte_chr((const char*)&lex->data[lex->byte_offset], lex->size - lex->byte_offset, '\n') +
                lex->loc.column),
          &lex->data[lex->byte_offset - lex->loc.column],
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

static JSValue
js_lexer_nextfn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  Lexer* lex;

  if(!(lex = js_lexer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue result = js_lexer_lex(ctx, this_val, argc, argv);

  if(JS_IsNumber(result)) {
    int32_t id;
    JS_ToInt32(ctx, &id, result);

    if(magic & YIELD_OBJ) {
      Token* tok;

      if((tok = lexer_token(lex, id, ctx))) {
        ret = js_token_wrap(ctx, token_ctor, tok);

        tok->opaque = js_value_obj2(ctx, this_val);
      }
    } else {
      ret = JS_NewInt32(ctx, id);
    }
  } else {
    ret = JS_DupValue(ctx, result);
  }

  JS_FreeValue(ctx, result);

  if(magic & YIELD_DONE_VALUE) {
    JSValue item = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, item, "value", ret);
    JS_SetPropertyStr(ctx, item, "done", JS_NewBool(ctx, js_is_null_or_undefined(ret)));
    return item;
  }

  return ret;
}

static JSValue
js_lexer_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  JSValue ret = js_lexer_nextfn(ctx, this_val, argc, argv, magic);

  *pdone = JS_IsUndefined(ret);

  return ret;
}

static JSValue
js_lexer_call(JSContext* ctx, JSValueConst func_obj, JSValueConst this_val, int argc, JSValueConst argv[], int flags) {
  Lexer* lex;

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

static JSValue
js_lexer_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue next, ret = JS_NewObject(ctx);
  JSAtom symbol = js_symbol_static_atom(ctx, "iterator");

  next = JS_NewCFunction2(ctx, (JSCFunction*)(void*)&js_lexer_nextfn, "next", 0, JS_CFUNC_generic_magic, magic);

  JS_DefinePropertyValue(
      ctx,
      ret,
      symbol,
      JS_NewCFunction2(ctx, (JSCFunction*)(void*)&JS_DupValue, "[Symbol.iterator]", 0, JS_CFUNC_generic, 0),
      JS_PROP_CONFIGURABLE);
  JS_FreeAtom(ctx, symbol);

  JS_DefinePropertyValueStr(ctx, ret, "next", js_function_bind_this(ctx, next, this_val), JS_PROP_CONFIGURABLE);
  return ret;

  return JS_DupValue(ctx, this_val);
}

static JSValue
js_lexer_fromfile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_CallConstructor(ctx, this_val, argc, argv);
  Lexer* lex;

  if((lex = js_lexer_data(ret))) {
    JSValueConst args[] = {JS_UNDEFINED, argv[0]};

    js_lexer_set_input(ctx, ret, countof(args), args);
  }

  return ret;
}

static JSValue
js_lexer_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_NewObject(ctx);

  js_set_tostringtag_value(ctx, ret, js_get_tostringtag_value(ctx, this_val));

  return ret;
}

static void
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
    JS_CFUNC_MAGIC_DEF("peekToken", 0, js_lexer_method, LEXER_PEEKTOKEN),
    JS_CFUNC_MAGIC_DEF("next", 0, js_lexer_nextfn, YIELD_ID),
    JS_CFUNC_MAGIC_DEF("nextToken", 0, js_lexer_nextfn, YIELD_OBJ),
    JS_CGETSET_MAGIC_DEF("size", js_lexer_get, 0, LEXER_SIZE),
    JS_CGETSET_MAGIC_DEF("charPos", js_lexer_get, js_lexer_set, LEXER_POSITION),
    JS_CGETSET_MAGIC_DEF("bytePos", js_lexer_get, 0, LEXER_BYTEPOSITION),
    JS_CGETSET_MAGIC_DEF("loc", js_lexer_get, 0, LEXER_LOCATION),
    JS_CGETSET_MAGIC_DEF("eof", js_lexer_get, 0, LEXER_ENDOFFILE),
    JS_CGETSET_MAGIC_DEF("mode", js_lexer_get, js_lexer_set, LEXER_MODE),
    JS_CGETSET_MAGIC_DEF("seq", js_lexer_get, 0, LEXER_SEQUENCE),
    JS_CGETSET_MAGIC_DEF("byteLength", js_lexer_get, 0, LEXER_BYTELENGTH),
    JS_CGETSET_MAGIC_DEF("charLength", js_lexer_get, 0, LEXER_LENGTH),
    JS_CGETSET_MAGIC_DEF("state", js_lexer_get, js_lexer_set, LEXER_STATE),
    JS_CGETSET_MAGIC_DEF("states", js_lexer_get, 0, LEXER_STATES),
    JS_CGETSET_MAGIC_DEF("stateDepth", js_lexer_get, 0, LEXER_STATE_DEPTH),
    JS_CGETSET_MAGIC_DEF("stateStack", js_lexer_get, 0, LEXER_STATE_STACK),
    JS_CGETSET_MAGIC_DEF("input", js_lexer_get, 0, LEXER_INPUT),
    JS_CGETSET_MAGIC_DEF("lexeme", js_lexer_get, 0, LEXER_LEXEME),
    JS_CGETSET_MAGIC_DEF("fileName", js_lexer_get, js_lexer_set, LEXER_FILENAME),
    JS_CFUNC_DEF("setInput", 1, js_lexer_set_input),
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
    JS_ALIAS_DEF("end", "popState"),
    JS_CFUNC_MAGIC_DEF("topState", 0, js_lexer_method, LEXER_TOP_STATE),
    JS_CFUNC_MAGIC_DEF("currentLine", 0, js_lexer_method, LEXER_CURRENT_LINE),
    JS_CFUNC_MAGIC_DEF("back", 0, js_lexer_method, LEXER_BACK),
    JS_CGETSET_MAGIC_DEF("ruleNames", js_lexer_get, 0, LEXER_RULENAMES),
    JS_CGETSET_MAGIC_DEF("rules", js_lexer_get, 0, LEXER_RULES),
    JS_CFUNC_DEF("lex", 0, js_lexer_lex),
    JS_CGETSET_DEF("tokens", js_lexer_tokens, 0),
    JS_CGETSET_DEF("states", js_lexer_states, 0),
    JS_CGETSET_DEF("stateStack", js_lexer_statestack, 0),
    JS_CFUNC_MAGIC_DEF("[Symbol.iterator]", 0, js_lexer_iterator, YIELD_OBJ | YIELD_DONE_VALUE),
    // JS_CFUNC_MAGIC_DEF("values", 0, js_lexer_iterator, YIELD_OBJ | YIELD_DONE_VALUE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Lexer", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_lexer_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("escape", 1, js_lexer_escape, 0),
    JS_CFUNC_MAGIC_DEF("unescape", 1, js_lexer_escape, 1),
    JS_CFUNC_DEF("toString", 1, js_lexer_tostring),
    JS_CFUNC_DEF("fromFile", 1, js_lexer_fromfile),
    JS_PROP_INT32_DEF("FIRST", LEXER_FIRST, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("LONGEST", LEXER_LONGEST, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("LAST", LEXER_LAST, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("YIELD_ID", YIELD_ID, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("YIELD_OBJ", YIELD_OBJ, JS_PROP_ENUMERABLE),
};

static int
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

  lexer_ctor = JS_NewCFunction2(ctx, js_lexer_constructor, "Lexer", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, lexer_ctor, lexer_proto);
  JS_SetPropertyFunctionList(ctx, lexer_ctor, js_lexer_static_funcs, countof(js_lexer_static_funcs));

  js_set_inspect_method(ctx, lexer_proto, js_lexer_inspect);

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
