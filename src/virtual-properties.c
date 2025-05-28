#include "virtual-properties.h"
#include "utils.h"
#include "debug.h"
#include "iteration.h"

/**
 * \addtogroup virtual-properties
 * @{
 */

enum {
  METHOD_HAS = 0,
  METHOD_GET,
  METHOD_SET,
  METHOD_DELETE,
  METHOD_KEYS,
};

static const int method_arity[] = {
    [METHOD_HAS] = 1,
    [METHOD_GET] = 1,
    [METHOD_SET] = 2,
    [METHOD_DELETE] = 1,
    [METHOD_KEYS] = 0,
};

struct VWrapper {
  struct VProps props;
  int ref_count;
  JSContext* ctx;
};

static VirtualWrapper*
wrapper_new(const VirtualProperties* virt, JSContext* ctx) {
  VirtualWrapper* vw;

  if(!(vw = js_malloc(ctx, sizeof(VirtualWrapper))))
    return 0;

  virtual_properties_copy(virt, &vw->props, ctx);
  vw->ctx = ctx;
  vw->ref_count = 1;

  return vw;
}

static void
wrapper_free(JSRuntime* rt, void* ptr) {
  VirtualWrapper* vw = ptr;

  if(--vw->ref_count == 0) {
    virtual_properties_free(&vw->props, vw->ctx);

    js_free_rt(rt, vw);
  }
}

static void
atoms_dup(JSContext*ctx, void* ptr) {
  JSAtom* atoms=ptr, *ret;
  uint32_t len;

  for(len = 0; atoms[len]; len++) {}

if(!(ret=js_mallocz(ctx,  (len+1) * sizeof(JSAtom))))
  return 0;

  for(uint32_t i = 0; i < len; i++)
    ret[i]= JS_DupAtom(ctx,  atoms[i]);

return ret;
}

typedef struct {
  JSAtom has, get, set, delete, keys;
} MethodAtoms;

static BOOL
map_has(const VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  MethodAtoms* atoms = vp->opaque;
  JSValue ret = JS_Invoke(ctx, vp->this_obj, atoms->has, 1, &prop);

  return JS_ToBool(ctx, ret);
}

static JSValue
map_get(const VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  MethodAtoms* atoms = vp->opaque;

  return JS_Invoke(ctx, vp->this_obj, atoms->get, 1, &prop);
}

static int
map_set(const VirtualProperties* vp, JSContext* ctx, JSValueConst prop, JSValue value) {
  MethodAtoms* atoms = vp->opaque;
  JSValueConst args[] = {
      prop,
      value,
  };

  JSValue ret = JS_Invoke(ctx, vp->this_obj, atoms->set, countof(args), args);

  if(JS_IsException(ret)) {
    JS_GetException(ctx);
    return -1;
  }

  JS_FreeValue(ctx, ret);
  return 0;
}

static BOOL
map_delete(const VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  MethodAtoms* atoms = vp->opaque;
  JSValue ret = JS_Invoke(ctx, vp->this_obj, atoms->delete, 1, &prop);

  return JS_ToBool(ctx, ret);
}

static JSValue
map_keys(const VirtualProperties* vp, JSContext* ctx, int flags) {
  MethodAtoms* atoms = vp->opaque;

  JSValue ret = JS_Invoke(ctx, vp->this_obj, atoms->keys, 0, 0);

  JSValue arr = iteration_array(ctx, ret);
  JS_FreeValue(ctx, ret);
  ret = arr;

  return ret;
}

static void
map_finalizer(JSRuntime* rt, void* ptr) {
  VirtualProperties* vp = ptr;
  MethodAtoms* atoms = vp->opaque;

  JS_FreeAtomRT(rt, atoms->has);
  JS_FreeAtomRT(rt, atoms->get);
  JS_FreeAtomRT(rt, atoms->set);
  JS_FreeAtomRT(rt, atoms->delete);
  JS_FreeAtomRT(rt, atoms->keys);

  js_free_rt(rt, atoms);

  vp->opaque = 0;
}

VirtualProperties
virtual_properties_map(JSContext* ctx, JSValueConst map) {
  JSValue map_prototype, map_obj;
  MethodAtoms* atoms = js_mallocz(ctx, sizeof(MethodAtoms) + sizeof(JSAtom));

  map_obj = JS_DupValue(ctx, map);
  map_prototype = js_global_prototype(ctx, "Map");

  *atoms = (MethodAtoms){
      JS_NewAtom(ctx, "has"),
      JS_NewAtom(ctx, "get"),
      JS_NewAtom(ctx, "set"),
      JS_NewAtom(ctx, "delete"),
      JS_NewAtom(ctx, "keys"),
   };

  JS_FreeValue(ctx, map_prototype);

  return (VirtualProperties){
      map_obj,
      map_has,
      map_get,
      map_set,
      map_delete,
      JS_HasProperty(ctx, map, atoms->keys) ? map_keys : 0,
      map_finalizer,
      atoms,
      atoms_dup,
  };
}

