#ifndef QUICKJS_LEXER_H
#define QUICKJS_LEXER_H

#include "lexer.h"
#include "token.h"

/**
 * \defgroup quickjs-lexer quickjs-lexer: Lexical scanner, regex based
 * @{
 */

extern VISIBLE JSClassID js_token_class_id, js_lexer_class_id;

JSValue js_token_wrap(JSContext*, Token*);
JSValue js_token_constructor(JSContext*, JSValueConst, int, JSValueConst[]);
JSValue js_lexer_new(JSContext*, JSValueConst, JSValueConst, JSValueConst);
JSValue js_lexer_wrap(JSContext*, Lexer*);
JSValue js_lexer_constructor(JSContext*, JSValueConst, int, JSValueConst[]);

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
