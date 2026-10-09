/* quickjs-wasm.c: WebAssembly.{validate,Module,Instance,Memory,Table,Global}
 * and the CompileError/LinkError/RuntimeError classes, over a wasm-backend.h
 * engine (the JS API of https://webassembly.github.io/spec/js-api/).
 * depends on: wasm-backend.h (engine), wasm3-backend.c or wamr-backend.c.
 * rule: every JS object holds a strong ref to the Env that owns the engine
 * context, so the context is freed after the last object using it. */

#include <quickjs.h>
#include <cutils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "defines.h"
#include "wasm-backend.h"

#ifndef countof
#define countof(x) (sizeof(x) / sizeof((x)[0]))
#endif

static JSClassID wasm_env_class_id, wasm_module_class_id, wasm_instance_class_id, wasm_memory_class_id, wasm_table_class_id, wasm_global_class_id,
    wasm_funcref_class_id;
static JSValue wasm_errors[3]; /* JS thread only: CompileError, LinkError, RuntimeError */
static JSValue module_proto, instance_proto, memory_proto, table_proto, global_proto;
static JSAtom funcref_atom; /* JS thread only: private Symbol key of an exported function's FuncRef */

typedef struct {
  WBRuntime* rt;
  WBContext* ctx;
} Env;

typedef struct {
  JSValue env; /* owned */
  WBModule* mod;
} Module;

typedef struct {
  JSValue env;    /* owned */
  JSValue module; /* owned: the engine may read the module while instantiated */
  JSValue deps;   /* owned: array of the import objects it was linked with */
  WBInstance* inst;
} Instance;

/* refcount: `owner` keeps the engine object alive (the Instance, or the Table
 * a function was read from); `owns` is set when this wrapper holds its own
 * engine reference (a standalone object made by `new`). */
typedef struct {
  JSValue env;
  JSValue owner;
  WBFunc* func; /* owned: a WB_DupExtern() reference */
} FuncRef;

typedef struct {
  JSValue env;
  JSValue owner;
  WBMemory* mem;
  int owns;
  JSValue buffer; /* owned, JS_UNDEFINED until first read */
  uint8_t* data;
} Memory;

typedef struct {
  JSValue env;
  JSValue owner;
  WBTable* table;
  int owns;
  JSValue elems; /* owned: array of the functions stored with set(), keeps them alive */
} Table;

typedef struct {
  JSValue env;
  JSValue owner;
  WBGlobal* global;
  int owns;
} Global;

typedef struct {
  JSRuntime* rt;
  JSContext* ctx;
  JSValue fn; /* owned */
  WBFuncType type;
} HostFunc;

/* ---- errors ---- */

/* throws the pending backend exception as the matching JS error.
 * returns JS_EXCEPTION; exception pending: a host failure keeps its own. */
static JSValue
throw_backend(JSContext* ctx, Env* env) {
  WBException ex;
  WBErrorKind kind = WB_GetException(env->ctx, &ex);

  switch(kind) {
    case WB_ERR_HOST: return JS_EXCEPTION;
    case WB_ERR_COMPILE:
    case WB_ERR_LINK:
    case WB_ERR_TRAP: {
      JSValue msg = JS_NewString(ctx, ex.message);
      JSValue err = JS_CallConstructor(ctx, wasm_errors[kind - WB_ERR_COMPILE], 1, &msg);

      JS_FreeValue(ctx, msg);
      return JS_Throw(ctx, err);
    }
    case WB_ERR_TYPE:
    case WB_ERR_UNSUPPORTED: return JS_ThrowTypeError(ctx, "%s", ex.message);
    case WB_ERR_NONE: return JS_ThrowInternalError(ctx, "wasm backend failed");
    default: return JS_ThrowRangeError(ctx, "%s", ex.message);
  }
}

/* throws a WebAssembly.LinkError. returns JS_EXCEPTION. */
static JSValue
throw_link_error(JSContext* ctx, const char* msg) {
  JSValue m = JS_NewString(ctx, msg);
  JSValue err = JS_CallConstructor(ctx, wasm_errors[1], 1, &m);

  JS_FreeValue(ctx, m);
  return JS_Throw(ctx, err);
}

/* ---- Env ---- */

static void
env_finalizer(JSRuntime* rt, JSValue val) {
  Env* e = JS_GetOpaque(val, wasm_env_class_id);

  if(e) {
    if(e->ctx)
      WB_FreeContext(e->ctx);
    if(e->rt)
      WB_FreeRuntime(e->rt);
    js_free_rt(rt, e);
  }
}

static JSClassDef wasm_env_class = {"WasmEnv", .finalizer = env_finalizer};

static JSValue
env_new(JSContext* ctx) {
  Env* e;
  JSValue obj = JS_NewObjectClass(ctx, wasm_env_class_id);

  if(JS_IsException(obj))
    return obj;
  if(!(e = js_mallocz(ctx, sizeof(*e)))) {
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
  }
  JS_SetOpaque(obj, e);

  if(!(e->rt = WB_NewRuntime()) || !(e->ctx = WB_NewContext(e->rt))) {
    JS_FreeValue(ctx, obj);
    return JS_ThrowInternalError(ctx, "cannot create %s runtime", WB_GetBackendName());
  }

  return obj;
}

static Env*
env_of(JSValueConst v) {
  return JS_GetOpaque(v, wasm_env_class_id);
}

/* a lazily created Env shared by every object made from `ctx`. */
static JSValue wasm_default_env = JS_UNINITIALIZED; /* JS thread only */

static JSValue
env_get(JSContext* ctx) {
  if(JS_IsUninitialized(wasm_default_env)) {
    wasm_default_env = env_new(ctx);
    if(JS_IsException(wasm_default_env)) {
      JSValue ex = wasm_default_env;

      wasm_default_env = JS_UNINITIALIZED;
      return ex;
    }
  }

  return JS_DupValue(ctx, wasm_default_env);
}

/* ---- values ---- */

static JSValue funcref_function(JSContext* ctx, JSValueConst env, JSValueConst owner, WBFunc* func, const char* name);
static WBFunc* funcref_get(JSContext* ctx, JSValueConst fn);

/* converts a JS value to a wasm value of type `t`.
 * returns 0, or -1 with an exception pending. */
static int
value_from_js(JSContext* ctx, WBValType t, JSValueConst v, WBValue* out) {
  out->type = t;
  switch(t) {
    case WB_I32: return JS_ToInt32(ctx, &out->u.i32, v);
    case WB_I64: return JS_ToBigInt64(ctx, &out->u.i64, v);
    case WB_F32: {
      double d;
      if(JS_ToFloat64(ctx, &d, v))
        return -1;
      out->u.f32 = (float)d;
      return 0;
    }
    case WB_F64: return JS_ToFloat64(ctx, &out->u.f64, v);
    default: JS_ThrowTypeError(ctx, "funcref and externref values are not supported here"); return -1;
  }
}

