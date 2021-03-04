
#include "virtual-properties.h"
#include "utils.h"

static BOOL
map_has(VirtualProperties* vp, JSContext* ctx, JSAtom prop) {
  JSValueConst has = ((JSValueConst*)vp->ptr)[0];
}

static JSValue
map_get(VirtualProperties* vp, JSContext* ctx, JSAtom prop) {
  JSValueConst get = ((JSValueConst*)vp->ptr)[1];
}

static int
map_set(VirtualProperties* vp, JSContext* ctx, JSAtom prop, JSValue value) {
  JSValueConst set = ((JSValueConst*)vp->ptr)[2];
}

VirtualProperties
virtual_properties_map(JSContext* ctx, JSValueConst map) {
  JSValue map_prototype;
  JSValueConst* values = js_mallocz(ctx, sizeof(JSValue) * 3);
  VirtualProperties vprops = {JS_DupValue(ctx, map), map_has, map_get, map_set, values};
  map_prototype = js_global_prototype(ctx, "Map");
  values[0] = JS_GetPropertyStr(ctx, map_prototype, "has");
  values[1] = JS_GetPropertyStr(ctx, map_prototype, "get");
  values[2] = JS_GetPropertyStr(ctx, map_prototype, "set");
  JS_FreeValue(ctx, map_prototype);

  return vprops;
}

static BOOL
object_has(VirtualProperties* vp, JSContext* ctx, JSAtom prop) {
  return JS_HasProperty(ctx, vp->this_obj, prop);
}

static JSValue
object_get(VirtualProperties* vp, JSContext* ctx, JSAtom prop) {
  return JS_GetProperty(ctx, vp->this_obj, prop);
}

static int
object_set(VirtualProperties* vp, JSContext* ctx, JSAtom prop, JSValue value) {
  return JS_SetProperty(ctx, vp->this_obj, prop, value);
}

VirtualProperties
virtual_properties_object(JSContext* ctx, JSValueConst obj) {
  VirtualProperties vprops = {JS_DupValue(ctx, obj), object_has, object_get, object_set};
  return vprops;
}
