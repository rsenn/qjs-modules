/* quickjs-wasm.c: WebAssembly.{validate,Module,Instance,Memory} and the
 * CompileError/LinkError/RuntimeError classes, over a wasm-backend.h engine.
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

static JSClassID wasm_env_class_id, wasm_module_class_id, wasm_instance_class_id, wasm_memory_class_id;
static JSValue wasm_errors[3]; /* JS thread only: CompileError, LinkError, RuntimeError */
static JSValue module_proto, instance_proto, memory_proto;

typedef struct {
  WBRuntime* rt;
  WBContext* ctx;
} Env;

typedef struct {
  JSValue env; /* owned */
  WBModule* mod;
} Module;

typedef struct {
  JSValue env;  /* owned */
  JSValue deps; /* owned: array of the import objects it was linked with */
  WBInstance* inst;
} Instance;

typedef struct {
  JSValue owner; /* owned: the Instance (or Memory) object keeping `mem` alive */
  JSValue env;   /* owned */
  WBMemory* mem;
  JSValue buffer; /* owned, JS_UNDEFINED until first read */
  uint8_t* data;
} Memory;

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
    case WB_ERR_TYPE: return JS_ThrowTypeError(ctx, "%s", ex.message);
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

/* converts a JS value to a wasm value of type `t`.
 * returns 0, or -1 with an exception pending. */