static JSValue
value_to_js(JSContext* ctx, const WBValue* v) {
  switch(v->type) {
    case WB_I32: return JS_NewInt32(ctx, v->u.i32);
    case WB_I64: return JS_NewBigInt64(ctx, v->u.i64);
    case WB_F32: return JS_NewFloat64(ctx, v->u.f32);
    case WB_F64: return JS_NewFloat64(ctx, v->u.f64);
    default: return JS_NULL;
  }
}

/* ToIndex-style conversion of a descriptor field: [EnforceRange] unsigned long.
 * returns 0, or -1 with a TypeError pending. */
static int
to_u32(JSContext* ctx, JSValueConst v, const char* what, uint32_t* out) {
  double d;

  if(JS_ToFloat64(ctx, &d, v))
    return -1;
  if(d != d || d < 0 || d > 4294967295.0) {
    JS_ThrowTypeError(ctx, "%s is out of range", what);
    return -1;
  }
  *out = (uint32_t)d;
  return 0;
}

/* reads descriptor.<name> (or `alt`) as an optional u32.
 * returns 1 when present, 0 when absent, -1 with an exception pending. */
static int
get_u32_field(JSContext* ctx, JSValueConst desc, const char* name, const char* alt, uint32_t* out) {
  JSValue v = JS_GetPropertyStr(ctx, desc, name);
  int ret;

  if(JS_IsException(v))
    return -1;
  if(JS_IsUndefined(v) && alt) {
    JS_FreeValue(ctx, v);
    if(JS_IsException(v = JS_GetPropertyStr(ctx, desc, alt)))
      return -1;
  }

  if(JS_IsUndefined(v)) {
    JS_FreeValue(ctx, v);
    return 0;
  }
  ret = to_u32(ctx, v, name, out) ? -1 : 1;
  JS_FreeValue(ctx, v);
  return ret;
}

/* parses { initial, maximum } of a Memory or Table descriptor.
 * returns 0, or -1 with TypeError or RangeError pending. */
static int
get_limits(JSContext* ctx, JSValueConst desc, uint32_t cap, WBLimits* lim) {
  uint32_t v;
  int r;

  memset(lim, 0, sizeof(*lim));
  if(!JS_IsObject(desc)) {
    JS_ThrowTypeError(ctx, "descriptor must be an object");
    return -1;
  }

  if((r = get_u32_field(ctx, desc, "initial", "minimum", &lim->min)) < 0)
    return -1;
  if(!r) {
    JS_ThrowTypeError(ctx, "descriptor needs an 'initial' member");
    return -1;
  }

  if((r = get_u32_field(ctx, desc, "maximum", NULL, &v)) < 0)
    return -1;
  if(r) {
    lim->has_max = 1;
    lim->max = v;
  }

  if(lim->min > cap || (lim->has_max && lim->max > cap)) {
    JS_ThrowRangeError(ctx, "size is larger than %u", cap);
    return -1;
  }

  if(lim->has_max && lim->max < lim->min) {
    JS_ThrowRangeError(ctx, "'maximum' is less than 'initial'");
    return -1;
  }

  return 0;
}

/* maps a value type name ("i32", "anyfunc", ...) to a WBValType.
 * returns 0, or -1 with a TypeError pending. */
static int
get_valtype(JSContext* ctx, JSValueConst v, WBValType* out) {
  const char* s = JS_ToCString(ctx, v);
  int ret = 0;

  if(!s)
    return -1;
  if(!strcmp(s, "i32"))
    *out = WB_I32;
  else if(!strcmp(s, "i64"))
    *out = WB_I64;
  else if(!strcmp(s, "f32"))
    *out = WB_F32;
  else if(!strcmp(s, "f64"))
    *out = WB_F64;
  else if(!strcmp(s, "anyfunc") || !strcmp(s, "funcref"))
    *out = WB_FUNCREF;
  else if(!strcmp(s, "externref"))
    *out = WB_EXTERNREF;
  else {
    JS_ThrowTypeError(ctx, "invalid value type '%s'", s);
    ret = -1;
  }
  JS_FreeCString(ctx, s);
  return ret;
}

/* ---- functions ---- */

/* wasm calls an imported JS function.
 * returns 0, or -1 with the JS exception left pending (kind WB_ERR_HOST). */
static int
host_call(WBContext* wctx, void* opaque, const WBValue* args, WBValue* results) {
  HostFunc* h = opaque;
  JSValue argv[16], ret;
  int status = -1;
  size_t i, n = h->type.nparams;

  (void)wctx;
  for(i = 0; i < n; i++)
    argv[i] = value_to_js(h->ctx, &args[i]);

  ret = JS_Call(h->ctx, h->fn, JS_UNDEFINED, (int)n, argv);
  for(i = 0; i < n; i++)
    JS_FreeValue(h->ctx, argv[i]);

  if(!JS_IsException(ret)) {
    status = h->type.nresults ? value_from_js(h->ctx, h->type.results[0], ret, &results[0]) : 0;
    JS_FreeValue(h->ctx, ret);
  }

  return status;
}

static void
host_finalize(void* opaque) {
  HostFunc* h = opaque;

  JS_FreeValueRT(h->rt, h->fn);
  free((void*)h->type.params);
  free((void*)h->type.results);
  free(h);
}

/* calls `f` with JS arguments, converting by its type.
 * returns the JS result, or JS_EXCEPTION. */
static JSValue
call_wbfunc(JSContext* ctx, Env* e, WBFunc* f, int argc, JSValueConst argv[]) {
  WBValue args[16], res[16];
  const WBFuncType* ft = WB_GetFuncType(f);
  JSValue ret = JS_UNDEFINED;

  if(ft->nparams > countof(args) || ft->nresults > countof(res))
    return JS_ThrowRangeError(ctx, "too many parameters or results");

  for(size_t i = 0; i < ft->nparams; i++) {
    JSValueConst a = (int)i < argc ? argv[i] : JS_UNDEFINED;

    if(value_from_js(ctx, ft->params[i], a, &args[i]))
      return JS_EXCEPTION;
  }

  if(WB_CallFunc(e->ctx, f, args, res))
    return throw_backend(ctx, e);

  if(ft->nresults == 1)
    ret = value_to_js(ctx, &res[0]);
  else if(ft->nresults > 1) {
    ret = JS_NewArray(ctx);
    for(size_t i = 0; i < ft->nresults; i++)
      JS_SetPropertyUint32(ctx, ret, (uint32_t)i, value_to_js(ctx, &res[i]));
  }

  return ret;
}

/* ---- exported functions (FuncRef) ---- */

static void
funcref_finalizer(JSRuntime* rt, JSValue val) {
  FuncRef* f = JS_GetOpaque(val, wasm_funcref_class_id);

  if(f) {
    WB_FreeExtern(env_of(f->env)->ctx, (WBExtern){WB_EXTERN_FUNC, {.func = f->func}});
    JS_FreeValueRT(rt, f->owner);
    JS_FreeValueRT(rt, f->env);
    js_free_rt(rt, f);
  }
}

