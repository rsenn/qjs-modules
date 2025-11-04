#ifndef TOKEN_H
#define TOKEN_H

#include <quickjs.h>
#include "lexer.h"

/**
 * \defgroup token token: Token object for the lexer
 * @{
 */
typedef struct Token {
  int ref_count;
  uint8_t* lexeme;
  int32_t id;
  uint32_t byte_length /*, char_length*/;
  Location* loc;
  uint64_t seq;
  void* opaque;
} Token;

Token* token_new(JSContext*);
Token* token_create(int, void*, size_t, JSContext*);
void token_release(Token*, JSRuntime*);
void token_free(Token*, JSRuntime*);
void token_set_lexeme(Token*, void*, size_t);
void token_set_location(Token*, Location*, JSContext*);
void token_copy_location(Token*, const Location*, JSContext*);

static inline int64_t
token_char_length(Token* tok) {
  return tok->lexeme ? utf8_strlen(tok->lexeme, tok->byte_length) : -1;
}

/*static inline OffsetLength
token_char_range(Token* tok) {
  return (OffsetLength){tok->loc ? tok->loc->char_offset : -1, token_char_length(tok)};
}*/

static inline PointerRange
token_byte_range(Token* tok) {
  return (PointerRange){tok->lexeme, tok->lexeme + tok->byte_length};
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
