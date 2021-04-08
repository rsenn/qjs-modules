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

void location_print(const Location* loc, DynBuf* dbuf);
Location location_dup(const Location* loc, JSContext* ctx);
void location_free(Location* loc, JSRuntime* rt);

typedef struct {
  char* name;
  char* expr;
  uint64_t mask;
  uint8_t* bytecode;
} LexerRule;

static const uint64_t MASK_ALL = ~(uint64_t)0;

enum { LEXER_FIRST = 0, LEXER_LAST = 1, LEXER_LONGEST = 2 };

enum { LEXER_EOF = -1, LEXER_ERROR_NOMATCH = -2, LEXER_ERROR_COMPILE = -3, LEXER_ERROR_EXEC = -4 };

typedef struct {
  int mode;
  size_t start;
  InputBuffer input;
  Location loc;
  Vector defines;
  Vector rules;
} Lexer;

void location_print(const Location* loc, DynBuf* dbuf);
Location location_dup(const Location* loc, JSContext* ctx);
void location_free(Location* loc, JSRuntime* rt);

void lexer_init(Lexer* lex, int mode, JSContext* ctx);
void lexer_set_input(Lexer* lex, InputBuffer input, char* filename);
void lexer_define(Lexer* lex, char* name, char* expr);
int lexer_rule_add(Lexer* lex, char* name, char* expr);
LexerRule* lexer_find_definition(Lexer* lex, const char* name, size_t namelen);

BOOL lexer_compile_rules(Lexer* lex, JSContext* ctx);
int lexer_next(Lexer* lex, uint64_t state, JSContext* ctx);
void lexer_dump(Lexer* lex, DynBuf* dbuf);
void lexer_free(Lexer* lex, JSContext* ctx);

static inline LexerRule*
lexer_rule_at(Lexer* lex, int id) {
  return vector_at(&lex->rules, sizeof(LexerRule), id);
}

#endif /* defined(LEXER_H) */
