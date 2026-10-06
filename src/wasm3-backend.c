/* wasm3-backend.c: wasm-backend.h on top of wasm3.
 * depends on: wasm-sections.c (imports, exports), wasm3 (third_party/wasm3).
 * rule: a Memory, Table or Global is shared by naming its owning module in
 * the importer's import section (rewritten per instance); the runtime keeps
 * every module until the context is freed. */

#include "wasm-backend.h"
#include "wasm-sections.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wasm3.h"
#include "m3_env.h"

/* returned from a raw function when an exception is already pending */
#define PENDING "exception pending"
#define MAX_VALUES 16

typedef struct List {
  void** v;
  size_t n;
} List;

struct WBRuntime {
  IM3Environment env;
};

struct WBContext {
  WBRuntime* rt;
  IM3Runtime m3;
  WBException ex;
  void* opaque;
  unsigned serial;
  List keep;  /* byte buffers wasm3 reads in place */
  List insts; /* WBInstance*, freed with the context */
};

struct WBModule {
  int refs;
  WBContext* ctx;
  uint8_t* bytes; /* owned by ctx->keep */
  size_t len;
  WBSections sec;
};

struct WBInstance {
  int refs;
  WBContext* ctx;
  IM3Module m3;
  char name[24];
  WBExport* exports;
  size_t nexports;
  WBExtern* held; /* the imports, referenced for the instance's lifetime */
  size_t nheld;
};

struct WBFunc {
  int refs;
  WBContext* ctx;
  WBFuncType type;
  WBValType* tbuf; /* params then results */
  WBHostFunc* fn;  /* host function, or NULL for a wasm function */
  void* opaque;
  WBFinalizer* fin;
  IM3Function wf;
};

/* where a Memory, Table or Global lives: the owner module's name and the
 * name it exports the object under */
typedef struct Owner {
  IM3Module m3;
  char module[24];
  char* field;
} Owner;

struct WBMemory {
  int refs;
  WBContext* ctx;
  Owner owner;
  uint32_t index;
};

struct WBTable {
  int refs;
  WBContext* ctx;
  Owner owner;
  uint32_t index; /* in the owner module's table index space */
  WBValType elem;
  WBLimits limits;
};

struct WBGlobal {
  int refs;
  WBContext* ctx;
  Owner owner;
  IM3Global g;
  WBValType type;
  int is_mutable;
};

/* ---- helpers ---- */

static void
set_ex(WBContext* ctx, WBErrorKind kind, const char* fmt, ...) {
  va_list ap;

  ctx->ex.kind = kind;
  va_start(ap, fmt);
  vsnprintf(ctx->ex.message, sizeof(ctx->ex.message), fmt, ap);
  va_end(ap);
}

static int
list_add(List* l, void* p) {
  void** v = realloc(l->v, (l->n + 1) * sizeof(void*));

  if(!v)
    return -1;

  l->v = v;
  l->v[l->n++] = p;
  return 0;
}

/* hands a buffer to the context; wasm3 keeps pointing into it. returns 0,
 * or -1 with an exception pending. */
