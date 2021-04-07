#define _GNU_SOURCE

#include "virtual-properties.h"
#include "utils.h"

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

  return (
      VirtualProperties){map_obj, map_has, map_delete, map_get, map_set, map_finalizer, adapter};
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
  return (VirtualProperties){JS_DupValue(ctx, obj),
                             object_has,
                             object_delete,
                             object_get,
                             object_set,
                             object_finalizer,
                             0};
}
