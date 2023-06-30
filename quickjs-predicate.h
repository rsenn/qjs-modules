#ifndef QUICKJS_PREDICATE_H
#define QUICKJS_PREDICATE_H

#include "predicate.h"

/**
 * \defgroup quickjs-predicate QuickJS module: predicate - Predicate function object
 * @{
 */
VISIBLE Predicate* js_predicate_data(JSValueConst);
VISIBLE Predicate* js_predicate_data2(JSContext*, JSValueConst);
VISIBLE JSValue js_predicate_wrap(JSContext*, Predicate);
VISIBLE JSValue js_predicate_new(JSContext*, JSValueConst proto, JSValueConst);
VISIBLE enum PredicateId predicate_id(JSValue);

extern VISIBLE JSClassID js_predicate_class_id;

/**
 * @}
 */

#endif /* defined(QUICKJS_PREDICATE_H) */
