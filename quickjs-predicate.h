#ifndef QUICKJS_PREDICATE_H
#define QUICKJS_PREDICATE_H

#include "predicate.h"

Predicate* js_predicate_data(JSValue);
Predicate* js_predicate_data2(JSContext*, JSValue);
JSValue js_predicate_wrap(JSContext*, Predicate);
JSValue js_predicate_new(JSContext*, JSValue proto, JSValue);

#endif /* defined(QUICKJS_PREDICATE_H) */
