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

void location_dump(const Location* loc, DynBuf* dbuf);
Location location_dup(const Location* loc, JSContext* ctx);
void location_free(Location* loc, JSRuntime* rt);

typedef struct {
  char* name;
  char* expr;
  uint8_t* bytecode;
} LexerRule;

enum { LEXER_FIRST = 0, LEXER_LONGEST };

typedef struct {
  int mode;
  size_t start;
  InputBuffer input;
  Location loc;
  Vector rules;
} Lexer;

void lexer_init(Lexer* lex, int mode, JSContext* ctx);
void lexer_set_input(Lexer* lex, InputBuffer input, char* filename);
int lexer_add_rule(Lexer* lex, char* name, char* expr);
LexerRule* lexer_find_rule(Lexer* lex, const char* name, size_t namelen);
BOOL lexer_compile_rules(Lexer* lex, JSContext* ctx);
int lexer_next(Lexer* lex, JSContext* ctx);
void lexer_dump(Lexer* lex, DynBuf* dbuf);
void lexer_free(Lexer* lex, JSContext* ctx);

#endif /* defined(LEXER_H) */