static BOOL
object_has(const VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  return js_has_property_value(ctx, vp->this_obj, prop);
}

static JSValue
object_get(const VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  return js_get_property_value(ctx, vp->this_obj, prop);
}

static int
object_set(const VirtualProperties* vp, JSContext* ctx, JSValueConst prop, JSValue value) {
  return js_set_property_value(ctx, vp->this_obj, prop, value);
}

static BOOL
object_delete(const VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  return js_delete_property_value(ctx, vp->this_obj, prop);
}

static JSValue
object_keys(const VirtualProperties* vp, JSContext* ctx, int flags) {
  JSAtom* tab_atom;
  uint32_t i, tab_atom_len;
  JSValue ret;

  if(!(tab_atom = js_object_properties(ctx, &tab_atom_len, vp->this_obj, flags)))
    return JS_ThrowInternalError(ctx, "unable to get object keys");

  ret = JS_NewArray(ctx);

  for(i = 0; i < tab_atom_len; i++)
    JS_SetPropertyUint32(ctx, ret, i, JS_AtomToValue(ctx, tab_atom[i]));

  return ret;
}

static void
object_finalizer(JSRuntime* rt, void* ptr) {
  VirtualProperties* vp = ptr;
}

VirtualProperties
virtual_properties_object(JSContext* ctx, JSValueConst obj) {
  return (VirtualProperties){
      JS_DupValue(ctx, obj),
      object_has,
      object_get,
      object_set,
      object_delete,
      object_keys,
      object_finalizer,
      0,
      0,
  };
}

typedef struct {
  JSAtom push, splice;
} ArrayMethodAtoms;

static int64_t
array_find(const VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  int64_t len;

  if((len = js_array_length(ctx, vp->this_obj)) != -1) {
    for(int64_t i = 0; i < len; i++) {
      JSValue entry = JS_GetPropertyUint32(ctx, vp->this_obj, i);
      JSValue key = JS_GetPropertyUint32(ctx, entry, 0);
      JS_FreeValue(ctx, entry);

      BOOL ret = js_value_equals(ctx, key, prop, FALSE);
      JS_FreeValue(ctx, key);

      if(ret)
        return i;
    }
  }

  return -1;
}

static BOOL
array_has(const VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  return array_find(vp, ctx, prop) != -1;
}

static JSValue
array_get(const VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  int64_t pos;

  if((pos = array_find(vp, ctx, prop)) != -1) {
    JSValue entry = JS_GetPropertyUint32(ctx, vp->this_obj, pos);
    JSValue value = JS_GetPropertyUint32(ctx, entry, 1);
    JS_FreeValue(ctx, entry);

    return value;
  }

  return JS_UNDEFINED;
}

static int
array_set(const VirtualProperties* vp, JSContext* ctx, JSValueConst prop, JSValue value) {
  JSValue entry = JS_NewArray(ctx);

  JS_SetPropertyUint32(ctx, entry, 0, JS_DupValue(ctx, prop));
  JS_SetPropertyUint32(ctx, entry, 1, value);
  JSValue ret = js_invoke(ctx, vp->this_obj, "push", 1, &entry);
  int32_t result = js_toint32(ctx, ret);
  JS_FreeValue(ctx, ret);

  return result;
}

static BOOL
array_delete(const VirtualProperties* vp, JSContext* ctx, JSValueConst prop) {
  int64_t pos;

  if((pos = array_find(vp, ctx, prop)) != -1) {
    JSValueConst args[] = {
        JS_NewInt64(ctx, pos),
        JS_NewInt32(ctx, 1),
    };
    JSValue ret = js_invoke(ctx, vp->this_obj, "splice", countof(args), args);
    BOOL result = js_array_length(ctx, ret) == 1;
    JS_FreeValue(ctx, ret);

    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    return result;
  }

  return FALSE;
}

static JSValue
array_keys(const VirtualProperties* vp, JSContext* ctx, int flags) {
  int64_t len;

  if((len = js_array_length(ctx, vp->this_obj)) != -1) {
    JSValue ret = JS_NewArray(ctx);

    for(int64_t i = 0; i < len; i++) {
      JSValue entry = JS_GetPropertyUint32(ctx, vp->this_obj, i);
      JS_SetPropertyUint32(ctx, ret, i, JS_GetPropertyUint32(ctx, entry, 0));
      JS_FreeValue(ctx, entry);
    }

    return ret;
  }

  return JS_UNDEFINED;
}

static void
array_finalizer(JSRuntime* rt, void* ptr) {
  VirtualProperties* vp = ptr;

   ArrayMethodAtoms* atoms = vp->opaque;

  JS_FreeAtomRT(rt, atoms->push);
  JS_FreeAtomRT(rt, atoms->splice);

  js_free_rt(rt, atoms);
}

