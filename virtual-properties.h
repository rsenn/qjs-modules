#ifndef VIRTUAL_PROPERTIES
#define VIRTUAL_PROPERTIES

#include "quickjs.h"
#include "cutils.h"

struct VirtualProperties;

typedef BOOL has_function_t(struct VirtualProperties*, JSContext*, JSAtom);
typedef JSValue get_function_t(struct VirtualProperties*, JSContext*, JSAtom);
typedef int set_function_t(struct VirtualProperties*, JSContext*, JSAtom, JSValue);

typedef struct VirtualProperties {
  JSValue this_obj;
  has_function_t* has;
  get_function_t* get;
  set_function_t* set;
  void* ptr;
} VirtualProperties;

VirtualProperties virtual_properties_map(JSContext*, JSValueConst);
VirtualProperties virtual_properties_object(JSContext*, JSValueConst);

static inline BOOL
virtual_properties_has(VirtualProperties* vprop, JSContext* ctx, JSAtom prop) {
  return vprop->has(vprop, ctx, prop);
}

static inline JSValue
virtual_properties_get(VirtualProperties* vprop, JSContext* ctx, JSAtom prop) {
  return vprop->get(vprop, ctx, prop);
}

static inline int
virtual_properties_set(VirtualProperties* vprop, JSContext* ctx, JSAtom prop, JSValue value) {
  return vprop->set(vprop, ctx, prop, value);
}

#endif /* defined(VIRTUAL_PROPERTIES) */
