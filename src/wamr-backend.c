/* wamr-backend.c: wasm-backend.h on top of WAMR (third_party/wamr).
 * depends on: wasm-sections.c (imports, exports), WAMR classic interpreter.
 * rule: no standalone Memory/Table/Global and no table/global/memory
 * imports (WAMR cannot share them across instances); host functions are
 * natives registered per (module, name) that look up the calling instance. */

#include "wasm-backend.h"
#include "wasm-sections.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wasm_export.h"

#define MAX_VALUES 16
#define STACK_SIZE (256 * 1024)
#define PENDING "Exception: host function failed"

struct WBRuntime {
  int unused;
};

struct WBContext {
  WBRuntime* rt;
  WBException ex;
  void* opaque;
};

struct WBModule {
  WBContext* ctx;
  uint8_t* bytes; /* WAMR reads the binary in place until unload */
  size_t len;
  wasm_module_t mod;
  WBSections sec;
};

struct WBInstance {
  int refs;
  WBContext* ctx;
  WBModule* mod;
  wasm_module_inst_t inst;
  wasm_exec_env_t env;
  WBExtern* held; /* the imports, referenced for the instance's lifetime */
  size_t nheld;
  WBExport* exports;
  size_t nexports;
};

struct WBFunc {
  int refs;
  WBContext* ctx;
  WBFuncType type;
  WBValType* tbuf; /* params then results */
  WBHostFunc* fn;  /* host function, or NULL for a wasm function */
  void* opaque;
  WBFinalizer* fin;
  WBInstance* inst; /* owner of a wasm function */
  wasm_function_inst_t wf;
};

struct WBMemory {
  int refs;
  WBInstance* inst; /* owner */
  wasm_memory_inst_t mem;
};

struct WBTable {
  int unused;
};

struct WBGlobal {
  int unused;
};

/* what a registered native knows about its import */
typedef struct Slot {
  const char* module;
  const char* name;
} Slot;

static int wamr_users;                  /* JS thread only: live WBRuntimes */
static Slot** wamr_slots;               /* every registered native */
static size_t wamr_nslots;              /* never freed before wasm_runtime_destroy() */
static NativeSymbol** wamr_symbol_sets; /* arrays handed to WAMR, kept alive */
static size_t wamr_nsets;

/* ---- helpers ---- */

static void
set_ex(WBContext* ctx, WBErrorKind kind, const char* fmt, ...) {
  va_list ap;

  ctx->ex.kind = kind;
  va_start(ap, fmt);
  vsnprintf(ctx->ex.message, sizeof(ctx->ex.message), fmt, ap);
  va_end(ap);
}

static char
sigchar(WBValType t) {
  switch(t) {
    case WB_I32: return 'i';
    case WB_I64: return 'I';
    case WB_F32: return 'f';
    case WB_F64: return 'F';
    default: return '?';
  }
}

static int
kind_of(WBValType t, wasm_valkind_t* k) {
  switch(t) {
    case WB_I32: *k = WASM_I32; return 0;
    case WB_I64: *k = WASM_I64; return 0;
    case WB_F32: *k = WASM_F32; return 0;
    case WB_F64: *k = WASM_F64; return 0;
    default: return -1;
  }
}

static int
type_equal(const WBFuncType* a, const WBFuncType* b) {
  return a->nparams == b->nparams && a->nresults == b->nresults && !memcmp(a->params, b->params, a->nparams * sizeof(WBValType)) &&
         !memcmp(a->results, b->results, a->nresults * sizeof(WBValType));
}

/* ---- backend, runtime, context ---- */

const char*
WB_GetBackendName(void) {
  return "wamr";
}

uint32_t
WB_GetCapabilities(void) {
  return 0;
}

WBRuntime*
WB_NewRuntime(void) {
  WBRuntime* rt = calloc(1, sizeof(*rt));

  if(!rt)
    return NULL;

  if(!wamr_users) {
    RuntimeInitArgs a;

    memset(&a, 0, sizeof(a));
    a.mem_alloc_type = Alloc_With_System_Allocator;

    if(!wasm_runtime_full_init(&a)) {
      free(rt);
      return NULL;
    }
  }

  wamr_users++;
  return rt;
}