static int
value_from_js(JSContext* ctx, WBValType t, JSValueConst v, WBValue* out) {
  out->type = t;
  switch(t) {
    case WB_I32: return JS_ToInt32(ctx, &out->u.i32, v);
    case WB_I64: {
      JSValue big = JS_ToBigInt64(ctx, &out->u.i64, v) ? JS_EXCEPTION : JS_UNDEFINED;
      return JS_IsException(big) ? -1 : 0;
    }
    case WB_F32: {
      double d;
      if(JS_ToFloat64(ctx, &d, v))
        return -1;
      out->u.f32 = (float)d;
      return 0;
    }
    case WB_F64: return JS_ToFloat64(ctx, &out->u.f64, v);
    default: JS_ThrowTypeError(ctx, "funcref and externref values are not supported yet"); return -1;
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

/* exported wasm function: data[0] is the Instance object, data[1] the index
 * into its export table. */
static JSValue
js_func_call(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue* data) {
  Instance* inst = JS_GetOpaque(data[0], wasm_instance_class_id);
  const WBExport* ex;
  size_t n;
  WBValue args[16], res[16];
  const WBFuncType* ft;
  JSValue ret = JS_UNDEFINED;

  if(WB_GetInstanceExports(env_of(inst->env)->ctx, inst->inst, &ex, &n))
    return throw_backend(ctx, env_of(inst->env));

  ft = WB_GetFuncType(ex[magic].ext.u.func);
  if(ft->nparams > countof(args) || ft->nresults > countof(res))
    return JS_ThrowRangeError(ctx, "too many parameters or results");

  for(size_t i = 0; i < ft->nparams; i++) {
    JSValueConst a = (int)i < argc ? argv[i] : JS_UNDEFINED;

    if(value_from_js(ctx, ft->params[i], a, &args[i]))
      return JS_EXCEPTION;
  }

  if(WB_CallFunc(env_of(inst->env)->ctx, ex[magic].ext.u.func, args, res))
    return throw_backend(ctx, env_of(inst->env));

  if(ft->nresults == 1)
    ret = value_to_js(ctx, &res[0]);
  else if(ft->nresults > 1) {
    ret = JS_NewArray(ctx);
    for(size_t i = 0; i < ft->nresults; i++)
      JS_SetPropertyUint32(ctx, ret, (uint32_t)i, value_to_js(ctx, &res[i]));
  }
  return ret;
}

/* ---- Memory ---- */

static void
memory_finalizer(JSRuntime* rt, JSValue val) {
  Memory* m = JS_GetOpaque(val, wasm_memory_class_id);

  if(m) {
    JS_FreeValueRT(rt, m->buffer);
    JS_FreeValueRT(rt, m->owner);
    JS_FreeValueRT(rt, m->env);
    js_free_rt(rt, m);
  }
}

static JSClassDef wasm_memory_class = {"Memory", .finalizer = memory_finalizer};

/* the backing store is owned by the engine: no free callback. */
static void
no_free(JSRuntime* rt, void* opaque, void* ptr) {}

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
  if(argc > 0 && JS_ToUint32(ctx, &delta, argv[0]))
    return JS_EXCEPTION;
  if((prev = WB_GrowMemory(env_of(m->env)->ctx, m->mem, delta)) < 0)
    return throw_backend(ctx, env_of(m->env));
  return JS_NewInt64(ctx, prev);
}

/* wraps `mem`, kept alive by `owner`. */
static JSValue
memory_wrap(JSContext* ctx, JSValueConst env, JSValueConst owner, WBMemory* mem) {
  Memory* m;
  JSValue obj = JS_NewObjectProtoClass(ctx, memory_proto, wasm_memory_class_id);

  if(JS_IsException(obj))
    return obj;
  if(!(m = js_mallocz(ctx, sizeof(*m)))) {
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
  }
  m->env = JS_DupValue(ctx, env);
  m->owner = JS_DupValue(ctx, owner);
  m->mem = mem;
  m->buffer = JS_UNDEFINED;
  JS_SetOpaque(obj, m);
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

/* ---- Instance ---- */

static void
instance_finalizer(JSRuntime* rt, JSValue val) {
  Instance* i = JS_GetOpaque(val, wasm_instance_class_id);

  if(i) {
    Env* e = env_of(i->env);

    if(i->inst && e)
      WB_FreeInstance(e->ctx, i->inst);
    JS_FreeValueRT(rt, i->deps);
    JS_FreeValueRT(rt, i->env);
    js_free_rt(rt, i);
  }
}

static JSClassDef wasm_instance_class = {"Instance", .finalizer = instance_finalizer};

/* resolves one import row from the importObject.
 * returns 0, or -1 with an exception pending (LinkError or TypeError). */
static int
resolve_import(JSContext* ctx, Env* e, JSValueConst env, JSValueConst imports, const WBImportDesc* d, WBExtern* out, JSValueConst deps, uint32_t* ndeps) {
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
      out->kind = WB_EXTERN_FUNC;
      if(!(out->u.func = WB_NewHostFunc(e->ctx, &h->type, host_call, h, host_finalize))) {
        host_finalize(h);
        throw_backend(ctx, e);
        goto done;
      }
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
    default: throw_link_error(ctx, "table and global imports are not supported yet"); break;
  }

done:
  JS_FreeValue(ctx, val);
  JS_FreeValue(ctx, mod);
  (void)env;
  return ret;
}

static JSValue
js_instance_ctor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  Module* mod = argc > 0 ? JS_GetOpaque2(ctx, argv[0], wasm_module_class_id) : NULL;
  Env* e;
  Instance* inst;
  const WBImportDesc* descs;
  const WBExport* exps;
  WBExtern* imports = NULL;
  size_t n, nexp;
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
  inst->deps = JS_NewArray(ctx);

  if(n && !(imports = calloc(n, sizeof(*imports)))) {
    JS_ThrowOutOfMemory(ctx);
    goto fail;
  }
  for(size_t i = 0; i < n; i++)
    if(resolve_import(ctx, e, mod->env, argv[1], &descs[i], &imports[i], inst->deps, &ndeps))
      goto fail;

  if(!(inst->inst = WB_NewInstance(e->ctx, mod->mod, imports, n))) {
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

  if(WB_GetInstanceExports(e->ctx, inst->inst, &exps, &nexp)) {
    throw_backend(ctx, e);
    goto fail_obj;
  }

  exports = JS_NewObjectProto(ctx, JS_NULL);
  for(size_t i = 0; i < nexp; i++) {
    JSValue v = JS_UNDEFINED;

    switch(exps[i].ext.kind) {
      case WB_EXTERN_FUNC: {
        JSValue data[1] = {obj};

        v = JS_NewCFunctionData(ctx, js_func_call, (int)WB_GetFuncType(exps[i].ext.u.func)->nparams, (int)i, 1, data);
        break;
      }
      case WB_EXTERN_MEMORY: v = memory_wrap(ctx, mod->env, obj, exps[i].ext.u.memory); break;
      default: break;
    }
    if(JS_IsException(v)) {
      JS_FreeValue(ctx, exports);
      goto fail_obj;
    }
    if(!JS_IsUndefined(v))
      JS_DefinePropertyValueStr(ctx, exports, exps[i].name, v, JS_PROP_ENUMERABLE);
  }
  JS_DefinePropertyValueStr(ctx, obj, "exports", exports, JS_PROP_ENUMERABLE);
  return obj;

fail_obj:
  JS_FreeValue(ctx, obj); /* the finalizer frees `inst` */
  return JS_EXCEPTION;

fail:
  free(imports);
  if(inst->inst)
    WB_FreeInstance(e->ctx, inst->inst);
  JS_FreeValue(ctx, inst->deps);
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
};

static const JSCFunctionListEntry js_wasm_funcs[] = {
    JS_CFUNC_DEF("validate", 1, js_validate),
};

static const JSCFunctionListEntry js_memory_proto_funcs[] = {
    JS_CGETSET_DEF("buffer", js_memory_buffer, 0),
    JS_CFUNC_DEF("grow", 1, js_memory_grow),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WebAssembly.Memory", JS_PROP_CONFIGURABLE),
};

static int
js_wasm_init(JSContext* ctx, JSModuleDef* m) {
  JSValue module_ctor, instance_ctor;
  static const char* const names[3] = {"CompileError", "LinkError", "RuntimeError"};

  JS_NewClassID(&wasm_env_class_id);
  JS_NewClassID(&wasm_module_class_id);
  JS_NewClassID(&wasm_instance_class_id);
  JS_NewClassID(&wasm_memory_class_id);
  JS_NewClass(JS_GetRuntime(ctx), wasm_env_class_id, &wasm_env_class);
  JS_NewClass(JS_GetRuntime(ctx), wasm_module_class_id, &wasm_module_class);
  JS_NewClass(JS_GetRuntime(ctx), wasm_instance_class_id, &wasm_instance_class);
  JS_NewClass(JS_GetRuntime(ctx), wasm_memory_class_id, &wasm_memory_class);

  module_proto = JS_NewObject(ctx);
  module_ctor = JS_NewCFunction2(ctx, js_module_ctor, "Module", 1, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, module_ctor, module_proto);
  JS_SetPropertyFunctionList(ctx, module_ctor, js_module_static_funcs, countof(js_module_static_funcs));
  JS_SetClassProto(ctx, wasm_module_class_id, JS_DupValue(ctx, module_proto));

  instance_proto = JS_NewObject(ctx);
  instance_ctor = JS_NewCFunction2(ctx, js_instance_ctor, "Instance", 1, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, instance_ctor, instance_proto);
  JS_SetClassProto(ctx, wasm_instance_class_id, JS_DupValue(ctx, instance_proto));

  memory_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, memory_proto, js_memory_proto_funcs, countof(js_memory_proto_funcs));
  JS_SetClassProto(ctx, wasm_memory_class_id, JS_DupValue(ctx, memory_proto));

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

  JS_SetModuleExportList(ctx, m, js_wasm_funcs, countof(js_wasm_funcs));
  JS_SetModuleExport(ctx, m, "backend", JS_NewString(ctx, WB_GetBackendName()));
  JS_SetModuleExport(ctx, m, "Module", module_ctor);
  JS_SetModuleExport(ctx, m, "Instance", instance_ctor);
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
  JS_AddModuleExport(ctx, m, "CompileError");
  JS_AddModuleExport(ctx, m, "LinkError");
  JS_AddModuleExport(ctx, m, "RuntimeError");
  return m;
}
