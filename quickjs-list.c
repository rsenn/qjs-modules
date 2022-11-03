#include "defines.h"
#include "quickjs-list.h"
#include "buffer-utils.h"
#include "utils.h"
#include <string.h>
#include "debug.h"

/**
 * \addtogroup quickjs-list
 * @{
 */


thread_local VISIBLE JSClassID js_list_class_id = 0;
thread_local JSValue list_proto = {{JS_TAG_UNDEFINED}}, list_ctor = {{JS_TAG_UNDEFINED}};

enum {
  METHOD_DEREF = 0,
  METHOD_TO_STRING,
  METHOD_TO_ARRAY,
  METHOD_INSPECT,
  METHOD_SHIFT,
  METHOD_PUSH,
  METHOD_CONCAT,
  METHOD_SLICE,
  METHOD_UP,
  METHOD_DOWN,
  METHOD_KEYS,
  METHOD_VALUES,
  METHOD_HIER,
};
enum {
  STATIC_FROM = 0,
  STATIC_FROM_ATOMS,
  STATIC_OF,
  STATIC_OF_ATOMS,
  STATIC_IS_LIST,
};
enum {
  PROP_LENGTH = 0,
  PROP_PATH,
  PROP_ATOMS,
};

VISIBLE JSValue
js_list_new(JSContext* ctx, JSValueConst proto, JSValueConst value) {
  List* list;
  JSValue obj = JS_UNDEFINED;

  if(!(list = js_mallocz(ctx, sizeof(List))))
    return JS_EXCEPTION;

init_list_head(&list->head);

  obj = JS_NewObjectProtoClass(ctx, proto, js_list_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, list);

  /*if(!list_from(list, ctx, value)) {
    JS_FreeValue(ctx, obj);
    obj = JS_ThrowTypeError(ctx, "List: argument 1 unknown type");
  }*/

  return obj;
fail:
  js_free(ctx, list);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

VISIBLE JSValue
js_list_wrap(JSContext* ctx, List* list) {
  JSValue obj;

  obj = JS_NewObjectProtoClass(ctx, list_proto, js_list_class_id);

  JS_SetOpaque(obj, list);

  return obj;
}
 
/*static JSValue
js_list_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  List* list;
  DynBuf dbuf;
  BOOL color = FALSE;

  if(!(list = JS_GetOpaque2(ctx, this_val, js_list_class_id)))
    return JS_EXCEPTION;

  if(argc > 1 && JS_IsObject(argv[1])) {
    color = js_get_propertystr_bool(ctx, argv[1], "colors");
  }

  js_dbuf_init(ctx, &dbuf);
  list_dump(list, ctx, &dbuf, color, -1);
  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
  return ret;
}*/

static JSValue
js_list_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, list_proto);

  return js_list_new(ctx, proto, argc > 0 ? argv[0] : JS_UNDEFINED);
}

/*static JSValue
js_list_deref(JSContext* ctx, List* list, JSValueConst this_arg, JSValueConst arg) {
  return list_deref(list, ctx, arg);
}*/

static JSValue
js_list_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  List* list;

  if(!(list = js_list_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
   
  }
  return JS_EXCEPTION;
}

static JSValue
js_list_get(JSContext* ctx, JSValueConst this_val, int magic) {
  List* list;
  JSValue ret = JS_UNDEFINED;

  if(!(list = js_list_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_LENGTH: {
      ret = JS_NewUint32(ctx, list->size);
      break;
    }
 
  }
  return ret;
}

static JSValue
js_list_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  List* list;
  JSValue ret = JS_UNDEFINED;
  if(!(list = js_list_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
   
  }
  return ret;
}

static JSValue
js_list_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret;

  switch(magic) {
  
  }
  return ret;
}

static void
js_list_finalizer(JSRuntime* rt, JSValue val) {
  List* list;

  if((list = js_list_data(val))) {
    js_free_rt(rt, list);
  }
  // JS_FreeValueRT(rt, val);
}

