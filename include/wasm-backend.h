#ifndef WASM_BACKEND_H
#define WASM_BACKEND_H

#include <stddef.h>
#include <stdint.h>

/* wasm-backend.h: one C interface over wasm3 and WAMR, shaped like quickjs.h.
 * depends on: <stddef.h>, <stdint.h>; implemented by one backend file
 * (wasm3 or WAMR, chosen at configure time) plus the shared section parser
 * that lists a module's imports and exports.
 * rule: a failing call returns -1 or NULL with the reason left in the
 * context, read with WB_GetException(); no call aborts or longjmps. */

/*  quickjs.h                        here
 *  JSRuntime / JS_NewRuntime        WBRuntime / WB_NewRuntime
 *  JSContext / JS_NewContext        WBContext / WB_NewContext
 *  JSValue / JS_DupValue            WBValue, WBExtern / WB_Dup*
 *  JS_FreeValue                     WB_Free*
 *  JS_EXCEPTION, JS_GetException    -1 or NULL, WB_GetException
 *
 *  WebAssembly JS API               here
 *  new Module(bytes)                WB_NewModule
 *  Module.imports() / exports()     WB_GetModuleImports / ...Exports
 *  new Instance(module, imports)    WB_NewInstance
 *  instance.exports                 WB_GetInstanceExports
 *  new Memory / Table / Global      WB_NewMemory / WB_NewTable / WB_NewGlobal
 *  validate(bytes)                  WB_ValidateModule
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WBRuntime WBRuntime;   /* the engine; one per thread */
typedef struct WBContext WBContext;   /* objects sharing a context may share state */
typedef struct WBModule WBModule;     /* compiled, not yet instantiated */
typedef struct WBInstance WBInstance; /* a module with its imports bound */
typedef struct WBFunc WBFunc;         /* exported wasm function or host function */
typedef struct WBMemory WBMemory;
typedef struct WBTable WBTable;
typedef struct WBGlobal WBGlobal;

#define WB_PAGE_SIZE 65536 /* bytes per memory page */

/* ---- values ---- */

typedef enum {
  WB_I32 = 1,
  WB_I64,
  WB_F32,
  WB_F64,
  WB_FUNCREF,   /* WBValue.u.ref is a WBFunc* (owned), NULL is ref.null */
  WB_EXTERNREF, /* WBValue.u.ref is an opaque host pointer, never touched here */
} WBValType;

typedef struct WBValue {
  WBValType type;
  union {
    int32_t i32;
    int64_t i64;
    float f32;
    double f64;
    void* ref;
  } u;
} WBValue;

/* ---- types and descriptors ---- */

typedef struct WBLimits {
  uint32_t min;
  uint32_t max;
  int has_max;
} WBLimits; /* pages for a memory, elements for a table */

typedef struct WBFuncType {
  const WBValType* params;
  size_t nparams;
  const WBValType* results;
  size_t nresults;
} WBFuncType;

typedef enum {
  WB_EXTERN_FUNC,
  WB_EXTERN_TABLE,
  WB_EXTERN_MEMORY,
  WB_EXTERN_GLOBAL,
} WBExternKind;

typedef struct WBExternType {
  WBExternKind kind;
  union {
    WBFuncType func;
    struct {
      WBValType elem;
      WBLimits limits;
    } table;
    WBLimits memory;
    struct {
      WBValType type;
      int is_mutable;
    } global;
  } u;
} WBExternType;

typedef struct WBImportDesc {
  const char* module; /* "env" in (import "env" "memory" ...) */
  const char* name;   /* "memory" */
  WBExternType type;
} WBImportDesc;

typedef struct WBExportDesc {
  const char* name;
  WBExternType type;
} WBExportDesc;

/* ---- extern handles ---- */

/* one imported or exported object; copying the struct does not take a ref */
typedef struct WBExtern {
  WBExternKind kind;
  union {
    WBFunc* func;
    WBTable* table;
    WBMemory* memory;
    WBGlobal* global;
  } u;
} WBExtern;

typedef struct WBExport {
  const char* name; /* borrowed: valid until WB_FreeInstance() */
  WBExtern ext;     /* borrowed too; WB_DupExtern() to keep it */
} WBExport;

/* ---- errors ---- */

/*  kind               JS error the caller raises
 *  WB_ERR_COMPILE     WebAssembly.CompileError
 *  WB_ERR_LINK        WebAssembly.LinkError
 *  WB_ERR_TRAP        WebAssembly.RuntimeError
 *  WB_ERR_HOST        none: a host function failed and its JS exception
 *                     is already pending in the JSContext; rethrow that
 *  WB_ERR_RANGE       RangeError
 *  WB_ERR_TYPE        TypeError
 *  WB_ERR_UNSUPPORTED the backend lacks the feature; caller picks the error
 *  WB_ERR_NOMEM       RangeError
 */
