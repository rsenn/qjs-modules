#include "defines.h"
#include "quickjs-pointer.h"
#include "buffer-utils.h"
#include "utils.h"
#include <string.h>
#include "debug.h"

/**
 * \addtogroup quickjs-pointer
 * @{
 */

JSClassID js_pointer_class_id = 0;
static JSValue pointer_proto, pointer_ctor;

enum {
  STATIC_FROM = 0,
  STATIC_FROM_ATOMS,
  STATIC_OF,
  STATIC_OF_ATOMS,
  STATIC_IS_POINTER,
};

typedef struct {
  Pointer *ptr, *alloc;
} PointerAlloc;

static PointerAlloc
js_pointer_read(JSContext* ctx, JSValueConst value) {
  Pointer *ret, *alloc = 0;
  if(!(ret = js_pointer_data(value))) {
    ret = alloc = pointer_new(ctx);
    pointer_from(ret, value, ctx);
  }
  return (PointerAlloc){ret, alloc};
}

static JSValue
js_pointer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj, proto;
  Pointer* ptr;

  if(!(ptr = pointer_new(ctx)))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, pointer_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_pointer_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  for(int i = 0; i < argc; i++)
    if(!pointer_from(ptr, argv[i], ctx)) {
      JS_ThrowTypeError(ctx, "Pointer: argument %d unknown type", i);
      goto fail;
    }

  JS_SetOpaque(obj, ptr);

  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_pointer_wrap(JSContext* ctx, Pointer* ptr) {
  JSValue obj = JS_NewObjectProtoClass(ctx, pointer_proto, js_pointer_class_id);

  JS_SetOpaque(obj, ptr);
  return obj;
}

