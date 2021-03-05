#define _GNU_SOURCE

#include "cutils.h"
#include "property-enumeration.h"
#include "quickjs.h"
#include "utils.h"
#include "pointer.h"
#include "virtual-properties.h"

#include <stdint.h>

static JSValue
js_deep_find(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue ret = JS_UNDEFINED;
  JSValueConst this_arg = argc > 2 ? argv[2] : JS_UNDEFINED;
  PropertyEnumeration* it;
  vector frames;

  if(!JS_IsFunction(ctx, argv[1]))
    return JS_ThrowTypeError(ctx, "argument 1 (predicate) is not a function");
  vector_init(&frames);

  it = property_enumeration_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);
  while((it = property_enumeration_recurse(&frames, ctx))) {
    BOOL result = property_enumeration_predicate(it, ctx, argv[1], this_arg);
    if(result) {
      ret = property_enumeration_value(it, ctx);
      break;
    }
  }
  property_enumeration_free(&frames, JS_GetRuntime(ctx));
  return ret;
}

static JSValue
js_deep_get(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Pointer* ptr;
  JSValue ret;
  if(!(ptr = pointer_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  pointer_from(ptr, ctx, argv[1], 0);
  ret = pointer_deref(ptr, ctx, argv[0]);
  pointer_free(ptr, ctx);
  return ret;
}

static JSValue
js_deep_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Pointer* ptr;
  JSValue obj;
  JSAtom prop;
  if(!(ptr = pointer_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  pointer_from(ptr, ctx, argv[1], 0);
  prop = pointer_pop(ptr);
  obj = pointer_acquire(ptr, ctx, argv[0]);

  if(!JS_IsException(obj))
    JS_SetProperty(ctx, obj, prop, argv[2]);
  JS_FreeAtom(ctx, prop);
  pointer_free(ptr, ctx);
  // return JS_UNDEFINED;
  return JS_DupValue(ctx, obj);
}

static JSValue
js_deep_unset(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Pointer* ptr;
  JSValue obj;
  JSAtom prop;
  if(!(ptr = pointer_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  pointer_from(ptr, ctx, argv[1], 0);
  prop = pointer_pop(ptr);
  obj = pointer_deref(ptr, ctx, argv[0]);

  if(!JS_IsException(obj))
    JS_DeleteProperty(ctx, obj, prop, 0);

  JS_FreeAtom(ctx, prop);
  pointer_free(ptr, ctx);
  return JS_DupValue(ctx, obj);
}

static JSValue
js_deep_flatten(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  JSValue value, path, ret = JS_UNDEFINED;
  JSValueConst this_arg, dest;
  PropertyEnumeration* it;
  vector frames, offsets;
  BOOL result;
  // DynBuf dbuf;
  int32_t level, prev;
  uint32_t mask = 0;
  VirtualProperties vmap;
  this_arg = argc > 2 ? argv[2] : JS_UNDEFINED;
  dest = argc > 1 ? argv[1] : JS_NewObject(ctx);

  if(js_is_map(ctx, dest))
    vmap = virtual_properties_map(ctx, dest);
  else
    vmap = virtual_properties_object(ctx, dest);

  /// dbuf_init(&dbuf);
  vector_init(&frames);
  vector_init(&offsets);
  property_enumeration_push(&frames, ctx, JS_DupValue(ctx, argv[0]), PROPENUM_DEFAULT_FLAGS);
  prev = 0;

  if(argc > 2)
    JS_ToUint32(ctx, &mask, argv[2]);

  while((it = property_enumeration_recurse(&frames, ctx))) {
    // const char* key;
    // size_t len;
    int dir, idx;
    // key = property_enumeration_keystr(it, ctx);
    path = property_enumeration_pathstr(&frames, ctx);
    // len = strlen(key);
    level = property_enumeration_level(it, &frames);
    dir = level - prev;
    //   if(dir <= 0) {for(idx = dir == 0 ? 0 : dir + 1; idx <= 0; idx++) dbuf_token_pop(&dbuf, '.'); }
    // if(dir >= 0) {dbuf_token_push(&dbuf, key, len, '.'); }
    ///  dbuf_0(&dbuf);
    // printf("level = %-2u  prev = %-2u  dir = %-2" PRId32 "  key = %-8s  dbuf = %s\n", level, prev, dir,
    // key, dbuf.buf);
    // JS_FreeCString(ctx, key);
    prev = level;
    if(mask) {
      JSValue value = property_enumeration_value(it, ctx);
      int32_t type = js_value_type(value);
      JS_FreeValue(ctx, value);
      if((mask & (1 << type)) == 0)
        continue;
    }
    value = property_enumeration_value(it, ctx);
    // dbuf_0(&dbuf);
    virtual_properties_set(&vmap, ctx, path, value);
    JS_FreeValue(ctx, value);
    JS_FreeValue(ctx, path);
  }
  property_enumeration_free(&frames, JS_GetRuntime(ctx));
  // dbuf_free(&dbuf);
  ret = vmap.this_obj;
  virtual_properties_free(&vmap, ctx);
  return ret;
}

static const JSCFunctionListEntry js_deep_funcs[] = {
    JS_CFUNC_DEF("find", 2, js_deep_find),
    JS_CFUNC_DEF("get", 2, js_deep_get),
    JS_CFUNC_DEF("set", 3, js_deep_set),
    JS_CFUNC_DEF("unset", 2, js_deep_unset),
    JS_CFUNC_DEF("flatten", 1, js_deep_flatten),
    JS_PROP_INT32_DEF("MASK_UNDEFINED", MASK_UNDEFINED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_NULL", MASK_NULL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_BOOL", MASK_BOOL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_INT", MASK_INT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_OBJECT", MASK_OBJECT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_STRING", MASK_STRING, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_SYMBOL", MASK_SYMBOL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_BIG_FLOAT", MASK_BIG_FLOAT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_BIG_INT", MASK_BIG_INT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_BIG_DECIMAL", MASK_BIG_DECIMAL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_FLOAT64", MASK_FLOAT64, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_NUMBER", MASK_NUMBER, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_ALL", MASK_ALL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_PRIMITIVE", MASK_PRIMITIVE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_ARRAY", MASK_ARRAY, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MASK_FUNCTION", MASK_FUNCTION, JS_PROP_ENUMERABLE)};

static int
js_deep_init(JSContext* ctx, JSModuleDef* m) {
  JS_SetModuleExportList(ctx, m, js_deep_funcs, countof(js_deep_funcs));
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_deep
#endif

JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_deep_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_deep_funcs, countof(js_deep_funcs));
  JS_AddModuleExport(ctx, m, "default");
  return m;
}
