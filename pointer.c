#define _GNU_SOURCE

#include "quickjs.h"
#include "cutils.h"
#include "libregexp.h"
#include <string.h>

JSClassID js_pointer_class_id;
JSValue pointer_proto, pointer_ctor, pointer_class;

enum pointer_methods { DEREF = 0, TO_STRING };
enum pointer_getters { PROP_PATH = 0 };

typedef struct {
  size_t n;
  JSAtom* atoms;
} Pointer;

static void
pointer_reset(Pointer* ptr, JSContext* ctx) {
  size_t i;

  for(i = 0; i < ptr->n; i++) JS_FreeAtom(ctx, ptr->atoms[i]);
  free(&ptr->atoms);
  ptr->atoms = 0;
  ptr->n = 0;
}

static void
pointer_atom_add(Pointer* ptr, JSContext* ctx, JSAtom atom) {
  ptr->atoms = realloc(ptr->atoms, (ptr->n + 1) * sizeof(JSAtom));
  ptr->atoms[ptr->n++] = atom;
}
static void
pointer_atom_dump(JSContext* ctx, JSAtom atom, DynBuf* db) {
  const char* str;

  str = JS_AtomToCString(ctx, atom);
  dbuf_putstr(db, str);
  JS_FreeCString(ctx, str);
}

static void
pointer_dump(Pointer* ptr, JSContext* ctx, DynBuf* db) {
  size_t i;

  dbuf_printf(db, "Pointer {\n  depth: %lu", ptr->n);
  dbuf_putstr(db, ",\n  atoms: ");
  
  for(i = 0; i < ptr->n; i++) {
    if(i > 0)
      dbuf_putstr(db, ", ");
    pointer_atom_dump(ctx, ptr->atoms[i], db);
  }

  dbuf_putstr(db, "\n}");
}

static JSValue
js_pointer_toarray(JSContext* ctx,Pointer* ptr) {
  size_t i;
  JSValue array = JS_NewArray(ctx);
   for(i = 0; i < ptr->n; i++) 
   JS_SetPropertyUint32(ctx, array, i, JS_AtomToValue(ctx, ptr->atoms[i]));
    return array;
}

static void
js_pointer_fromarray(JSContext* ctx,Pointer* ptr, JSValueConst array) {
  int64_t i,len;
  JSValue prop;
  JSAtom atom;
prop = JS_GetPropertyStr(ctx, array, "length");
JS_ToInt64(ctx, &len, prop);
JS_FreeValue(ctx, prop);
pointer_reset(ptr, ctx);
ptr->atoms = malloc(sizeof(JSAtom)*len);
   for(i = 0; i < len; i++) {
  prop= JS_GetPropertyUint32(ctx, array, i);
ptr->atoms [i] = JS_ValueToAtom(ctx, prop);
JS_FreeValue(ctx, prop);
}
}

static JSValue
js_pointer_ctor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
  Pointer* ptr;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  if(!(ptr = js_mallocz(ctx, sizeof(Pointer))))
    return JS_EXCEPTION;

  pointer_reset(ptr, ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_pointer_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, ptr);

  if(argc > 0) {
    size_t i;
    for(i = 0; i < argc; i++) {
      JSAtom atom;
      atom = JS_ValueToAtom(ctx, argv[i]);
      pointer_atom_add(ptr, ctx, atom);
      // JS_FreeAtom(ctx, atom);
    }
  }

  return obj;
fail:
  js_free(ctx, ptr);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_pointer_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  Pointer* ptr;
  DynBuf dbuf;
  JSValue ret;
  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_pointer_class_id)))
    return JS_EXCEPTION;

  dbuf_init(&dbuf);

  pointer_dump(ptr, ctx, &dbuf);
  dbuf_put(&dbuf, "\0", 1);
  ret = JS_NewStringLen(ctx, dbuf.buf, dbuf.size - 1);
  dbuf_free(&dbuf);

  return ret;
}

static JSValue
js_pointer_deref(JSContext* ctx, Pointer* ptr, JSValueConst this_arg, JSValueConst obj) {
  size_t i;
  for(i = 0; i < ptr->n; i++) {
    JSAtom atom = ptr->atoms[i];

    obj = JS_GetProperty(ctx, obj, atom);

    if(JS_IsException(obj))
      break;
  }
  return obj;
}

static JSValue
js_pointer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  Pointer* ptr;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_pointer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case DEREF: return js_pointer_deref(ctx, ptr, this_val, argv[0]);
  }
  return JS_EXCEPTION;
}

static JSValue
js_pointer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Pointer* ptr;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_pointer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_PATH: {
return js_pointer_toarray(ctx, ptr);
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_pointer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Pointer* ptr;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_pointer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_PATH: {
js_pointer_fromarray(ctx, ptr, value);
      break;
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_pointer_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  return JS_UNDEFINED;
}

void
js_pointer_finalizer(JSRuntime* rt, JSValue val) {
  Pointer* ptr = JS_GetOpaque(val, js_pointer_class_id);
  if(ptr) {

    if(ptr->atoms) {
      uint32_t i;
      for(i = 0; i < ptr->n; i++) JS_FreeAtomRT(rt, ptr->atoms[i]);
      free(ptr->atoms);
    }
    js_free_rt(rt, ptr);
  }

  // JS_FreeValueRT(rt, val);
}

JSClassDef js_pointer_class = {
    .class_name = "Pointer",
    .finalizer = js_pointer_finalizer,
};

static const JSCFunctionListEntry js_pointer_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("deref", 1, js_pointer_method, DEREF),
    JS_CGETSET_MAGIC_DEF("path", js_pointer_get, js_pointer_set, PROP_PATH),
    JS_CFUNC_DEF("toString", 0, js_pointer_tostring),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Pointer", JS_PROP_CONFIGURABLE)};

static const JSCFunctionListEntry js_pointer_static_funcs[] = {JS_CFUNC_MAGIC_DEF("from", 1, js_pointer_funcs, 0)};

static int
js_pointer_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_pointer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_pointer_class_id, &js_pointer_class);

  pointer_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, pointer_proto, js_pointer_proto_funcs, countof(js_pointer_proto_funcs));
  JS_SetClassProto(ctx, js_pointer_class_id, pointer_proto);

  pointer_class = JS_NewCFunction2(ctx, js_pointer_ctor, "Pointer", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, pointer_class, pointer_proto);
  JS_SetPropertyFunctionList(ctx, pointer_class, js_pointer_static_funcs, countof(js_pointer_static_funcs));

  if(m)
    JS_SetModuleExport(ctx, m, "Pointer", pointer_class);

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_pointer
#endif

JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, &js_pointer_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "Pointer");
  return m;
}
