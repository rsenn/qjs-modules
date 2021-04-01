#ifndef QUICKJS_LEXER_H
#define QUICKJS_LEXER_H

#include "utils.h"
#include "quickjs-predicate.h"

enum token_types {
  COMMENT = 0,
  STRING_LITERAL,
  TEMPLATE_LITERAL,
  NUMERIC_LITERAL,
  BOOLEAN_LITERAL,
  NULL_LITERAL,
  PUNCTUATOR,
  KEYWORD,
  IDENTIFIER,
  REGEXP_LITERAL,
  PROP_EOF

};

typedef struct {
  uint32_t line;
  uint32_t column;
} Location;

typedef struct {
  uint32_t start;
  uint32_t length;
} Line;

typedef struct {
  const uint8_t* data;
  uint32_t length;
  uint32_t offset;
  enum token_types id;
  Location loc;
} Token;

typedef union Lexer {
  InputBuffer input;
  struct {
    const uint8_t* data;
    size_t size;
    size_t pos;
    void (*free)(JSContext*, const char*);
    size_t start;
    Location loc;
    size_t nkeywords;
    char** keywords;
    JSValue state_fn;
    size_t ref_count;
  };
} Lexer;
extern JSClassID js_token_class_id, js_lexer_class_id;

static inline Token*
js_token_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_token_class_id);
}

JSValue js_token_wrap(JSContext*, Token*);
JSValue js_token_new(JSContext*, Token);

static inline Lexer*
js_lexer_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_lexer_class_id);
}

JSValue js_lexer_wrap(JSContext*, Lexer*);

static inline Location
lexer_location(const Lexer* lex) {
  Location loc;
  loc.line = lex->loc.line;
  loc.column = lex->loc.column;
  return loc;
}

static inline int
lexer_peekc(Lexer* lex) {
  if(lex->pos < lex->size) {
    uint8_t c = lex->data[lex->pos];
    return c;
  }
  return -1;
}

static inline int
lexer_getc(Lexer* lex) {
  uint8_t c = lex->data[lex->pos];
  if(c == '\n') {
    lex->loc.line++;
    lex->loc.column = 0;
  } else {
    lex->loc.column++;
  }
  lex->pos++;
  return c;
}
static inline void
lexer_ignore(Lexer* lex) {
  lex->start = lex->pos;
}

static inline void
lexer_skip_until(Lexer* lex, JSContext* ctx, Predicate* pred) {
  while(lex->pos < lex->size) {
    int c = lexer_peekc(lex);

    JSValue str = JS_NewStringLen(ctx, (const char*)&c, 1);

    if(predicate_eval(pred, ctx, 1, &str) > 0)
      break;

    c = lexer_getc(lex);
  }
}

static inline Token
lexer_token(Lexer* lex, JSContext* ctx, int id) {
  Token tok;
  tok.data = lex->data;
  tok.offset = lex->start;
  tok.length = lex->pos - lex->start;
  tok.id = id;
  tok.loc = lexer_location(lex);

  lexer_ignore(lex);

  return tok;
}

static inline void
lexer_init(Lexer* lex) {
  memset(lex, 0, sizeof(Lexer));
}

#endif /* defined(QUICKJS_LEXER_H) */