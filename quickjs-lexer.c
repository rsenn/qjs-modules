#define _GNU_SOURCE

#include "quickjs.h"
#include "libregexp.h"
#include "quickjs-lexer.h"
#include "vector.h"
#include "quickjs-predicate.h"
#include <string.h>
#include <ctype.h>

#define ALPHA_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"

VISIBLE JSClassID js_syntaxerror_class_id = 0, js_token_class_id = 0, js_lexer_class_id = 0;

static void lexer_free(Lexer*, JSRuntime*);

static void
location_dump(const Location* loc, DynBuf* dbuf) {
  if(loc->file) {
    dbuf_putstr(dbuf, loc->file);
    dbuf_putc(dbuf, ':');
  }
  dbuf_printf(dbuf, "%" PRId32 ":%" PRId32, loc->line, loc->column);
}

static Location
location_dup(const Location* loc, JSContext* ctx) {
  Location ret = {0, 0, 0};
  if(loc->file)
    ret.file = js_strdup(ctx, loc->file);
  ret.line = loc->line;
  ret.column = loc->column;
  return ret;
}

static void
location_free(Location* loc, JSRuntime* rt) {
  if(loc->file)
    js_free_rt(rt, (char*)loc->file);
  memset(loc, 0, sizeof(Location));
}

static void
js_position_dump(JSContext* ctx, JSValueConst this_val, DynBuf* dbuf) {
  JSValue file;
  file = JS_GetPropertyStr(ctx, this_val, "file");
  if(JS_IsString(file)) {
    js_value_print(ctx, file, dbuf);
    dbuf_putc(dbuf, ':');
    JS_FreeValue(ctx, file);
  }
  dbuf_printf(dbuf, "%" PRId32 ":%" PRId32, js_get_propertystr_int32(ctx, this_val, "line"), js_get_propertystr_int32(ctx, this_val, "column"));
}

static JSValue
js_position_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  DynBuf db;
  JSValue ret;
  js_dbuf_init(ctx, &db);
  js_position_dump(ctx, this_val, &db);

  ret = JS_NewStringLen(ctx, (const char*)db.buf, db.size);
  dbuf_free(&db);
  return ret;
}

static JSValue
js_position_new(JSContext* ctx, const Location* loc) {
  JSValue ret = JS_NewObject(ctx);
  if(loc->file)
    JS_SetPropertyStr(ctx, ret, "file", JS_NewString(ctx, loc->file));
  JS_SetPropertyStr(ctx, ret, "line", JS_NewUint32(ctx, loc->line + 1));
  JS_SetPropertyStr(ctx, ret, "column", JS_NewUint32(ctx, loc->column + 1));
  JS_SetPropertyStr(ctx, ret, "toString", JS_NewCFunction(ctx, &js_position_tostring, "toString", 0));
  return ret;
}

enum syntaxerror_getters { SYNTAXERROR_PROP_LOC = 0 };

static JSValue syntaxerror_proto, syntaxerror_constructor, syntaxerror_ctor;

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
  if(argc > 0)
    JS_SetPropertyStr(ctx, obj, "message", argv[0]);

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
      ret = js_position_new(ctx, &err->loc);
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
  location_dump(&err->loc, &db);

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
    // printf("js_syntaxerror_finalizer\n");
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_syntaxerror_class = {
    .class_name = "SyntaxError",
    .finalizer = js_syntaxerror_finalizer,
};

static const JSCFunctionListEntry js_syntaxerror_proto_funcs[] = {JS_CGETSET_ENUMERABLE_DEF("loc", js_syntaxerror_get, 0, SYNTAXERROR_PROP_LOC),
                                                                  JS_CFUNC_DEF("toString", 0, js_syntaxerror_tostring),
                                                                  JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SyntaxError", JS_PROP_CONFIGURABLE)};

static JSValue token_proto, token_constructor, token_ctor;

enum token_methods { TO_STRING = 0 };
enum token_getters { TOKEN_PROP_BYTELENGTH = 0, TOKEN_PROP_CHARLENGTH, TOKEN_PROP_OFFSET, TOKEN_PROP_LEXEME, TOKEN_PROP_LOC, TOKEN_PROP_ID, TOKEN_PROP_TYPE };

static inline int
keywords_cmp(const char** w1, const char** w2) {
  return strcmp(*w1, *w2);
}

static void
token_free(Token* tok, JSRuntime* rt) {
  if(tok->lexer) {
    --tok->lexer->ref_count;
    tok->lexer = 0;
  }

  location_free(&tok->loc, rt);
  // js_free_rt(rt, tok);
}

static const char*
token_type(const Token* tok) {
  return ((const char* const[]){"COMMENT",
                                "STRING_LITERAL",
                                "TEMPLATE_LITERAL",
                                "NUMERIC_LITERAL",
                                "BOOLEAN_LITERAL",
                                "NULL_LITERAL",
                                "PUNCTUATOR",
                                "KEYWORD",
                                "IDENTIFIER",
                                "REGEXP_LITERAL",
                                0})[tok->id];
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
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, tok);

  return obj;
fail:
  js_free(ctx, tok);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_token_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Token* tok;

  if(!(tok = js_token_data(ctx, this_val)))
    return JS_EXCEPTION;

  return JS_NewStringLen(ctx, (const char*)&tok->data[tok->offset], tok->byte_length);
}

