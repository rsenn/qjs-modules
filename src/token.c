#include "lexer.h"
#include "debug.h"
#include "token.h"

Token*
token_new(JSContext* ctx) {
  Token* tok;

  if((tok = js_mallocz(ctx, sizeof(Token)))) {
    tok->ref_count = 1;
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
    location_free_rt(tok->loc, rt);
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

Token*
token_create(int id, Location* loc, const char* lexeme, size_t len, JSContext* ctx) {
  Token* tok;

  if((tok = token_new(ctx))) {
    tok->id = id;
    tok->loc = loc;

    tok->byte_length = len;

    tok->lexeme = (uint8_t*)js_strndup(ctx, lexeme, len);
    tok->char_length = utf8_strlen(lexeme, len);
  }

  return tok;
}
