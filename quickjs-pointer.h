#ifndef QUICKJS_POINTER_H
#define QUICKJS_POINTER_H

#include "pointer.h"
#include "utils.h"
#include "utils.h"

/**
 * \defgroup quickjs-pointer QuickJS module: pointer - JS Object pointers (deep keys)
 * @{
 */
extern VISIBLE JSClassID js_pointer_class_id;
 extern VISIBLE JSValue pointer_proto, pointer_ctor;

static inline Pointer*
js_pointer_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_pointer_class_id);
}

static inline Pointer*
js_pointer_data(JSValueConst value) {
  return JS_GetOpaque(value, js_pointer_class_id);
}

VISIBLE JSValue js_pointer_wrap(JSContext*, Pointer*);
VISIBLE JSValue js_pointer_new(JSContext*, JSValueConst, JSValueConst);

/**
 * @}
 */

#endif /* defined(QUICKJS_POINTER_H) */
