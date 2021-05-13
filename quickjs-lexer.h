#ifndef QUICKJS_LEXER_H
#define QUICKJS_LEXER_H

#include "lexer.h"
#include "list.h"

typedef struct {
  Location loc;
  const char* line;
  const char* message;
} SyntaxError;

typedef struct {
  int id;
  char* lexeme;
  uint32_t byte_offset;
  uint32_t byte_length;
  uint32_t char_length;
  Location loc;
  JSValue loc_val;
  Lexer* lexer;
} Token;

extern JSClassID js_location_class_id, js_syntaxerror_class_id, js_token_class_id, js_lexer_class_id;

JSValue js_lexer_new(JSContext* ctx, JSValue proto, JSValueConst in, JSValue mode);
JSValue js_lexer_wrap(JSContext* ctx, Lexer* lex);
JSValue js_syntaxerror_new(JSContext* ctx, SyntaxError arg);
JSValue js_token_wrap(JSContext* ctx, Token* tok);

static inline Location*
js_location_data(JSContext* ctx, JSValueConst value) {
  Location* loc;
  loc = JS_GetOpaque(value, js_location_class_id);
  return loc;
}

static inline SyntaxError*
js_syntaxerror_data(JSContext* ctx, JSValueConst value) {
  SyntaxError* err;
  // err = JS_GetOpaque2(ctx, value, js_syntaxerror_class_id);
  err = JS_GetOpaque(value, js_syntaxerror_class_id);
  return err;
}

static inline Token*
js_token_data(JSContext* ctx, JSValueConst value) {
  Token* tok;
  // tok = JS_GetOpaque2(ctx, value, js_token_class_id);
  tok = JS_GetOpaque(value, js_token_class_id);
  return tok;
}

static inline Lexer*
js_lexer_data(JSContext* ctx, JSValueConst value) {
  Lexer* lex;
  lex = JS_GetOpaque2(ctx, value, js_lexer_class_id);
  return lex;
}

#endif /* defined(QUICKJS_LEXER_H) */
