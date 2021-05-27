#ifndef LEXER_H
#define LEXER_H

#include "utils.h"
#include "vector.h"
#include <string.h>

typedef struct Location {
  char* file;
  uint32_t line;
  uint32_t column;
  int64_t pos;
  char* str;
} Location;

void location_print(const Location* loc, DynBuf* dbuf);
Location location_dup(const Location* loc, JSContext* ctx);
void location_free_rt(Location* loc, JSRuntime* rt);

static inline BOOL
location_isnull(const Location* loc) {
  return !loc->file && !loc->line && !loc->column && !loc->pos;
}

typedef struct {
  char* name;
  char* expr;
  uint64_t mask;
  uint8_t* bytecode;
  void* opaque;
  char* expansion;
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
void location_init(Location*);
void location_zero(Location*);
void location_add(Location*, const Location* other);
void location_sub(Location*, const Location* other);
void location_count(Location*, const char* x, size_t n);
void location_free(Location*, JSContext* ctx);
void location_free_rt(Location*, JSRuntime* rt);
int lexer_state_findb(Lexer*, const char* state, size_t slen);
int lexer_state_new(Lexer*, const char* name, size_t len);
int lexer_state_push(Lexer*, const char* state);
int lexer_state_pop(Lexer*);
int lexer_state_top(Lexer*, int i);
char* lexer_state_name(Lexer*, int state);
void lexer_states_dump(Lexer*, uint64_t mask, DynBuf* dbuf);
BOOL lexer_rule_expand(Lexer*, char* p, DynBuf* db);
int lexer_rule_add(Lexer*, char* name, char* expr);
LexerRule* lexer_rule_find(Lexer*, const char* name);
void lexer_rule_free(LexerRule*, JSContext* ctx);
void lexer_rule_free_rt(LexerRule*, JSRuntime* rt);
void lexer_rule_dump(Lexer*, LexerRule* rule, DynBuf* dbuf);
void lexer_init(Lexer*, enum lexer_mode mode, JSContext* ctx);
void lexer_set_input(Lexer*, InputBuffer input, char* filename);
void lexer_define(Lexer*, char* name, char* expr);
LexerRule* lexer_find_definition(Lexer*, const char* name, size_t namelen);
BOOL lexer_compile_rules(Lexer*, JSContext* ctx);
int lexer_peek(Lexer*, uint64_t state, JSContext* ctx);
size_t lexer_skip(Lexer*);
char* lexer_lexeme(Lexer*, size_t* lenp);
int lexer_next(Lexer*, uint64_t state, JSContext* ctx);
void lexer_free(Lexer*, JSContext* ctx);
void lexer_free_rt(Lexer*, JSRuntime* rt);
void lexer_dump(Lexer*, DynBuf* dbuf);

void lexer_set_location(Lexer* lex, const Location* loc, JSContext* ctx);

static inline int
lexer_state_find(Lexer* lex, const char* state) {
  return lexer_state_findb(lex, state, strlen(state));
}

static inline LexerRule*
lexer_rule_at(Lexer* lex, int id) {
  return vector_at(&lex->rules, sizeof(LexerRule), id);
}

static inline size_t
lexer_state_depth(Lexer* lex) {
  return vector_size(&lex->state_stack, sizeof(int32_t));
}

static inline size_t
lexer_num_states(Lexer* lex) {
  return vector_size(&lex->states, sizeof(char*));
}

static inline char*
lexer_state_topname(Lexer* lex) {
  return lexer_state_name(lex, lexer_state_top(lex, 0));
}

size_t input_skip(InputBuffer* input, size_t end, Location* loc);

#endif /* defined(LEXER_H) */
