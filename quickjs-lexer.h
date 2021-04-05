#ifndef QUICKJS_LEXER_H
#define QUICKJS_LEXER_H

#include "list.h"
#include "utils.h"
#include "quickjs-predicate.h"
#include "vector.h"

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
  Location loc;
  const char* message;
} SyntaxError;

typedef union Lexer {
  struct InputBuffer input;
  struct {
    const uint8_t* data;
    size_t size;
    size_t pos;
    void (*free)(JSContext*, const char*);
    size_t start;
    Location loc;
    vector charlengths;
    /*size_t nkeywords;
    char** keywords;*/
    JSValue state_fn;
    size_t ref_count;
    struct list_head tokens;
  };
} Lexer;

typedef struct {
  struct list_head link;
  const uint8_t* data;
  uint32_t length;
  uint32_t offset;
  enum token_types id;
  Location loc;
  Lexer* lexer;
} Token;

extern JSClassID js_token_class_id, js_lexer_class_id;

JSValue js_syntax_error_new(JSContext*, SyntaxError arg);

static inline Token*
js_token_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_token_class_id);
}

JSValue js_token_wrap(JSContext*, Token*);

static inline void
token_free(Token* tok, JSRuntime* rt) {
  if(tok->lexer)
    if(--tok->lexer->ref_count == 0)
      lexer_free(tok->lexer, rt);
  js_free_rt(rt, tok);
}

static inline Token*
token_new(Lexer* lex, JSRuntime* rt) {
  Token* ret = js_mallocz_rt(rt, sizeof(Token));
  if((ret->lexer = lex))
    ret->lexer->ref_count++;

  return ret;
}

static inline const char*
token_type(const Token* tok) {
  return ((const char* const[]){"COMMENT",
                                "STRING_LITERAL",
                                "TEMPLATE_LITERAL",
                                "NUMERIC_LITERAL",
                                "BOOLEAN_LITERAL",
                                "NULL_LITERAL",
                                "PUNCTUATOR",
                                "KEYWORD",
                                "IDENTIFIER",
                                "REGEXP_LITERAL",
                                0})[tok->id];
}

uint8_t* lexer_get(Lexer* lex, size_t* lenp);
uint32_t lexer_getc(Lexer* lex, size_t* lenp);
void lexer_ignore(Lexer* lex);
void lexer_skip_until(Lexer* lex, JSContext* ctx, Predicate* pred);
Token lexer_token(Lexer* lex, JSContext* ctx, int id);
void lexer_init(Lexer* lex, JSContext* ctx);
void lexer_free(Lexer* lex, JSRuntime* rt);

static inline Location
lexer_location(const Lexer* lex) {
  Location loc;
  loc.line = lex->loc.line;
  loc.column = lex->loc.column;
  return loc;
}

static inline void
location_dump(DynBuf* dbuf, const Location* loc) {
  dbuf_printf(dbuf, "{ line: %3zu, column: %3zu }", loc->line + 1, loc->column + 1);
}

static inline size_t
lexer_distance(Lexer* lex) {
  size_t* p;
  size_t n = 0;
  vector_foreach_t(&lex->charlengths, p) { n += *p; }
  return n;
}

static inline uint8_t*
lexer_peek(Lexer* lex, size_t* lenp) {
  return js_input_buffer_peek(&lex->input, lenp);
}

static inline uint32_t
lexer_peekc(Lexer* lex, size_t* lenp) {
  return js_input_buffer_peekc(&lex->input, lenp);
}

static inline size_t
lexer_remain(Lexer* lex) {
  return js_input_buffer_remain(&lex->input);
}

static inline size_t
lexer_eof(Lexer* lex) {
  return js_input_buffer_eof(&lex->input);
}

static inline Lexer*
js_lexer_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_lexer_class_id);
}

JSValue js_lexer_wrap(JSContext*, Lexer*);

#endif /* defined(QUICKJS_LEXER_H) */