typedef enum {
  WB_ERR_NONE = 0,
  WB_ERR_COMPILE,
  WB_ERR_LINK,
  WB_ERR_TRAP,
  WB_ERR_HOST,
  WB_ERR_RANGE,
  WB_ERR_TYPE,
  WB_ERR_UNSUPPORTED,
  WB_ERR_NOMEM,
} WBErrorKind;

typedef struct WBException {
  WBErrorKind kind;
  char message[240];
} WBException;

/* ---- capabilities ---- */

/*  flag                       set when the backend ...
 *  WB_CAP_STANDALONE_MEMORY   can create a Memory before any instance and
 *                             share it between instances
 *  WB_CAP_STANDALONE_TABLE    same for a Table
 *  WB_CAP_STANDALONE_GLOBAL   same for a Global
 *  WB_CAP_SIMD                accepts SIMD instructions
 *  WB_CAP_THREADS             accepts shared memory and atomics
 *  WB_CAP_REF_TYPES           passes funcref and externref across calls
 */
#define WB_CAP_STANDALONE_MEMORY (1u << 0)
#define WB_CAP_STANDALONE_TABLE (1u << 1)
#define WB_CAP_STANDALONE_GLOBAL (1u << 2)
#define WB_CAP_SIMD (1u << 3)
#define WB_CAP_THREADS (1u << 4)
#define WB_CAP_REF_TYPES (1u << 5)

/* the backend's name, "wasm3" or "wamr". never throws: returns a literal. */
const char* WB_GetBackendName(void);

/* the WB_CAP_* flags of the linked backend. never throws. */
uint32_t WB_GetCapabilities(void);

/* ---- runtime and context ---- */

/* creates the engine. returns NULL with errno set. */
WBRuntime* WB_NewRuntime(void);

/* frees the runtime; every context must be freed first. never throws. */
void WB_FreeRuntime(WBRuntime* rt);

/* creates a context: the unit inside which objects can be shared.
 * returns NULL with errno set. */
WBContext* WB_NewContext(WBRuntime* rt);

/* frees the context and every module, instance and object still in it.
 * never throws. */
void WB_FreeContext(WBContext* ctx);

/* user pointer, like JS_SetContextOpaque(): a host function reads it to
 * find its JSContext. never throws. */
void WB_SetContextOpaque(WBContext* ctx, void* opaque);
void* WB_GetContextOpaque(WBContext* ctx);

/* moves the pending exception into `*out` and clears it, like
 * JS_GetException(). never throws: returns its kind, WB_ERR_NONE when
 * nothing was pending. */
WBErrorKind WB_GetException(WBContext* ctx, WBException* out);

/* ---- modules ---- */

/* checks a binary without keeping it, like WebAssembly.validate().
 * returns 0, or -1 with an exception pending (WB_ERR_COMPILE). */
int WB_ValidateModule(WBContext* ctx, const uint8_t* bytes, size_t len);

/* compiles a binary; `bytes` is copied, the caller keeps its buffer.
 * returns NULL with an exception pending (WB_ERR_COMPILE). */
WBModule* WB_NewModule(WBContext* ctx, const uint8_t* bytes, size_t len);

/* drops the caller's reference; instances keep what they need.
 * never throws. */
void WB_FreeModule(WBContext* ctx, WBModule* mod);

/* lists the module's imports in binary order.
 *
 *   WBModule*             mod  module to read
 *   const WBImportDesc**  out  array, borrowed until WB_FreeModule()
 *   size_t*               n    number of entries
 *
 *   returns int                0, or -1 with an exception pending
 */
int WB_GetModuleImports(WBContext* ctx, WBModule* mod, const WBImportDesc** out, size_t* n);

/* same for exports, in binary order. returns 0, or -1 with an exception
 * pending. */
int WB_GetModuleExports(WBContext* ctx, WBModule* mod, const WBExportDesc** out, size_t* n);

/* ---- instances ---- */

/* instantiates `mod`, runs its start function.
 *
 *   WBModule*        mod       module to instantiate
 *   const WBExtern*  imports   one per WB_GetModuleImports() row, in order
 *   size_t           nimports  must equal the import count
 *
 *   returns WBInstance*  owned reference, or NULL with an exception pending
 *                        (WB_ERR_LINK for a missing or mismatched import,
 *                        WB_ERR_TRAP when the start function traps)
 */
WBInstance* WB_NewInstance(WBContext* ctx, WBModule* mod, const WBExtern* imports, size_t nimports);

/* drops the caller's reference; exported objects keep the instance alive.
 * never throws. */
void WB_FreeInstance(WBContext* ctx, WBInstance* inst);

/* lists the instance's exports in binary order. `*out` is borrowed until
 * WB_FreeInstance(). returns 0, or -1 with an exception pending. */
int WB_GetInstanceExports(WBContext* ctx, WBInstance* inst, const WBExport** out, size_t* n);

/* ---- reference counting ---- */

/* refcount: a WBExtern or funcref WBValue handed out by an
 * `out` parameter is owned by the caller; one read from a borrowed array
 * (WBExport.ext) is not, so dup it before storing it. */