static JSValue
js_token_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Token* tok;
  DynBuf dbuf;
  JSValue ret;

  if(!(tok = js_token_data(ctx, this_val)))
    return JS_EXCEPTION;

  js_dbuf_init(ctx, &dbuf);

  dbuf_putstr(&dbuf, "Token { loc: ");
  location_dump(&tok->loc, &dbuf);

  dbuf_printf(&dbuf, ", offset: %3" PRIu32 ", byte_length: %3" PRIu32 ", type: Token.%-15s, lexeme: '", tok->offset, tok->byte_length, token_type(tok));

  dbuf_put_escaped(&dbuf, (const char*)&tok->data[tok->offset], tok->byte_length);
  dbuf_putstr(&dbuf, "' }");

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
      ret = JS_NewUint32(ctx, tok->char_length);
      break;
    }

    case TOKEN_PROP_OFFSET: {
      ret = JS_NewInt64(ctx, tok->offset);
      break;
    }

    case TOKEN_PROP_LEXEME: {
      ret = JS_NewStringLen(ctx, (const char*)&tok->data[tok->offset], tok->byte_length);
      break;
    }

    case TOKEN_PROP_LOC: {
      ret = js_position_new(ctx, &tok->loc);
      break;
    }

    case TOKEN_PROP_ID: {
      ret = JS_NewInt32(ctx, tok->id);
      break;
    }

    case TOKEN_PROP_TYPE: {
      ret = JS_NewString(ctx, token_type(tok));
      break;
    }
  }
  return ret;
}

static JSValue
js_token_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Token* tok;

  if(!(tok = js_token_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {}
  return JS_UNDEFINED;
}

static JSValue
js_token_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  return JS_UNDEFINED;
}

