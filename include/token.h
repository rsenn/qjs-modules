#ifndef TOKEN_H
#define TOKEN_H

#include <quickjs.h>
#include "lexer.h"

/**
 * \defgroup token
 * @{
 */
typedef struct {
  int ref_count, id;
  uint8_t* lexeme;
  uint32_t byte_length, char_length;
  Lexer* lexer;
  Location* loc;
  uint64_t seq;
} Token;

Token* token_new(JSContext*);
Token* token_create(int, const char*, size_t len, JSContext* ctx);
void token_release(Token*, JSRuntime*);
void token_free(Token*, JSRuntime*);

static inline Location*
token_set_location(Token* tok, Location* loc) {
  return tok->loc = location_dup(loc);
}

static inline OffsetLength
token_char_range(Token* tok) {
  OffsetLength ret = {tok->loc ? tok->loc->char_offset : -1, tok->char_length};
  return ret;
}

static inline OffsetLength
token_byte_range(Token* tok) {
  OffsetLength ret = {tok->loc ? tok->loc->byte_offset : -1, tok->byte_length};
  return ret;
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
