#ifndef VIRTUAL_PROPERTIES_H
#define VIRTUAL_PROPERTIES_H

#include <quickjs.h>
#include <cutils.h>
#include "utils.h"

/**
 * \defgroup virtual-properties virtual-properties: Virtual property manipulation
 * @{
 */
typedef struct VProps VirtualProperties;

typedef BOOL VFunctionHas(const VirtualProperties*, JSContext*, JSValueConst);
typedef JSValue VFunctionGet(const VirtualProperties*, JSContext*, JSValueConst);
typedef int VFunctionSet(const VirtualProperties*, JSContext*, JSValueConst, JSValueConst);
typedef BOOL VFunctionDelete(const VirtualProperties*, JSContext*, JSValueConst);
typedef JSValue VFunctionKeys(const VirtualProperties*, JSContext*, int);

typedef void* DupFunction(JSContext*, void*);

typedef struct VProps {
  JSValue this_obj;
  VFunctionHas* has;
  VFunctionDelete* delete;
  VFunctionGet* get;
  VFunctionSet* set;
  VFunctionKeys* keys;
  FinalizerFunc* finalize;
  void* opaque;
  DupFunction* dup;
} VirtualProperties;

typedef struct VWrapper VirtualWrapper;

VirtualProperties virtual_properties_map(JSContext*, JSValueConst);
VirtualProperties virtual_properties_object(JSContext*, JSValueConst);
VirtualProperties virtual_properties_array(JSContext*, JSValueConst);
JSValue virtual_properties_wrap(VirtualProperties, JSContext*);

void virtual_properties_copy(const VirtualProperties*, VirtualProperties*, JSContext*);

static inline BOOL
virtual_properties_has(VirtualProperties* vprop, JSContext* ctx, JSValueConst prop) {
  return vprop->has(vprop, ctx, prop);
}

static inline BOOL
virtual_properties_delete(VirtualProperties* vprop, JSContext* ctx, JSValueConst prop) {
  return vprop->delete(vprop, ctx, prop);
}

static inline JSValue
virtual_properties_get(VirtualProperties* vprop, JSContext* ctx, JSValueConst prop) {
  return vprop->get(vprop, ctx, prop);
}

static inline int
virtual_properties_set(VirtualProperties* vprop, JSContext* ctx, JSValueConst prop, JSValue value) {
  return vprop->set(vprop, ctx, prop, value);
}

static inline JSValue
virtual_properties_keys(VirtualProperties* vprop, JSContext* ctx, int flags) {
  return vprop->keys(vprop, ctx, flags);
}

static inline int
virtual_properties_setstr(VirtualProperties* vprop, JSContext* ctx, const char* prop, JSValue value) {
  JSValue key = JS_NewString(ctx, prop);
  int ret = virtual_properties_set(vprop, ctx, key, value);
  JS_FreeValue(ctx, key);
  return ret;
}

static inline void
virtual_properties_free_rt(VirtualProperties* vprop, JSRuntime* rt) {
  vprop->finalize(rt, vprop);
}

static inline void
virtual_properties_free(VirtualProperties* vprop, JSContext* ctx) {
  virtual_properties_free_rt(vprop, JS_GetRuntime(ctx));
}

static inline VirtualProperties
virtual_properties(JSContext* ctx, JSValueConst value) {
  if(js_is_array(ctx, value))
    return virtual_properties_array(ctx, value);

  if(js_is_map(ctx, value))
    return virtual_properties_map(ctx, value);

  /* if(!JS_IsObject(value) || JS_IsNull(value))
     return JS_ThrowTypeError(ctx, "argument must be Array, Map-like or plain Object");*/

  return virtual_properties_object(ctx, value);
}

/**
 * @}
 */
#endif /* defined(VIRTUAL_PROPERTIES_H) */
