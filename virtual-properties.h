#ifndef VIRTUAL_PROPERTIES_H
#define VIRTUAL_PROPERTIES_H

#include "quickjs.h"
#include "cutils.h"

struct VProps;

typedef BOOL has_function_t(struct VProps*, JSContext*, JSValueConst);
typedef BOOL delete_function_t(struct VProps*, JSContext*, JSValueConst);
typedef JSValue get_function_t(struct VProps*, JSContext*, JSValueConst);
typedef int set_function_t(struct VProps*, JSContext*, JSValueConst, JSValue);
typedef void destroy_function_t(struct VProps*, JSContext*);

typedef struct VProps {
  JSValue this_obj;
  has_function_t* has;
  delete_function_t* delete;
  get_function_t* get;
  set_function_t* set;
  destroy_function_t* finalize;
  void* opaque;
} VirtualProperties;

VirtualProperties virtual_properties_map(JSContext*, JSValueConst);
VirtualProperties virtual_properties_object(JSContext*, JSValueConst);

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

static inline int
virtual_properties_setstr(VirtualProperties* vprop, JSContext* ctx, const char* prop, JSValue value) {
  JSValue key = JS_NewString(ctx, prop);
  int ret = virtual_properties_set(vprop, ctx, key, value);
  JS_FreeValue(ctx, key);
  return ret;
}

static inline void
virtual_properties_free(VirtualProperties* vprop, JSContext* ctx) {
  vprop->finalize(vprop, ctx);
}

#endif /* defined(VIRTUAL_PROPERTIES_H) */
