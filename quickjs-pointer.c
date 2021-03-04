#define _GNU_SOURCE

#include "quickjs.h"
#include "cutils.h"
#include "libregexp.h"
#include "utils.h"
#include "quickjs-pointer.h"
#include <string.h>

JSClassID js_pointer_class_id = 0;
JSValue pointer_proto, pointer_constructor, pointer_ctor;

enum pointer_methods { METHOD_DEREF = 0, METHOD_TO_STRING, METHOD_TO_ARRAY, METHOD_INSPECT, METHOD_SHIFT, METHOD_SLICE, METHOD_KEYS, METHOD_VALUES };
enum pointer_functions { STATIC_FROM = 0, STATIC_OF };
enum pointer_getters { PROP_LENGTH = 0, PROP_PATH };

Pointer*
pointer_new(JSContext* ctx) {
  Pointer* ptr = js_mallocz(ctx, sizeof(Pointer));
  return ptr;
}

void
pointer_reset(Pointer* ptr, JSContext* ctx) {
  size_t i;

  if(ptr->atoms) {
    for(i = 0; i < ptr->n; i++) js_atom_free(ctx, ptr->atoms[i]);
    free(ptr->atoms);
    ptr->atoms = 0;
  }
  ptr->n = 0;
}

void
pointer_truncate(Pointer* ptr, JSContext* ctx, size_t size) {
  if(size == 0) {
    pointer_reset(ptr, ctx);
    return;
  }
  if(ptr->atoms) {
    size_t i;
    for(i = ptr->n - 1; i >= size; i--) js_atom_free(ctx, ptr->atoms[i]);

    ptr->atoms = realloc(ptr->atoms, sizeof(JSAtom) * size);
    ptr->n = size;
  }
}

void
pointer_push(Pointer* ptr, JSAtom atom) {
  ptr->atoms = realloc(ptr->atoms, (ptr->n + 1) * sizeof(JSAtom));
  ptr->atoms[ptr->n++] = atom;
}

void
pointer_dump(Pointer* ptr, JSContext* ctx, DynBuf* db, BOOL color) {
  size_t i;

  dbuf_printf(db, "Pointer(%lu) ", ptr->n);

  for(i = 0; i < ptr->n; i++) {
    if(i > 0)
      dbuf_putstr(db, color ? "\x1b[1;36m.\x1b[m" : ".");
    js_atom_dump(ctx, ptr->atoms[i], db, color);
  }
}

void
pointer_tostring(Pointer* ptr, JSContext* ctx, DynBuf* db) {
  size_t i, j;
  const char* str;

  for(i = 0; i < ptr->n; i++) {
    if(i > 0)
      dbuf_putc(db, '.');

    str = JS_AtomToCString(ctx, ptr->atoms[i]);

    for(j = 0; str[j]; j++) {
      if(str[j] == '.')
        dbuf_putc(db, '\\');
      dbuf_putc(db, str[j]);
    }
  }
}

size_t
pointer_parse(Pointer* ptr, JSContext* ctx, const char* str, size_t len) {
  size_t pos, delim;
  JSAtom atom;
  while(len) {
    delim = 0;
    for(;;) {
      delim += byte_chr(&str[delim], len - delim, '.');
      if(delim < len && delim > 0 && str[delim - 1] == '\\') {
        ++delim;
        continue;
      }
      break;
    }
    atom = JS_NewAtomLen(ctx, str, delim);
    pointer_push(ptr, atom);
    str += delim;
    len -= delim;
    if(len > 0) {
      ++str;
      --len;
    }
  }
  return ptr->n;
}

Pointer*
pointer_slice(Pointer* ptr, JSContext* ctx, int64_t start, int64_t end) {
  Pointer* ret = js_mallocz(ctx, sizeof(Pointer));
  int64_t i;

  start = mod_int32(start, ptr->n);
  end = mod_int32(end, ptr->n);
  if(end == 0)
    end = ptr->n;

  ret->n = end - start;
  ret->atoms = ret->n ? malloc(sizeof(JSAtom) * ret->n) : 0;

  for(i = start; i < end; i++) ret->atoms[i - start] = js_atom_dup(ctx, ptr->atoms[i]);

  return ret;
}

JSValue
pointer_shift(Pointer* ptr, JSContext* ctx, JSValueConst obj) {
  JSValue ret;
  if(ptr->n) {
    JSAtom atom;
    int64_t i;
    atom = ptr->atoms[0];
    for(i = 1; i < ptr->n; i++) { ptr->atoms[i - 1] = ptr->atoms[i]; }
    ptr->n--;
    ret = JS_GetProperty(ctx, obj, atom);
    js_atom_free(ctx, atom);
  }
  return ret;
}

Pointer*
js_pointer_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_pointer_class_id);
}

JSValue
js_pointer_wrap(JSContext* ctx, Pointer* ptr) {
  JSValue obj;
  obj = JS_NewObjectProtoClass(ctx, pointer_proto, js_pointer_class_id);
  JS_SetOpaque(obj, ptr);
  return obj;
}