static const JSCFunctionListEntry js_list_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("deref", 1, js_list_method, METHOD_DEREF),
    JS_CFUNC_MAGIC_DEF("toString", 0, js_list_method, METHOD_TO_STRING),
    JS_CFUNC_MAGIC_DEF("toArray", 0, js_list_method, METHOD_TO_ARRAY),
    JS_CFUNC_MAGIC_DEF("shift", 1, js_list_method, METHOD_SHIFT),
    JS_CFUNC_MAGIC_DEF("push", 1, js_list_method, METHOD_PUSH),
    JS_CFUNC_MAGIC_DEF("concat", 1, js_list_method, METHOD_CONCAT),
    JS_CFUNC_MAGIC_DEF("slice", 2, js_list_method, METHOD_SLICE),
    JS_CFUNC_MAGIC_DEF("up", 1, js_list_method, METHOD_UP),
    JS_CFUNC_MAGIC_DEF("down", 0, js_list_method, METHOD_DOWN),
    JS_CFUNC_MAGIC_DEF("keys", 0, js_list_method, METHOD_KEYS),
    JS_CFUNC_MAGIC_DEF("values", 0, js_list_method, METHOD_VALUES),
    JS_CFUNC_MAGIC_DEF("hier", 0, js_list_method, METHOD_HIER),
    JS_ALIAS_DEF("toPrimitive", "toString"),
    JS_ALIAS_DEF("[Symbol.iterator]", "keys"),
    JS_CGETSET_MAGIC_DEF("length", js_list_get, 0, PROP_LENGTH),
    JS_CGETSET_MAGIC_DEF("path", js_list_get, js_list_set, PROP_PATH),
    JS_CGETSET_MAGIC_DEF("atoms", js_list_get, 0, PROP_ATOMS),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "List", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_list_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("from", 1, js_list_funcs, STATIC_FROM),
    JS_CFUNC_MAGIC_DEF("fromAtoms", 1, js_list_funcs, STATIC_FROM_ATOMS),
    JS_CFUNC_MAGIC_DEF("of", 0, js_list_funcs, STATIC_OF),
    JS_CFUNC_MAGIC_DEF("ofAtoms", 0, js_list_funcs, STATIC_OF_ATOMS),
    JS_CFUNC_MAGIC_DEF("isList", 1, js_list_funcs, STATIC_IS_LIST),
};

static int
js_list_get_own_property(JSContext* ctx, JSPropertyDescriptor* pdesc, JSValueConst obj, JSAtom prop) {
  List* list = js_list_data2(ctx, obj);

  JSValue value = JS_UNDEFINED;
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    if(index < 0)
      index = ((index % list->size) + list->size) % list->size;

    if(index < (int64_t)list->size) {
  /*    JSAtom key = list->atoms[index];
      value = (key & (1U << 31)) ? JS_NewUint32(ctx, key & (~(1U << 31))) : JS_AtomToValue(ctx, key);

      if(pdesc) {
        pdesc->flags = JS_PROP_ENUMERABLE;
        pdesc->value = value;
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
      }*/
      return TRUE;
    }
  }
  return FALSE;
}

static int
js_list_get_own_property_names(JSContext* ctx, JSPropertyEnum** ptab, uint32_t* plen, JSValueConst obj) {
  List* list;
  uint32_t i, len;
  JSPropertyEnum* props;
  if((list = js_list_data2(ctx, obj)))
    len = list->size;
  else {
    JSValue length = JS_GetPropertyStr(ctx, obj, "length");
    JS_ToUint32(ctx, &len, length);
    JS_FreeValue(ctx, length);
  }

  props = js_malloc(ctx, sizeof(JSPropertyEnum) * (len + 1));

  for(i = 0; i < len; i++) {
    props[i].is_enumerable = TRUE;
    props[i].atom = i | (1U << 31);
  }

  props[len].is_enumerable = TRUE;
  props[len].atom = JS_NewAtom(ctx, "length");

  *ptab = props;
  *plen = len + 1;
  return 0;
}