static void
funcref_gc_mark(JSRuntime* rt, JSValueConst val, JS_MarkFunc* mark_func) {
  FuncRef* f = JS_GetOpaque(val, wasm_funcref_class_id);

  if(f) {
    JS_MarkValue(rt, f->owner, mark_func);
    JS_MarkValue(rt, f->env, mark_func);
  }
}

static JSClassDef wasm_funcref_class = {"WasmFuncRef", .finalizer = funcref_finalizer, .gc_mark = funcref_gc_mark};

/* the function object behind an exported wasm function: data[0] is its FuncRef. */
static JSValue
js_funcref_call(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue* data) {
  FuncRef* f = JS_GetOpaque(data[0], wasm_funcref_class_id);

  return call_wbfunc(ctx, env_of(f->env), f->func, argc, argv);
}

/* wraps `func` (a reference is taken) as a JS function named `name`.
 * returns the function, or JS_EXCEPTION. */
static JSValue
funcref_function(JSContext* ctx, JSValueConst env, JSValueConst owner, WBFunc* func, const char* name) {
  FuncRef* f = js_mallocz(ctx, sizeof(*f));
  JSValue holder, fn;

  if(!f)
    return JS_EXCEPTION;
  holder = JS_NewObjectClass(ctx, wasm_funcref_class_id);
  if(JS_IsException(holder)) {
    js_free(ctx, f);
    return holder;
  }
  f->env = JS_DupValue(ctx, env);
  f->owner = JS_DupValue(ctx, owner);
  f->func = WB_DupExtern(env_of(env)->ctx, (WBExtern){WB_EXTERN_FUNC, {.func = func}}).u.func;
  JS_SetOpaque(holder, f);

  fn = JS_NewCFunctionData(ctx, js_funcref_call, (int)WB_GetFuncType(func)->nparams, 0, 1, &holder);
  if(!JS_IsException(fn)) {
    JS_DefinePropertyValueStr(ctx, fn, "name", JS_NewString(ctx, name), JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValue(ctx, fn, funcref_atom, JS_DupValue(ctx, holder), 0);
  }
  JS_FreeValue(ctx, holder);
  return fn;
}

/* the engine function behind an exported wasm function.
 * borrowed: valid while `fn` lives. returns NULL when `fn` is not one (no exception). */
static WBFunc*
funcref_get(JSContext* ctx, JSValueConst fn) {
  JSValue holder;
  FuncRef* f;

  if(!JS_IsFunction(ctx, fn))
    return NULL;
  holder = JS_GetProperty(ctx, fn, funcref_atom);
  f = JS_GetOpaque(holder, wasm_funcref_class_id);
  JS_FreeValue(ctx, holder);
  return f ? f->func : NULL;
}

/* ---- Memory ---- */

static void
memory_finalizer(JSRuntime* rt, JSValue val) {
  Memory* m = JS_GetOpaque(val, wasm_memory_class_id);

  if(m) {
    if(m->owns)
      WB_FreeExtern(env_of(m->env)->ctx, (WBExtern){WB_EXTERN_MEMORY, {.memory = m->mem}});
    JS_FreeValueRT(rt, m->buffer);
    JS_FreeValueRT(rt, m->owner);
    JS_FreeValueRT(rt, m->env);
    js_free_rt(rt, m);
  }
}

static void
memory_gc_mark(JSRuntime* rt, JSValueConst val, JS_MarkFunc* mark_func) {
  Memory* m = JS_GetOpaque(val, wasm_memory_class_id);

  if(m) {
    JS_MarkValue(rt, m->buffer, mark_func);
    JS_MarkValue(rt, m->owner, mark_func);
    JS_MarkValue(rt, m->env, mark_func);
  }
}

static JSClassDef wasm_memory_class = {"Memory", .finalizer = memory_finalizer, .gc_mark = memory_gc_mark};

/* the backing store is owned by the engine: no free callback. */
static void
no_free(JSRuntime* rt, void* opaque, void* ptr) {
}

/* Memory.prototype.buffer: an ArrayBuffer over the live memory; replaced
 * (and the old one detached) whenever the engine moved or grew it. */
static JSValue
js_memory_buffer(JSContext* ctx, JSValueConst this_val) {
  Memory* m = JS_GetOpaque2(ctx, this_val, wasm_memory_class_id);
  size_t size;
  uint8_t* data;

  if(!m)
    return JS_EXCEPTION;

  data = WB_GetMemoryData(m->mem, &size);
  if(!JS_IsUndefined(m->buffer)) {
    size_t old;

    if(m->data == data && JS_GetArrayBuffer(ctx, &old, m->buffer) && old == size)
      return JS_DupValue(ctx, m->buffer);
    JS_DetachArrayBuffer(ctx, m->buffer);
    JS_FreeValue(ctx, m->buffer);
  }
  m->data = data;
  m->buffer = JS_NewArrayBuffer(ctx, data, size, no_free, NULL, FALSE);
  return JS_IsException(m->buffer) ? JS_EXCEPTION : JS_DupValue(ctx, m->buffer);
}

/* Memory.prototype.grow(delta): returns the previous size in pages. */
static JSValue
js_memory_grow(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Memory* m = JS_GetOpaque2(ctx, this_val, wasm_memory_class_id);
  uint32_t delta = 0;
  int64_t prev;

  if(!m)
    return JS_EXCEPTION;
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "Memory.grow needs a delta");
  if(to_u32(ctx, argv[0], "delta", &delta))
    return JS_EXCEPTION;
  if((prev = WB_GrowMemory(env_of(m->env)->ctx, m->mem, delta)) < 0)
    return throw_backend(ctx, env_of(m->env));
  return JS_NewInt64(ctx, prev);
}

/* wraps `mem`; with `owner` undefined the wrapper owns the reference it holds. */
static JSValue
memory_wrap(JSContext* ctx, JSValueConst proto, JSValueConst env, JSValueConst owner, WBMemory* mem) {
  Memory* m;
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, wasm_memory_class_id);

  if(JS_IsException(obj))
    return obj;
  if(!(m = js_mallocz(ctx, sizeof(*m)))) {
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
  }
  m->env = JS_DupValue(ctx, env);
  m->owner = JS_DupValue(ctx, owner);
  m->mem = mem;
  m->owns = JS_IsUndefined(owner);
  m->buffer = JS_UNDEFINED;
  JS_SetOpaque(obj, m);
  return obj;
}

