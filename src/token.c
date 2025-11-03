#include "lexer.h"
#include "debug.h"
#include "token.h"

static uint64_t token_seq;

/**
 * \addtogroup token
 * @{
 */
Token*
token_new(JSContext* ctx) {
  Token* tok;

  if((tok = js_mallocz(ctx, sizeof(Token)))) {
    tok->ref_count = 1;
    tok->id = -1;
    tok->seq = ++token_seq;
  }

  return tok;
}

void
token_release(Token* tok, JSRuntime* rt) {
  js_free_rt(rt, tok->lexeme);
  tok->lexeme = 0;

  if(tok->lexer) {
    lexer_free(tok->lexer, rt);
    tok->lexer = 0;
  }

  if(tok->loc) {
    location_free(tok->loc, rt);
    tok->loc = 0;
  }
}

void
token_free(Token* tok, JSRuntime* rt) {
  if(--tok->ref_count == 0) {
    token_release(tok, rt);
    js_free_rt(rt, tok);
  }
}

void
token_set_lexeme(Token* tok, void* lexeme, size_t len, JSContext* ctx) {
  if(lexeme) {
    tok->lexeme = lexeme;
    tok->byte_length = len;
    tok->char_length = utf8_strlen(lexeme, len);
  } else {
    js_free(ctx, tok->lexeme);
    tok->lexeme = 0;
    tok->byte_length = 0;
    tok->char_length = 0;
  }
}

void
token_set_location(Token* tok, Location* loc, JSContext* ctx) {
  if(tok->loc) {
    location_free(tok->loc, JS_GetRuntime(ctx));
    tok->loc = 0;
  }

  tok->loc = loc ? location_dup(loc) : 0;
}

void
token_copy_location(Token* tok, const Location* loc, JSContext* ctx) {
  if(!tok->loc)
    tok->loc = location_clone(loc, ctx);
  else
    location_copy(tok->loc, loc, ctx);
}

Token*
token_create(int id, const char* lexeme, size_t len, JSContext* ctx) {
  Token* tok;

  if((tok = token_new(ctx))) {
    tok->id = id;

    token_set_lexeme(tok, lexeme ? js_strndup(ctx, lexeme, len) : 0, len, ctx);
  }

  return tok;
}

/**
 * @}
 */