static JSValue
js_pointer_serialize(JSContext* ctx, JSValueConst this_val) {
  Pointer* ptr;
  DynBuf dbuf;
  JSValue ret;

  if(!(ptr = js_pointer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  js_dbuf_init(ctx, &dbuf);
  Writer wr = writer_from_dynbuf(&dbuf);
  pointer_serialize(ptr, &wr, ctx);
  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
  return ret;
}

static JSValue
js_pointer_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  DynBuf dbuf;
  BOOL color = FALSE, reparseable = FALSE;
  Pointer* ptr = js_pointer_data(this_val);

  if(argc > 1 && JS_IsObject(argv[1])) {
    color = js_get_propertystr_bool(ctx, argv[1], "colors");
    reparseable = js_get_propertystr_bool(ctx, argv[1], "reparseable");
  }

  js_dbuf_init(ctx, &dbuf);
  Writer wr = writer_from_dynbuf(&dbuf);

  if(reparseable)
    writer_puts(&wr, "new Pointer('");

  if(ptr)
    pointer_dump(ptr, &wr, color && !reparseable, -1, ctx);

  if(reparseable)
    writer_puts(&wr, "')");

  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
  return ret;
}

enum {
  METHOD_DEREF,
  METHOD_TO_STRING,
  METHOD_TO_ARRAY,
  METHOD_INSPECT,
  METHOD_SHIFT,
  METHOD_UNSHIFT,
  METHOD_PUSH,
  METHOD_POP,
  METHOD_VALUES,
  METHOD_HIER,
  METHOD_AT,
};

static JSValue
js_pointer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Pointer* ptr;

  if(!(ptr = js_pointer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_DEREF: {
      return pointer_deref(ptr, argv[0], ctx);
    }

    case METHOD_TO_STRING: {
      return js_pointer_serialize(ctx, this_val);
    }

    case METHOD_TO_ARRAY: {
      return pointer_toarray(ptr, ctx);
    }

    case METHOD_VALUES: {
      JSValue array = pointer_toarray(ptr, ctx);
      JSValue iter = js_iterator_new(ctx, array);
      JS_FreeValue(ctx, array);
      return iter;
    }

    case METHOD_SHIFT: {
      return pointer_shift(ptr, ctx);
    }

    case METHOD_UNSHIFT: {
      int i;

      for(i = argc - 1; i >= 0; i--)
        if(!pointer_unshift(ptr, argv[i], ctx))
          return JS_ThrowInternalError(ctx, "argument %d unshift() failed", i);

      return JS_DupValue(ctx, this_val);
    }

    case METHOD_POP: {
      return pointer_pop(ptr, ctx);
    }

    case METHOD_PUSH: {
      int i;

      for(i = 0; i < argc; i++)
        if(!pointer_push(ptr, argv[i], ctx))
          return JS_ThrowInternalError(ctx, "argument %d push() failed", i);

      return JS_DupValue(ctx, this_val);
    }

    case METHOD_HIER: {
      JSValue ret = JS_NewArray(ctx);
      size_t i, j = 0;

      for(i = 1; i <= ptr->n; i++) {
        Pointer* h = pointer_slice(ptr, 0, i, ctx);
        JSValue value = js_pointer_wrap(ctx, h);

        if(argc >= 1) {
          JSValue ret = JS_Call(ctx, argv[0], JS_UNDEFINED, 1, &value);
          JS_FreeValue(ctx, value);
          value = ret;
        }

        JS_SetPropertyUint32(ctx, ret, j++, value);
      }

      return ret;
    }

    case METHOD_AT: {
      JSValue ret = JS_UNDEFINED;

      if(ptr->n > 0) {
        int32_t index = -1;

        if(!JS_ToInt32(ctx, &index, argv[0]))
          ret = JS_AtomToValue(ctx, pointer_at(ptr, index));
      }

      return ret;
    }
  }

  return JS_EXCEPTION;
}

enum {
  METHOD_CONCAT,
  METHOD_SLICE,
  METHOD_SPLICE,
  METHOD_UP,
  METHOD_DOWN,
};

static JSValue
js_pointer_method1(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Pointer *ptr, *ret = 0;

  if(!(ptr = js_pointer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_SLICE: {
      int64_t s = js_int64_default(ctx, argv[0], 0);
      int64_t e = js_int64_default(ctx, argv[1], INT64_MAX);

      ret = pointer_slice(ptr, s, e, ctx);
      break;
    }

    case METHOD_SPLICE: {
      int64_t s, l;

      if((s = js_int64_default(ctx, argv[0], 0)) < 0)
        s = MOD_NUM(s, (int64_t)ptr->n);

      if((l = js_int64_default(ctx, argv[1], ptr->n - s)) < 0)
        l = MOD_NUM(l, (int64_t)ptr->n) - s;

      JSAtom* atoms = 0;

      if(argc > 2)
        atoms = js_argv_to_atoms(ctx, argc - 2, argv + 2);

      ret = pointer_splice(ptr, s, s + l, atoms, atoms ? argc - 2 : 0, ctx);

      if(atoms)
        js_free(ctx, atoms);

      break;
    }

    case METHOD_UP: {
      int64_t n = js_int64_default(ctx, argv[0], -1);

      ret = pointer_slice(ptr, 0, n < 0 ? ptr->n + n : n, ctx);
      break;
    }

    case METHOD_DOWN: {
      if((ret = pointer_clone(ptr, ctx))) {
        int i;

        for(i = 0; i < argc; i++)
          pointer_push(ret, argv[i], ctx);
      }
      break;
    }

    case METHOD_CONCAT: {
      if((ret = pointer_clone(ptr, ctx))) {
        if(!pointer_append(ret, argc, argv, ctx)) {
          ret = 0;
          break;
        }
      }

      break;
    }
  }

  return ret ? js_pointer_wrap(ctx, ret) : JS_EXCEPTION;
}

enum {
  METHOD_EQUAL,
  METHOD_STARTSWITH,
  METHOD_ENDSWITH,
  METHOD_COMPARE,
  METHOD_COMMON,
  METHOD_RELATIVETO,
};

static JSValue
js_pointer_method2(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Pointer* ptr = 0;
  PointerAlloc other;
  JSValue ret = JS_UNDEFINED;

  if(!(ptr = js_pointer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  other = js_pointer_read(ctx, argv[0]);

  switch(magic) {
    case METHOD_EQUAL: {
      ret = JS_NewBool(ctx, pointer_equal(ptr, other.ptr));
      break;
    }
    case METHOD_STARTSWITH: {
      ret = JS_NewBool(ctx, pointer_startswith(ptr, other.ptr));
      break;
    }
    case METHOD_ENDSWITH: {
      ret = JS_NewBool(ctx, pointer_endswith(ptr, other.ptr));
      break;
    }
    case METHOD_COMPARE: {
      int32_t aoffs = 0, boffs = 0;
      uint32_t len = MIN_NUM(ptr->n, other.ptr->n);

      if(argc > 1)
        JS_ToInt32(ctx, &aoffs, argv[1]);

      if(argc > 2)
        JS_ToInt32(ctx, &boffs, argv[2]);

      if(argc > 3)
        JS_ToUint32(ctx, &len, argv[3]);

      ret = JS_NewInt32(ctx, pointer_compare(ptr, other.ptr, aoffs, boffs, len));
      break;
    }
    case METHOD_COMMON: {
      int r = pointer_compare(ptr, other.ptr, 0, 0, MIN_NUM(ptr->n, other.ptr->n));
      ret = JS_NewUint32(ctx, r < 0 ? -r - 1 : r);
      break;
    }
    case METHOD_RELATIVETO: {
      if(!pointer_startswith(ptr, other.ptr))
        return JS_ThrowInternalError(ctx, "path doesn't start with argument 1 path");
      uint32_t offset = other.ptr->n;
      Pointer* ptr2;
      if((ptr2 = pointer_slice(ptr, offset, ptr->n, ctx)))
        ret = js_pointer_wrap(ctx, ptr2);
      break;
    }
  }

  if(other.alloc)
    pointer_free(other.alloc, JS_GetRuntime(ctx));

  return ret;
}

enum {
  PROP_LENGTH = 0,
  PROP_PATH,
  PROP_ATOMS,
};

static JSValue
js_pointer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Pointer* ptr;
  JSValue ret = JS_UNDEFINED;

  if(!(ptr = js_pointer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_LENGTH: {
      ret = JS_NewUint32(ctx, ptr->n);
      break;
    }

    case PROP_PATH: {
      ret = pointer_toarray(ptr, ctx);
      break;
    }

    case PROP_ATOMS: {
      ret = pointer_uint32array(ptr, ctx);
      break;
    }
  }

  return ret;
}

static JSValue
js_pointer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Pointer* ptr;
  JSValue ret = JS_UNDEFINED;

  if(!(ptr = js_pointer_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_PATH: {
      pointer_reset(ptr, JS_GetRuntime(ctx));
      pointer_fromiterable(ptr, value, ctx);
      break;
    }

    case PROP_ATOMS: {
      int32_t* intv;
      size_t vlen;

      if((intv = js_array_to_int32v(ctx, &vlen, value))) {
        pointer_reset(ptr, JS_GetRuntime(ctx));
        ptr->atoms = (JSAtom*)intv;
        ptr->n = vlen;
      }

      break;
    }
  }

  return ret;
}

static JSValue
js_pointer_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_EXCEPTION;

  switch(magic) {
    case STATIC_FROM: {
      ret = js_pointer_constructor(ctx, pointer_ctor, argc, argv);
      break;
    }

    case STATIC_FROM_ATOMS: {
      Pointer* ptr;

      if((ptr = pointer_new(ctx))) {
        if((ptr->atoms = (JSAtom*)js_array_to_int32v(ctx, &ptr->n, argv[0])))
          ret = js_pointer_wrap(ctx, ptr);
        else
          pointer_free(ptr, JS_GetRuntime(ctx));
      }

      break;
    }

    case STATIC_OF: {
      int i;
      Pointer* ptr;

      if(!(ptr = pointer_new(ctx)))
        return JS_EXCEPTION;

      ret = js_pointer_wrap(ctx, ptr);

      for(i = 0; i < argc; i++)
        pointer_push(ptr, argv[i], ctx);

      break;
    }
      /* case STATIC_COMMON: {
         for(int i = 0; i < argc; i++){

         }

         break;
       }*/

    case STATIC_OF_ATOMS: {
      Pointer* ptr;

      if((ptr = pointer_new(ctx))) {
        int32_t* intv;

        if((intv = js_argv_to_int32v(ctx, argc, argv))) {
          ptr->atoms = (JSAtom*)intv;
          ptr->n = argc;
          ret = js_pointer_wrap(ctx, ptr);
        } else {
          pointer_free(ptr, JS_GetRuntime(ctx));
        }
      }

      break;
    }

    case STATIC_IS_POINTER: {
      Pointer* ptr = js_pointer_data(argv[0]);
      ret = JS_NewBool(ctx, !!ptr);
      break;
    }
  }

  return ret;
}

static void
js_pointer_finalizer(JSRuntime* rt, JSValue val) {
  Pointer* ptr;

  if((ptr = js_pointer_data(val)))
    pointer_free(ptr, rt);
}

static const JSCFunctionListEntry js_pointer_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("deref", 1, js_pointer_method, METHOD_DEREF),
    JS_CFUNC_MAGIC_DEF("toString", 0, js_pointer_method, METHOD_TO_STRING),
    JS_CFUNC_MAGIC_DEF("toArray", 0, js_pointer_method, METHOD_TO_ARRAY),
    JS_CFUNC_MAGIC_DEF("shift", 0, js_pointer_method, METHOD_SHIFT),
    JS_CFUNC_MAGIC_DEF("unshift", 1, js_pointer_method, METHOD_UNSHIFT),
    JS_CFUNC_MAGIC_DEF("pop", 0, js_pointer_method, METHOD_POP),
    JS_CFUNC_MAGIC_DEF("push", 1, js_pointer_method, METHOD_PUSH),

    JS_CFUNC_MAGIC_DEF("values", 0, js_pointer_method, METHOD_VALUES),
    JS_CFUNC_MAGIC_DEF("hier", 0, js_pointer_method, METHOD_HIER),
    JS_CFUNC_MAGIC_DEF("at", 1, js_pointer_method, METHOD_AT),

    JS_CFUNC_MAGIC_DEF("concat", 1, js_pointer_method1, METHOD_CONCAT),
    JS_CFUNC_MAGIC_DEF("slice", 0, js_pointer_method1, METHOD_SLICE),
    JS_CFUNC_MAGIC_DEF("splice", 0, js_pointer_method1, METHOD_SPLICE),
    JS_CFUNC_MAGIC_DEF("up", 1, js_pointer_method1, METHOD_UP),
    JS_CFUNC_MAGIC_DEF("down", 0, js_pointer_method1, METHOD_DOWN),
    JS_ALIAS_DEF("truncate", "up"),
    JS_CFUNC_MAGIC_DEF("equal", 1, js_pointer_method2, METHOD_EQUAL),
    JS_CFUNC_MAGIC_DEF("compare", 1, js_pointer_method2, METHOD_COMPARE),
    JS_CFUNC_MAGIC_DEF("common", 1, js_pointer_method2, METHOD_COMMON),
    JS_CFUNC_MAGIC_DEF("relativeTo", 1, js_pointer_method2, METHOD_RELATIVETO),
    JS_CFUNC_MAGIC_DEF("startsWith", 1, js_pointer_method2, METHOD_STARTSWITH),
    JS_CFUNC_MAGIC_DEF("endsWith", 1, js_pointer_method2, METHOD_ENDSWITH),
    JS_ALIAS_DEF("toPrimitive", "toString"),
    JS_ALIAS_DEF("[Symbol.iterator]", "values"),
    JS_CGETSET_MAGIC_DEF("length", js_pointer_get, 0, PROP_LENGTH),
    JS_CGETSET_MAGIC_DEF("path", js_pointer_get, js_pointer_set, PROP_PATH),
    JS_CGETSET_MAGIC_DEF("atoms", js_pointer_get, 0, PROP_ATOMS),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Pointer", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_pointer_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("from", 1, js_pointer_funcs, STATIC_FROM),
    JS_CFUNC_MAGIC_DEF("fromAtoms", 1, js_pointer_funcs, STATIC_FROM_ATOMS),
    JS_CFUNC_MAGIC_DEF("of", 0, js_pointer_funcs, STATIC_OF),
    JS_CFUNC_MAGIC_DEF("ofAtoms", 0, js_pointer_funcs, STATIC_OF_ATOMS),
    JS_CFUNC_MAGIC_DEF("isPointer", 1, js_pointer_funcs, STATIC_IS_POINTER),
};

static int
js_pointer_get_own_property(JSContext* ctx, JSPropertyDescriptor* pdesc, JSValueConst obj, JSAtom prop) {
  Pointer* pointer = js_pointer_data2(ctx, obj);
  int64_t index;

  if(JS_HasProperty(ctx, pointer_proto, prop))
    return FALSE;

  if(js_atom_is_symbol(ctx, prop))
    return FALSE;

  if(js_atom_is_index(ctx, &index, prop)) {

    if(index < 0)
      index = POINTER_INDEX(pointer, index);

    if(POINTER_INRANGE(pointer, index)) {
      JSValue value = JS_UNDEFINED;
      JSAtom* key_p;

      if((key_p = pointer_ptr(pointer, index)))
        value = JS_AtomToValue(ctx, *key_p);

      if(pdesc) {
        pdesc->flags = JS_PROP_ENUMERABLE;
        pdesc->value = value;
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
      }

      return TRUE;
    }
  }

  return FALSE;
}

static int
js_pointer_get_own_property_names(JSContext* ctx, JSPropertyEnum** ptab, uint32_t* plen, JSValueConst obj) {
  Pointer* pointer;

  if((pointer = js_pointer_data2(ctx, obj))) {
    uint32_t i, len = pointer->n;
    JSPropertyEnum* props;

    if((props = js_malloc(ctx, sizeof(JSPropertyEnum) * len))) {

      for(i = 0; i < len; i++) {
        props[i].is_enumerable = TRUE;
        props[i].atom = JS_ATOM_FROMINT(i);
      }

      *ptab = props;
      *plen = len;
    }
  }

  return 0;
}

static int
js_pointer_set_property(
    JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst value, JSValueConst receiver, int flags) {
  Pointer* pointer;

  if((pointer = js_pointer_data2(ctx, obj))) {
    int64_t index;

    if(js_atom_is_index(ctx, &index, prop)) {
      if(index == (int64_t)pointer->n) {
        pointer_push(pointer, value, ctx);
      } else {
        index = POINTER_INDEX(pointer, index);
        assert(POINTER_INRANGE(pointer, index));

        JS_FreeAtom(ctx, pointer->atoms[index]);
        pointer->atoms[index] = JS_ValueToAtom(ctx, value);
      }

      return TRUE;
    }
  }

  return FALSE;
}

static JSClassExoticMethods js_pointer_exotic_methods = {
    .get_own_property = js_pointer_get_own_property,
    .get_own_property_names = js_pointer_get_own_property_names,
    .set_property = js_pointer_set_property,
};

static JSClassDef js_pointer_class = {
    .class_name = "Pointer",
    .finalizer = js_pointer_finalizer,
    .exotic = &js_pointer_exotic_methods,
};

static int
js_pointer_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_pointer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_pointer_class_id, &js_pointer_class);

  pointer_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, pointer_proto, js_pointer_proto_funcs, countof(js_pointer_proto_funcs));

  JSValue array_proto = js_global_prototype(ctx, "Array");

  JS_DefinePropertyValueStr(
      ctx, pointer_proto, "map", JS_GetPropertyStr(ctx, array_proto, "map"), JS_PROP_CONFIGURABLE);
  JS_DefinePropertyValueStr(
      ctx, pointer_proto, "reduce", JS_GetPropertyStr(ctx, array_proto, "reduce"), JS_PROP_CONFIGURABLE);
  JS_DefinePropertyValueStr(
      ctx, pointer_proto, "forEach", JS_GetPropertyStr(ctx, array_proto, "forEach"), JS_PROP_CONFIGURABLE);
  JS_DefinePropertyValueStr(
      ctx, pointer_proto, "keys", JS_GetPropertyStr(ctx, array_proto, "keys"), JS_PROP_CONFIGURABLE);

  JS_FreeValue(ctx, array_proto);

  js_set_inspect_method(ctx, pointer_proto, js_pointer_inspect);

  pointer_ctor = JS_NewCFunction2(ctx, js_pointer_constructor, "Pointer", 1, JS_CFUNC_constructor, 0);

  JS_SetPropertyFunctionList(ctx, pointer_ctor, js_pointer_static_funcs, countof(js_pointer_static_funcs));

  JS_SetClassProto(ctx, js_pointer_class_id, pointer_proto);
  JS_SetConstructor(ctx, pointer_ctor, pointer_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "Pointer", pointer_ctor);

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_pointer
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_pointer_init)))
    JS_AddModuleExport(ctx, m, "Pointer");

  return m;
}

/**
 * @}
 */
