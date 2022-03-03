#ifndef LEXER_H
#define LEXER_H

#include "location.h"
#include "vector.h"
#include "buffer-utils.h"
#include <string.h>

/**
 * \defgroup lexer Lexical scanner, regex based
 * @{
 */
typedef struct lexical_rule {
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

enum {
  LEXER_EOF = -1,
  LEXER_ERROR_NOMATCH = -2,
  LEXER_ERROR_COMPILE = -3,
  LEXER_ERROR_EXEC = -4,
};

typedef struct lexical_scanner {
  union {
    int ref_count;
    Location loc;
  };
  InputBuffer input;
  enum lexer_mode mode;
  size_t /*start, */ byte_length;
  int32_t token_id, state;
  Vector defines;
  Vector rules;
  Vector states;
  Vector state_stack;
  uint64_t seq;
} Lexer;

int lexer_state_findb(Lexer*, const char*, size_t slen);
int lexer_state_new(Lexer*, const char*, size_t len);
int lexer_state_push(Lexer*, const char*);
int lexer_state_pop(Lexer*);
int lexer_state_top(Lexer*, int);
char* lexer_states_skip(char*);
void lexer_states_dump(Lexer*, uint64_t, DynBuf* dbuf);
char* lexer_rule_regex(LexerRule*);
BOOL lexer_rule_expand(Lexer*, char*, DynBuf* db);
int lexer_rule_add(Lexer*, char*, char* expr);
LexerRule* lexer_rule_find(Lexer*, const char*);
void lexer_rule_free(LexerRule*, JSContext*);
void lexer_rule_free_rt(LexerRule*, JSRuntime*);
void lexer_rule_dump(Lexer*, LexerRule*, DynBuf* dbuf);
void lexer_init(Lexer*, enum lexer_mode, JSContext* ctx);
void lexer_set_input(Lexer*, InputBuffer, int32_t file_atom);
void lexer_define(Lexer*, char*, char* expr);
LexerRule* lexer_find_definition(Lexer*, const char*, size_t namelen);
BOOL lexer_compile_rules(Lexer*, JSContext*);
int lexer_peek(Lexer*, uint64_t, JSContext* ctx);
size_t lexer_skip(Lexer*);
size_t lexer_charlen(Lexer*);
char* lexer_lexeme(Lexer*, size_t*);
int lexer_next(Lexer*, uint64_t, JSContext* ctx);
void lexer_set_location(Lexer*, const Location*, JSContext* ctx);
void lexer_release(Lexer*, JSContext*);
void lexer_free(Lexer*, JSContext*);
void lexer_release_rt(Lexer*, JSRuntime*);
void lexer_free_rt(Lexer*, JSRuntime*);

void lexer_dump(Lexer*, DynBuf*);

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