void
WB_FreeRuntime(WBRuntime* rt) {
  if(!rt)
    return;

  if(!--wamr_users) {
    wasm_runtime_destroy();

    for(size_t i = 0; i < wamr_nslots; i++) {
      free((void*)wamr_slots[i]->module);
      free((void*)wamr_slots[i]->name);
      free(wamr_slots[i]);
    }

    for(size_t i = 0; i < wamr_nsets; i++) {
      free((void*)wamr_symbol_sets[i]->signature);
      free(wamr_symbol_sets[i]);
    }

    free(wamr_slots);
    free(wamr_symbol_sets);
    wamr_slots = NULL;
    wamr_symbol_sets = NULL;
    wamr_nslots = wamr_nsets = 0;
  }

  free(rt);
}

WBContext*
WB_NewContext(WBRuntime* rt) {
  WBContext* ctx = calloc(1, sizeof(*ctx));

  if(ctx)
    ctx->rt = rt;

  return ctx;
}

void
WB_FreeContext(WBContext* ctx) {
  free(ctx);
}

void
WB_SetContextOpaque(WBContext* ctx, void* opaque) {
  ctx->opaque = opaque;
}

void*
WB_GetContextOpaque(WBContext* ctx) {
  return ctx->opaque;
}

WBErrorKind
WB_GetException(WBContext* ctx, WBException* out) {
  WBErrorKind kind = ctx->ex.kind;

  *out = ctx->ex;
  ctx->ex.kind = WB_ERR_NONE;
  ctx->ex.message[0] = 0;
  return kind;
}

/* ---- host function natives ---- */

static WBFunc* find_import_func(WBInstance* inst, const Slot* slot);

/* the raw native every function import lands in: finds the host function
 * the calling instance was linked with and runs it. */
static void
import_trampoline(wasm_exec_env_t env, uint64_t* raw) {
  wasm_module_inst_t mi = wasm_runtime_get_module_inst(env);
  WBInstance* inst = wasm_runtime_get_custom_data(mi);
  const Slot* slot = wasm_runtime_get_function_attachment(env);
  WBFunc* f = inst && slot ? find_import_func(inst, slot) : NULL;
  WBValue args[MAX_VALUES], res[MAX_VALUES];
  uint32_t* w = (uint32_t*)raw;
  size_t pos = 0;

  if(!f) {
    wasm_runtime_set_exception(mi, "import is not linked");
    return;
  }

  for(size_t i = 0; i < f->type.nparams; i++) {
    args[i].type = f->type.params[i];

    switch(args[i].type) {
      case WB_I32: args[i].u.i32 = (int32_t)w[pos++]; break;
      case WB_F32: memcpy(&args[i].u.f32, &w[pos++], 4); break;
      case WB_I64:
        memcpy(&args[i].u.i64, &w[pos], 8);
        pos += 2;
        break;
      default:
        memcpy(&args[i].u.f64, &w[pos], 8);
        pos += 2;
        break;
    }
  }

  memset(res, 0, sizeof(res));

  if(f->fn(f->ctx, f->opaque, args, res)) {
    if(f->ctx->ex.kind == WB_ERR_NONE)
      f->ctx->ex.kind = WB_ERR_HOST;

    wasm_runtime_set_exception(mi, "host function failed");
    return;
  }

  if(f->type.nresults) {
    switch(f->type.results[0]) {
      case WB_I32: *(int32_t*)w = res[0].u.i32; break;
      case WB_F32: memcpy(w, &res[0].u.f32, 4); break;
      case WB_I64: memcpy(w, &res[0].u.i64, 8); break;
      default: memcpy(w, &res[0].u.f64, 8); break;
    }
  }
}

static int
slot_known(const char* module, const char* name) {
  for(size_t i = 0; i < wamr_nslots; i++)
    if(!strcmp(wamr_slots[i]->module, module) && !strcmp(wamr_slots[i]->name, name))
      return 1;

  return 0;
}

/* registers an import with WAMR once per (module, name), before any load.
 * returns 0, or -1 with an exception pending. */