static void
js_token_finalizer(JSRuntime* rt, JSValue val) {
  Token* tok = JS_GetOpaque(val, js_token_class_id);
  if(tok) {

    // printf("js_token_finalizer\n");
    token_free(tok, rt);
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_token_class = {
    .class_name = "Token",
    .finalizer = js_token_finalizer,
};

static const JSCFunctionListEntry js_token_proto_funcs[] = {JS_CGETSET_MAGIC_DEF("byteLength", js_token_get, NULL, TOKEN_PROP_BYTELENGTH),
                                                            JS_CGETSET_MAGIC_DEF("charLength", js_token_get, NULL, TOKEN_PROP_CHARLENGTH),
                                                            JS_CGETSET_MAGIC_DEF("offset", js_token_get, NULL, TOKEN_PROP_OFFSET),
                                                            JS_CGETSET_MAGIC_DEF("loc", js_token_get, NULL, TOKEN_PROP_LOC),
                                                            JS_CGETSET_MAGIC_DEF("id", js_token_get, NULL, TOKEN_PROP_ID),
                                                            JS_CGETSET_MAGIC_DEF("type", js_token_get, NULL, TOKEN_PROP_TYPE),
                                                            JS_CGETSET_MAGIC_DEF("lexeme", js_token_get, NULL, TOKEN_PROP_LEXEME),
                                                            JS_CFUNC_DEF("toString", 0, js_token_tostring),
                                                            JS_CFUNC_DEF("inspect", 0, js_token_inspect),
                                                            JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Token", JS_PROP_CONFIGURABLE)};

static const JSCFunctionListEntry js_token_static_funcs[] = {JS_PROP_INT32_DEF("COMMENT", TOKEN_ID_COMMENT, JS_PROP_ENUMERABLE),
                                                             JS_PROP_INT32_DEF("STRING_LITERAL", TOKEN_ID_STRING_LITERAL, JS_PROP_ENUMERABLE),
                                                             JS_PROP_INT32_DEF("TEMPLATE_LITERAL", TOKEN_ID_TEMPLATE_LITERAL, JS_PROP_ENUMERABLE),
                                                             JS_PROP_INT32_DEF("NUMERIC_LITERAL", TOKEN_ID_NUMERIC_LITERAL, JS_PROP_ENUMERABLE),
                                                             JS_PROP_INT32_DEF("BOOLEAN_LITERAL", TOKEN_ID_BOOLEAN_LITERAL, JS_PROP_ENUMERABLE),
                                                             JS_PROP_INT32_DEF("NULL_LITERAL", TOKEN_ID_NULL_LITERAL, JS_PROP_ENUMERABLE),
                                                             JS_PROP_INT32_DEF("PUNCTUATOR", TOKEN_ID_PUNCTUATOR, JS_PROP_ENUMERABLE),
                                                             JS_PROP_INT32_DEF("KEYWORD", TOKEN_ID_KEYWORD, JS_PROP_ENUMERABLE),
                                                             JS_PROP_INT32_DEF("IDENTIFIER", TOKEN_ID_IDENTIFIER, JS_PROP_ENUMERABLE),
                                                             JS_PROP_INT32_DEF("REGEXP_LITERAL", TOKEN_ID_REGEXP_LITERAL, JS_PROP_ENUMERABLE),
                                                             JS_PROP_INT32_DEF("EOF", EOF, JS_PROP_ENUMERABLE)};

static JSValue lexer_proto, lexer_constructor, lexer_ctor;

static const char punctuator_chars[] = "=.-%}>,*[<!/]~&(;?|):+^{@";

enum lexer_methods {
  LEXER_METHOD_PEEKC = 0,
  LEXER_METHOD_GETC,
  LEXER_METHOD_SKIPC,
  LEXER_METHOD_IGNORE,
  LEXER_METHOD_GET_RANGE,
  LEXER_METHOD_CURRENT_LINE,
  LEXER_METHOD_ACCEPT,
  LEXER_METHOD_ACCEPT_RUN,
  LEXER_METHOD_BACKUP,
  LEXER_METHOD_MATCH,
  LEXER_METHOD_SKIPUNTIL,
  LEXER_METHOD_ADD_TOKEN,
  LEXER_METHOD_ERROR
};

enum lexer_getters {
  LEXER_PROP_SIZE = 0,
  LEXER_PROP_POS,
  LEXER_PROP_START,
  LEXER_PROP_EOF,
  //  LEXER_PROP_CURRENT_LINE,
  LEXER_PROP_STATEFN,
  LEXER_PROP_FILENAME,
  LEXER_PROP_LOC
};

enum lexer_ctype {
  LEXER_CTYPE_ALPHA = 0,
  LEXER_CTYPE_DIGIT,
  LEXER_CTYPE_XDIGIT,
  LEXER_CTYPE_IDENTIFIER,
  LEXER_CTYPE_IDENTIFIER_FIRST,
  LEXER_CTYPE_NEWLINE,
  LEXER_CTYPE_ODIGIT,
  LEXER_CTYPE_PUNCT,
  LEXER_CTYPE_PUNCT_CHAR,
  LEXER_CTYPE_QUOTE,
  LEXER_CTYPE_REGEXP,
  LEXER_CTYPE_SPACE
};

static void
lexer_init(Lexer* lex, JSContext* ctx) {
  memset(lex, 0, sizeof(Lexer));
  init_list_head(&lex->tokens);
  vector_init(&lex->charlengths, ctx);
  lex->ref_count = 1;
  lex->state_fn = JS_UNDEFINED;
}

static void
lexer_free(Lexer* lex, JSRuntime* rt) {
  struct list_head *el, *el1;

  list_for_each_safe(el, el1, &lex->tokens) {
    Token* tok = list_entry(el, Token, link);
    token_free(tok, rt);
  }

  memset(&lex->tokens, 0, sizeof(struct list_head));
  vector_free(&lex->charlengths);
  JS_FreeValueRT(rt, lex->state_fn);
  lex->state_fn = JS_UNDEFINED;
  location_free(&lex->loc, rt);

  if(--lex->ref_count == 0)
    js_free_rt(rt, lex);
}

static Token*
lexer_token(Lexer* lex, JSContext* ctx, int id) {
  Token* tok;
  if((tok = js_mallocz(ctx, sizeof(Token)))) {
    tok->id = id;
    tok->ref_count = 1;

    if((tok->lexer = lex)) {
      tok->data = lex->data;
      tok->offset = lex->start;
      tok->byte_length = lex->pos - lex->start;
      tok->char_length = vector_size(&lex->charlengths, sizeof(size_t));
      tok->loc = location_dup(&lex->loc, ctx);

      lex->ref_count++;
    }
  }
  return tok;
}

static size_t
lexer_distance(Lexer* lex) {
  size_t* p;
  size_t n = 0;
  vector_foreach_t(&lex->charlengths, p) { n += *p; }
  return n;
}

static const uint8_t*
lexer_peek(Lexer* lex, size_t* lenp) {
  return js_input_buffer_peek(&lex->input, lenp);
}

static uint32_t
lexer_peekc(Lexer* lex, size_t* lenp) {
  return js_input_buffer_peekc(&lex->input, lenp);
}

static size_t
lexer_remain(Lexer* lex) {
  return js_input_buffer_remain(&lex->input);
}

static size_t
lexer_eof(Lexer* lex) {
  return js_input_buffer_eof(&lex->input);
}

static size_t
lexer_ignore(Lexer* lex) {
  const uint8_t *p, *end, *next;
  size_t n;
  p = &lex->data[lex->start];
  end = &lex->data[lex->pos];
  while(p < end) {
    uint32_t c = unicode_from_utf8(p, end - p, &next);
    if(c == '\n') {
      lex->loc.line++;
      lex->loc.column = 0;
    } else {
      lex->loc.column++;
    }
    p = next;
  }
  n = p - lex->data - lex->start;
  assert(lex->start + lexer_distance(lex) == lex->pos);
  vector_clear(&lex->charlengths);
  lex->start += n;
  assert(lex->start == lex->pos);
  return n;
}

static uint32_t
lexer_getc(Lexer* lex, size_t* lenp) {
  uint32_t ret;
  size_t n;
  if(lenp == 0)
    lenp = &n;
  ret = js_input_buffer_getc(&lex->input, lenp);
  vector_put(&lex->charlengths, lenp, sizeof(size_t));
  return ret;
}

static const uint8_t*
lexer_get(Lexer* lex, size_t* lenp) {
  const uint8_t* ret;
  size_t n;
  if(lenp == 0)
    lenp = &n;
  ret = js_input_buffer_get(&lex->input, lenp);
  vector_put(&lex->charlengths, lenp, sizeof(size_t));
  return ret;
}

static void
lexer_skip_until(Lexer* lex, JSContext* ctx, Predicate* pred) {
  while(!lexer_eof(lex)) {
    size_t len;
    const uint8_t* p = lexer_peek(lex, &len);
    JSValue str = JS_NewStringLen(ctx, (const char*)p, len);
    if(predicate_eval(pred, ctx, 1, &str) > 0)
      break;
    lexer_get(lex, 0);
  }
}

static size_t
lexer_skip(Lexer* lex, int ntimes) {
  size_t r = 0;
  if(!lexer_eof(lex)) {
    size_t n;
    while(ntimes-- > 0) {
      lexer_get(lex, &n);
      r += n;
    }
  }
  return r;
}

static size_t
lexer_accept(Lexer* lex, JSContext* ctx, JSValueConst pred) {
  size_t started = lex->pos;
  if(!lexer_eof(lex)) {
    JSValue ch;
    size_t len;
    const uint8_t* p = lexer_peek(lex, &len);
    ch = JS_NewStringLen(ctx, (const char*)p, len);
    if(predicate_call(ctx, pred, 1, &ch) == 1)
      lexer_get(lex, 0);
    JS_FreeValue(ctx, ch);
  }
  return lex->pos - started;
}

JSValue
js_lexer_new(JSContext* ctx, JSValueConst proto, JSValueConst value) {
  Lexer *lex, *ptr2;
  JSValue obj = JS_UNDEFINED;
  if(!(lex = js_mallocz(ctx, sizeof(Lexer))))
    return JS_EXCEPTION;

  lexer_init(lex, ctx);

  obj = JS_NewObjectProtoClass(ctx, proto, js_lexer_class_id);

  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, lex);

  lex->input = js_input_buffer(ctx, value);
  /* lex->state_fn = JS_UNDEFINED;
   lex->ref_count++;*/

  return obj;
fail:
  js_free(ctx, lex);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_lexer_wrap(JSContext* ctx, Lexer* lex) {
  JSValue obj;
  Lexer* ret;

  lex->ref_count++;

  obj = JS_NewObjectProtoClass(ctx, lexer_proto, js_lexer_class_id);
  JS_SetOpaque(obj, lex);
  return obj;
}

static JSValue
js_lexer_inspect(JSContext* ctx, JSValueConst this_val, BOOL color) {
  Lexer* lex;
  DynBuf dbuf;
  JSValue ret;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  js_dbuf_init(ctx, &dbuf);

  dbuf_printf(&dbuf, "Lexer { start: %zu, pos: %zu, size: %zu }", lex->start, lex->pos, lex->size);

  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
  return ret;
}

static JSValue
js_lexer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  JSValue proto, ret;
  Lexer* lex;
  /* using new_target to get the prototype is necessary when the
      class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = lexer_proto; // JS_DupValue(ctx, lexer_proto);

  ret = js_lexer_new(ctx, proto, argc > 0 ? argv[0] : JS_UNDEFINED);

  if((lex = js_lexer_data(ctx, ret))) {
    if(argc > 1) {
      lex->loc.file = js_tostring(ctx, argv[1]);
    }
  }
  return ret;
}

static JSValue
js_lexer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  Lexer* lex;
  JSValue ret = JS_UNDEFINED;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LEXER_METHOD_PEEKC: {
      if(!lexer_eof(lex)) {
        size_t len;
        const uint8_t* buf = lexer_peek(lex, &len);
        ret = JS_NewStringLen(ctx, (const char*)buf, len);
      }
      break;
    }

    case LEXER_METHOD_GETC: {
      if(!lexer_eof(lex)) {
        size_t len;
        const uint8_t* buf = lexer_get(lex, &len);
        ret = JS_NewStringLen(ctx, (const char*)buf, len);
      }
      break;
    }

    case LEXER_METHOD_SKIPC: {
      if(!lexer_eof(lex)) {
        int32_t ntimes = 1;
        const uint8_t* p;
        size_t n;
        if(argc > 0)
          JS_ToInt32(ctx, &ntimes, argv[0]);
        while(ntimes-- > 0) { p = lexer_get(lex, &n); }
        ret = JS_NewStringLen(ctx, (const char*)p, n);
      }
      break;
    }

    case LEXER_METHOD_MATCH: {
      uint32_t pos = lex->pos;
      const uint8_t* ptr = &lex->data[lex->pos];

      if(!lexer_eof(lex)) {
        Predicate* pred = js_predicate_data(ctx, argv[0]);

        if(pred && pred->id == PREDICATE_REGEXP) {
          int capture_count = 0;
          uint8_t* capture[CAPTURE_COUNT_MAX * 2];

          pred = predicate_dup(pred, ctx);

          if(pred->regexp.expr.source[0] != '^') {
            str0_insert(&pred->regexp.expr.source, ctx, 0, "^", 1);
            pred->regexp.expr.len += 1;
          }

          capture_count = predicate_regexp_compile(pred, ctx);

          if(lre_exec(capture, pred->regexp.bytecode, ptr, 0, lex->size - lex->pos, 0, ctx)) {
            size_t start, end;
            start = capture[0] - ptr;
            end = capture[1] - ptr;

            // printf("capture[0] %u - %u\n", start, end);

            if(start == 0 && end > 0)
              lexer_skip(lex, end);
          }

          predicate_free(pred, ctx);
        } else if(JS_IsString(argv[0])) {
          size_t len;
          const char* str = JS_ToCStringLen(ctx, &len, argv[0]);

          if(lexer_remain(lex) >= len && !memcmp(ptr, str, len))
            lexer_skip(lex, len);
        }
      }

      ret = JS_NewUint32(ctx, lex->pos - pos);
      break;
    }

    case LEXER_METHOD_SKIPUNTIL: {
      if(!lexer_eof(lex)) {
        JSValueConst pred;
        if(!JS_IsFunction(ctx, argv[0]))
          return JS_ThrowTypeError(ctx, "argument 1 is not a function");
        pred = argv[0];
        while(lex->pos < lex->size) {
          size_t n;
          const uint8_t* p = lexer_peek(lex, &n);
          JSValue str = JS_NewStringLen(ctx, (const char*)p, n);
          JSValue ret = JS_Call(ctx, pred, this_val, 1, &str);
          BOOL b = JS_ToBool(ctx, ret);
          JS_FreeValue(ctx, ret);
          if(b) {
            ret = str;
            break;
          }
          JS_FreeValue(ctx, str);
          lexer_get(lex, 0);
        }
      }
      break;
    }

    case LEXER_METHOD_IGNORE: {
      lexer_ignore(lex);
      break;
    }

    case LEXER_METHOD_GET_RANGE: {
      size_t start, end;
      start = lex->start;
      end = lex->pos;
      if(argc > 0) {
        js_value_to_size(ctx, &start, argv[0]);
        if(argc > 1)
          js_value_to_size(ctx, &end, argv[1]);
      }
      ret = JS_NewStringLen(ctx, (const char*)&lex->data[start], end - start);
      break;
    }

    case LEXER_METHOD_CURRENT_LINE: {
      size_t start, end;
      start = lex->start;
      end = lex->pos;
      while(start > 0 && lex->data[start - 1] != '\n') start--;
      while(end < lex->size && lex->data[end] != '\n') end++;
      ret = JS_NewStringLen(ctx, (const char*)&lex->data[start], end - start);
      break;
    }

    case LEXER_METHOD_ACCEPT: {
      ret = JS_NewUint32(ctx, lexer_accept(lex, ctx, argv[0]));
      break;
    }

    case LEXER_METHOD_ACCEPT_RUN: {
      size_t started = lex->pos;
      if(!lexer_eof(lex)) {
        JSValueConst pred = argv[0];
        while(lexer_remain(lex)) {
          size_t len;
          const uint8_t* p = lexer_peek(lex, &len);
          JSValue ch = JS_NewStringLen(ctx, (const char*)p, len);
          BOOL b = predicate_call(ctx, pred, 1, &ch);
          // printf("acceptRun %.*s = %i\n", len, p, b);
          JS_FreeValue(ctx, ch);
          if(!b)
            break;
          lexer_get(lex, 0);
        }
      }
      ret = JS_NewUint32(ctx, lex->pos - started);
      break;
    }

    case LEXER_METHOD_BACKUP: {
      size_t started = lex->pos;
      int32_t ntimes = 1;
      if(argc > 0)
        JS_ToInt32(ctx, &ntimes, argv[0]);
      while(ntimes-- > 0 && lex->pos > 0 && vector_size(&lex->charlengths, sizeof(size_t))) {
        size_t n = *(size_t*)vector_back(&lex->charlengths, sizeof(size_t));
        vector_pop(&lex->charlengths, sizeof(size_t));
        lex->pos -= n;
      }
      ret = JS_NewStringLen(ctx, (const char*)&lex->data[lex->pos], started - lex->pos);
      break;
    }

    case LEXER_METHOD_ADD_TOKEN: {
      int32_t tokId;
      Token* tok;
      JS_ToInt32(ctx, &tokId, argv[0]);

      if(tokId > TOKEN_ID_EOF || tokId < TOKEN_ID_COMMENT) {
        JS_ThrowRangeError(ctx, "Token id not between TOKEN_ID_COMMENT (%d) and TOKEN_ID_EOF (%d)", TOKEN_ID_COMMENT, TOKEN_ID_EOF);
        ret = JS_EXCEPTION;
      } else if((tok = lexer_token(lex, ctx, tokId))) {
        list_add_tail(&tok->link, &lex->tokens);
        lexer_ignore(lex);
        // printf("lexer addToken %s «%.*s»\n", token_type(tok), lex->pos - lex->start, &lex->data[lex->start]);
      }
      break;
    }

    case LEXER_METHOD_ERROR: {
      SyntaxError error;

      error.message = js_tostring(ctx, argv[0]);
      error.offset = lex->start;
      error.byte_length = lex->pos - lex->start;
      error.loc = location_dup(&lex->loc, ctx);
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
      ret = JS_NewInt64(ctx, lex->pos);
      break;
    }

    case LEXER_PROP_SIZE: {
      ret = JS_NewInt64(ctx, lex->size);
      break;
    }

    case LEXER_PROP_START: {
      ret = JS_NewInt64(ctx, lex->start);
      break;
    }

    case LEXER_PROP_EOF: {
      ret = JS_NewBool(ctx, lex->pos >= lex->size);
      break;
    }

    case LEXER_PROP_STATEFN: {
      ret = JS_DupValue(ctx, lex->state_fn);
      break;
    }

    case LEXER_PROP_FILENAME: {
      ret = lex->loc.file ? JS_NewString(ctx, lex->loc.file) : JS_UNDEFINED;
      break;
    }

    case LEXER_PROP_LOC: {
      ret = js_position_new(ctx, &lex->loc);
      break;
    }
  }
  return ret;
}

static JSValue
js_lexer_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  return JS_DupValue(ctx, this_val);
}

static JSValue
js_lexer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Lexer* lex;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case LEXER_PROP_POS: {
      js_value_to_size(ctx, &lex->pos, value);
      break;
    }

    case LEXER_PROP_SIZE: {
      js_value_to_size(ctx, &lex->size, value);
      break;
    }

    case LEXER_PROP_START: {
      js_value_to_size(ctx, &lex->start, value);
      break;
    }

    case LEXER_PROP_FILENAME: {
      if(lex->loc.file)
        js_free(ctx, (char*)lex->loc.file);
      lex->loc.file = js_tostring(ctx, value);
      break;
    }

    case LEXER_PROP_STATEFN: {
      if(JS_IsFunction(ctx, value)) {
        if(!JS_IsUndefined(lex->state_fn))
          JS_FreeValue(ctx, lex->state_fn);
        lex->state_fn = JS_DupValue(ctx, value);
      }
      break;
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_lexer_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, BOOL* pdone, int magic) {
  JSValue ret = JS_UNDEFINED;
  Lexer* lex;
  size_t pos;

  if(!(lex = js_lexer_data(ctx, this_val)))
    return JS_EXCEPTION;

  *pdone = FALSE;

  while(list_empty(&lex->tokens) && lex->pos < lex->size) {
    pos = lex->pos;
    for(;;) {
      ret = JS_Call(ctx, lex->state_fn, this_val, 0, 0);
      //
      if(JS_IsException(ret) /*JS_IsObject(except)*/) {
        JSValue except = JS_GetException(ctx);
        printf("except = %s\n", JS_ToCString(ctx, except));
        JS_SetPropertyStr(ctx, this_val, "exception", JS_DupValue(ctx, except));
        *pdone = FALSE;
        return JS_NULL;
      }

      if(JS_IsFunction(ctx, ret)) {
        if(!JS_IsUndefined(lex->state_fn))
          JS_FreeValue(ctx, lex->state_fn);
        lex->state_fn = JS_DupValue(ctx, ret);

        /*{
          const char* name = js_function_name(ctx, lex->state_fn);
          printf("lexer state_fn='%s' @=<%.*s>\n", name ? name : "<anonymous>", 10, &lex->data[lex->start]);
          if(name)
            JS_FreeCString(ctx, name);
        }*/
        if(pos == lex->pos)
          continue;
      }
      break;
    }
    if(pos == lex->pos) {
      *pdone = TRUE;
      return JS_ThrowRangeError(ctx, "Lexer pos before=%zu after=%zu", pos, lex->pos);
    }
  }

  if(!list_empty(&lex->tokens)) {
    Token* tok = list_entry(lex->tokens.next, Token, link);
    list_del(&tok->link);
    return js_token_wrap(ctx, tok);
  }

  *pdone = TRUE;
  return ret;
}

