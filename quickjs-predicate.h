#ifndef QUICKJS_PREDICATE_H
#define QUICKJS_PREDICATE_H

#include "predicate.h"

extern VISIBLE thread_local JSClassID js_predicate_class_id;

static inline Predicate*
js_predicate_data(JSValueConst value) {
  return JS_GetOpaque(value, js_predicate_class_id);
}

static inline Predicate*
js_predicate_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_predicate_class_id);
}

JSValue js_predicate_wrap(JSContext*, Predicate);
JSValue js_predicate_new(JSContext*, JSValue proto, JSValue);

#endif /* defined(QUICKJS_PREDICATE_H) */