static int
register_import(WBContext* ctx, const WBImportDesc* d) {
  const WBFuncType* t = &d->type.u.func;
  Slot* slot;
  NativeSymbol* sym;
  char* sig;
  Slot** slots;
  NativeSymbol** sets;

  if(slot_known(d->module, d->name))
    return 0;

  slot = calloc(1, sizeof(*slot));
  sym = calloc(1, sizeof(*sym));
  sig = malloc(t->nparams + t->nresults + 4);

  if(!slot || !sym || !sig)
    goto nomem;

  slot->module = strdup(d->module);
  slot->name = strdup(d->name);
  sym->symbol = slot->name;
  sym->func_ptr = (void*)import_trampoline;
  sym->attachment = slot;

  {
    char* p = sig;

    *p++ = '(';

    for(size_t i = 0; i < t->nparams; i++)
      *p++ = sigchar(t->params[i]);

    *p++ = ')';

    for(size_t i = 0; i < t->nresults; i++)
      *p++ = sigchar(t->results[i]);

    *p = 0;
  }

  /* WAMR's signature letters: i32 'i', i64 'I', f32 'f', f64 'F' */
  sym->signature = sig;

  if(!wasm_runtime_register_natives_raw(slot->module, sym, 1)) {
    set_ex(ctx, WB_ERR_LINK, "cannot register import %s.%s", d->module, d->name);
    goto fail;
  }

  slots = realloc(wamr_slots, (wamr_nslots + 1) * sizeof(*slots));

  if(slots)
    wamr_slots = slots;

  sets = realloc(wamr_symbol_sets, (wamr_nsets + 1) * sizeof(*sets));

  if(sets)
    wamr_symbol_sets = sets;

  if(!slots || !sets)
    goto nomem;

  wamr_slots[wamr_nslots++] = slot;
  wamr_symbol_sets[wamr_nsets++] = sym;
  return 0;

nomem:
  set_ex(ctx, WB_ERR_NOMEM, "out of memory");
fail:
  free(slot);
  free(sym);
  free(sig);
  return -1;
}

/* ---- modules ---- */

WBModule*
WB_NewModule(WBContext* ctx, const uint8_t* bytes, size_t len) {
  WBModule* m = calloc(1, sizeof(*m));
  char err[160];

  if(!m) {
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return NULL;
  }

  m->ctx = ctx;
  m->len = len;

  if(!(m->bytes = malloc(len ? len : 1))) {
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    goto fail;
  }

  memcpy(m->bytes, bytes, len);

  if(wb_sections_parse(&m->sec, m->bytes, len, err, sizeof(err))) {
    set_ex(ctx, WB_ERR_COMPILE, "%s", err);
    goto fail;
  }

  for(size_t i = 0; i < m->sec.nimports; i++) {
    const WBImportDesc* d = &m->sec.imports[i];

    if(d->type.kind != WB_EXTERN_FUNC) {
      set_ex(ctx, WB_ERR_UNSUPPORTED, "wamr backend: table, memory and global imports are not supported");
      goto fail;
    }

    if(register_import(ctx, d))
      goto fail;
  }

  if(!(m->mod = wasm_runtime_load(m->bytes, (uint32_t)len, err, sizeof(err)))) {
    set_ex(ctx, WB_ERR_COMPILE, "%s", err);
    goto fail;
  }

  return m;

fail:
  wb_sections_free(&m->sec);
  free(m->bytes);
  free(m);
  return NULL;
}

int
WB_ValidateModule(WBContext* ctx, const uint8_t* bytes, size_t len) {
  WBModule* m = WB_NewModule(ctx, bytes, len);

  if(!m)
    return -1;

  WB_FreeModule(ctx, m);
  return 0;
}

void
WB_FreeModule(WBContext* ctx, WBModule* mod) {
  (void)ctx;

  if(!mod)
    return;

  wasm_runtime_unload(mod->mod);
  wb_sections_free(&mod->sec);
  free(mod->bytes);
  free(mod);
}

int
WB_GetModuleImports(WBContext* ctx, WBModule* mod, const WBImportDesc** out, size_t* n) {
  (void)ctx;
  *out = mod->sec.imports;
  *n = mod->sec.nimports;
  return 0;
}

int
WB_GetModuleExports(WBContext* ctx, WBModule* mod, const WBExportDesc** out, size_t* n) {
  (void)ctx;
  *out = mod->sec.exports;
  *n = mod->sec.nexports;
  return 0;
}

/* ---- functions ---- */