VirtualProperties
virtual_properties_array(JSContext* ctx, JSValueConst obj) {
  ArrayMethodAtoms* atoms = js_mallocz(ctx, sizeof(ArrayMethodAtoms)+ sizeof(JSAtom));
 
  *atoms = (ArrayMethodAtoms){
      JS_NewAtom(ctx, "push"),
      JS_NewAtom(ctx, "splice"),
   };

  return (VirtualProperties){
      JS_DupValue(ctx, obj),
      array_has,
      array_get,
      array_set,
      array_delete,
      array_keys,
      array_finalizer,
      atoms,
      atoms_dup,
  };
}

static JSValue
virtual_properties_getset(
    JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  VirtualWrapper* vw = opaque;
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case METHOD_HAS: {
      ret = JS_NewBool(ctx, virtual_has(&vw->props, ctx, argv[0]));
      break;
    }

    case METHOD_GET: {
      ret = virtual_get(&vw->props, ctx, argv[0]);
      break;
    }

    case METHOD_SET: {
      if(virtual_set(&vw->props, ctx, argv[0], argv[1]))
        ret = JS_ThrowInternalError(ctx, "failed to set()");

      break;
    }

    case METHOD_DELETE: {
      ret = JS_NewBool(ctx, virtual_delete(&vw->props, ctx, argv[0]));
      break;
    }

    case METHOD_KEYS: {
      int32_t flags = (JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY);

      if(argc > 0)
        JS_ToInt32(ctx, &flags, argv[0]);

      ret = virtual_keys(&vw->props, ctx, flags);
      break;
    }
  }

  return ret;
}

JSValue
virtual_properties_method(VirtualProperties virt, int magic, JSContext* ctx) {
  VirtualWrapper* vw;
  JSValue obj;

  if(!(vw = wrapper_new(&virt, ctx)))
    return JS_EXCEPTION;

  obj = js_function_cclosure(ctx, virtual_properties_getset, method_arity[magic], magic, vw, wrapper_free);

  return obj;
}

JSValue
virtual_properties_wrap(VirtualProperties virt, JSContext* ctx) {
  JSValue obj;

  if(!(virt.get && virt.set))
    return JS_ThrowInternalError(ctx, "virtual property needs at least get & set methods");

  obj = JS_NewObjectProto(ctx, JS_NULL);

  if(virt.has)
    JS_DefinePropertyValueStr(ctx, obj, "has", virtual_properties_method(virt, METHOD_HAS, ctx), JS_PROP_CONFIGURABLE);

  if(virt.get)
    JS_DefinePropertyValueStr(ctx, obj, "get", virtual_properties_method(virt, METHOD_GET, ctx), JS_PROP_CONFIGURABLE);

  if(virt.set)
    JS_DefinePropertyValueStr(ctx, obj, "set", virtual_properties_method(virt, METHOD_SET, ctx), JS_PROP_CONFIGURABLE);

  if(virt.delete)
    JS_DefinePropertyValueStr(
        ctx, obj, "delete", virtual_properties_method(virt, METHOD_DELETE, ctx), JS_PROP_CONFIGURABLE);

  if(virt.keys)
    JS_DefinePropertyValueStr(
        ctx, obj, "keys", virtual_properties_method(virt, METHOD_KEYS, ctx), JS_PROP_CONFIGURABLE);

  return obj;
}

void
virtual_properties_copy(const VirtualProperties* src, VirtualProperties* dst, JSContext* ctx) {
  dst->this_obj = JS_DupValue(ctx, src->this_obj);
  dst->has = src->has;
  dst->get = src->get;
  dst->set = src->set;
  dst->delete = src->delete;
  dst->keys = src->keys;
  dst->finalize = src->finalize;
  dst->opaque = src->opaque_dup ? src->opaque_dup(ctx, src->opaque) : src->opaque;
  dst->opaque_dup = src->opaque_dup;
}

VirtualProperties
virtual_properties(JSContext* ctx, JSValueConst value) {
  if(js_is_array(ctx, value))
    return virtual_properties_array(ctx, value);

  if(js_is_map(ctx, value))
    return virtual_properties_map(ctx, value);

  if(js_is_object(ctx, value))
    return virtual_properties_object(ctx, value);

  JS_ThrowTypeError(ctx, "argument must be Array, Map-like or plain Object");
  return VIRTUAL_PROPERTIES_INIT();
}

void
virtual_properties_free_rt(VirtualProperties* virt, JSRuntime* rt) {
  virt->finalize(rt, virt);

  JS_FreeValueRT(rt, virt->this_obj);
  virt->this_obj = JS_UNDEFINED;
}

/**
 * @}
 */
