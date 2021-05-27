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

void location_print(const Location* loc, DynBuf* dbuf);
Location location_dup(const Location* loc, JSContext* ctx);
void location_zero(Location* loc);
void location_add(Location* loc, const Location* other);
void location_free(Location* loc, JSContext* ctx);
void location_free_rt(Location* loc, JSRuntime* rt);

int lexer_state_findb(Lexer* lex, const char* state, size_t slen);
int lexer_state_new(Lexer* lex, const char* name, size_t len);
int lexer_state_push(Lexer* lex, const char* state);
int lexer_state_pop(Lexer* lex);
int lexer_state_top(Lexer* lex, int i);
char* lexer_state_name(Lexer* lex, int state);
void lexer_states_dump(Lexer* lex, uint64_t mask, DynBuf* dbuf);

BOOL lexer_rule_expand(Lexer* lex, char* p, DynBuf* db);
int lexer_rule_add(Lexer* lex, char* name, char* expr);
LexerRule* lexer_rule_find(Lexer* lex, const char* name);
void lexer_rule_free(LexerRule* rule, JSContext* ctx);
void lexer_rule_free_rt(LexerRule* rule, JSRuntime* rt);
void lexer_rule_dump(Lexer* lex, LexerRule* rule, DynBuf* dbuf);

void lexer_init(Lexer* lex, enum lexer_mode mode, JSContext* ctx);
void lexer_set_input(Lexer* lex, InputBuffer input, char* filename);
static inline void
lexer_set_location(Lexer* lex, const Location* loc, JSContext* ctx) {
  lex->start = loc->pos;
  lex->input.pos = loc->pos;
  location_free(&lex->loc, ctx);
  lex->loc = location_dup(loc, ctx);
}

void lexer_define(Lexer* lex, char* name, char* expr);
LexerRule* lexer_find_definition(Lexer* lex, const char* name, size_t namelen);
BOOL lexer_compile_rules(Lexer* lex, JSContext* ctx);
int lexer_peek(Lexer* lex, uint64_t state, JSContext* ctx);
size_t lexer_skip(Lexer* lex);
char* lexer_lexeme(Lexer* lex, size_t* lenp);
int lexer_next(Lexer* lex, uint64_t state, JSContext* ctx);
void lexer_free(Lexer* lex, JSContext* ctx);
void lexer_free_rt(Lexer* lex, JSRuntime* rt);
void lexer_dump(Lexer* lex, DynBuf* dbuf);

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

static inline char*
lexer_state_topname(Lexer* lex) {
  return lexer_state_name(lex, lexer_state_top(lex, 0));
}

static inline size_t
input_skip(InputBuffer* input, size_t end, Location* loc) {
  size_t n = 0;
  while(input->pos < end) {
    size_t prev = input->pos;
    if(input_buffer_getc(input) == '\n') {
      loc->line++;
      loc->column = 0;
    } else {
      loc->column++;
    }
    loc->pos++;
    n++;
  }
  return n;
}

#endif /* defined(LEXER_H) */
