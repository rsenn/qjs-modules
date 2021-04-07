#ifndef LEXER_H
#define LEXER_H

#include "utils.h"
#include "vector.h"
#include <string.h>

typedef struct {
  const char* file;
  uint32_t line;
  uint32_t column;
} Location;

typedef struct {
  char* name;
  char* expr;
  uint8_t* bytecode;
} LexerRule;

enum { LEXER_FIRST = 0, LEXER_LONGEST };

typedef struct {
  InputBuffer input;
  int mode;
  size_t start;
  Location loc;
  Vector rules;
} Lexer;

void lexer_init(Lexer* lex, int mode, JSContext* ctx);
void lexer_set_input(Lexer* lex, InputBuffer input, char* filename);
int lexer_add_rule(Lexer* lex, char* name, char* expr);
LexerRule* lexer_find_rule(Lexer* lex, const char* name, size_t namelen);
BOOL lexer_compile_rules(Lexer* lex, JSContext* ctx);
int lexer_next(Lexer* lex, JSContext* ctx);
void lexer_free(Lexer* lex, JSContext* ctx);

#endif /* defined(LEXER_H) */