/* new Memory({ initial, maximum }): standalone memory, shareable between instances. */
static JSValue
js_memory_ctor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  WBLimits lim;
  JSValue env, proto, obj;
  WBMemory* mem;
  JSValue shared;

  if(argc < 1)
    return JS_ThrowTypeError(ctx, "WebAssembly.Memory needs a descriptor");
  if(get_limits(ctx, argv[0], 65536, &lim))
    return JS_EXCEPTION;
  shared = JS_GetPropertyStr(ctx, argv[0], "shared");
  if(JS_ToBool(ctx, shared)) {
    JS_FreeValue(ctx, shared);
    return JS_ThrowTypeError(ctx, "shared memories are not supported");
  }
  JS_FreeValue(ctx, shared);

  if(JS_IsException(env = env_get(ctx)))
    return env;
  if(!(mem = WB_NewMemory(env_of(env)->ctx, &lim))) {
    throw_backend(ctx, env_of(env));
    JS_FreeValue(ctx, env);
    return JS_EXCEPTION;
  }
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  obj = JS_IsException(proto) ? proto : memory_wrap(ctx, proto, env, JS_UNDEFINED, mem);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    WB_FreeExtern(env_of(env)->ctx, (WBExtern){WB_EXTERN_MEMORY, {.memory = mem}});
  JS_FreeValue(ctx, env);
  return obj;
}

/* ---- Table ---- */

static void
table_finalizer(JSRuntime* rt, JSValue val) {
  Table* t = JS_GetOpaque(val, wasm_table_class_id);

  if(t) {
    if(t->owns)
      WB_FreeExtern(env_of(t->env)->ctx, (WBExtern){WB_EXTERN_TABLE, {.table = t->table}});
    JS_FreeValueRT(rt, t->elems);
    JS_FreeValueRT(rt, t->owner);
    JS_FreeValueRT(rt, t->env);
    js_free_rt(rt, t);
  }
}

static void
table_gc_mark(JSRuntime* rt, JSValueConst val, JS_MarkFunc* mark_func) {
  Table* t = JS_GetOpaque(val, wasm_table_class_id);

  if(t) {
    JS_MarkValue(rt, t->elems, mark_func);
    JS_MarkValue(rt, t->owner, mark_func);
    JS_MarkValue(rt, t->env, mark_func);
  }
}

static JSClassDef wasm_table_class = {"Table", .finalizer = table_finalizer, .gc_mark = table_gc_mark};

static JSValue
js_table_length(JSContext* ctx, JSValueConst this_val) {
  Table* t = JS_GetOpaque2(ctx, this_val, wasm_table_class_id);

  return t ? JS_NewUint32(ctx, WB_GetTableSize(t->table)) : JS_EXCEPTION;
}

/* Table.prototype.get(index): the function stored there, or null. */
static JSValue
js_table_get(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Table* t = JS_GetOpaque2(ctx, this_val, wasm_table_class_id);
  uint32_t index = 0;
  WBValue v;
  JSValue fn;
  char name[16];

  if(!t)
    return JS_EXCEPTION;
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "Table.get needs an index");
  if(to_u32(ctx, argv[0], "index", &index))
    return JS_EXCEPTION;
  if(WB_GetTableElem(env_of(t->env)->ctx, t->table, index, &v))
    return throw_backend(ctx, env_of(t->env));
  if(v.type != WB_FUNCREF || !v.u.ref)
    return JS_NULL;

  snprintf(name, sizeof(name), "%u", index);
  fn = funcref_function(ctx, t->env, this_val, v.u.ref, name);
  WB_FreeValue(env_of(t->env)->ctx, v);
  return fn;
}

/* stores `fn` (an exported function, or null/undefined) in `t` at `index`.
 * returns 0, or -1 with an exception pending. */
static int
table_store(JSContext* ctx, Table* t, uint32_t index, JSValueConst fn) {
  WBValue v = {WB_FUNCREF, {.ref = NULL}};

  if(!JS_IsNull(fn) && !JS_IsUndefined(fn) && !(v.u.ref = funcref_get(ctx, fn))) {
    JS_ThrowTypeError(ctx, "value must be an exported WebAssembly function or null");
    return -1;
  }

  if(WB_SetTableElem(env_of(t->env)->ctx, t->table, index, &v)) {
    throw_backend(ctx, env_of(t->env));
    return -1;
  }
  JS_SetPropertyUint32(ctx, t->elems, index, JS_DupValue(ctx, JS_IsUndefined(fn) ? JS_NULL : fn));
  return 0;
}

/* Table.prototype.set(index, value): value defaults to null. */
static JSValue
js_table_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Table* t = JS_GetOpaque2(ctx, this_val, wasm_table_class_id);
  uint32_t index = 0;

  if(!t)
    return JS_EXCEPTION;
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "Table.set needs an index");
  if(to_u32(ctx, argv[0], "index", &index))
    return JS_EXCEPTION;
  return table_store(ctx, t, index, argc > 1 ? argv[1] : JS_NULL) ? JS_EXCEPTION : JS_UNDEFINED;
}

/* Table.prototype.grow(delta, value?): returns the previous length. */
static JSValue
js_table_grow(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Table* t = JS_GetOpaque2(ctx, this_val, wasm_table_class_id);
  uint32_t delta = 0;
  WBValue v = {WB_FUNCREF, {.ref = NULL}};
  int64_t prev;

  if(!t)
    return JS_EXCEPTION;
  if(argc < 1)
    return JS_ThrowTypeError(ctx, "Table.grow needs a delta");
  if(to_u32(ctx, argv[0], "delta", &delta))
    return JS_EXCEPTION;
  if(argc > 1 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]) && !(v.u.ref = funcref_get(ctx, argv[1])))
    return JS_ThrowTypeError(ctx, "value must be an exported WebAssembly function or null");
  if((prev = WB_GrowTable(env_of(t->env)->ctx, t->table, delta, &v)) < 0)
    return throw_backend(ctx, env_of(t->env));
  for(uint32_t i = 0; i < delta && argc > 1 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]); i++)
    JS_SetPropertyUint32(ctx, t->elems, (uint32_t)prev + i, JS_DupValue(ctx, argv[1]));
  return JS_NewInt64(ctx, prev);
}

static JSValue
table_wrap(JSContext* ctx, JSValueConst proto, JSValueConst env, JSValueConst owner, WBTable* table) {
  Table* t;
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, wasm_table_class_id);

  if(JS_IsException(obj))
    return obj;
  if(!(t = js_mallocz(ctx, sizeof(*t)))) {
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
  }
  t->env = JS_DupValue(ctx, env);
  t->owner = JS_DupValue(ctx, owner);
  t->table = table;
  t->owns = JS_IsUndefined(owner);
  t->elems = JS_NewArray(ctx);
  JS_SetOpaque(obj, t);
  return obj;
}

