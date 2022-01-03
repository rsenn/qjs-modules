#ifndef QUICKJS_PREDICATE_H
#define QUICKJS_PREDICATE_H

#include "predicate.h"

/**
 * \defgroup quickjs-predicate QuickJS module: predicate - Predicate function object
 * @{
 */
Predicate* js_predicate_data(JSValueConst);
Predicate* js_predicate_data2(JSContext*, JSValueConst);
enum predicate_id predicate_id(JSValueConst);
JSValue js_predicate_wrap(JSContext*, Predicate);
JSValue js_predicate_new(JSContext*, JSValueConst proto, JSValueConst);

/**
 * @}
 */

#endif /* defined(QUICKJS_PREDICATE_H) */