/* adds a reference to an extern's object and returns it. never throws. */
WBExtern WB_DupExtern(WBContext* ctx, WBExtern ext);

/* drops a reference; the object is freed at zero. never throws. */
void WB_FreeExtern(WBContext* ctx, WBExtern ext);

/* same for a value: only WB_FUNCREF holds a reference. never throws. */
WBValue WB_DupValue(WBContext* ctx, WBValue v);
void WB_FreeValue(WBContext* ctx, WBValue v);

/* ---- functions ---- */

/* host function behind an import: reads `args`, writes `results`, both
 * sized by the WBFuncType it was created with.
 * returns 0, or -1 after leaving a JS exception pending; the wasm call
 * then fails with WB_ERR_HOST. */
typedef int WBHostFunc(WBContext* ctx, void* opaque, const WBValue* args, WBValue* results);

/* called once when the host function is freed. */
typedef void WBFinalizer(void* opaque);

/* wraps a host function so it can be an import or a table element.
 * `type` is copied. returns NULL with an exception pending. */
WBFunc* WB_NewHostFunc(WBContext* ctx, const WBFuncType* type, WBHostFunc* fn, void* opaque, WBFinalizer* fin);

/* the function's signature; borrowed until the WBFunc is freed. never
 * throws. */
const WBFuncType* WB_GetFuncType(WBFunc* func);

/* calls a function; `args` and `results` are sized by WB_GetFuncType().
 * returns 0, or -1 with an exception pending (WB_ERR_TRAP, WB_ERR_HOST,
 * WB_ERR_TYPE for an argument of the wrong type). */
int WB_CallFunc(WBContext* ctx, WBFunc* func, const WBValue* args, WBValue* results);

/* ---- memories ---- */

/* creates a memory of `limits` pages, zeroed.
 * returns NULL with an exception pending (WB_ERR_UNSUPPORTED without
 * WB_CAP_STANDALONE_MEMORY, WB_ERR_RANGE for bad limits). */
WBMemory* WB_NewMemory(WBContext* ctx, const WBLimits* limits);

/* the memory's bytes and size.
 *
 * borrowed: the pointer is valid until WB_GrowMemory() on this memory or
 * the next WB_CallFunc(), since wasm code can grow it. compare the pointer
 * and size after each call to notice a grow.
 *
 * never throws: returns the base address, `*size` in bytes. */
uint8_t* WB_GetMemoryData(WBMemory* mem, size_t* size);

/* grows by `delta` pages; the old data may move.
 * returns the previous size in pages, or -1 with an exception pending
 * (WB_ERR_RANGE past the maximum, WB_ERR_NOMEM). */
int64_t WB_GrowMemory(WBContext* ctx, WBMemory* mem, uint32_t delta);

/* ---- tables ---- */

/* creates a table of `elem` (WB_FUNCREF or WB_EXTERNREF) elements, all
 * null. returns NULL with an exception pending (WB_ERR_UNSUPPORTED without
 * WB_CAP_STANDALONE_TABLE). */
WBTable* WB_NewTable(WBContext* ctx, WBValType elem, const WBLimits* limits);

/* number of elements. never throws. */
uint32_t WB_GetTableSize(WBTable* table);

/* reads element `index` into `*out`, owned by the caller.
 * returns 0, or -1 with an exception pending (WB_ERR_RANGE). */
int WB_GetTableElem(WBContext* ctx, WBTable* table, uint32_t index, WBValue* out);

/* stores `v` at `index`; the table takes its own reference.
 * returns 0, or -1 with an exception pending (WB_ERR_RANGE, WB_ERR_TYPE). */
int WB_SetTableElem(WBContext* ctx, WBTable* table, uint32_t index, const WBValue* v);

/* grows by `delta` elements, new ones set to `*init`.
 * returns the previous size, or -1 with an exception pending
 * (WB_ERR_RANGE). */
int64_t WB_GrowTable(WBContext* ctx, WBTable* table, uint32_t delta, const WBValue* init);

/* ---- globals ---- */

/* creates a global holding `*init`; its type is init->type.
 * returns NULL with an exception pending (WB_ERR_UNSUPPORTED without
 * WB_CAP_STANDALONE_GLOBAL). */
WBGlobal* WB_NewGlobal(WBContext* ctx, int is_mutable, const WBValue* init);

/* reads the value into `*out`, owned by the caller. never throws. */
void WB_GetGlobal(WBContext* ctx, WBGlobal* global, WBValue* out);

/* stores `*v`; the type must match.
 * returns 0, or -1 with an exception pending (WB_ERR_TYPE for an immutable
 * global or a mismatched type). */
int WB_SetGlobal(WBContext* ctx, WBGlobal* global, const WBValue* v);

/* the global's type and mutability. never throws. */
WBValType WB_GetGlobalType(WBGlobal* global, int* is_mutable);

#ifdef __cplusplus
}
#endif

#endif /* WASM_BACKEND_H */