/* new Table({ element: "anyfunc", initial, maximum }, value?). */
static JSValue
js_table_ctor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  WBLimits lim;
  WBValType elem;
  JSValue env, proto, obj, el;
  WBTable* table;
  int r;

  if(argc < 1)
    return JS_ThrowTypeError(ctx, "WebAssembly.Table needs a descriptor");
  if(get_limits(ctx, argv[0], 10000000, &lim))
    return JS_EXCEPTION;
  el = JS_GetPropertyStr(ctx, argv[0], "element");
  r = JS_IsUndefined(el) ? (JS_ThrowTypeError(ctx, "descriptor needs an 'element' member"), -1) : get_valtype(ctx, el, &elem);
  JS_FreeValue(ctx, el);
  if(r)
    return JS_EXCEPTION;
  if(elem != WB_FUNCREF && elem != WB_EXTERNREF)
    return JS_ThrowTypeError(ctx, "'element' must be \"anyfunc\" or \"externref\"");

  if(JS_IsException(env = env_get(ctx)))
    return env;
  if(!(table = WB_NewTable(env_of(env)->ctx, elem, &lim))) {
    throw_backend(ctx, env_of(env));
    JS_FreeValue(ctx, env);
    return JS_EXCEPTION;
  }
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  obj = JS_IsException(proto) ? proto : table_wrap(ctx, proto, env, JS_UNDEFINED, table);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj)) {
    WB_FreeExtern(env_of(env)->ctx, (WBExtern){WB_EXTERN_TABLE, {.table = table}});
  } else if(argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
    Table* t = JS_GetOpaque(obj, wasm_table_class_id);

    for(uint32_t i = 0; i < lim.min; i++)
      if(table_store(ctx, t, i, argv[1])) {
        JS_FreeValue(ctx, obj);
        obj = JS_EXCEPTION;
        break;
      }
  }
  JS_FreeValue(ctx, env);
  return obj;
}

/* ---- Global ---- */

static void
global_finalizer(JSRuntime* rt, JSValue val) {
  Global* g = JS_GetOpaque(val, wasm_global_class_id);

  if(g) {
    if(g->owns)
      WB_FreeExtern(env_of(g->env)->ctx, (WBExtern){WB_EXTERN_GLOBAL, {.global = g->global}});
    JS_FreeValueRT(rt, g->owner);
    JS_FreeValueRT(rt, g->env);
    js_free_rt(rt, g);
  }
}

static void
global_gc_mark(JSRuntime* rt, JSValueConst val, JS_MarkFunc* mark_func) {
  Global* g = JS_GetOpaque(val, wasm_global_class_id);

  if(g) {
    JS_MarkValue(rt, g->owner, mark_func);
    JS_MarkValue(rt, g->env, mark_func);
  }
}

static JSClassDef wasm_global_class = {"Global", .finalizer = global_finalizer, .gc_mark = global_gc_mark};

static JSValue
global_wrap(JSContext* ctx, JSValueConst proto, JSValueConst env, JSValueConst owner, WBGlobal* global) {
  Global* g;
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, wasm_global_class_id);

  if(JS_IsException(obj))
    return obj;
  if(!(g = js_mallocz(ctx, sizeof(*g)))) {
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
  }
  g->env = JS_DupValue(ctx, env);
  g->owner = JS_DupValue(ctx, owner);
  g->global = global;
  g->owns = JS_IsUndefined(owner);
  JS_SetOpaque(obj, g);
  return obj;
}

/* Global.prototype.value / valueOf(): the current value. */
static JSValue
js_global_get(JSContext* ctx, JSValueConst this_val) {
  Global* g = JS_GetOpaque2(ctx, this_val, wasm_global_class_id);
  WBValue v;

  if(!g)
    return JS_EXCEPTION;
  WB_GetGlobal(env_of(g->env)->ctx, g->global, &v);
  return value_to_js(ctx, &v);
}

static JSValue
js_global_valueof(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return js_global_get(ctx, this_val);
}

static JSValue
js_global_set(JSContext* ctx, JSValueConst this_val, JSValueConst value) {
  Global* g = JS_GetOpaque2(ctx, this_val, wasm_global_class_id);
  WBValue v;
  int mut;

  if(!g)
    return JS_EXCEPTION;
  WB_GetGlobalType(g->global, &mut);
  if(!mut)
    return JS_ThrowTypeError(ctx, "can't set the value of an immutable global");
  if(value_from_js(ctx, WB_GetGlobalType(g->global, NULL), value, &v))
    return JS_EXCEPTION;
  if(WB_SetGlobal(env_of(g->env)->ctx, g->global, &v))
    return throw_backend(ctx, env_of(g->env));
  return JS_UNDEFINED;
}

/* creates a standalone global holding `value` converted to type `t`.
 * returns NULL with an exception pending. */
static WBGlobal*
global_create(JSContext* ctx, Env* e, WBValType t, int is_mutable, JSValueConst value) {
  WBValue v;
  WBGlobal* g;

  memset(&v, 0, sizeof(v));
  v.type = t;
  if(!JS_IsUndefined(value) && value_from_js(ctx, t, value, &v))
    return NULL;
  if(!(g = WB_NewGlobal(e->ctx, is_mutable, &v)))
    throw_backend(ctx, e);
  return g;
}

/* new Global({ value: "i32", mutable }, v). */
static JSValue
js_global_ctor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  WBValType t;
  JSValue env, proto, obj, tv, mv;
  WBGlobal* g;
  int is_mutable, r;

  if(argc < 1 || !JS_IsObject(argv[0]))
    return JS_ThrowTypeError(ctx, "WebAssembly.Global needs a descriptor object");
  tv = JS_GetPropertyStr(ctx, argv[0], "value");
  r = JS_IsUndefined(tv) ? (JS_ThrowTypeError(ctx, "descriptor needs a 'value' member"), -1) : get_valtype(ctx, tv, &t);
  JS_FreeValue(ctx, tv);
  if(r)
    return JS_EXCEPTION;
  if(t == WB_FUNCREF || t == WB_EXTERNREF)
    return JS_ThrowTypeError(ctx, "reference-typed globals are not supported");
  mv = JS_GetPropertyStr(ctx, argv[0], "mutable");
  is_mutable = JS_ToBool(ctx, mv);
  JS_FreeValue(ctx, mv);

  if(JS_IsException(env = env_get(ctx)))
    return env;
  if(!(g = global_create(ctx, env_of(env), t, is_mutable, argc > 1 ? argv[1] : JS_UNDEFINED))) {
    JS_FreeValue(ctx, env);
    return JS_EXCEPTION;
  }
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  obj = JS_IsException(proto) ? proto : global_wrap(ctx, proto, env, JS_UNDEFINED, g);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    WB_FreeExtern(env_of(env)->ctx, (WBExtern){WB_EXTERN_GLOBAL, {.global = g}});
  JS_FreeValue(ctx, env);
  return obj;
}

/* ---- Module ---- */

static const char* const kind_names[] = {"function", "table", "memory", "global"};

static void
module_finalizer(JSRuntime* rt, JSValue val) {
  Module* m = JS_GetOpaque(val, wasm_module_class_id);

  if(m) {
    Env* e = env_of(m->env);

    if(m->mod && e)
      WB_FreeModule(e->ctx, m->mod);
    JS_FreeValueRT(rt, m->env);
    js_free_rt(rt, m);
  }
}

static JSClassDef wasm_module_class = {"Module", .finalizer = module_finalizer};

/* the bytes of a BufferSource argument.
 * returns NULL with an exception pending. */