static JSValue
js_lexer_ctype(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  JSValue b;
  int c;
  int32_t result;

  if(JS_IsString(argv[0])) {
    const char* str;
    str = JS_ToCString(ctx, argv[0]);
    c = *str;
    JS_FreeCString(ctx, str);
  } else {
    int32_t num = 0;
    JS_ToInt32(ctx, &num, argv[0]);
    c = num;
  }

  switch(magic) {
    case LEXER_CTYPE_ALPHA: {
      result = str_contains(ALPHA_CHARS, c);
      break;
    }

    case LEXER_CTYPE_SPACE: {
      result = (c == 9 || c == 0xb || c == 0xc || c == ' ');
      break;
    }

    case LEXER_CTYPE_DIGIT: {
      result = str_contains("0123456789", c);
      break;
    }

    case LEXER_CTYPE_XDIGIT: {
      result = str_contains("0123456789ABCDEFabcdef", c);
      break;
    }

    case LEXER_CTYPE_ODIGIT: {
      result = c >= '0' && c <= '7';
      break;
    }

    case LEXER_CTYPE_NEWLINE: {
      result = c == '\r' || c == '\n';
      break;
    }

    case LEXER_CTYPE_QUOTE: {
      result = c == '"' || c == '\'' || c == '`';
      break;
    }

    case LEXER_CTYPE_REGEXP: {
      result = c == '/';
      break;
    }

    case LEXER_CTYPE_PUNCT_CHAR: {
      result = (str_contains("=.-%}>,*[<!/]~&(;?|):+^{@", c));
      break;
    }

    case LEXER_CTYPE_IDENTIFIER: {
      result = (str_contains(ALPHA_CHARS "0123456789$_", c));
      break;
    }

    case LEXER_CTYPE_IDENTIFIER_FIRST: {
      result = (str_contains(ALPHA_CHARS "$_", c));
      break;
    }
    default: {
      result = -1;
      JS_ThrowRangeError(ctx, "js_lexer_ctype invalid magic: %d", magic);
      break;
    }
  }
  b = JS_NewInt32(ctx, result);
  return b;
}