static JSValue
js_pointer_tostring(JSContext* ctx, JSValueConst this_val) {
  Pointer* ptr;
  DynBuf dbuf;
  JSValue ret;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_pointer_class_id)))
    return JS_EXCEPTION;

  dbuf_init(&dbuf);
  pointer_tostring(ptr, ctx, &dbuf);
  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
  return ret;
}

static JSValue
js_pointer_inspect(JSContext* ctx, JSValueConst this_val, BOOL color) {
  Pointer* ptr;
  DynBuf dbuf;
  JSValue ret;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_pointer_class_id)))
    return JS_EXCEPTION;

  dbuf_init(&dbuf);
  pointer_dump(ptr, ctx, &dbuf, color);
  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);
  return ret;
}

static JSValue
js_pointer_toarray(JSContext* ctx, Pointer* ptr) {
  size_t i;
  JSValue array = JS_NewArray(ctx);
  for(i = 0; i < ptr->n; i++) JS_SetPropertyUint32(ctx, array, i, JS_AtomToValue(ctx, ptr->atoms[i]));
  return array;
}

static void
js_pointer_fromarray(JSContext* ctx, Pointer* ptr, JSValueConst array) {
  int64_t i, len;
  JSValue prop;
  JSAtom atom;
  len = js_array_length(ctx, array);
  pointer_reset(ptr, ctx);

  if(len >= 0) {
    ptr->atoms = malloc(sizeof(JSAtom) * len);
    for(i = 0; i < len; i++) {
      prop = JS_GetPropertyUint32(ctx, array, i);
      ptr->atoms[i] = JS_ValueToAtom(ctx, prop);
      JS_FreeValue(ctx, prop);
    }
    ptr->n = len;
  }
}

static void
js_pointer_fromiterable(JSContext* ctx, Pointer* ptr, JSValueConst arg) {
  IteratorValue item;
  JSValue iter = js_iterator_new(ctx, arg);

  pointer_reset(ptr, ctx);

  for(;;) {
    item = js_iterator_next(ctx, iter);
    if(item.done)
      break;
    pointer_push(ptr, JS_ValueToAtom(ctx, item.value));
    JS_FreeValue(ctx, item.value);
  }
}

static JSValue
js_pointer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
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
    proto = JS_DupValue(ctx, pointer_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, js_pointer_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, ptr);
  if(argc > 0) {
    size_t i;
    if(JS_IsString(argv[0])) {
      size_t len;
      const char* str;
      str = JS_ToCStringLen(ctx, &len, argv[0]);
      pointer_parse(ptr, ctx, str, len);
      JS_FreeCString(ctx, str);

    } else if(JS_IsArray(ctx, argv[0])) {
      int64_t len;
      len = js_array_length(ctx, argv[0]);
      for(i = 0; i < len; i++) {
        JSValue arg = JS_GetPropertyUint32(ctx, argv[0], i);
        JSAtom atom;
        atom = JS_ValueToAtom(ctx, arg);
        pointer_push(ptr, atom);
        JS_FreeValue(ctx, arg);
      }
    } else {
      for(i = 0; i < argc; i++) {
        JSAtom atom;
        atom = JS_ValueToAtom(ctx, argv[i]);
        pointer_push(ptr, atom);
      }
    }
  }

  return obj;
fail:
  js_free(ctx, ptr);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_pointer_next(JSContext* ctx, Pointer* ptr, JSValueConst this_arg, JSValueConst obj) {
  size_t i;

  for(i = 0; i < ptr->n; i++) {
    JSAtom atom = ptr->atoms[i];

    obj = JS_GetProperty(ctx, obj, atom);

    if(JS_IsException(obj)) {
      DynBuf dbuf;
      dbuf_init(&dbuf);
      pointer_dump(ptr, ctx, &dbuf, TRUE);
      dbuf_0(&dbuf);
      obj = JS_ThrowReferenceError(ctx, "ptr %s", dbuf.buf);
      dbuf_free(&dbuf);
      break;
    }
  }
  return obj;
}

static JSValue
js_pointer_deref(JSContext* ctx, Pointer* ptr, JSValueConst this_arg, JSValueConst arg) {
  size_t i;
  JSValue obj = JS_DupValue(ctx, arg);

  for(i = 0; i < ptr->n; i++) {
    JSAtom atom = ptr->atoms[i];
    JSValue child = JS_GetProperty(ctx, obj, atom);
    JS_FreeValue(ctx, obj);

    if(JS_IsException(child)) {
      DynBuf dbuf;
      dbuf_init(&dbuf);

      pointer_dump(ptr, ctx, &dbuf, TRUE);
      dbuf_0(&dbuf);

      obj = JS_ThrowReferenceError(ctx, "ptr %s", dbuf.buf);
      dbuf_free(&dbuf);
      break;
    }
    obj = child;
  }
  return obj;
}

