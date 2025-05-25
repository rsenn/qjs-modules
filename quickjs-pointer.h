#ifndef QUICKJS_POINTER_H
#define QUICKJS_POINTER_H

#include "pointer.h"
#include "utils.h"
#include "defines.h"

/**
 * \defgroup quickjs-pointer quickjs-pointer: JS Object pointers (deep keys)
 * @{
 */
VISIBLE extern Pointer* js_pointer_data2(JSContext* ctx, JSValueConst value);
VISIBLE extern Pointer* js_pointer_data(JSValueConst value);

VISIBLE JSValue js_pointer_wrap(JSContext*, Pointer*);
VISIBLE JSValue js_pointer_new(JSContext*, JSValueConst, JSValueConst);

/**
 * @}
 */

#endif /* defined(QUICKJS_POINTER_H) */
