#include "virtual-properties.h"
#include "utils.h"
#include "js-utils.h"
#include <quickjs.h>
#include "debug.h"

struct VWrapper {
  struct VProps props;
  int ref_count;
  JSContext* ctx;
};

/**
 * \addtogroup virtual-properties
 * @{
 */
struct MapMethodAtoms {
  JSAtom has, delete, get, set;
};

static BOOL
map_has(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  struct MapMethodAtoms* atoms = vp->opaque;
  JSValue ret = JS_Invoke(ctx, vp->this_obj, atoms->has, 1, &prop);
  return JS_ToBool(ctx, ret);
}

static BOOL
map_delete(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  struct MapMethodAtoms* atoms = vp->opaque;
  JSValue ret = JS_Invoke(ctx, vp->this_obj, atoms->delete, 1, &prop);
  return JS_ToBool(ctx, ret);
}

static JSValue
map_get(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  struct MapMethodAtoms* atoms = vp->opaque;
  return JS_Invoke(ctx, vp->this_obj, atoms->get, 1, &prop);
}

static int
map_set(VirtualProperties* vp, JSContext* ctx, JSValueConst prop, JSValue value) {
  struct MapMethodAtoms* atoms = vp->opaque;
  int32_t r = -1;
  JSValueConst args[2] = {prop, value};
  JSValue ret = JS_Invoke(ctx, vp->this_obj, atoms->set, 2, args);
  JS_ToInt32(ctx, &r, ret);
  return r;
}

static void
map_finalizer(VirtualProperties* vp, JSContext* ctx) {
  struct MapMethodAtoms* atoms = vp->opaque;
  JS_FreeAtom(ctx, atoms->has);
  JS_FreeAtom(ctx, atoms->delete);
  JS_FreeAtom(ctx, atoms->get);
  JS_FreeAtom(ctx, atoms->set);
  js_free(ctx, atoms);
  vp->opaque = 0;
}

static void*
opaque_dup(dup_function* fn, void* ptr, JSContext* ctx) {
  assert(fn);
  return fn(ptr, ctx);
}

VirtualProperties
virtual_properties_map(JSContext* ctx, JSValueConst map) {
  JSValue map_prototype, map_obj;
  struct MapMethodAtoms* atoms = js_mallocz(ctx, sizeof(struct MapMethodAtoms));
  map_obj = JS_DupValue(ctx, map);

  map_prototype = js_global_prototype(ctx, "Map");
  atoms->has = JS_NewAtom(ctx, "has");
  atoms->delete = JS_NewAtom(ctx, "delete");
  atoms->get = JS_NewAtom(ctx, "get");
  atoms->set = JS_NewAtom(ctx, "set");

  JS_FreeValue(ctx, map_prototype);

  return (VirtualProperties){
      map_obj,
      map_has,
      map_delete,
      map_get,
      map_set,
      map_finalizer,
      atoms,
      0,
  };
}

static BOOL
object_has(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  return js_has_propertyvalue(ctx, vp->this_obj, prop);
}

static BOOL
object_delete(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  return js_delete_propertyvalue(ctx, vp->this_obj, prop);
}

static JSValue
object_get(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  return js_get_propertyvalue(ctx, vp->this_obj, prop);
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
  return (VirtualProperties){
      JS_DupValue(ctx, obj),
      object_has,
      object_delete,
      object_get,
      object_set,
      object_finalizer,
      0,
      0,
  };
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
  int64_t pos;

  if((pos = array_find(vp, ctx, prop)) != -1) {
    JSValueConst args[] = {JS_NewInt64(ctx, pos), JS_NewInt32(ctx, 1)};
    JSValue ret = js_invoke(ctx, vp->this_obj, "splice", 2, args);
    JS_FreeValue(ctx, ret);
    return TRUE;
  }

  return FALSE;
}

static JSValue
array_get(VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  int64_t pos;
  JSValue ret = JS_UNDEFINED;

  if((pos = array_find(vp, ctx, prop)) != -1) {
    JSValue entry = JS_GetPropertyUint32(ctx, vp->this_obj, pos);
    JSValue value = JS_GetPropertyUint32(ctx, entry, 1);
    JS_FreeValue(ctx, entry);
    ret = value;
  }

  return ret;
}

