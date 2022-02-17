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

BOOL
token_release_rt(Token* tok, JSRuntime* rt) {
  --tok->ref_count;
  if(tok->ref_count == 0) {
    location_release_rt(&tok->loc, rt);
    js_free_rt(rt, tok->lexeme);
    tok->lexeme = 0;
    return TRUE;
  }
  // printf("token_release_rt %p ref_count=%d\n", tok, tok->ref_count);
  return FALSE;
}

void
token_free_rt(Token* tok, JSRuntime* rt) {
  token_release_rt(tok, rt);
  if(tok->ref_count == 0)
    js_free_rt(rt, tok);
}

Token*
token_create(int id, Location* loc, const char* lexeme, size_t len, JSContext* ctx) {
  Token* tok;

  if((tok = token_new(ctx))) {
    tok->id = id;
    tok->loc = loc;

    tok->byte_length = len;

    tok->lexeme = js_strndup(ctx, lexeme, len);
    tok->char_length = utf8_strlen(lexeme, len);
  }

  return tok;
}