static JSValue
js_pointer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  Pointer* ptr;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_pointer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_DEREF: {
      return js_pointer_deref(ctx, ptr, this_val, argv[0]);
    }
    case METHOD_TO_STRING: {
      return js_pointer_tostring(ctx, this_val);
    }
    case METHOD_TO_ARRAY: {
      return js_pointer_toarray(ctx, ptr);
    }
    case METHOD_INSPECT: {
      return js_pointer_inspect(ctx, this_val, TRUE);
    }
    case METHOD_SLICE: {
      return js_pointer_wrap(ctx, pointer_slice(ptr, ctx, js_int64_default(ctx, argv[0], 0), js_int64_default(ctx, argv[1], 0)));
    }
    case METHOD_KEYS: {
      JSValue array = js_pointer_toarray(ctx, ptr);
      JSValue iter = js_iterator_new(ctx, array);
      JS_FreeValue(ctx, array);
      return iter;
    }
    case METHOD_VALUES: {
      JSValue array = js_pointer_toarray(ctx, ptr);
      JSValue iter = js_iterator_new(ctx, array);
      JS_FreeValue(ctx, array);
      return iter;
    }
    case METHOD_SHIFT: {
      return pointer_shift(ptr, ctx, argv[0]);
    }
  }
  return JS_EXCEPTION;
}

static JSValue
js_pointer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Pointer* ptr;

  if(!(ptr = JS_GetOpaque2(ctx, this_val, js_pointer_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_LENGTH: {
      return JS_NewInt64(ctx, ptr->n);
    }
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
      js_pointer_fromiterable(ctx, ptr, value);
      break;
    }
  }
  return JS_UNDEFINED;
}

static JSValue
js_pointer_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  JSValue ret;
  Pointer* ptr;

  if(!(ptr = pointer_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  ret = js_pointer_wrap(ctx, ptr);

  switch(magic) {
    case STATIC_FROM: {
      if(JS_IsArray(ctx, argv[0]))
        js_pointer_fromarray(ctx, ptr, argv[0]);
      else
        js_pointer_fromiterable(ctx, ptr, argv[0]);
      break;
    }
    case STATIC_OF: {
      int i;
      for(i = 0; i < argc; i++) {
        JSAtom atom = JS_ValueToAtom(ctx, argv[i]);
        pointer_push(ptr, atom);
      }
      break;
    }
  }
  return ret;
}

void
js_pointer_finalizer(JSRuntime* rt, JSValue val) {
  Pointer* ptr;

  if((ptr = JS_GetOpaque(val, js_pointer_class_id))) {
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
static const JSCFunctionListEntry js_pointer_proto_funcs[] = {JS_CFUNC_MAGIC_DEF("deref", 1, js_pointer_method, METHOD_DEREF),
                                                              JS_CFUNC_MAGIC_DEF("toString", 0, js_pointer_method, METHOD_TO_STRING),
                                                              JS_CFUNC_MAGIC_DEF("toArray", 0, js_pointer_method, METHOD_TO_ARRAY),
                                                              JS_CFUNC_MAGIC_DEF("inspect", 0, js_pointer_method, METHOD_INSPECT),
                                                              JS_CFUNC_MAGIC_DEF("shift", 1, js_pointer_method, METHOD_SHIFT),
                                                              JS_CFUNC_MAGIC_DEF("slice", 2, js_pointer_method, METHOD_SLICE),
                                                              JS_CFUNC_MAGIC_DEF("keys", 0, js_pointer_method, METHOD_KEYS),
                                                              JS_CFUNC_MAGIC_DEF("values", 0, js_pointer_method, METHOD_VALUES),
                                                              JS_ALIAS_DEF("toPrimitive", "toString"),
                                                              JS_ALIAS_DEF("[Symbol.iterator]", "keys"),
                                                              JS_CGETSET_MAGIC_DEF("length", js_pointer_get, 0, PROP_LENGTH),
                                                              JS_CGETSET_MAGIC_DEF("path", js_pointer_get, js_pointer_set, PROP_PATH),
                                                              JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Pointer", JS_PROP_C_W_E)};

static const JSCFunctionListEntry js_pointer_static_funcs[] = {JS_CFUNC_MAGIC_DEF("from", 1, js_pointer_funcs, STATIC_FROM), JS_CFUNC_MAGIC_DEF("of", 0, js_pointer_funcs, STATIC_OF)};

static int
js_pointer_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_pointer_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_pointer_class_id, &js_pointer_class);

  pointer_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, pointer_proto, js_pointer_proto_funcs, countof(js_pointer_proto_funcs));
  JS_SetClassProto(ctx, js_pointer_class_id, pointer_proto);

  pointer_ctor = JS_NewCFunction2(ctx, js_pointer_constructor, "Pointer", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, pointer_ctor, pointer_proto);
  JS_SetPropertyFunctionList(ctx, pointer_ctor, js_pointer_static_funcs, countof(js_pointer_static_funcs));

  if(m) {
    JS_SetModuleExport(ctx, m, "Pointer", pointer_ctor);
  }

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
