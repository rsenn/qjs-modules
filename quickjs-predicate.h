#ifndef QUICKJS_PREDICATE_H
#define QUICKJS_PREDICATE_H

#include "predicate.h"

extern VISIBLE thread_local JSClassID js_predicate_class_id;

VISIBLE Predicate* js_predicate_data2(JSContext* ctx, JSValueConst value);
VISIBLE Predicate* js_predicate_data(JSValueConst value);

JSValue js_predicate_wrap(JSContext*, Predicate);
JSValue js_predicate_new(JSContext*, JSValue proto, JSValue);

#endif /* defined(QUICKJS_PREDICATE_H) */
