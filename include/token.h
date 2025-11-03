#ifndef TOKEN_H
#define TOKEN_H

#include <quickjs.h>
#include "lexer.h"

/**
 * \defgroup token token: Token object for the lexer
 * @{
 */
typedef struct Token {
  int ref_count, id;
  uint8_t* lexeme;
  uint32_t byte_length, char_length;
  Lexer* lexer;
  Location* loc;
  uint64_t seq;
} Token;

Token* token_new(JSContext*);
Token* token_create(int, const char*, size_t, JSContext*);
void token_release(Token*, JSRuntime*);
void token_free(Token*, JSRuntime*);
void token_set_lexeme(Token*, void*, size_t, JSContext*);
void token_set_location(Token*, Location*, JSContext*);
void token_copy_location(Token*, const Location*, JSContext*);

static inline OffsetLength
token_char_range(Token* tok) {
  return (OffsetLength){tok->loc ? tok->loc->char_offset : -1, tok->char_length};
}

static inline OffsetLength
token_byte_range(Token* tok) {
  return (OffsetLength){tok->loc ? tok->loc->byte_offset : -1, tok->byte_length};
}

static inline Token*
token_dup(Token* tok) {
  ++tok->ref_count;
  return tok;
}

/**
 * @}
 */

#endif /* defined(TOKEN_H) */