static void
js_lexer_finalizer(JSRuntime* rt, JSValue val) {
  Lexer* lex;

  if((lex = JS_GetOpaque(val, js_lexer_class_id))) {
    // printf("lex->ref_count=%zu\n", lex->ref_count);
    if(--lex->ref_count <= 0) {
      printf("js_lexer_finalizer\n");
      lexer_free(lex, rt);
    }
  }
  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_lexer_class = {
    .class_name = "Lexer",
    .finalizer = js_lexer_finalizer,
};

static const JSCFunctionListEntry js_lexer_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_lexer_next, 0),
    JS_CFUNC_MAGIC_DEF("getc", 0, js_lexer_method, LEXER_METHOD_GETC),
    JS_CFUNC_MAGIC_DEF("peek", 0, js_lexer_method, LEXER_METHOD_PEEKC),
    JS_CFUNC_MAGIC_DEF("skip", 0, js_lexer_method, LEXER_METHOD_SKIPC),
    JS_CFUNC_MAGIC_DEF("match", 1, js_lexer_method, LEXER_METHOD_MATCH),
    JS_CFUNC_MAGIC_DEF("skipUntil", 0, js_lexer_method, LEXER_METHOD_SKIPUNTIL),
    JS_CFUNC_MAGIC_DEF("ignore", 0, js_lexer_method, LEXER_METHOD_IGNORE),
    JS_CFUNC_MAGIC_DEF("getRange", 0, js_lexer_method, LEXER_METHOD_GET_RANGE),
    JS_CFUNC_MAGIC_DEF("currentLine", 0, js_lexer_method, LEXER_METHOD_CURRENT_LINE),
    JS_CFUNC_MAGIC_DEF("accept", 1, js_lexer_method, LEXER_METHOD_ACCEPT),
    JS_CFUNC_MAGIC_DEF("acceptRun", 1, js_lexer_method, LEXER_METHOD_ACCEPT_RUN),
    JS_CFUNC_MAGIC_DEF("backup", 0, js_lexer_method, LEXER_METHOD_BACKUP),
    JS_CFUNC_MAGIC_DEF("addToken", 0, js_lexer_method, LEXER_METHOD_ADD_TOKEN),
    JS_CFUNC_MAGIC_DEF("error", 1, js_lexer_method, LEXER_METHOD_ERROR),
    JS_CGETSET_MAGIC_DEF("size", js_lexer_get, js_lexer_set, LEXER_PROP_SIZE),
    JS_CGETSET_MAGIC_DEF("pos", js_lexer_get, js_lexer_set, LEXER_PROP_POS),
    JS_CGETSET_MAGIC_DEF("start", js_lexer_get, js_lexer_set, LEXER_PROP_START),
    JS_CGETSET_MAGIC_DEF("loc", js_lexer_get, 0, LEXER_PROP_LOC), // JS_CGETSET_MAGIC_DEF("columnIndex", js_lexer_get,
                                                                  // 0, LEXER_PROP_COLUMN_INDEX),
    // JS_CGETSET_MAGIC_DEF("currentLine", js_lexer_get, 0, LEXER_PROP_CURRENT_LINE),
    JS_CGETSET_MAGIC_DEF("eof", js_lexer_get, 0, LEXER_PROP_EOF),
    JS_CGETSET_MAGIC_DEF("stateFn", js_lexer_get, js_lexer_set, LEXER_PROP_STATEFN),
    JS_CGETSET_MAGIC_DEF("filename", js_lexer_get, js_lexer_set, LEXER_PROP_FILENAME),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_lexer_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Lexer", JS_PROP_C_W_E)};

