#ifndef LEXER_H
#define LEXER_H

#include "location.h"
#include "vector.h"
#include "buffer-utils.h"
#include <string.h>

/**
 * \defgroup lexer lexer: Lexical scanner, regex based
 * @{
 */

typedef struct Token Token;

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

typedef enum lexer_result {
  LEXER_EOF = -1,
  LEXER_EXCEPTION = -2,
  LEXER_ERROR_NOMATCH = -3,
  LEXER_ERROR_COMPILE = -4,
  LEXER_ERROR_EXEC = -5,
} LexerResult;

typedef struct {
  union {
    Location loc;
    struct {
      int ref_count;
      int32_t file, line, column;
      int64_t char_offset, byte_offset;
    };
  };
  union {
    InputBuffer input;
    struct {
      uint8_t* data;
      size_t size;
      union {
        //  size_t pos;
        OffsetLength range;
      };
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
BOOL lexer_rule_expand(Lexer*, char* p, DynBuf* db);
int lexer_rule_add(Lexer*, char* name, char* expr);
LexerRule* lexer_rule_find(Lexer*, const char* name);
void lexer_rule_release_rt(LexerRule*, JSRuntime* rt);
void lexer_rule_dump(Lexer*, LexerRule* rule, DynBuf* dbuf);
Lexer* lexer_new(JSContext*);
void lexer_init(Lexer*, enum lexer_mode mode, JSContext* ctx);
void lexer_define(Lexer*, char* name, char* expr);
LexerRule* lexer_find_definition(Lexer*, const char* name, size_t namelen);
BOOL lexer_compile_rules(Lexer*, JSContext* ctx);
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
Token* lexer_token(Lexer* lex, int32_t id, JSContext* ctx);
char* lexer_current_line(Lexer* lex, JSContext* ctx);
char* lexer_lexeme_s(Lexer* lex, JSContext* ctx, int (*escape_fn)(int));

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
