// -- HEADER GUARDS --
#ifndef LEXER_H
#define LEXER_H
// -- INCLUDES --
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <regex.h>
// -- MACROS --
#define MAX_NUM_MATCHES 6
#define MAX_NUM_PATTERNS 16
#define MAX_NUM_TOKENS 128
#define MAX_SOURCE_SIZE 512
// -- DATA STRUCTURES --
typedef struct {
  char* name;
  char* regex;
} pattern_t;

typedef struct {
  char* type;
  char* str;
} token_t;

struct state_t {
  regex_t* obj;
  pattern_t* patterns;
  unsigned int npatterns;
  token_t* tokens;
  unsigned int ntokens;
  char* buffer;
  unsigned int buffsize;
} * state;

typedef struct {
  token_t* toks;
  unsigned int ntoks;
} results_t;
// -- PROTOTYPES --
results_t lexer(char* source, unsigned int npatterns, ...);
static void tokenize(char* source);
static void addtoken(char* tokenpattern, char* tokenname);
static void releasetoken(const char* typename);
static void incbuffer(const char c);
static void decbuffer(void);
static void resetbuffer(void);
static bool isvalid(const char* pattern, const char* target);
static void initstateconfig(bool new);
// -- IMPLEMENTATION
results_t
lexer(char* source, unsigned int npatterns, ...) {
  initstateconfig(true);
  va_list args;
  va_start(args, npatterns);
  for(int i = 0; i < npatterns; i++) { addtoken(va_arg(args, char*), va_arg(args, char*)); }
  va_end(args);
  tokenize(source);
  results_t retval = {.toks = state->tokens, .ntoks = state->ntokens};
  initstateconfig(false);
  return retval;
}

static void
tokenize(char* source) {
  if(strlen(source) > MAX_SOURCE_SIZE) {
    return;
  }
  for(int i = 0; i < strlen(source); i++) {
    if(source[i] == ' ') {
      continue;
    }
    incbuffer(source[i]);
    bool success = false;
    char* recentname = NULL;
    for(int j = 0; j < state->npatterns; j++) {
      if(isvalid(state->patterns[j].regex, state->buffer)) {
        recentname = state->patterns[j].name;
        success = true;
        break;
      }
    }
    while(success) {
      if(i == strlen(source) - 1) {
        if(recentname != NULL) {
          releasetoken(recentname);
        }
        break;
      }
      incbuffer(source[++i]);
      success = false;
      for(int j = 0; j < state->npatterns; j++) {
        if(isvalid(state->patterns[j].regex, state->buffer)) {
          recentname = state->patterns[j].name;
          success = true;
        }
      }
      if(!success) {
        i--;
        decbuffer();
        releasetoken(recentname);
        resetbuffer();
        break;
      }
    }
  }
}

static void
addtoken(char* tokenpattern, char* tokenname) {
  if(state->ntokens < MAX_NUM_TOKENS) {
    pattern_t tmp = {.name = tokenname, .regex = tokenpattern};
    state->patterns[state->npatterns++] = tmp;
  }
}

static void
releasetoken(const char* typename) {
  if(state->ntokens < MAX_NUM_TOKENS) {
    token_t tmp = {.type = (char*)typename, .str = state->buffer};
    state->tokens[state->ntokens++] = tmp;
  }
}

static void
incbuffer(const char c) {
  state->buffer = realloc(state->buffer, (state->buffsize + 2));
  state->buffer[state->buffsize++] = c;
  state->buffer[state->buffsize] = '\0';
}

static void
decbuffer(void) {
  if(state->buffsize != 0) {
    state->buffer = realloc(state->buffer, (state->buffsize - 1));
    state->buffsize--;
  }
}

static void
resetbuffer(void) {
  state->buffer = malloc(1);
  state->buffsize = 0;
}

static bool
isvalid(const char* pattern, const char* target) {
  regcomp(state->obj, pattern, REG_EXTENDED);
  regmatch_t* matches = malloc(sizeof(regmatch_t) * MAX_NUM_MATCHES);
  if(regexec(state->obj, target, MAX_NUM_MATCHES, matches, 0) != 0) {
    return false;
  }
  const unsigned int len = strlen(target);
  for(int i = 0; i < MAX_NUM_MATCHES; i++) {
    if(matches[i].rm_so == 0 && matches[i].rm_eo == len && matches[i].rm_eo != 0) {
      return true;
    } else if(matches[i].rm_so == -1) {
      break;
    }
  }
  return false;
}

static void
initstateconfig(bool new) {
  if(new) {
    state = malloc(sizeof(struct state_t));
    state->obj = malloc(sizeof(regex_t));
    state->patterns = malloc(sizeof(pattern_t) * MAX_NUM_PATTERNS);
    state->tokens = malloc(sizeof(token_t) * MAX_NUM_TOKENS);
    state->buffer = malloc(1);
  }
}
#endif