static int
keep(WBContext* ctx, void* buf) {
  if(list_add(&ctx->keep, buf)) {
    free(buf);
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return -1;
  }

  return 0;
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
valtype_from_m3(M3ValueType t, WBValType* out) {
  switch(t) {
    case c_m3Type_i32: *out = WB_I32; return 0;
    case c_m3Type_i64: *out = WB_I64; return 0;
    case c_m3Type_f32: *out = WB_F32; return 0;
    case c_m3Type_f64: *out = WB_F64; return 0;
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
  return "wasm3";
}

uint32_t
WB_GetCapabilities(void) {
  return WB_CAP_STANDALONE_MEMORY;
}

WBRuntime*
WB_NewRuntime(void) {
  WBRuntime* rt = calloc(1, sizeof(*rt));

  if(rt && !(rt->env = m3_NewEnvironment())) {
    free(rt);
    rt = NULL;
  }

  return rt;
}

void
WB_FreeRuntime(WBRuntime* rt) {
  if(rt) {
    m3_FreeEnvironment(rt->env);
    free(rt);
  }
}

WBContext*
WB_NewContext(WBRuntime* rt) {
  WBContext* ctx = calloc(1, sizeof(*ctx));

  if(!ctx)
    return NULL;

  ctx->rt = rt;

  if(!(ctx->m3 = m3_NewRuntime(rt->env, 1024 * 1024, ctx))) {
    free(ctx);
    return NULL;
  }

  return ctx;
}

static void free_extern_force(WBExtern ext);

void
WB_FreeContext(WBContext* ctx) {
  if(!ctx)
    return;

  for(size_t i = 0; i < ctx->insts.n; i++) {
    WBInstance* inst = ctx->insts.v[i];

    for(size_t j = 0; j < inst->nexports; j++)
      free_extern_force(inst->exports[j].ext);

    for(size_t j = 0; j < inst->nheld; j++)
      WB_FreeExtern(ctx, inst->held[j]);

    free(inst->exports);
    free(inst->held);
    free(inst);
  }

  m3_FreeRuntime(ctx->m3);

  for(size_t i = 0; i < ctx->keep.n; i++)
    free(ctx->keep.v[i]);

  free(ctx->keep.v);
  free(ctx->insts.v);
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

/* ---- modules ---- */

/* checks the sections and wasm3's parse, keeping a copy of the bytes for the
 * life of the context (wasm3 reads them in place).
 * returns the copy, or NULL with an exception pending. */
static uint8_t*
check_module(WBContext* ctx, const uint8_t* bytes, size_t len) {
  WBSections sec;
  IM3Module m;
  M3Result r;
  char err[128];
  uint8_t* copy;

  if(wb_sections_parse(&sec, bytes, len, err, sizeof(err))) {
    set_ex(ctx, WB_ERR_COMPILE, "%s", err);
    return NULL;
  }

  wb_sections_free(&sec);

  if(!(copy = malloc(len ? len : 1))) {
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return NULL;
  }

  memcpy(copy, bytes, len);

  if(keep(ctx, copy))
    return NULL;

  if((r = m3_ParseModule(ctx->rt->env, &m, copy, (uint32_t)len))) {
    set_ex(ctx, WB_ERR_COMPILE, "%s", r);
    return NULL;
  }

  m3_FreeModule(m);
  return copy;
}

int
WB_ValidateModule(WBContext* ctx, const uint8_t* bytes, size_t len) {
  return check_module(ctx, bytes, len) ? 0 : -1;
}

WBModule*
WB_NewModule(WBContext* ctx, const uint8_t* bytes, size_t len) {
  WBModule* mod;
  uint8_t* copy;
  char err[128];

  if(!(copy = check_module(ctx, bytes, len)))
    return NULL;

  if(!(mod = calloc(1, sizeof(*mod)))) {
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return NULL;
  }

  if(wb_sections_parse(&mod->sec, copy, len, err, sizeof(err))) {
    set_ex(ctx, WB_ERR_COMPILE, "%s", err);
    free(mod);
    return NULL;
  }

  mod->refs = 1;
  mod->ctx = ctx;
  mod->bytes = copy;
  mod->len = len;
  return mod;
}

void
WB_FreeModule(WBContext* ctx, WBModule* mod) {
  (void)ctx;

  if(mod && --mod->refs == 0) {
    wb_sections_free(&mod->sec);
    free(mod);
  }
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

int
WB_GetCustomSection(WBContext* ctx, WBModule* mod, const char* name, size_t idx, const uint8_t** data, size_t* len) {
  (void)ctx;
  return wb_sections_custom(&mod->sec, mod->bytes, name, idx, data, len);
}

/* ---- functions ---- */

static WBFunc*
new_func(WBContext* ctx, const WBFuncType* type) {
  WBFunc* f = calloc(1, sizeof(*f));
  size_t n = type->nparams + type->nresults;

  if(!f || !(f->tbuf = calloc(n ? n : 1, sizeof(WBValType)))) {
    free(f);
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return NULL;
  }

  memcpy(f->tbuf, type->params, type->nparams * sizeof(WBValType));
  memcpy(f->tbuf + type->nparams, type->results, type->nresults * sizeof(WBValType));
  f->type = (WBFuncType){f->tbuf, type->nparams, f->tbuf + type->nparams, type->nresults};
  f->refs = 1;
  f->ctx = ctx;
  return f;
}

WBFunc*
WB_NewHostFunc(WBContext* ctx, const WBFuncType* type, WBHostFunc* fn, void* opaque, WBFinalizer* fin) {
  WBFunc* f;

  if(type->nparams > MAX_VALUES || type->nresults > MAX_VALUES) {
    set_ex(ctx, WB_ERR_RANGE, "too many parameters or results");
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

/* a wasm function value from an IM3Function, e.g. a table element.
 * returns NULL with an exception pending. */
static WBFunc*
wrap_m3_function(WBContext* ctx, IM3Function f) {
  WBValType params[MAX_VALUES], results[MAX_VALUES];
  uint32_t np = m3_GetArgCount(f), nr = m3_GetRetCount(f);
  WBFuncType type = {params, np, results, nr};
  WBFunc* fn;

  if(np > MAX_VALUES || nr > MAX_VALUES) {
    set_ex(ctx, WB_ERR_UNSUPPORTED, "too many parameters or results");
    return NULL;
  }

  for(uint32_t i = 0; i < np; i++)
    if(valtype_from_m3(m3_GetArgType(f, i), &params[i])) {
      set_ex(ctx, WB_ERR_UNSUPPORTED, "unsupported parameter type");
      return NULL;
    }

  for(uint32_t i = 0; i < nr; i++)
    if(valtype_from_m3(m3_GetRetType(f, i), &results[i])) {
      set_ex(ctx, WB_ERR_UNSUPPORTED, "unsupported result type");
      return NULL;
    }

  if(!(fn = new_func(ctx, &type)))
    return NULL;

  fn->wf = f;
  return fn;
}

static void
value_to_slot(const WBValue* v, uint64_t* slot) {
  *slot = 0;

  switch(v->type) {
    case WB_I32: memcpy(slot, &v->u.i32, 4); break;
    case WB_I64: memcpy(slot, &v->u.i64, 8); break;
    case WB_F32: memcpy(slot, &v->u.f32, 4); break;
    case WB_F64: memcpy(slot, &v->u.f64, 8); break;
    default: break;
  }
}

static void
slot_to_value(WBValType type, const uint64_t* slot, WBValue* v) {
  v->type = type;

  switch(type) {
    case WB_I32: memcpy(&v->u.i32, slot, 4); break;
    case WB_I64: memcpy(&v->u.i64, slot, 8); break;
    case WB_F32: memcpy(&v->u.f32, slot, 4); break;
    case WB_F64: memcpy(&v->u.f64, slot, 8); break;
    default: v->u.ref = NULL; break;
  }
}

/* a wasm import's body: converts the raw stack to WBValues, calls the
 * WBFunc the importer was linked to, and writes the results back. */
static const void*
import_trampoline(IM3Runtime rt, IM3ImportContext ic, uint64_t* sp, void* mem) {
  WBFunc* f = ic->userdata;
  WBValue args[MAX_VALUES], res[MAX_VALUES];
  size_t np = f->type.nparams, nr = f->type.nresults;
  int rc;

  (void)rt;
  (void)mem;

  for(size_t i = 0; i < np; i++)
    slot_to_value(f->type.params[i], &sp[nr + i], &args[i]);

  rc = WB_CallFunc(f->ctx, f, args, res);

  if(rc) {
    if(f->ctx->ex.kind == WB_ERR_NONE)
      set_ex(f->ctx, WB_ERR_HOST, "host function failed");

    return PENDING;
  }

  for(size_t i = 0; i < nr; i++)
    value_to_slot(&res[i], &sp[i]);

  return m3Err_none;
}

int
WB_CallFunc(WBContext* ctx, WBFunc* func, const WBValue* args, WBValue* results) {
  size_t np = func->type.nparams, nr = func->type.nresults;
  const void* ap[MAX_VALUES];
  const void* rp[MAX_VALUES];
  M3Result r;

  if(func->fn) {
    if(func->fn(ctx, func->opaque, args, results)) {
      if(ctx->ex.kind == WB_ERR_NONE)
        set_ex(ctx, WB_ERR_HOST, "host function failed");

      return -1;
    }

    return 0;
  }

  for(size_t i = 0; i < np; i++) {
    if(args[i].type != func->type.params[i]) {
      set_ex(ctx, WB_ERR_TYPE, "argument %zu has the wrong type", i);
      return -1;
    }

    switch(args[i].type) {
      case WB_I32: ap[i] = &args[i].u.i32; break;
      case WB_I64: ap[i] = &args[i].u.i64; break;
      case WB_F32: ap[i] = &args[i].u.f32; break;
      default: ap[i] = &args[i].u.f64; break;
    }
  }

  if((r = m3_Call(func->wf, (uint32_t)np, ap))) {
    if(strcmp(r, PENDING))
      set_ex(ctx, WB_ERR_TRAP, "%s", r);
    else if(ctx->ex.kind == WB_ERR_NONE)
      set_ex(ctx, WB_ERR_HOST, "host function failed");

    return -1;
  }

  for(size_t i = 0; i < nr; i++) {
    results[i].type = func->type.results[i];

    switch(results[i].type) {
      case WB_I32: rp[i] = &results[i].u.i32; break;
      case WB_I64: rp[i] = &results[i].u.i64; break;
      case WB_F32: rp[i] = &results[i].u.f32; break;
      default: rp[i] = &results[i].u.f64; break;
    }
  }

  if(nr && (r = m3_GetResults(func->wf, (uint32_t)nr, rp))) {
    set_ex(ctx, WB_ERR_TRAP, "%s", r);
    return -1;
  }

  return 0;
}

/* ---- memories ---- */

static WBMemory*
new_memory(WBContext* ctx, IM3Module m3, const char* module_name, const char* field, uint32_t index) {
  WBMemory* mem = calloc(1, sizeof(*mem));

  if(!mem || !(mem->owner.field = strdup(field))) {
    free(mem);
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return NULL;
  }

  mem->refs = 1;
  mem->ctx = ctx;
  mem->owner.m3 = m3;
  snprintf(mem->owner.module, sizeof(mem->owner.module), "%s", module_name);
  mem->index = index;
  return mem;
}

WBMemory*
WB_NewMemory(WBContext* ctx, const WBLimits* limits) {
  /* (module (memory (export "m") min [max])), loaded under its own name so
   * that an importer's import section can point at it */
  uint8_t b[48] = {0, 'a', 's', 'm', 1, 0, 0, 0};
  size_t n = 8, sec;
  uint8_t* copy;
  IM3Module m3;
  WBMemory* mem;
  M3Result r;
  char name[24];

  if(limits->has_max && limits->max < limits->min) {
    set_ex(ctx, WB_ERR_RANGE, "maximum is less than initial");
    return NULL;
  }

  b[n++] = 5; /* memory section */
  sec = n++;
  b[n++] = 1;
  b[n++] = limits->has_max ? 1 : 0;
  n += wb_put_u32(b + n, limits->min);

  if(limits->has_max)
    n += wb_put_u32(b + n, limits->max);

  b[sec] = (uint8_t)(n - sec - 1);
  b[n++] = 7; /* export section */
  b[n++] = 5;
  b[n++] = 1;
  b[n++] = 1;
  b[n++] = 'm';
  b[n++] = 2;
  b[n++] = 0;

  if(!(copy = malloc(n)))
    return set_ex(ctx, WB_ERR_NOMEM, "out of memory"), NULL;

  memcpy(copy, b, n);

  if(keep(ctx, copy))
    return NULL;

  if((r = m3_ParseModule(ctx->rt->env, &m3, copy, (uint32_t)n))) {
    set_ex(ctx, WB_ERR_RANGE, "%s", r);
    return NULL;
  }

  snprintf(name, sizeof(name), "wbm%u", ++ctx->serial);

  /* the name outlives this function: it lives in the Memory below */
  if(!(mem = new_memory(ctx, m3, name, "m", 0))) {
    m3_FreeModule(m3);
    return NULL;
  }

  m3_SetModuleName(m3, mem->owner.module);

  if((r = m3_LoadModule(ctx->m3, m3))) {
    set_ex(ctx, WB_ERR_RANGE, "%s", r);
    free(mem->owner.field);
    free(mem);
    return NULL;
  }

  return mem;
}

uint8_t*
WB_GetMemoryData(WBMemory* mem, size_t* size) {
  return m3_GetMemory(mem->owner.m3, size, mem->index);
}

int64_t
WB_GrowMemory(WBContext* ctx, WBMemory* mem, uint32_t delta) {
  IM3Memory mm;
  uint64_t old;
  M3Result r;

  if(mem->index >= mem->owner.m3->numMemories || !(mm = mem->owner.m3->memories[mem->index])) {
    set_ex(ctx, WB_ERR_RANGE, "no such memory");
    return -1;
  }

  old = mm->numPages;

  if(mm->hasMax && old + delta > mm->maxPages) {
    set_ex(ctx, WB_ERR_RANGE, "memory.grow beyond the maximum");
    return -1;
  }

  if(delta && (r = ResizeMemory(ctx->m3, mm, old + delta))) {
    set_ex(ctx, WB_ERR_NOMEM, "%s", r);
    return -1;
  }

  return (int64_t)old;
}

/* ---- tables ---- */

/* loads `b` (n bytes) as a module of its own, named so that an importer's
 * import section can point at it; fills `owner`.
 * returns 0, or -1 with an exception pending. */
static int
load_synth(WBContext* ctx, const uint8_t* b, size_t n, Owner* owner, const char* field) {
  uint8_t* copy = malloc(n);
  IM3Module m3;
  M3Result r;

  if(!copy)
    return set_ex(ctx, WB_ERR_NOMEM, "out of memory"), -1;

  memcpy(copy, b, n);

  if(keep(ctx, copy))
    return -1;

  if((r = m3_ParseModule(ctx->rt->env, &m3, copy, (uint32_t)n))) {
    set_ex(ctx, WB_ERR_RANGE, "%s", r);
    return -1;
  }

  if(!(owner->field = strdup(field))) {
    m3_FreeModule(m3);
    return set_ex(ctx, WB_ERR_NOMEM, "out of memory"), -1;
  }

  owner->m3 = m3;
  snprintf(owner->module, sizeof(owner->module), "wbm%u", ++ctx->serial);
  m3_SetModuleName(m3, owner->module);

  if((r = m3_LoadModule(ctx->m3, m3))) {
    set_ex(ctx, WB_ERR_RANGE, "%s", r);
    free(owner->field);
    owner->field = NULL;
    return -1;
  }

  return 0;
}

/* the engine's table behind `table`. never throws: NULL if it is gone. */
static IM3Table
find_table(WBTable* table) {
  return table->index < table->owner.m3->numTables ? table->owner.m3->tables[table->index] : NULL;
}

WBTable*
WB_NewTable(WBContext* ctx, WBValType elem, const WBLimits* limits) {
  /* (module (table (export "t") min [max] funcref)) */
  uint8_t b[48] = {0, 'a', 's', 'm', 1, 0, 0, 0};
  size_t n = 8, sec;
  WBTable* t;

  if(elem != WB_FUNCREF) {
    set_ex(ctx, WB_ERR_UNSUPPORTED, "only funcref (anyfunc) tables are supported");
    return NULL;
  }

  if(limits->has_max && limits->max < limits->min) {
    set_ex(ctx, WB_ERR_RANGE, "maximum is less than initial");
    return NULL;
  }

  b[n++] = 4; /* table section */
  sec = n++;
  b[n++] = 1;
  b[n++] = 0x70;
  b[n++] = limits->has_max ? 1 : 0;
  n += wb_put_u32(b + n, limits->min);

  if(limits->has_max)
    n += wb_put_u32(b + n, limits->max);

  b[sec] = (uint8_t)(n - sec - 1);
  b[n++] = 7; /* export section */
  b[n++] = 5;
  b[n++] = 1;
  b[n++] = 1;
  b[n++] = 't';
  b[n++] = 1;
  b[n++] = 0;

  if(!(t = calloc(1, sizeof(*t))))
    return set_ex(ctx, WB_ERR_NOMEM, "out of memory"), NULL;

  if(load_synth(ctx, b, n, &t->owner, "t")) {
    free(t);
    return NULL;
  }

  t->refs = 1;
  t->ctx = ctx;
  t->elem = elem;
  t->limits = *limits;
  return t;
}

uint32_t
WB_GetTableSize(WBTable* table) {
  IM3Table t = find_table(table);

  return t ? t->size : 0;
}

int
WB_GetTableElem(WBContext* ctx, WBTable* table, uint32_t index, WBValue* out) {
  IM3Table t = find_table(table);
  IM3Function f;

  if(!t || index >= t->size) {
    set_ex(ctx, WB_ERR_RANGE, "table index %u out of range", index);
    return -1;
  }

  out->type = WB_FUNCREF;
  out->u.ref = NULL;

  if((f = t->elements[index])) {
    M3Result r;

    if(!f->compiled && (r = CompileFunction(f))) {
      set_ex(ctx, WB_ERR_LINK, "%s", r);
      return -1;
    }

    if(!(out->u.ref = wrap_m3_function(ctx, f)))
      return -1;
  }

  return 0;
}

int
WB_SetTableElem(WBContext* ctx, WBTable* table, uint32_t index, const WBValue* v) {
  IM3Table t = find_table(table);

  if(!t || index >= t->size) {
    set_ex(ctx, WB_ERR_RANGE, "table index %u out of range", index);
    return -1;
  }

  if(v->type != WB_FUNCREF) {
    set_ex(ctx, WB_ERR_TYPE, "value is not a function reference");
    return -1;
  }

  if(v->u.ref && !((WBFunc*)v->u.ref)->wf) {
    set_ex(ctx, WB_ERR_TYPE, "only exported WebAssembly functions can be stored in a table");
    return -1;
  }

  t->elements[index] = v->u.ref ? ((WBFunc*)v->u.ref)->wf : NULL;
  return 0;
}

int64_t
WB_GrowTable(WBContext* ctx, WBTable* table, uint32_t delta, const WBValue* init) {
  IM3Table t = find_table(table);
  IM3Runtime rt = ctx->m3;
  void* fill = NULL;
  uint32_t old, max;
  void** elements;

  if(!t) {
    set_ex(ctx, WB_ERR_RANGE, "no such table");
    return -1;
  }

  if(init && init->u.ref && !((WBFunc*)init->u.ref)->wf) {
    set_ex(ctx, WB_ERR_TYPE, "only exported WebAssembly functions can be stored in a table");
    return -1;
  }

  if(init && init->u.ref)
    fill = ((WBFunc*)init->u.ref)->wf;

  old = t->size;
  max = t->maxSize ? t->maxSize : d_m3MaxSaneTableSize;

  if(delta > max - old || (rt->tableElementsLimit && delta > rt->tableElementsLimit - rt->tableElementsUsed)) {
    set_ex(ctx, WB_ERR_RANGE, "table.grow beyond the maximum");
    return -1;
  }

  if(!delta)
    return old;

  if(!(elements = m3_ReallocArray(void*, t->elements, (size_t)old + delta, old))) {
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return -1;
  }

  rt->tableElementsUsed += delta;
  t->elements = elements;
  t->size = old + delta;

  for(uint32_t i = old; i < t->size; i++)
    t->elements[i] = fill;

  return old;
}

/* ---- globals ---- */

/* writes `v` as a signed LEB128 at `dst`; returns the byte count. */
static size_t
put_sleb(uint8_t* dst, int64_t v) {
  size_t n = 0;
  int more = 1;

  while(more) {
    uint8_t byte = v & 0x7f;

    v >>= 7;
    more = !((v == 0 && !(byte & 0x40)) || (v == -1 && (byte & 0x40)));
    dst[n++] = more ? (byte | 0x80) : byte;
  }

  return n;
}

WBGlobal*
WB_NewGlobal(WBContext* ctx, int is_mutable, const WBValue* init) {
  /* (module (global (export "g") (mut? T) (T.const v))) */
  uint8_t b[48] = {0, 'a', 's', 'm', 1, 0, 0, 0};
  size_t n = 8, sec;
  WBGlobal* g;

  b[n++] = 6; /* global section */
  sec = n++;
  b[n++] = 1;

  switch(init->type) {
    case WB_I32:
      b[n++] = 0x7f;
      b[n++] = is_mutable ? 1 : 0;
      b[n++] = 0x41;
      n += put_sleb(b + n, init->u.i32);
      break;
    case WB_I64:
      b[n++] = 0x7e;
      b[n++] = is_mutable ? 1 : 0;
      b[n++] = 0x42;
      n += put_sleb(b + n, init->u.i64);
      break;
    case WB_F32:
      b[n++] = 0x7d;
      b[n++] = is_mutable ? 1 : 0;
      b[n++] = 0x43;
      memcpy(b + n, &init->u.f32, 4); /* little-endian: IEEE bits as stored */
      n += 4;
      break;
    case WB_F64:
      b[n++] = 0x7c;
      b[n++] = is_mutable ? 1 : 0;
      b[n++] = 0x44;
      memcpy(b + n, &init->u.f64, 8); /* little-endian: IEEE bits as stored */
      n += 8;
      break;
    default: set_ex(ctx, WB_ERR_UNSUPPORTED, "only i32, i64, f32 and f64 globals are supported"); return NULL;
  }

  b[n++] = 0x0b;
  b[sec] = (uint8_t)(n - sec - 1);
  b[n++] = 7; /* export section */
  b[n++] = 5;
  b[n++] = 1;
  b[n++] = 1;
  b[n++] = 'g';
  b[n++] = 3;
  b[n++] = 0;

  if(!(g = calloc(1, sizeof(*g))))
    return set_ex(ctx, WB_ERR_NOMEM, "out of memory"), NULL;

  if(load_synth(ctx, b, n, &g->owner, "g")) {
    free(g);
    return NULL;
  }

  if(!(g->g = m3_FindGlobal(g->owner.m3, "g"))) {
    free(g->owner.field);
    free(g);
    set_ex(ctx, WB_ERR_LINK, "global lookup failed");
    return NULL;
  }

  g->refs = 1;
  g->ctx = ctx;
  g->type = init->type;
  g->is_mutable = is_mutable;
  return g;
}

void
WB_GetGlobal(WBContext* ctx, WBGlobal* global, WBValue* out) {
  M3TaggedValue tv;

  (void)ctx;
  memset(out, 0, sizeof(*out));
  out->type = global->type;

  if(!m3_GetGlobal(global->g, &tv)) {
    switch(global->type) {
      case WB_I32: out->u.i32 = (int32_t)tv.value.i32; break;
      case WB_I64: out->u.i64 = (int64_t)tv.value.i64; break;
      case WB_F32: out->u.f32 = tv.value.f32; break;
      case WB_F64: out->u.f64 = tv.value.f64; break;
      default: break;
    }
  }
}

int
WB_SetGlobal(WBContext* ctx, WBGlobal* global, const WBValue* v) {
  M3TaggedValue tv;
  M3Result r;

  if(!global->is_mutable || v->type != global->type) {
    set_ex(ctx, WB_ERR_TYPE, "%s", global->is_mutable ? "type mismatch" : "global is immutable");
    return -1;
  }

  memset(&tv, 0, sizeof(tv));

  switch(v->type) {
    case WB_I32: tv.type = c_m3Type_i32, tv.value.i32 = (uint32_t)v->u.i32; break;
    case WB_I64: tv.type = c_m3Type_i64, tv.value.i64 = (uint64_t)v->u.i64; break;
    case WB_F32: tv.type = c_m3Type_f32, tv.value.f32 = v->u.f32; break;
    default: tv.type = c_m3Type_f64, tv.value.f64 = v->u.f64; break;
  }

  if((r = m3_SetGlobal(global->g, &tv))) {
    set_ex(ctx, WB_ERR_TYPE, "%s", r);
    return -1;
  }

  return 0;
}

WBValType
WB_GetGlobalType(WBGlobal* global, int* is_mutable) {
  if(is_mutable)
    *is_mutable = global->is_mutable;

  return global->type;
}

/* ---- reference counting ---- */

static void
free_extern_force(WBExtern ext) {
  switch(ext.kind) {
    case WB_EXTERN_FUNC:
      if(ext.u.func->fin)
        ext.u.func->fin(ext.u.func->opaque);

      free(ext.u.func->tbuf);
      free(ext.u.func);
      break;
    case WB_EXTERN_MEMORY:
      free(ext.u.memory->owner.field);
      free(ext.u.memory);
      break;
    case WB_EXTERN_TABLE:
      free(ext.u.table->owner.field);
      free(ext.u.table);
      break;
    case WB_EXTERN_GLOBAL:
      free(ext.u.global->owner.field);
      free(ext.u.global);
      break;
  }
}

static int*
refs_of(WBExtern ext) {
  switch(ext.kind) {
    case WB_EXTERN_FUNC: return &ext.u.func->refs;
    case WB_EXTERN_MEMORY: return &ext.u.memory->refs;
    case WB_EXTERN_TABLE: return &ext.u.table->refs;
    default: return &ext.u.global->refs;
  }
}

WBExtern
WB_DupExtern(WBContext* ctx, WBExtern ext) {
  (void)ctx;
  ++*refs_of(ext);
  return ext;
}

void
WB_FreeExtern(WBContext* ctx, WBExtern ext) {
  (void)ctx;

  if(--*refs_of(ext) == 0)
    free_extern_force(ext);
}

WBValue
WB_DupValue(WBContext* ctx, WBValue v) {
  if(v.type == WB_FUNCREF && v.u.ref)
    WB_DupExtern(ctx, (WBExtern){WB_EXTERN_FUNC, {.func = v.u.ref}});

  return v;
}

void
WB_FreeValue(WBContext* ctx, WBValue v) {
  if(v.type == WB_FUNCREF && v.u.ref)
    WB_FreeExtern(ctx, (WBExtern){WB_EXTERN_FUNC, {.func = v.u.ref}});
}

/* ---- instances ---- */

/* the import section's bytes with every memory, table and global import
 * renamed to the module and export that owns it. returns a buffer owned by
 * the context, or NULL with an exception pending. */
/* the (module, field) pair an import is written with in the instance's
 * bytes: its own names for a function, its owner's for anything shared. */
static void
import_names(const WBImportDesc* d, WBExtern ext, const char** mname, const char** fname) {
  *mname = d->module;
  *fname = d->name;

  switch(d->type.kind) {
    case WB_EXTERN_MEMORY:
      *mname = ext.u.memory->owner.module;
      *fname = ext.u.memory->owner.field;
      break;
    case WB_EXTERN_TABLE:
      *mname = ext.u.table->owner.module;
      *fname = ext.u.table->owner.field;
      break;
    case WB_EXTERN_GLOBAL:
      *mname = ext.u.global->owner.module;
      *fname = ext.u.global->owner.field;
      break;
    default: break;
  }
}

static uint8_t*
rewrite_imports(WBContext* ctx, WBModule* mod, const WBExtern* imports, size_t* outlen) {
  WBSections* s = &mod->sec;
  size_t cap = mod->len + 64, n;
  uint8_t* out;
  uint8_t* p;

  for(size_t i = 0; i < s->nimports; i++) {
    const char *m, *f;

    import_names(&s->imports[i], imports[i], &m, &f);
    cap += 64 + strlen(m) + strlen(f);
  }

  if(!(out = malloc(cap))) {
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return NULL;
  }

  memcpy(out, mod->bytes, s->import_off);
  p = out + s->import_off;

  {
    uint8_t* payload = malloc(cap);
    uint8_t* q = payload;

    if(!payload) {
      free(out);
      set_ex(ctx, WB_ERR_NOMEM, "out of memory");
      return NULL;
    }

    q += wb_put_u32(q, (uint32_t)s->nimports);

    for(size_t i = 0; i < s->nimports; i++) {
      const WBImportDesc* d = &s->imports[i];
      const char *mname, *fname;
      size_t ml, fl;

      import_names(d, imports[i], &mname, &fname);
      ml = strlen(mname);
      fl = strlen(fname);
      q += wb_put_u32(q, (uint32_t)ml);
      memcpy(q, mname, ml);
      q += ml;
      q += wb_put_u32(q, (uint32_t)fl);
      memcpy(q, fname, fl);
      q += fl;

      switch(d->type.kind) {
        case WB_EXTERN_FUNC:
          *q++ = 0;
          q += wb_put_u32(q, s->import_typeidx[i]);
          break;
        case WB_EXTERN_TABLE:
          *q++ = 1;
          *q++ = d->type.u.table.elem == WB_FUNCREF ? 0x70 : 0x6f;
          *q++ = d->type.u.table.limits.has_max;
          q += wb_put_u32(q, d->type.u.table.limits.min);

          if(d->type.u.table.limits.has_max)
            q += wb_put_u32(q, d->type.u.table.limits.max);

          break;
        case WB_EXTERN_MEMORY:
          *q++ = 2;
          *q++ = d->type.u.memory.has_max;
          q += wb_put_u32(q, d->type.u.memory.min);

          if(d->type.u.memory.has_max)
            q += wb_put_u32(q, d->type.u.memory.max);

          break;
        case WB_EXTERN_GLOBAL:
          *q++ = 3;
          *q++ = d->type.u.global.type == WB_I32 ? 0x7f : d->type.u.global.type == WB_I64 ? 0x7e : d->type.u.global.type == WB_F32 ? 0x7d : 0x7c;
          *q++ = d->type.u.global.is_mutable;
          break;
      }
    }

    *p++ = 2;
    p += wb_put_u32(p, (uint32_t)(q - payload));
    memcpy(p, payload, (size_t)(q - payload));
    p += q - payload;
    free(payload);
  }

  n = (size_t)(p - out);
  memcpy(p, mod->bytes + s->import_end, mod->len - s->import_end);
  *outlen = n + (mod->len - s->import_end);

  if(keep(ctx, out))
    return NULL;

  return out;
}

static int
link_functions(WBContext* ctx, WBInstance* inst, WBModule* mod, const WBExtern* imports) {
  WBSections* s = &mod->sec;

  for(size_t i = 0; i < s->nimports; i++) {
    const WBImportDesc* d = &s->imports[i];
    WBFunc* f;
    char sig[2 * MAX_VALUES + 4];
    size_t k = 0;
    M3Result r;

    if(d->type.kind != WB_EXTERN_FUNC)
      continue;

    f = imports[i].u.func;

    if(!type_equal(&f->type, &d->type.u.func)) {
      set_ex(ctx, WB_ERR_LINK, "import %s.%s: function signature mismatch", d->module, d->name);
      return -1;
    }

    if(f->type.nresults > 1) {
      set_ex(ctx, WB_ERR_UNSUPPORTED, "import %s.%s: multiple results", d->module, d->name);
      return -1;
    }

    sig[k++] = f->type.nresults ? sigchar(f->type.results[0]) : 'v';
    sig[k++] = '(';

    for(size_t j = 0; j < f->type.nparams; j++)
      sig[k++] = sigchar(f->type.params[j]);

    sig[k++] = ')';
    sig[k] = 0;

    if((r = m3_LinkRawFunctionEx(inst->m3, d->module, d->name, sig, import_trampoline, f)) && r != m3Err_functionLookupFailed) {
      set_ex(ctx, WB_ERR_LINK, "import %s.%s: %s", d->module, d->name, r);
      return -1;
    }
  }

  return 0;
}

static int
make_exports(WBContext* ctx, WBInstance* inst, WBModule* mod) {
  WBSections* s = &mod->sec;

  if(!(inst->exports = calloc(s->nexports ? s->nexports : 1, sizeof(WBExport)))) {
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return -1;
  }

  for(size_t i = 0; i < s->nexports; i++) {
    const WBExportDesc* d = &s->exports[i];
    WBExport* e = &inst->exports[i];

    e->name = d->name;
    e->ext.kind = d->type.kind;

    switch(d->type.kind) {
      case WB_EXTERN_FUNC: {
        IM3Function f;
        M3Result r;
        WBFunc* fn;

        if((r = m3_FindFunctionIn(&f, inst->m3, d->name))) {
          set_ex(ctx, WB_ERR_LINK, "export %s: %s", d->name, r);
          return -1;
        }

        if(!(fn = new_func(ctx, &d->type.u.func)))
          return -1;

        fn->wf = f;
        e->ext.u.func = fn;
        break;
      }
      case WB_EXTERN_MEMORY: {
        uint32_t idx;
        M3Result r;

        if((r = m3_FindExportedMemory(inst->m3, d->name, &idx))) {
          set_ex(ctx, WB_ERR_LINK, "export %s: %s", d->name, r);
          return -1;
        }

        if(!(e->ext.u.memory = new_memory(ctx, inst->m3, inst->name, d->name, idx)))
          return -1;

        break;
      }
      case WB_EXTERN_TABLE: {
        WBTable* t = calloc(1, sizeof(*t));

        if(!t || !(t->owner.field = strdup(d->name))) {
          free(t);
          set_ex(ctx, WB_ERR_NOMEM, "out of memory");
          return -1;
        }

        t->refs = 1;
        t->ctx = ctx;
        t->owner.m3 = inst->m3;
        snprintf(t->owner.module, sizeof(t->owner.module), "%s", inst->name);
        t->index = d->index;
        t->elem = d->type.u.table.elem;
        t->limits = d->type.u.table.limits;
        e->ext.u.table = t;
        break;
      }
      case WB_EXTERN_GLOBAL: {
        WBGlobal* g = calloc(1, sizeof(*g));

        if(!g || !(g->owner.field = strdup(d->name))) {
          free(g);
          set_ex(ctx, WB_ERR_NOMEM, "out of memory");
          return -1;
        }

        g->refs = 1;
        g->ctx = ctx;
        g->owner.m3 = inst->m3;
        snprintf(g->owner.module, sizeof(g->owner.module), "%s", inst->name);

        if(!(g->g = m3_FindGlobal(inst->m3, d->name))) {
          free(g->owner.field);
          free(g);
          set_ex(ctx, WB_ERR_LINK, "export %s: global lookup failed", d->name);
          return -1;
        }

        g->type = d->type.u.global.type;
        g->is_mutable = d->type.u.global.is_mutable;
        e->ext.u.global = g;
        break;
      }
    }

    inst->nexports = i + 1;
  }

  return 0;
}

WBInstance*
WB_NewInstance(WBContext* ctx, WBModule* mod, const WBExtern* imports, size_t nimports) {
  WBSections* s = &mod->sec;
  WBInstance* inst;
  const uint8_t* bytes = mod->bytes;
  size_t len = mod->len;
  int rewrite = 0;
  M3Result r;

  if(nimports != s->nimports) {
    set_ex(ctx, WB_ERR_LINK, "expected %zu imports, got %zu", s->nimports, nimports);
    return NULL;
  }

  for(size_t i = 0; i < nimports; i++) {
    if(imports[i].kind != s->imports[i].type.kind) {
      set_ex(ctx, WB_ERR_LINK, "import %s.%s: wrong kind of object", s->imports[i].module, s->imports[i].name);
      return NULL;
    }

    if(imports[i].kind != WB_EXTERN_FUNC)
      rewrite = 1;
  }

  if(rewrite && !(bytes = rewrite_imports(ctx, mod, imports, &len)))
    return NULL;

  if(!(inst = calloc(1, sizeof(*inst)))) {
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return NULL;
  }

  inst->refs = 1;
  inst->ctx = ctx;
  snprintf(inst->name, sizeof(inst->name), "wbi%u", ++ctx->serial);

  if(list_add(&ctx->insts, inst)) {
    free(inst);
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return NULL;
  }

  if((r = m3_ParseModule(ctx->rt->env, &inst->m3, bytes, (uint32_t)len))) {
    set_ex(ctx, WB_ERR_COMPILE, "%s", r);
    return NULL;
  }

  m3_SetModuleName(inst->m3, inst->name);

  inst->held = calloc(nimports ? nimports : 1, sizeof(WBExtern));

  if(!inst->held) {
    m3_FreeModule(inst->m3);
    inst->m3 = NULL;
    set_ex(ctx, WB_ERR_NOMEM, "out of memory");
    return NULL;
  }

  for(size_t i = 0; i < nimports; i++)
    inst->held[inst->nheld++] = WB_DupExtern(ctx, imports[i]);

  /* wasm3 owns the module from here on, whether or not this works; it
   * resolves memory, table and global imports now, function imports below */
  if((r = m3_LoadModule(ctx->m3, inst->m3))) {
    set_ex(ctx, WB_ERR_LINK, "%s", r);
    return NULL;
  }

  if(link_functions(ctx, inst, mod, imports))
    return NULL;

  if((r = m3_RunStart(inst->m3))) {
    if(strcmp(r, PENDING))
      set_ex(ctx, !strncmp(r, "[trap]", 6) ? WB_ERR_TRAP : WB_ERR_LINK, "%s", r);
    else if(ctx->ex.kind == WB_ERR_NONE)
      set_ex(ctx, WB_ERR_HOST, "host function failed");

    return NULL;
  }

  if(make_exports(ctx, inst, mod))
    return NULL;

  return inst;
}

void
WB_FreeInstance(WBContext* ctx, WBInstance* inst) {
  (void)ctx;

  if(inst)
    inst->refs--;
}

int
WB_GetInstanceExports(WBContext* ctx, WBInstance* inst, const WBExport** out, size_t* n) {
  (void)ctx;
  *out = inst->exports;
  *n = inst->nexports;
  return 0;
}
