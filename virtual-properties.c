#include "virtual-properties.h"
#include "utils.h"
#include <quickjs.h>
#include "debug.h"

/**
 * \addtogroup virtual-properties
 * @{
 */
struct MapAdapter {
  JSAtom has, delete, get, set;
};

static BOOL
map_has(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  struct MapAdapter* adapter = vp->opaque;
  JSValue ret;
  ret = JS_Invoke(ctx, vp->this_obj, adapter->has, 1, &prop);
  return JS_ToBool(ctx, ret);
}

static BOOL
map_delete(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  struct MapAdapter* adapter = vp->opaque;
  JSValue ret;
  ret = JS_Invoke(ctx, vp->this_obj, adapter->delete, 1, &prop);
  return JS_ToBool(ctx, ret);
}

static JSValue
map_get(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  struct MapAdapter* adapter = vp->opaque;
  return JS_Invoke(ctx, vp->this_obj, adapter->get, 1, &prop);
}

static int
map_set(VirtualProperties* vp, JSContext* ctx, JSValueConst prop, JSValue value) {
  struct MapAdapter* adapter = vp->opaque;
  int32_t r = -1;
  JSValueConst args[2] = {prop, value};
  JSValue ret;
  ret = JS_Invoke(ctx, vp->this_obj, adapter->set, 2, args);
  JS_ToInt32(ctx, &r, ret);
  return r;
}

static void
map_finalizer(VirtualProperties* vp, JSContext* ctx) {
  struct MapAdapter* adapter = vp->opaque;
  JS_FreeAtom(ctx, adapter->has);
  JS_FreeAtom(ctx, adapter->delete);
  JS_FreeAtom(ctx, adapter->get);
  JS_FreeAtom(ctx, adapter->set);
  js_free(ctx, adapter);
  vp->opaque = 0;
}

VirtualProperties
virtual_properties_map(JSContext* ctx, JSValueConst map) {
  JSValue map_prototype, map_obj;
  struct MapAdapter* adapter = js_mallocz(ctx, sizeof(struct MapAdapter));
  map_obj = JS_DupValue(ctx, map);

  map_prototype = js_global_prototype(ctx, "Map");
  adapter->has = JS_NewAtom(ctx, "has");
  adapter->delete = JS_NewAtom(ctx, "delete");
  adapter->get = JS_NewAtom(ctx, "get");
  adapter->set = JS_NewAtom(ctx, "set");

  JS_FreeValue(ctx, map_prototype);

  return (VirtualProperties){map_obj, map_has, map_delete, map_get, map_set, map_finalizer, adapter};
}

static BOOL
object_has(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  JSAtom atom;
  BOOL ret;
  atom = JS_ValueToAtom(ctx, prop);
  ret = JS_HasProperty(ctx, vp->this_obj, atom);
  JS_FreeAtom(ctx, atom);
  return ret;
}

static BOOL
object_delete(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  JSAtom atom;
  BOOL ret;
  atom = JS_ValueToAtom(ctx, prop);
  ret = JS_DeleteProperty(ctx, vp->this_obj, atom, 0);
  JS_FreeAtom(ctx, atom);
  return ret;
}

static JSValue
object_get(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  JSAtom atom;
  JSValue ret;
  atom = JS_ValueToAtom(ctx, prop);
  ret = JS_GetProperty(ctx, vp->this_obj, atom);
  JS_FreeAtom(ctx, atom);
  return ret;
}

static int
object_set(VirtualProperties* vp, JSContext* ctx, JSValueConst prop, JSValue value) {
  JSAtom atom;
  int ret;
  atom = JS_ValueToAtom(ctx, prop);
  ret = JS_SetProperty(ctx, vp->this_obj, atom, value);
  JS_FreeAtom(ctx, atom);
  return ret;
}

static void
object_finalizer(VirtualProperties* vp, JSContext* ctx) {
}

VirtualProperties
virtual_properties_object(JSContext* ctx, JSValueConst obj) {
  return (VirtualProperties){JS_DupValue(ctx, obj), object_has, object_delete, object_get, object_set, object_finalizer, 0};
}

static int64_t
array_find(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  uint32_t i, n = js_array_length(ctx, vp->this_obj);
  BOOL ret = FALSE;
  for(i = 0; i < n; i++) {
    JSValue entry = JS_GetPropertyUint32(ctx, vp->this_obj, i);
    JSValue key = JS_GetPropertyUint32(ctx, entry, 0);
    JS_FreeValue(ctx, entry);
    ret = js_value_equals(ctx, key, prop);
    JS_FreeValue(ctx, key);
    if(ret)
      return i;
  }
  return -1;
}

static BOOL
array_has(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  return array_find(vp, ctx, prop) != -1;
}

static BOOL
array_delete(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  int64_t index;

  if((index = array_find(vp, ctx, prop)) != -1) {
    JSValueConst args[] = {JS_NewInt64(ctx, index), JS_NewInt32(ctx, 1)};
    JSValue ret = js_invoke(ctx, vp->this_obj, "splice", 2, args);
    JS_FreeValue(ctx, ret);
    return TRUE;
  }

  return FALSE;
}

static JSValue
array_get(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  int64_t index;
  JSValue ret = JS_UNDEFINED;
  if((index = array_find(vp, ctx, prop)) != -1) {
    JSValue entry = JS_GetPropertyUint32(ctx, vp->this_obj, index);
    JSValue value = JS_GetPropertyUint32(ctx, entry, 1);
    JS_FreeValue(ctx, entry);
    ret = value;
  }

  return ret;
}

static int
array_set(VirtualProperties* vp, JSContext* ctx, JSValueConst prop, JSValue value) {
  int64_t index;
  JSValue entry, ret = JS_UNDEFINED;
  /*if((index = array_find(vp, ctx, prop)) != -1) {
    entry = JS_GetPropertyUint32(ctx, vp->this_obj, index);
    JS_SetPropertyUint32(ctx, entry, 1, value);
    JS_FreeValue(ctx, entry);
  } else */
  {
    entry = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, entry, 0, JS_DupValue(ctx, prop));
    JS_SetPropertyUint32(ctx, entry, 1, value);
    ret = js_invoke(ctx, vp->this_obj, "push", 1, &entry);
    JS_FreeValue(ctx, ret);
  }
  return 0;
}

static void
array_finalizer(VirtualProperties* vp, JSContext* ctx) {
}

VirtualProperties
virtual_properties_array(JSContext* ctx, JSValueConst obj) {
  return (VirtualProperties){JS_DupValue(ctx, obj), array_has, array_delete, array_get, array_set, array_finalizer, 0};
}

/**
 * @}
 */