static const uint8_t*
get_bytes(JSContext* ctx, JSValueConst v, size_t* len) {
  const uint8_t* p;
  size_t off, bytes, bpe;
  JSValue ab = JS_GetTypedArrayBuffer(ctx, v, &off, &bytes, &bpe);

  if(!JS_IsException(ab)) {
    size_t total;

    p = JS_GetArrayBuffer(ctx, &total, ab);
    JS_FreeValue(ctx, ab);
    *len = bytes;
    return p ? p + off : NULL;
  }
  JS_FreeValue(ctx, JS_GetException(ctx));
  return JS_GetArrayBuffer(ctx, len, v);
}

static JSValue
js_module_ctor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  const uint8_t* bytes;
  size_t len;
  Module* m;
  JSValue proto, obj, env;

  if(argc < 1)
    return JS_ThrowTypeError(ctx, "WebAssembly.Module needs a BufferSource");
  if(!(bytes = get_bytes(ctx, argv[0], &len)))
    return JS_ThrowTypeError(ctx, "argument is not a BufferSource");

  env = env_get(ctx);
  if(JS_IsException(env))
    return env;

  if(!(m = js_mallocz(ctx, sizeof(*m)))) {
    JS_FreeValue(ctx, env);
    return JS_EXCEPTION;
  }
  m->env = env;

  if(!(m->mod = WB_NewModule(env_of(env)->ctx, bytes, len))) {
    throw_backend(ctx, env_of(env));
    goto fail;
  }

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, wasm_module_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, m);
  return obj;

fail:
  if(m->mod)
    WB_FreeModule(env_of(env)->ctx, m->mod);
  JS_FreeValue(ctx, m->env);
  js_free(ctx, m);
  return JS_EXCEPTION;
}

/* Module.imports(module) / Module.exports(module): [{module?, name, kind}]. */
static JSValue
js_module_list(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Module* m = argc > 0 ? JS_GetOpaque2(ctx, argv[0], wasm_module_class_id) : NULL;
  Env* e;
  const WBImportDesc* imp;
  const WBExportDesc* exp;
  size_t n;
  JSValue arr;

  if(!m)
    return argc > 0 ? JS_EXCEPTION : JS_ThrowTypeError(ctx, "argument is not a WebAssembly.Module");
  e = env_of(m->env);

  if(magic ? WB_GetModuleExports(e->ctx, m->mod, &exp, &n) : WB_GetModuleImports(e->ctx, m->mod, &imp, &n))
    return throw_backend(ctx, e);

  arr = JS_NewArray(ctx);
  for(size_t i = 0; i < n; i++) {
    JSValue o = JS_NewObject(ctx);
    const WBExternType* t = magic ? &exp[i].type : &imp[i].type;

    if(!magic)
      JS_SetPropertyStr(ctx, o, "module", JS_NewString(ctx, imp[i].module));
    JS_SetPropertyStr(ctx, o, "name", JS_NewString(ctx, magic ? exp[i].name : imp[i].name));
    JS_SetPropertyStr(ctx, o, "kind", JS_NewString(ctx, kind_names[t->kind]));
    JS_SetPropertyUint32(ctx, arr, (uint32_t)i, o);
  }

  return arr;
}

/* Module.customSections(module, name): the payloads as ArrayBuffer copies. */
static JSValue
js_module_custom_sections(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Module* m = argc > 0 ? JS_GetOpaque2(ctx, argv[0], wasm_module_class_id) : NULL;
  const char* name;
  const uint8_t* data;
  size_t len;
  JSValue arr;

  if(!m)
    return argc > 0 ? JS_EXCEPTION : JS_ThrowTypeError(ctx, "argument is not a WebAssembly.Module");
  if(argc < 2)
    return JS_ThrowTypeError(ctx, "Module.customSections needs a section name");
  if(!(name = JS_ToCString(ctx, argv[1])))
    return JS_EXCEPTION;

  arr = JS_NewArray(ctx);
  for(uint32_t i = 0; !WB_GetCustomSection(env_of(m->env)->ctx, m->mod, name, i, &data, &len); i++)
    JS_SetPropertyUint32(ctx, arr, i, JS_NewArrayBufferCopy(ctx, data, len));
  JS_FreeCString(ctx, name);
  return arr;
}

/* ---- Instance ---- */

static void
instance_finalizer(JSRuntime* rt, JSValue val) {
  Instance* i = JS_GetOpaque(val, wasm_instance_class_id);

  if(i) {
    Env* e = env_of(i->env);

    if(i->inst && e)
      WB_FreeInstance(e->ctx, i->inst);
    JS_FreeValueRT(rt, i->deps);
    JS_FreeValueRT(rt, i->module);
    JS_FreeValueRT(rt, i->env);
    js_free_rt(rt, i);
  }
}

static void
instance_gc_mark(JSRuntime* rt, JSValueConst val, JS_MarkFunc* mark_func) {
  Instance* i = JS_GetOpaque(val, wasm_instance_class_id);

  if(i) {
    JS_MarkValue(rt, i->deps, mark_func);
    JS_MarkValue(rt, i->module, mark_func);
    JS_MarkValue(rt, i->env, mark_func);
  }
}

static JSClassDef wasm_instance_class = {"Instance", .finalizer = instance_finalizer, .gc_mark = instance_gc_mark};

/* engine objects made for one instantiation (a Global from a plain number);
 * released once WB_NewInstance() has taken its own references. */
typedef struct {
  WBExtern* v;
  size_t n;
} Created;

/* resolves one import row from the importObject.
 * returns 0, or -1 with an exception pending (LinkError or TypeError). */
