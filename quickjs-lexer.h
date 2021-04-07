#ifndef QUICKJS_LEXER_H
#define QUICKJS_LEXER_H

#include "lexer.h"
#include "list.h"

enum { LEXER_EOF = -1 };

typedef struct {
  const uint8_t* data;
  uint32_t byte_length;
  uint32_t offset;
  Location loc;
  const char* message;
} SyntaxError;

typedef struct {
  int id;
  const uint8_t* data;
  uint32_t byte_length;
  uint32_t char_length;
  uint32_t offset;
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

static inline SyntaxError*
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

#endif /* defined(QUICKJS_LEXER_H) */
