#ifndef QUICKJS_LEXER_H
#define QUICKJS_LEXER_H

#include "list.h"
#include "utils.h"
#include "predicate.h"
#include "vector.h"

typedef enum TokId {
  TOKEN_ID_COMMENT = 0,
  TOKEN_ID_STRING_LITERAL,
  TOKEN_ID_TEMPLATE_LITERAL,
  TOKEN_ID_NUMERIC_LITERAL,
  TOKEN_ID_BOOLEAN_LITERAL,
  TOKEN_ID_NULL_LITERAL,
  TOKEN_ID_PUNCTUATOR,
  TOKEN_ID_KEYWORD,
  TOKEN_ID_IDENTIFIER,
  TOKEN_ID_REGEXP_LITERAL,
  TOKEN_ID_EOF
} TokenType;

typedef struct {
  const char* file;
  uint32_t line;
  uint32_t column;
} Location;

typedef struct {
  const uint8_t* data;
  uint32_t byte_length;
  uint32_t offset;
  Location loc;
  const char* message;
} SyntaxError;

typedef union {
  struct InputBuffer input;
  struct {
    const uint8_t* data;
    size_t size;
    size_t pos;
    void (*free)(JSContext*, const char*);
    size_t start;
    Location loc;
    vector charlengths;
    JSValue state_fn;
    ssize_t ref_count;
    struct list_head tokens;
  };
} Lexer;

typedef struct {
  const uint8_t* data;
  uint32_t byte_length;
  uint32_t num_chars;
  uint32_t offset;
  TokenType id;
  Location loc;
  Lexer* lexer;
  ssize_t ref_count;
  struct list_head link;
} Token;

extern JSClassID js_syntaxerror_class_id, js_token_class_id, js_lexer_class_id;

JSValue js_lexer_new(JSContext* ctx, JSValue proto, JSValue value);
JSValue js_lexer_wrap(JSContext* ctx, Lexer* lex);
JSValue js_syntaxerror_new(JSContext* ctx, SyntaxError arg);
JSValue js_token_wrap(JSContext* ctx, Token* tok);

static inline Token*
js_syntaxerror_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_syntaxerror_class_id);
}

static inline Token*
js_token_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_token_class_id);
}

static inline Lexer*
js_lexer_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_lexer_class_id);
}

#endif /* defined(QUICKJS_LEXER_H) */ struct list_head link;