static int
resolve_import(JSContext* ctx, Env* e, JSValueConst imports, const WBImportDesc* d, WBExtern* out, JSValueConst deps, uint32_t* ndeps, Created* created) {
  JSValue mod = JS_GetPropertyStr(ctx, imports, d->module), val = JS_UNDEFINED;
  int ret = -1;

  if(!JS_IsObject(mod)) {
    JS_ThrowTypeError(ctx, "import object field '%s' is not an Object", d->module);
    goto done;
  }
  val = JS_GetPropertyStr(ctx, mod, d->name);

  switch(d->type.kind) {
    case WB_EXTERN_FUNC: {
      HostFunc* h;
      WBValType* params;
      WBValType* results;
      const WBFuncType* ft = &d->type.u.func;
      WBFunc* wasm_fn = funcref_get(ctx, val);

      out->kind = WB_EXTERN_FUNC;
      if(wasm_fn) {
        out->u.func = wasm_fn; /* an exported wasm function links directly */
        ret = 0;
        break;
      }

      if(!JS_IsFunction(ctx, val)) {
        throw_link_error(ctx, "import is not a function");
        goto done;
      }
      h = calloc(1, sizeof(*h));
      params = malloc((ft->nparams + 1) * sizeof(*params));
      results = malloc((ft->nresults + 1) * sizeof(*results));
      memcpy(params, ft->params, ft->nparams * sizeof(*params));
      memcpy(results, ft->results, ft->nresults * sizeof(*results));
      h->rt = JS_GetRuntime(ctx);
      h->ctx = ctx;
      h->fn = JS_DupValue(ctx, val);
      h->type = (WBFuncType){params, ft->nparams, results, ft->nresults};
      if(!(out->u.func = WB_NewHostFunc(e->ctx, &h->type, host_call, h, host_finalize))) {
        host_finalize(h);
        throw_backend(ctx, e);
        goto done;
      }
      created->v[created->n++] = *out;
      ret = 0;
      break;
    }
    case WB_EXTERN_MEMORY: {
      Memory* m = JS_GetOpaque(val, wasm_memory_class_id);

      if(!m) {
        throw_link_error(ctx, "import is not a WebAssembly.Memory");
        goto done;
      }
      out->kind = WB_EXTERN_MEMORY;
      out->u.memory = m->mem;
      JS_SetPropertyUint32(ctx, deps, (*ndeps)++, JS_DupValue(ctx, val));
      ret = 0;
      break;
    }
    case WB_EXTERN_TABLE: {
      Table* t = JS_GetOpaque(val, wasm_table_class_id);

      if(!t) {
        throw_link_error(ctx, "import is not a WebAssembly.Table");
        goto done;
      }
      out->kind = WB_EXTERN_TABLE;
      out->u.table = t->table;
      JS_SetPropertyUint32(ctx, deps, (*ndeps)++, JS_DupValue(ctx, val));
      ret = 0;
      break;
    }
    case WB_EXTERN_GLOBAL: {
      Global* g = JS_GetOpaque(val, wasm_global_class_id);
      WBValType t = d->type.u.global.type;

      out->kind = WB_EXTERN_GLOBAL;
      if(g) {
        out->u.global = g->global;
        JS_SetPropertyUint32(ctx, deps, (*ndeps)++, JS_DupValue(ctx, val));
      } else if(d->type.u.global.is_mutable) {
        throw_link_error(ctx, "a mutable global import must be a WebAssembly.Global");
        goto done;
      } else if((t == WB_I64 ? JS_IsBigInt(ctx, val) : JS_IsNumber(val))) {
        if(!(out->u.global = global_create(ctx, e, t, 0, val)))
          goto done;
        created->v[created->n++] = *out;
      } else {
        throw_link_error(ctx, "global import must be a number, bigint or WebAssembly.Global");
        goto done;
      }
      ret = 0;
      break;
    }
  }

done:
  JS_FreeValue(ctx, val);
  JS_FreeValue(ctx, mod);
  return ret;
}

/* the JS object for one export; `index` names a function. */
static JSValue
export_wrap(JSContext* ctx, JSValueConst env, JSValueConst inst, WBExtern ext, const char* fname) {
  switch(ext.kind) {
    case WB_EXTERN_FUNC: return funcref_function(ctx, env, inst, ext.u.func, fname);
    case WB_EXTERN_MEMORY: return memory_wrap(ctx, memory_proto, env, inst, ext.u.memory);
    case WB_EXTERN_TABLE: return table_wrap(ctx, table_proto, env, inst, ext.u.table);
    default: return global_wrap(ctx, global_proto, env, inst, ext.u.global);
  }
}

static JSValue
js_instance_ctor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  Module* mod = argc > 0 ? JS_GetOpaque2(ctx, argv[0], wasm_module_class_id) : NULL;
  Env* e;
  Instance* inst;
  const WBImportDesc* descs;
  const WBExportDesc* edescs;
  const WBExport* exps;
  WBExtern* imports = NULL;
  Created created = {NULL, 0};
  size_t n, nexp, nedesc;
  uint32_t ndeps = 0;
  JSValue proto, obj = JS_UNDEFINED, exports;

  if(!mod)
    return argc > 0 ? JS_EXCEPTION : JS_ThrowTypeError(ctx, "WebAssembly.Instance needs a Module");

  e = env_of(mod->env);

  if(WB_GetModuleImports(e->ctx, mod->mod, &descs, &n))
    return throw_backend(ctx, e);

  if(n && !(argc > 1 && JS_IsObject(argv[1])))
    return JS_ThrowTypeError(ctx, "imports must be an object when the module has imports");

  if(!(inst = js_mallocz(ctx, sizeof(*inst))))
    return JS_EXCEPTION;

  inst->env = JS_DupValue(ctx, mod->env);
  inst->module = JS_DupValue(ctx, argv[0]);
  inst->deps = JS_NewArray(ctx);

  if(n && (!(imports = calloc(n, sizeof(*imports))) || !(created.v = calloc(n, sizeof(*created.v))))) {
    JS_ThrowOutOfMemory(ctx);
    goto fail;
  }

  for(size_t i = 0; i < n; i++)
    if(resolve_import(ctx, e, argv[1], &descs[i], &imports[i], inst->deps, &ndeps, &created))
      goto fail;

  inst->inst = WB_NewInstance(e->ctx, mod->mod, imports, n);
  for(size_t i = 0; i < created.n; i++)
    WB_FreeExtern(e->ctx, created.v[i]);
  free(created.v);
  created.v = NULL;

  if(!inst->inst) {
    throw_backend(ctx, e);
    goto fail;
  }

  free(imports);
  imports = NULL;

  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, wasm_instance_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, inst);

  if(WB_GetInstanceExports(e->ctx, inst->inst, &exps, &nexp) || WB_GetModuleExports(e->ctx, mod->mod, &edescs, &nedesc)) {
    throw_backend(ctx, e);
    goto fail_obj;
  }

  exports = JS_NewObjectProto(ctx, JS_NULL);
  for(size_t i = 0; i < nexp; i++) {
    char fname[16] = "0";
    JSValue v;

    for(size_t j = 0; j < nedesc; j++)
      if(!strcmp(edescs[j].name, exps[i].name)) {
        snprintf(fname, sizeof(fname), "%u", edescs[j].index);
        break;
      }

    v = export_wrap(ctx, mod->env, obj, exps[i].ext, fname);
    if(JS_IsException(v)) {
      JS_FreeValue(ctx, exports);
      goto fail_obj;
    }

    JS_DefinePropertyValueStr(ctx, exports, exps[i].name, v, JS_PROP_ENUMERABLE);
  }
  JS_PreventExtensions(ctx, exports);
  JS_DefinePropertyValueStr(ctx, obj, "exports", exports, JS_PROP_ENUMERABLE);
  return obj;

fail_obj:
  JS_FreeValue(ctx, obj); /* the finalizer frees `inst` */
  return JS_EXCEPTION;

fail:
  free(imports);
  if(created.v) {
    for(size_t i = 0; i < created.n; i++)
      WB_FreeExtern(e->ctx, created.v[i]);
    free(created.v);
  }

  if(inst->inst)
    WB_FreeInstance(e->ctx, inst->inst);

  JS_FreeValue(ctx, inst->deps);
  JS_FreeValue(ctx, inst->module);
  JS_FreeValue(ctx, inst->env);
  js_free(ctx, inst);
  return JS_EXCEPTION;
}

/* ---- module functions ---- */