static WBFunc*
new_func(WBContext* ctx, const WBFuncType* type) {
  WBFunc* f = calloc(1, sizeof(*f));

  if(!f || !(f->tbuf = malloc((type->nparams + type->nresults + 1) * sizeof(WBValType)))) {
    free(f);
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return NULL;
  }

  memcpy(f->tbuf, type->params, type->nparams * sizeof(WBValType));
  memcpy(f->tbuf + type->nparams, type->results, type->nresults * sizeof(WBValType));
  f->refs = 1;
  f->ctx = ctx;
  f->type = (WBFuncType){f->tbuf, type->nparams, f->tbuf + type->nparams, type->nresults};
  return f;
}

WBFunc*
WB_NewHostFunc(WBContext* ctx, const WBFuncType* type, WBHostFunc* fn, void* opaque, WBFinalizer* fin) {
  WBFunc* f;

  if(type->nparams > MAX_VALUES || type->nresults > 1) {
    set_ex(ctx, WB_ERR_UNSUPPORTED, "wamr backend: at most one result");
    return NULL;
  }

  if(!(f = new_func(ctx, type)))
    return NULL;

  f->fn = fn;
  f->opaque = opaque;
  f->fin = fin;
  return f;
}

const WBFuncType*
WB_GetFuncType(WBFunc* func) {
  return &func->type;
}

static void free_instance(WBInstance* inst);

static void
free_func(WBFunc* f) {
  if(--f->refs)
    return;

  if(f->fin)
    f->fin(f->opaque);

  free(f->tbuf);
  free(f);
}

int
WB_CallFunc(WBContext* ctx, WBFunc* func, const WBValue* args, WBValue* results) {
  wasm_val_t a[MAX_VALUES], r[MAX_VALUES];
  const WBFuncType* t = &func->type;

  if(t->nparams > MAX_VALUES || t->nresults > MAX_VALUES) {
    set_ex(ctx, WB_ERR_RANGE, "too many parameters or results");
    return -1;
  }

  if(func->fn)
    return func->fn(ctx, func->opaque, args, results);

  memset(a, 0, sizeof(a));
  memset(r, 0, sizeof(r));

  for(size_t i = 0; i < t->nparams; i++) {
    if(args[i].type != t->params[i] || kind_of(t->params[i], &a[i].kind)) {
      set_ex(ctx, WB_ERR_TYPE, "argument %zu has the wrong type", i);
      return -1;
    }

    switch(args[i].type) {
      case WB_I32: a[i].of.i32 = args[i].u.i32; break;
      case WB_I64: a[i].of.i64 = args[i].u.i64; break;
      case WB_F32: a[i].of.f32 = args[i].u.f32; break;
      default: a[i].of.f64 = args[i].u.f64; break;
    }
  }

  for(size_t i = 0; i < t->nresults; i++)
    kind_of(t->results[i], &r[i].kind);

  ctx->ex.kind = WB_ERR_NONE;

  if(!wasm_runtime_call_wasm_a(func->inst->env, func->wf, (uint32_t)t->nresults, r, (uint32_t)t->nparams, a)) {
    if(ctx->ex.kind == WB_ERR_HOST)
      return -1;

    set_ex(ctx, WB_ERR_TRAP, "%s", wasm_runtime_get_exception(func->inst->inst));
    wasm_runtime_clear_exception(func->inst->inst);
    return -1;
  }

  for(size_t i = 0; i < t->nresults; i++) {
    results[i].type = t->results[i];

    switch(t->results[i]) {
      case WB_I32: results[i].u.i32 = r[i].of.i32; break;
      case WB_I64: results[i].u.i64 = r[i].of.i64; break;
      case WB_F32: results[i].u.f32 = r[i].of.f32; break;
      default: results[i].u.f64 = r[i].of.f64; break;
    }
  }

  return 0;
}

/* ---- instances ---- */

static WBFunc*
find_import_func(WBInstance* inst, const Slot* slot) {
  const WBSections* s = &inst->mod->sec;

  for(size_t i = 0; i < s->nimports && i < inst->nheld; i++)
    if(s->imports[i].type.kind == WB_EXTERN_FUNC && !strcmp(s->imports[i].module, slot->module) && !strcmp(s->imports[i].name, slot->name))
      return inst->held[i].u.func;

  return NULL;
}