static const JSCFunctionListEntry js_lexer_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("isAlphaChar", 1, js_lexer_ctype, LEXER_CTYPE_ALPHA),
    JS_CFUNC_MAGIC_DEF("isDecimalDigit", 1, js_lexer_ctype, LEXER_CTYPE_DIGIT),
    JS_CFUNC_MAGIC_DEF("isHexDigit", 1, js_lexer_ctype, LEXER_CTYPE_XDIGIT),
    JS_CFUNC_MAGIC_DEF("isIdentifierChar", 1, js_lexer_ctype, LEXER_CTYPE_IDENTIFIER),
    JS_CFUNC_MAGIC_DEF("isIdentifierFirstChar", 1, js_lexer_ctype, LEXER_CTYPE_IDENTIFIER_FIRST),
    JS_CFUNC_MAGIC_DEF("isLineTerminator", 1, js_lexer_ctype, LEXER_CTYPE_NEWLINE),
    JS_CFUNC_MAGIC_DEF("isOctalDigit", 1, js_lexer_ctype, LEXER_CTYPE_ODIGIT),
    JS_CFUNC_MAGIC_DEF("isPunctuator", 1, js_lexer_ctype, LEXER_CTYPE_PUNCT),
    JS_CFUNC_MAGIC_DEF("isPunctuatorChar", 1, js_lexer_ctype, LEXER_CTYPE_PUNCT_CHAR),
    JS_CFUNC_MAGIC_DEF("isQuoteChar", 1, js_lexer_ctype, LEXER_CTYPE_QUOTE),
    JS_CFUNC_MAGIC_DEF("isRegExpChar", 1, js_lexer_ctype, LEXER_CTYPE_REGEXP),
    JS_CFUNC_MAGIC_DEF("isWhitespace", 1, js_lexer_ctype, LEXER_CTYPE_SPACE),
};

