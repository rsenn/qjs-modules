#ifndef QUICKJS_PREDICATE_H
#define QUICKJS_PREDICATE_H

#include "predicate.h"

/**
 * \defgroup quickjs-predicate quickjs-predicate: Predicate function object
 * @{
 */
VISIBLE JSValue js_predicate_wrap(JSContext*, Predicate);

VISIBLE Predicate* js_predicate_data(JSValueConst value);
VISIBLE Predicate* js_predicate_data2(JSContext* ctx, JSValueConst value);

/**
 * @}
 */

#endif /* defined(QUICKJS_PREDICATE_H) */