static int
js_list_has_property(JSContext* ctx, JSValueConst obj, JSAtom prop) {
  List* list = js_list_data2(ctx, obj);
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    if(index < 0)
      index = ((index % (int64_t)(list->size + 1)) + list->size);

    if(index < (int64_t)list->size)
      return TRUE;
  } else if(js_atom_is_length(ctx, prop)) {
    return TRUE;
  } else {
    JSValue proto = JS_GetPrototype(ctx, obj);
    if(JS_IsObject(proto) && JS_HasProperty(ctx, proto, prop))
      return TRUE;
  }

  return FALSE;
}

static JSValue
js_list_get_property(JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst receiver) {
  List* list = js_list_data2(ctx, obj);
  JSValue value = JS_UNDEFINED;
  int64_t index;
  int32_t entry;

  if(js_atom_is_index(ctx, &index, prop)) {
    if(index < 0)
      index = ((index % (int64_t)(list->size + 1)) + list->size);

    if(index < (int64_t)list->size) {
/*      JSAtom key = list->atoms[index];
      value = (key & (1U << 31)) ? JS_NewUint32(ctx, key & (~(1U << 31))) : JS_AtomToValue(ctx, key);*/
    }
  } else if(js_atom_is_length(ctx, prop)) {
    value = JS_NewUint32(ctx, list->size);
  } else if((entry = js_find_cfunction_atom(ctx, js_list_proto_funcs, countof(js_list_proto_funcs), prop, JS_DEF_CGETSET_MAGIC)) >= 0) {

    // printf("entry: %d magic: %d\n", entry,
    // js_list_proto_funcs[entry].magic);
    value = js_list_get(ctx, obj, js_list_proto_funcs[entry].magic);

  } else {
    JSValue proto = JS_IsUndefined(list_proto) ? JS_GetPrototype(ctx, obj) : list_proto;
    if(JS_IsObject(proto))
      value = JS_GetProperty(ctx, proto, prop);
  }

  return value;
}

static int
js_list_set_property(JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst value, JSValueConst receiver, int flags) {
  List* list = js_list_data2(ctx, obj);
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    if(index < 0)
      index = ((index % (int64_t)(list->size + 1)) + list->size);

   /* if(index == (int64_t)list->size)
      list_push(list, ctx, value);
    else if(index < (int64_t)list->size)
      list->atoms[index] = JS_ValueToAtom(ctx, value);*/
    return TRUE;
  }

  return FALSE;
}

static JSClassExoticMethods js_list_exotic_methods = {
    .get_own_property = js_list_get_own_property,
    .get_own_property_names = js_list_get_own_property_names,
    .has_property = js_list_has_property,
    .get_property = js_list_get_property,
    .set_property = js_list_set_property,
};

static JSClassDef js_list_class = {
    .class_name = "List",
    .finalizer = js_list_finalizer,
    .exotic = &js_list_exotic_methods,
};

static int
js_list_init(JSContext* ctx, JSModuleDef* m) {
  JSAtom inspectAtom;
  JS_NewClassID(&js_list_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_list_class_id, &js_list_class);

  list_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, list_proto, js_list_proto_funcs, countof(js_list_proto_funcs));
/*
  JSValue array_proto = js_global_prototype(ctx, "Array");

  JS_DefinePropertyValueStr(ctx, list_proto, "map", JS_GetPropertyStr(ctx, array_proto, "map"), JS_PROP_CONFIGURABLE);
  JS_DefinePropertyValueStr(ctx, list_proto, "reduce", JS_GetPropertyStr(ctx, array_proto, "reduce"), JS_PROP_CONFIGURABLE);
  JS_DefinePropertyValueStr(ctx, list_proto, "forEach", JS_GetPropertyStr(ctx, array_proto, "forEach"), JS_PROP_CONFIGURABLE);
*/
 // js_set_inspect_method(ctx, list_proto, js_list_inspect);

  JS_SetClassProto(ctx, js_list_class_id, list_proto);

  list_ctor = JS_NewCFunction2(ctx, js_list_constructor, "List", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, list_ctor, list_proto);
  JS_SetPropertyFunctionList(ctx, list_ctor, js_list_static_funcs, countof(js_list_static_funcs));

  if(m) {
    JS_SetModuleExport(ctx, m, "List", list_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_list
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, &js_list_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "List");
  return m;
}

/**
 * @}
 */
