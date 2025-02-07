#ifndef LEXER_H
#define LEXER_H

#include "location.h"
#include "vector.h"
#include "buffer-utils.h"
#include <stdbool.h>
#include <string.h>

/**
 * \defgroup lexer lexer: Lexical scanner, regex based
 * @{
 */
typedef struct {
  char* name;
  char* expr;
  uint64_t mask;
  uint8_t* bytecode;
  void* opaque;
  char* expansion;
} LexerRule;

static const uint64_t MASK_ALL = ~(uint64_t)0;

enum lexer_mode {
  LEXER_FIRST = 0,
  LEXER_LAST = 1,
  LEXER_LONGEST = 2,
};

enum lexer_result {
  LEXER_EOF = -1,
  LEXER_EXCEPTION = -2,
  LEXER_ERROR_NOMATCH = -3,
  LEXER_ERROR_COMPILE = -4,
  LEXER_ERROR_EXEC = -5,
};

typedef struct {
  union {
    int ref_count;
    Location loc;
  };
  union {
    InputBuffer input;
    struct {
      uint8_t* data;
      size_t size;
      size_t pos;
    };
  };
  enum lexer_mode mode;
  size_t byte_length;
  int32_t token_id, state;
  Vector defines;
  Vector rules;
  Vector states;
  Vector state_stack;
  uint64_t seq;
} Lexer;

int lexer_state_findb(Lexer*, const char* state, size_t slen);
int lexer_state_new(Lexer*, const char* name, size_t len);
int lexer_state_push(Lexer*, const char* state);
int lexer_state_pop(Lexer*);
int lexer_state_top(Lexer*, int i);
char* lexer_states_skip(char*);
void lexer_states_dump(Lexer*, uint64_t mask, DynBuf* dbuf);
char* lexer_rule_regex(LexerRule*);
bool lexer_rule_expand(Lexer*, char* p, DynBuf* db);
int lexer_rule_add(Lexer*, char* name, char* expr);
LexerRule* lexer_rule_find(Lexer*, const char* name);
void lexer_rule_release_rt(LexerRule*, JSRuntime* rt);
void lexer_rule_dump(Lexer*, LexerRule* rule, DynBuf* dbuf);
void lexer_init(Lexer*, enum lexer_mode mode, JSContext* ctx);
void lexer_define(Lexer*, char* name, char* expr);
LexerRule* lexer_find_definition(Lexer*, const char* name, size_t namelen);
bool lexer_compile_rules(Lexer*, JSContext* ctx);
int lexer_peek(Lexer*, /* uint64_t state,*/ unsigned start_rule, JSContext* ctx);
size_t lexer_skip_n(Lexer*, size_t bytes);
size_t lexer_skip(Lexer*);
size_t lexer_charlen(Lexer*);
void lexer_clear_token(Lexer*);
char* lexer_lexeme(Lexer*, size_t* lenp);
int lexer_next(Lexer*, JSContext* ctx);
void lexer_set_input(Lexer*, InputBuffer input, int32_t file_atom);
void lexer_set_location(Lexer*, const Location* loc, JSContext* ctx);
Location lexer_get_location(Lexer*, JSContext* ctx);
void lexer_release(Lexer*, JSRuntime* rt);
void lexer_free(Lexer*, JSRuntime* rt);
void lexer_dump(Lexer*, DynBuf* dbuf);

static inline Lexer*
lexer_dup(Lexer* lex) {
  ++lex->ref_count;
  return lex;
}

static inline LexerRule*
lexer_rule_at(Lexer* lex, int id) {
  return vector_at(&lex->rules, sizeof(LexerRule), id);
}

static inline int
lexer_state_find(Lexer* lex, const char* state) {
  return lexer_state_findb(lex, state, strlen(state));
}

static inline size_t
lexer_state_depth(Lexer* lex) {
  return vector_size(&lex->state_stack, sizeof(int32_t));
}

static inline char*
lexer_state_name(Lexer* lex, int state) {
  char** name_p;

  name_p = vector_at(&lex->states, sizeof(char*), state);

  return name_p ? *name_p : 0;
}

static inline size_t
lexer_num_states(Lexer* lex) {
  return vector_size(&lex->states, sizeof(char*));
}

static inline char*
lexer_state_topname(Lexer* lex) {
  return lexer_state_name(lex, lexer_state_top(lex, 0));
}

/**
 * @}
 */
#endif /* defined(LEXER_H) */