static void
free_instance(WBInstance* inst) {
  if(--inst->refs)
    return;

  /* refcount: an export owns itself; a dup'd one also holds a ref on the
   * instance, so every export is at refs == 1 here */
  for(size_t i = 0; i < inst->nexports; i++) {
    if(inst->exports[i].ext.kind == WB_EXTERN_FUNC)
      free_func(inst->exports[i].ext.u.func);
    else
      free(inst->exports[i].ext.u.memory);
  }

  if(inst->env)
    wasm_runtime_destroy_exec_env(inst->env);

  if(inst->inst)
    wasm_runtime_deinstantiate(inst->inst);

  for(size_t i = 0; i < inst->nheld; i++)
    WB_FreeExtern(inst->ctx, inst->held[i]);

  free(inst->held);
  free(inst->exports);
  free(inst);
}

/* builds the exported functions and the default memory.
 * returns 0, or -1 with an exception pending. */
static int
make_exports(WBContext* ctx, WBInstance* inst) {
  const WBSections* s = &inst->mod->sec;

  if(!(inst->exports = calloc(s->nexports ? s->nexports : 1, sizeof(WBExport)))) {
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return -1;
  }

  for(size_t i = 0; i < s->nexports; i++) {
    const WBExportDesc* d = &s->exports[i];
    WBExport* e = &inst->exports[inst->nexports];

    if(d->type.kind == WB_EXTERN_FUNC) {
      WBFunc* f = new_func(ctx, &d->type.u.func);

      if(!f)
        return -1;

      f->wf = wasm_runtime_lookup_function(inst->inst, d->name);
      f->inst = inst;
      e->name = d->name;
      e->ext.kind = WB_EXTERN_FUNC;
      e->ext.u.func = f;
      inst->nexports++;
    } else if(d->type.kind == WB_EXTERN_MEMORY) {
      WBMemory* m = calloc(1, sizeof(*m));

      if(!m) {
        set_ex(ctx, WB_ERR_NOMEM, "out of memory");
        return -1;
      }

      m->refs = 1;
      m->inst = inst;
      m->mem = wasm_runtime_get_default_memory(inst->inst);
      e->name = d->name;
      e->ext.kind = WB_EXTERN_MEMORY;
      e->ext.u.memory = m;
      inst->nexports++;
    }
  }

  return 0;
}

WBInstance*
WB_NewInstance(WBContext* ctx, WBModule* mod, const WBExtern* imports, size_t nimports) {
  WBInstance* inst;
  char err[160];

  if(nimports != mod->sec.nimports) {
    set_ex(ctx, WB_ERR_LINK, "expected %zu imports, got %zu", mod->sec.nimports, nimports);
    return NULL;
  }

  for(size_t i = 0; i < nimports; i++) {
    const WBImportDesc* d = &mod->sec.imports[i];

    if(imports[i].kind != WB_EXTERN_FUNC || !type_equal(&imports[i].u.func->type, &d->type.u.func)) {
      set_ex(ctx, WB_ERR_LINK, "import %s.%s: incompatible import type", d->module, d->name);
      return NULL;
    }
  }

  if(!(inst = calloc(1, sizeof(*inst)))) {
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return NULL;
  }

  inst->refs = 1;
  inst->ctx = ctx;
  inst->mod = mod;

  if(!(inst->held = calloc(nimports ? nimports : 1, sizeof(WBExtern)))) {
    free(inst);
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return NULL;
  }

  for(size_t i = 0; i < nimports; i++)
    inst->held[inst->nheld++] = WB_DupExtern(ctx, imports[i]);

  if(!(inst->inst = wasm_runtime_instantiate(mod->mod, STACK_SIZE, 0, err, sizeof(err)))) {
    set_ex(ctx, WB_ERR_LINK, "%s", err);
    goto fail;
  }

  wasm_runtime_set_custom_data(inst->inst, inst);

  if(!(inst->env = wasm_runtime_create_exec_env(inst->inst, STACK_SIZE))) {
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    goto fail;
  }

  if(make_exports(ctx, inst))
    goto fail;

  return inst;

fail:
  free_instance(inst);
  return NULL;
}

void
WB_FreeInstance(WBContext* ctx, WBInstance* inst) {
  (void)ctx;
  free_instance(inst);
}

int
WB_GetInstanceExports(WBContext* ctx, WBInstance* inst, const WBExport** out, size_t* n) {
  (void)ctx;
  *out = inst->exports;
  *n = inst->nexports;
  return 0;
}

/* ---- externs and values ---- */

