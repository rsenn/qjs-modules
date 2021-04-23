#ifndef LEXER_H
#define LEXER_H

#include "utils.h"
#include "vector.h"
#include <string.h>

typedef struct {
  const char* file;
  uint32_t line;
  uint32_t column;
  int64_t pos;
} Location;

void location_print(const Location* loc, DynBuf* dbuf);
Location location_dup(const Location* loc, JSContext* ctx);
void location_free(Location* loc, JSRuntime* rt);

typedef struct {
  char* name;
  char* expr;
  int state;
  uint64_t mask;
  uint8_t* bytecode;
  void* opaque;
} LexerRule;

static const uint64_t MASK_ALL = ~(uint64_t)0;

enum lexer_mode { LEXER_FIRST = 0, LEXER_LAST = 1, LEXER_LONGEST = 2 };

enum { LEXER_EOF = -1, LEXER_ERROR_NOMATCH = -2, LEXER_ERROR_COMPILE = -3, LEXER_ERROR_EXEC = -4 };

typedef struct {
  enum lexer_mode mode;
  size_t start;
  size_t bytelen;
  int32_t tokid;
  int32_t state;
  InputBuffer input;
  Location loc;
  Vector defines;
  Vector rules;
  Vector states;
  Vector state_stack;
} Lexer;

void location_print(const Location*, DynBuf* dbuf);
Location location_dup(const Location*, JSContext* ctx);
void location_free(Location*, JSRuntime* rt);

void lexer_init(Lexer*, enum lexer_mode mode, JSContext* ctx);
void lexer_set_input(Lexer*, InputBuffer input, char* filename);
void lexer_define(Lexer*, char* name, char* expr);
size_t lexer_state_parse(const char*, const char** state);
int lexer_state_find(Lexer*, const char*);
int lexer_state_new(Lexer*, char*);
int lexer_state_push(Lexer*, const char*);
int lexer_state_pop(Lexer*);
int lexer_state_top(Lexer*, int i);
const char* lexer_state_name(Lexer*, int);
int lexer_rule_add(Lexer*, char* name, char* expr);
BOOL lexer_rule_expand(Lexer*, char* expr, DynBuf* db);
LexerRule* lexer_find_definition(Lexer*, const char* name, size_t namelen);
BOOL lexer_compile_rules(Lexer*, JSContext* ctx);
int lexer_peek(Lexer*, uint64_t state, JSContext* ctx);
size_t lexer_skip(Lexer*);
int lexer_next(Lexer*, uint64_t state, JSContext* ctx);
void lexer_dump(Lexer*, DynBuf* dbuf);
void lexer_free(Lexer*, JSContext* ctx);

static inline LexerRule*
lexer_rule_at(Lexer* lex, int id) {
  return vector_at(&lex->rules, sizeof(LexerRule), id);
}

LexerRule* lexer_rule_find(Lexer* lex, const char* name);

#endif /* defined(LEXER_H) */