static int
array_set(VirtualProperties* vp, JSContext* ctx, JSValueConst prop, JSValue value) {
  int64_t pos;
  JSValue entry, ret = JS_UNDEFINED;
  /*if((pos = array_find(vp, ctx, prop)) != -1) {
    entry = JS_GetPropertyUint32(ctx, vp->this_obj, pos);
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
  return (VirtualProperties){
      JS_DupValue(ctx, obj),
      array_has,
      array_delete,
      array_get,
      array_set,
      array_finalizer,
      0,
      0,
  };
}

static VirtualWrapper*
wrapper_new(VirtualProperties* vprops, JSContext* ctx) {
  VirtualWrapper* vwrap;

  if(!(vwrap = js_malloc(ctx, sizeof(VirtualWrapper))))
    return 0;

  virtual_properties_copy(vprops, &vwrap->props, ctx);
  vwrap->ctx = ctx;
  vwrap->ref_count = 1;

  return vwrap;
}

static void
wrapper_free(void* ptr) {
  VirtualWrapper* vwrap = ptr;

  if(--vwrap->ref_count == 0) {
    virtual_properties_free(&vwrap->props, vwrap->ctx);

    js_free(vwrap->ctx, vwrap);
  }
}

enum {
  METHOD_HAS = 0,
  METHOD_GET,
  METHOD_SET,
  METHOD_DELETE,
};

static JSValue
virtual_properties_getset(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, void* opaque) {
  VirtualWrapper* vwrap = opaque;
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case METHOD_HAS: {
      ret = JS_NewBool(ctx, virtual_properties_has(&vwrap->props, ctx, argv[0]));
      break;
    }
    case METHOD_GET: {
      ret = virtual_properties_get(&vwrap->props, ctx, argv[0]);
      break;
    }
    case METHOD_SET: {
      virtual_properties_set(&vwrap->props, ctx, argv[0], argc > 1 ? argv[1] : JS_UNDEFINED);
      break;
    }
    case METHOD_DELETE: {
      ret = JS_NewBool(ctx, virtual_properties_delete(&vwrap->props, ctx, argv[0]));
      break;
    }
  }
  return ret;
}

JSValue
virtual_properties_method(VirtualProperties vprop, int magic, JSContext* ctx) {
  VirtualWrapper* vwrap;
  JSValue obj;

  if(!(vwrap = wrapper_new(&vprop, ctx)))
    return JS_ThrowOutOfMemory(ctx);

  obj = js_function_cclosure(ctx, virtual_properties_getset, 1, magic, vwrap, wrapper_free);

  return obj;
}

JSValue
virtual_properties_wrap(VirtualProperties vprop, JSContext* ctx) {
  JSValue obj;

  if(!(vprop.get && vprop.set))
    return JS_ThrowInternalError(ctx, "virtual property needs at least get & set methods");

  obj = JS_NewObject(ctx);

  if(vprop.has)
    JS_SetPropertyStr(ctx, obj, "has", virtual_properties_method(vprop, METHOD_HAS, ctx));
  if(vprop.get)
    JS_SetPropertyStr(ctx, obj, "get", virtual_properties_method(vprop, METHOD_GET, ctx));
  if(vprop.set)
    JS_SetPropertyStr(ctx, obj, "set", virtual_properties_method(vprop, METHOD_SET, ctx));
  if(vprop.delete)
    JS_SetPropertyStr(ctx, obj, "delete", virtual_properties_method(vprop, METHOD_DELETE, ctx));

  return obj;
}

void
virtual_properties_copy(const VirtualProperties* vprop, VirtualProperties* dest, JSContext* ctx) {
  dest->this_obj = JS_DupValue(ctx, vprop->this_obj);
  dest->has = vprop->has;
  dest->delete = vprop->delete;
  dest->get = vprop->get;
  dest->set = vprop->set;
  dest->finalize = vprop->finalize;

  dest->opaque = vprop->dup ? opaque_dup(vprop->dup, vprop->opaque, ctx) : vprop->opaque;
}
/**
 * @}
 */