/* WebAssembly.validate(bytes): true when `bytes` is a loadable module. */
static JSValue
js_validate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const uint8_t* bytes;
  size_t len;
  JSValue env, ret;
  Env* e;
  WBException ex;

  if(argc < 1 || !(bytes = get_bytes(ctx, argv[0], &len)))
    return JS_ThrowTypeError(ctx, "argument is not a BufferSource");

  if(JS_IsException(env = env_get(ctx)))
    return env;

  e = env_of(env);

  ret = JS_NewBool(ctx, !WB_ValidateModule(e->ctx, bytes, len));
  WB_GetException(e->ctx, &ex);
  JS_FreeValue(ctx, env);
  return ret;
}

static const JSCFunctionListEntry js_module_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("imports", 1, js_module_list, 0),
    JS_CFUNC_MAGIC_DEF("exports", 1, js_module_list, 1),
    JS_CFUNC_DEF("customSections", 2, js_module_custom_sections),
};

static const JSCFunctionListEntry js_wasm_funcs[] = {
    JS_CFUNC_DEF("validate", 1, js_validate),
};

static const JSCFunctionListEntry js_module_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WebAssembly.Module", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_instance_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WebAssembly.Instance", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_memory_proto_funcs[] = {
    JS_CGETSET_DEF("buffer", js_memory_buffer, 0),
    JS_CFUNC_DEF("grow", 1, js_memory_grow),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WebAssembly.Memory", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_table_proto_funcs[] = {
    JS_CGETSET_DEF("length", js_table_length, 0),
    JS_CFUNC_DEF("get", 1, js_table_get),
    JS_CFUNC_DEF("set", 1, js_table_set),
    JS_CFUNC_DEF("grow", 1, js_table_grow),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WebAssembly.Table", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_global_proto_funcs[] = {
    JS_CGETSET_DEF("value", js_global_get, js_global_set),
    JS_CFUNC_DEF("valueOf", 0, js_global_valueof),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WebAssembly.Global", JS_PROP_CONFIGURABLE),
};

/* registers one class: id, class def, prototype with its methods, constructor.
 * returns the constructor, or JS_EXCEPTION. */
static JSValue
define_class(
    JSContext* ctx, JSClassID* id, JSClassDef* def, JSValue* proto, const JSCFunctionListEntry* funcs, int nfuncs, JSCFunction* ctor, const char* name) {
  JSValue c;

  JS_NewClassID(id);
  JS_NewClass(JS_GetRuntime(ctx), *id, def);
  *proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, *proto, funcs, nfuncs);
  c = JS_NewCFunction2(ctx, ctor, name, 1, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, c, *proto);
  JS_SetClassProto(ctx, *id, JS_DupValue(ctx, *proto));
  return c;
}

static int
js_wasm_init(JSContext* ctx, JSModuleDef* m) {
  JSValue module_ctor, instance_ctor, memory_ctor, table_ctor, global_ctor, sym;
  static const char* const names[3] = {"CompileError", "LinkError", "RuntimeError"};

  JS_NewClassID(&wasm_env_class_id);
  JS_NewClass(JS_GetRuntime(ctx), wasm_env_class_id, &wasm_env_class);
  JS_NewClassID(&wasm_funcref_class_id);
  JS_NewClass(JS_GetRuntime(ctx), wasm_funcref_class_id, &wasm_funcref_class);

  sym = JS_Eval(ctx, "Symbol('WebAssembly.funcref')", strlen("Symbol('WebAssembly.funcref')"), "<wasm>", JS_EVAL_TYPE_GLOBAL);
  if(JS_IsException(sym))
    return -1;
  funcref_atom = JS_ValueToAtom(ctx, sym);
  JS_FreeValue(ctx, sym);

  module_ctor = define_class(
      ctx, &wasm_module_class_id, &wasm_module_class, &module_proto, js_module_proto_funcs, countof(js_module_proto_funcs), js_module_ctor, "Module");
  JS_SetPropertyFunctionList(ctx, module_ctor, js_module_static_funcs, countof(js_module_static_funcs));
  instance_ctor = define_class(ctx,
                               &wasm_instance_class_id,
                               &wasm_instance_class,
                               &instance_proto,
                               js_instance_proto_funcs,
                               countof(js_instance_proto_funcs),
                               js_instance_ctor,
                               "Instance");
  memory_ctor = define_class(
      ctx, &wasm_memory_class_id, &wasm_memory_class, &memory_proto, js_memory_proto_funcs, countof(js_memory_proto_funcs), js_memory_ctor, "Memory");
  table_ctor =
      define_class(ctx, &wasm_table_class_id, &wasm_table_class, &table_proto, js_table_proto_funcs, countof(js_table_proto_funcs), js_table_ctor, "Table");
  global_ctor = define_class(
      ctx, &wasm_global_class_id, &wasm_global_class, &global_proto, js_global_proto_funcs, countof(js_global_proto_funcs), js_global_ctor, "Global");

  for(int i = 0; i < 3; i++) {
    char src[160];
    JSValue cls;

    snprintf(src, sizeof(src), "(class %s extends Error { constructor(m) { super(m); this.name = '%s'; } })", names[i], names[i]);
    cls = JS_Eval(ctx, src, strlen(src), "<wasm>", JS_EVAL_TYPE_GLOBAL);
    if(JS_IsException(cls))
      return -1;
    wasm_errors[i] = cls;
    JS_SetModuleExport(ctx, m, names[i], JS_DupValue(ctx, cls));
  }

  /* exports, in order: validate, backend, Module, Instance, Memory, Table,
   * Global, CompileError, LinkError, RuntimeError */
  JS_SetModuleExportList(ctx, m, js_wasm_funcs, countof(js_wasm_funcs));
  JS_SetModuleExport(ctx, m, "backend", JS_NewString(ctx, WB_GetBackendName()));
  JS_SetModuleExport(ctx, m, "Module", module_ctor);
  JS_SetModuleExport(ctx, m, "Instance", instance_ctor);
  JS_SetModuleExport(ctx, m, "Memory", memory_ctor);
  JS_SetModuleExport(ctx, m, "Table", table_ctor);
  JS_SetModuleExport(ctx, m, "Global", global_ctor);
  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_wasm
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if(!(m = JS_NewCModule(ctx, module_name, js_wasm_init)))
    return NULL;
  JS_AddModuleExportList(ctx, m, js_wasm_funcs, countof(js_wasm_funcs));
  JS_AddModuleExport(ctx, m, "backend");
  JS_AddModuleExport(ctx, m, "Module");
  JS_AddModuleExport(ctx, m, "Instance");
  JS_AddModuleExport(ctx, m, "Memory");
  JS_AddModuleExport(ctx, m, "Table");
  JS_AddModuleExport(ctx, m, "Global");
  JS_AddModuleExport(ctx, m, "CompileError");
  JS_AddModuleExport(ctx, m, "LinkError");
  JS_AddModuleExport(ctx, m, "RuntimeError");
  return m;
}
