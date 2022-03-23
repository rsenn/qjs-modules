#ifndef QUICKJS_POINTER_H
#define QUICKJS_POINTER_H

#include "include/pointer.h"
#include "include/utils.h"
#include "include/utils.h"

/**
 * \defgroup quickjs-pointer QuickJS module: pointer - JS Object pointers (deep keys)
 * @{
 */
extern thread_local JSClassID js_pointer_class_id;

static inline Pointer*
js_pointer_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_pointer_class_id);
}

static inline Pointer*
js_pointer_data(JSValueConst value) {
  return JS_GetOpaque(value, js_pointer_class_id);
}

JSValue js_pointer_wrap(JSContext*, Pointer*);
JSValue js_pointer_new(JSContext*, JSValueConst , JSValueConst);

/**
 * @}
 */

#endif /* defined(QUICKJS_POINTER_H) */
