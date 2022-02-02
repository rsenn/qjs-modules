#ifndef QUICKJS_LEXER_H
#define QUICKJS_LEXER_H

#include "lexer.h"
#include "token.h"

/**
 * \defgroup quickjs-lexer QuickJS module: lexer - Lexical scanner, regex based
 * @{
 */

extern thread_local JSClassID js_token_class_id, js_lexer_class_id;

JSValue js_lexer_new(JSContext* ctx, JSValueConst proto, JSValueConst in, JSValueConst mode);
JSValue js_lexer_wrap(JSContext* ctx, Lexer* lex);
JSValue js_token_wrap(JSContext* ctx, Token* tok);

static inline Token*
js_token_data(JSValueConst value) {
  return JS_GetOpaque(value, js_token_class_id);
}

static inline Token*
js_token_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_token_class_id);
}

static inline Lexer*
js_lexer_data(JSValueConst value) {
  return JS_GetOpaque(value, js_lexer_class_id);
}

static inline Lexer*
js_lexer_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_lexer_class_id);
}

/**
 * @}
 */

#endif /* defined(QUICKJS_LEXER_H) */
