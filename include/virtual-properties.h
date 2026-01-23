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
  VFunctionGet* get;
  VFunctionSet* set;
  VFunctionDelete* delete;
  VFunctionKeys* keys;
  FinalizerFunc* finalize;
  void* opaque;
  DupFunction* opaque_dup;
} VirtualProperties;

#define VIRTUAL_PROPERTIES_INIT() \
  (VirtualProperties) { JS_EXCEPTION, 0, 0, 0, 0, 0, 0, 0, 0 }

VirtualProperties virtual_properties(JSContext*, JSValueConst);
VirtualProperties virtual_properties_map(JSContext*, JSValueConst);
VirtualProperties virtual_properties_object(JSContext*, JSValueConst);
VirtualProperties virtual_properties_array(JSContext*, JSValueConst);

JSValue virtual_properties_wrap(VirtualProperties, JSContext*);
void virtual_properties_free_rt(VirtualProperties*, JSRuntime*);
void virtual_properties_copy(const VirtualProperties*, VirtualProperties*, JSContext*);

BOOL virtual_has(const VirtualProperties*, JSContext*, JSValueConst);
JSValueConst virtual_get(const VirtualProperties*, JSContext*, JSValueConst);
int virtual_set(const VirtualProperties*, JSContext*, JSValueConst, JSValueConst);
BOOL virtual_delete(const VirtualProperties*, JSContext*, JSValueConst);
JSValueConst virtual_keys(const VirtualProperties*, JSContext*, int);

static inline void
virtual_properties_free(VirtualProperties* vprop, JSContext* ctx) {
  virtual_properties_free_rt(vprop, JS_GetRuntime(ctx));
}

/**
 * @}
 */
#endif /* defined(VIRTUAL_PROPERTIES_H) */