WBExtern
WB_DupExtern(WBContext* ctx, WBExtern ext) {
  (void)ctx;

  switch(ext.kind) {
    case WB_EXTERN_FUNC:
      ext.u.func->refs++;

      if(ext.u.func->inst)
        ext.u.func->inst->refs++;

      break;
    case WB_EXTERN_MEMORY:
      ext.u.memory->refs++;
      ext.u.memory->inst->refs++;
      break;
    default: break;
  }

  return ext;
}

void
WB_FreeExtern(WBContext* ctx, WBExtern ext) {
  (void)ctx;

  switch(ext.kind) {
    case WB_EXTERN_FUNC: {
      WBInstance* owner = ext.u.func->inst;

      free_func(ext.u.func);

      if(owner)
        free_instance(owner);

      break;
    }
    case WB_EXTERN_MEMORY: {
      WBInstance* owner = ext.u.memory->inst;

      if(!--ext.u.memory->refs)
        free(ext.u.memory);

      free_instance(owner);
      break;
    }
    default: break;
  }
}

WBValue
WB_DupValue(WBContext* ctx, WBValue v) {
  (void)ctx;
  return v;
}

void
WB_FreeValue(WBContext* ctx, WBValue v) {
  (void)ctx;
  (void)v;
}

/* ---- memory ---- */

WBMemory*
WB_NewMemory(WBContext* ctx, const WBLimits* limits) {
  (void)limits;
  set_ex(ctx, WB_ERR_UNSUPPORTED, "wamr backend: standalone memories are not supported");
  return NULL;
}

uint8_t*
WB_GetMemoryData(WBMemory* mem, size_t* size) {
  *size = (size_t)wasm_memory_get_cur_page_count(mem->mem) * wasm_memory_get_bytes_per_page(mem->mem);
  return wasm_memory_get_base_address(mem->mem);
}

int64_t
WB_GrowMemory(WBContext* ctx, WBMemory* mem, uint32_t delta) {
  uint32_t prev = (uint32_t)wasm_memory_get_cur_page_count(mem->mem);

  if(!wasm_memory_enlarge(mem->mem, delta)) {
    set_ex(ctx, WB_ERR_RANGE, "memory.grow failed");
    return -1;
  }

  return prev;
}

/* ---- tables and globals ---- */

WBTable*
WB_NewTable(WBContext* ctx, WBValType elem, const WBLimits* limits) {
  (void)elem;
  (void)limits;
  set_ex(ctx, WB_ERR_UNSUPPORTED, "wamr backend: standalone tables are not supported");
  return NULL;
}

uint32_t
WB_GetTableSize(WBTable* table) {
  (void)table;
  return 0;
}

int
WB_GetTableElem(WBContext* ctx, WBTable* table, uint32_t index, WBValue* out) {
  (void)table;
  (void)index;
  (void)out;
  set_ex(ctx, WB_ERR_UNSUPPORTED, "wamr backend: tables are not supported");
  return -1;
}

int
WB_SetTableElem(WBContext* ctx, WBTable* table, uint32_t index, const WBValue* v) {
  (void)table;
  (void)index;
  (void)v;
  set_ex(ctx, WB_ERR_UNSUPPORTED, "wamr backend: tables are not supported");
  return -1;
}

int64_t
WB_GrowTable(WBContext* ctx, WBTable* table, uint32_t delta, const WBValue* init) {
  (void)table;
  (void)delta;
  (void)init;
  set_ex(ctx, WB_ERR_UNSUPPORTED, "wamr backend: tables are not supported");
  return -1;
}

WBGlobal*
WB_NewGlobal(WBContext* ctx, int is_mutable, const WBValue* init) {
  (void)is_mutable;
  (void)init;
  set_ex(ctx, WB_ERR_UNSUPPORTED, "wamr backend: standalone globals are not supported");
  return NULL;
}

void
WB_GetGlobal(WBContext* ctx, WBGlobal* global, WBValue* out) {
  (void)global;
  set_ex(ctx, WB_ERR_UNSUPPORTED, "wamr backend: globals are not supported");
  memset(out, 0, sizeof(*out));
}

int
WB_SetGlobal(WBContext* ctx, WBGlobal* global, const WBValue* v) {
  (void)global;
  (void)v;
  set_ex(ctx, WB_ERR_UNSUPPORTED, "wamr backend: globals are not supported");
  return -1;
}

WBValType
WB_GetGlobalType(WBGlobal* global, int* is_mutable) {
  (void)global;
  *is_mutable = 0;
  return WB_I32;
}