static int
js_lexer_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_syntaxerror_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_syntaxerror_class_id, &js_syntaxerror_class);

  syntaxerror_proto = JS_NewError(ctx);
  JS_SetPropertyFunctionList(ctx, syntaxerror_proto, js_syntaxerror_proto_funcs, countof(js_syntaxerror_proto_funcs));
  JS_SetClassProto(ctx, js_syntaxerror_class_id, syntaxerror_proto);

  syntaxerror_ctor = JS_NewCFunction2(ctx, js_syntaxerror_constructor, "SyntaxError", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, syntaxerror_ctor, syntaxerror_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "SyntaxError", syntaxerror_ctor);

  JS_NewClassID(&js_token_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_token_class_id, &js_token_class);

  token_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, token_proto, js_token_proto_funcs, countof(js_token_proto_funcs));
  JS_SetClassProto(ctx, js_token_class_id, token_proto);

  token_ctor = JS_NewCFunction2(ctx, js_token_constructor, "Token", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, token_ctor, token_proto);
  JS_SetPropertyFunctionList(ctx, token_ctor, js_token_static_funcs, countof(js_token_static_funcs));

  if(m) {
    JS_SetModuleExport(ctx, m, "Token", token_ctor);
  }
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

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, &js_lexer_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "SyntaxError");
  JS_AddModuleExport(ctx, m, "Token");
  JS_AddModuleExport(ctx, m, "Lexer");
  return m;
}
