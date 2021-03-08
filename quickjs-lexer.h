#ifndef QUICKJS_LEXER_H
#define QUICKJS_LEXER_H

#include "utils.h"

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
  InputValue input;
  struct {
    const uint8_t* data;
    size_t size;
    size_t pos;
    void (*free)(JSContext*, const char*);
    size_t start;
    Location loc;
    size_t nkeywords;
    char** keywords;
  };
} Lexer;
extern JSClassID js_token_class_id;

static inline Token*
js_token_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_token_class_id);
}

JSValue js_token_wrap(JSContext*, Token*);

#endif /* defined(QUICKJS_LEXER_H) */