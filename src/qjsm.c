#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#endif
#if !defined(__wasi__) && !defined(_WIN32)
#include <dlfcn.h>
#endif

#ifdef HAVE_QUICKJS_CONFIG_H
#include <quickjs-config.h>
#else
#warning HAVE_QUICKJS_CONFIG_H has not been defined!
#endif

#ifndef QUICKJS_MODULE_PATH
#warning No QUICKJS_MODULE_PATH defined (usually in quickjs-config.h)
#ifdef QUICKJS_PREFIX
#define QUICKJS_MODULE_PATH QUICKJS_PREFIX "/lib/quickjs"
#else
#error No QUICKJS_PREFIX defined
#endif
#endif

#ifndef QJS_BIGNUM_EXT
#warning No bignum!
#endif

#ifndef CONFIG_SHEXT
#ifdef _WIN32
#define CONFIG_SHEXT ".dll"
#elif defined(__APPLE__)
#define CONFIG_SHEXT ".dylib"
#else
#define CONFIG_SHEXT ".so"
#endif
#endif

#include <list.h>
#include <cutils.h>
#include "path.h"
#include "utils.h"
#include "vector.h"
#include <quickjs-libc.h>
#include "buffer-utils.h"
#include "base64.h"
#include "debug.h"

/* Logs `fmt` (printf-style, with the current jsm_stack depth and calling function name
   prefixed) when the DEBUG_MODULE verbosity variable is >= `level`. Deliberately shares its
   name with the `DEBUG_MODULE` variable below: a function-like macro only expands when
   followed by '(', so `DEBUG_MODULE(...)` calls and bare `DEBUG_MODULE` variable reads never
   collide. */
#define DEBUG_MODULE(level, fmt, args...) \
  if(debug_module >= (level)) \
    printf("(%zu) %-21s" fmt "\n", jsm_stack_count(), __FUNCTION__, args);

/* --- extern declarations for functions defined elsewhere (quickjs-libc.c / libc) --- */

#if !DONT_HAVE_MALLOC_USABLE_SIZE && !defined(ANDROID)
#if HAVE_MALLOC_USABLE_SIZE
#ifndef HAVE_MALLOC_USABLE_SIZE_DEFINITION
extern size_t malloc_usable_size(void*);
#endif
#endif
#endif

/* --- type definitions --- */

/* One entry in the `loaded_modules` list: associates a resolved module name with the
    JSModuleDef the engine ended up loading for it. */
typedef struct {
  struct list_head link;
  char* name;
  JSModuleDef* module;
  char* importer; /* name the importing module was loaded under, or 0; its index is looked up on demand */
  BOOL hooked;    /* produced by a registerHooks() hook */
} LoadedModule;

/* Describes one builtin module (either a native C module with an init function, or a
    precompiled-bytecode module) as registered in the jsm_builtins table. */
typedef struct {
  const char* module_name;
  JSModuleDef* (*module_func)(JSContext*, const char*);
  const uint8_t* byte_code;
  uint32_t byte_code_len;
  JSModuleDef* def;
  BOOL initialized;
} BuiltinModule;

/* Opaque state threaded through the jsm_trace_malloc* allocator, used to compute
    pointer offsets relative to a fixed base address for -T/--trace output. */
struct trace_malloc_data {
  uint8_t* base;
};

/* Magic values for the scriptList/scriptFile/scriptDir/__filename/__dirname getters
    (jsm_stack_get), selecting what view of jsm_stack to return. */
enum {
  SCRIPT_LIST,
  SCRIPT_FILE,
  SCRIPT_FILENAME,
  SCRIPT_DIRNAME,
};

/* Magic values for jsm_eval_script (the evalFile/evalBuf globals), selecting whether the
    source comes from a file path or an in-memory buffer. */
enum {
  EVAL_FILE,
  EVAL_BUF,
};

enum { HOOK_RESOLVE, HOOK_LOAD };

/* Signature of a module-path lookup helper: takes a candidate module name and either
    returns a newly allocated, resolved path or 0 if it didn't match. */
typedef char* ModuleLoader(JSContext*, const char*);

#if defined(__APPLE__)
#define MALLOC_OVERHEAD 0
#else
#define MALLOC_OVERHEAD 8
#endif

/* --- global/static variables --- */

static thread_local int debug_module = 0;
static thread_local Vector debug_list = VECTOR_INIT();
static thread_local Vector module_list = VECTOR_INIT();
static thread_local struct list_head loaded_modules;

static const char jsm_default_module_path[] = QUICKJS_MODULE_PATH;

static JSValue package_json, jsm_promise;
static BOOL jsm_hooks_alive = FALSE; /* process hooks may still run (context not torn down) */
static int jsm_exit_code = 0;        /* code reported to 'exit' when libc exit() is reached directly */
static char* exename;
static size_t exelen;
static JSRuntime* jsm_rt;
static JSContext* jsm_ctx;
static int interactive = 0;

#define SEMI ";"

static const char* const module_extensions = CONFIG_SHEXT SEMI ".js" SEMI "/index.js";

static thread_local Vector jsm_stack = VECTOR_INIT();
static thread_local Vector jsm_builtins = VECTOR_INIT();
static thread_local char* jsm_importer; /* importer seen by the last normalize, for LoadedModule.parent */

/* registerHooks() state */
static thread_local JSValue* module_hooks;
static thread_local size_t module_hooks_len;
static thread_local JSValue hook_formats;
static thread_local BOOL hook_formats_set;

#ifndef JS_MODULE_LOADER_OLD
/* the import attributes of the module being loaded (borrowed, NULL when none),
 * for js_module_loader() in jsm_module_loader(). */
static thread_local const JSValue* jsm_import_attributes = NULL;
#define JSM_IMPORT_ATTRIBUTES() (jsm_import_attributes ? *jsm_import_attributes : JS_UNDEFINED)
#endif

#ifdef QJS_BIGNUM_EXT
static int bignum_ext = 1;
#endif

/* --- builtin lists --- */

#define BUILTIN_NATIVE(name) extern JSModuleDef* js_init_module_##name(JSContext*, const char*);
#define BUILTIN_COMPILED(name) \
  extern const uint8_t qjsc_##name[]; \
  extern const uint32_t qjsc_##name##_size;

/* quickjs-builtins.h expands to one BUILTIN_NATIVE(name)/BUILTIN_COMPILED(name)
   invocation per builtin module; here that generates the `extern` declarations for each
   module's init function/bytecode blob, so they can be referenced from the
   RECORD_NATIVE/RECORD_COMPILED initializers below and from
   jsm_init() (which redefines these same two macros to build the runtime table). */
#include "quickjs-builtins.h"

#ifdef QJS_BIGNUM_EXT
#if HAVE_QJSCALC
BUILTIN_COMPILED(qjscalc);
#endif
#endif

#undef BUILTIN_NATIVE
#undef BUILTIN_COMPILED

#define RECORD_COMPILED(name) {#name, 0, qjsc_##name, qjsc_##name##_size, 0, FALSE}
#define RECORD_NATIVE(name) {#name, js_init_module_##name, 0, 0, 0, FALSE}

/* --- function prototypes --- */

static size_t jsm_stack_count(void);
static JSModuleDef* jsm_module_at(int);
static JSModuleDef* jsm_hook_load(JSContext*, const char*, void*);

/* --- functions --- */

/* Checks whether a module name should be resolved via QUICKJS_MODULE_PATH search. */
static inline BOOL
is_searchable(const char* path) {
  return !path_isexplicit(path);
}

/* Checks whether a string contains a '.' or a path separator. */
static inline BOOL
has_dot_or_slash(const char* s) {
  return !!s[str_chrs(s, "." PATHSEP_S, sizeof(PATHSEP_S))];
}

/* ModuleLoader-shaped wrapper around path_isfile1(): accepts a candidate path as-is. */
static char*
is_module(JSContext* ctx, const char* module_name) {
  BOOL yes = path_isfile1(module_name);

  DEBUG_MODULE(3, "(module_name: \"%s\") = %s", module_name, yes ? "TRUE" : "FALSE");

  return yes ? js_strdup(ctx, module_name) : 0;
}

/* Checks whether a module name already ends in one of module_extensions. */
static int
module_has_suffix(const char* module_name) {
  for(const char* ext = module_extensions; *ext; ext += str_nchrs(ext, SEMI "\n", sizeof(SEMI))) {
    size_t n = str_chrs(ext, SEMI "\n", sizeof(SEMI));

    if(str_endb(module_name, ext, n))
      return strlen(module_name) - n;

    ext += n;
  }

  return 0;
}

/* Calls process.__qjsm_hooks__[name](...argv) from lib/process.js; JS_UNDEFINED if absent.
 *
 * the hooks drive the process events (exit, beforeExit, uncaughtException, ...);
 * an exception thrown by a hook is printed and swallowed.
 */
static JSValue
jsm_hook_call(JSContext* ctx, const char* name, int argc, JSValueConst* argv) {
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue process = JS_GetPropertyStr(ctx, global, "process");
  JSValue hooks = JS_IsObject(process) ? JS_GetPropertyStr(ctx, process, "__qjsm_hooks__") : JS_UNDEFINED;
  JSValue fn = JS_IsObject(hooks) ? JS_GetPropertyStr(ctx, hooks, name) : JS_UNDEFINED;
  JSValue ret = JS_UNDEFINED;

  if(JS_IsFunction(ctx, fn)) {
    ret = JS_Call(ctx, fn, hooks, argc, argv);

    if(JS_IsException(ret)) {
      JSValue exception = JS_GetException(ctx);

      js_error_print(ctx, exception);
      JS_FreeValue(ctx, exception);
      ret = JS_UNDEFINED;
    }
  }

  JS_FreeValue(ctx, fn);
  JS_FreeValue(ctx, hooks);
  JS_FreeValue(ctx, process);
  JS_FreeValue(ctx, global);
  return ret;
}

/* Calls a hook and returns whether it answered true. */
static BOOL
jsm_hook_bool(JSContext* ctx, const char* name, int argc, JSValueConst* argv) {
  JSValue ret = jsm_hook_call(ctx, name, argc, argv);
  BOOL result = JS_ToBool(ctx, ret) > 0;

  JS_FreeValue(ctx, ret);
  return result;
}

/* Emits 'exit' once and returns the final exit code (a listener may change it). */
static int
jsm_hook_exit(JSContext* ctx, int code) {
  JSValue arg = JS_NewInt32(ctx, code);
  JSValue ret = jsm_hook_call(ctx, "exit", 1, &arg);
  int32_t result = code;

  if(JS_IsNumber(ret))
    JS_ToInt32(ctx, &result, ret);

  JS_FreeValue(ctx, ret);
  return result;
}

/* atexit fallback: libc exit() reached without 'exit' having been emitted. */
static void
jsm_atexit(void) {
  if(jsm_hooks_alive)
    jsm_hook_exit(jsm_ctx, jsm_exit_code);
}

/*
 * Host promise-rejection callback wired up via JS_SetHostPromiseRejectionTracker().
 *
 * Dedupes on the rendered message/stack text, since a single top-level module throw can
 * settle more than one internal promise and the engine invokes this more than once for
 * what is really the same error.
 */
static void
jsm_promise_rejection_tracker(JSContext* ctx, JSValueConst promise, JSValueConst reason, BOOL is_handled, void* opaque) {
  /* A single top-level throw in a module can settle more than one internal promise, so
     the engine invokes this tracker more than once for what is really the same error.
     Raw JSValue pointers can't be compared across calls (a freed reason can have its
     address reused by an unrelated later rejection), so dedupe on the rendered
     message/stack text instead. */
  static char* last_msg = 0;
  char* msg;
  JSValue hook_args[3] = {promise, reason, is_handled ? JS_TRUE : JS_FALSE};

  /* process.on('unhandledRejection' | 'uncaughtException' | 'rejectionHandled') */
  if(jsm_hooks_alive && jsm_hook_bool(ctx, "rejection", 3, hook_args) && !is_handled)
    return;

  /* The std tracker drops its pending entry on "handled"; swallowing it here makes every
     rejection that is handled later still get reported (and exit 1) at shutdown. */
  if(is_handled) {
    js_std_promise_rejection_tracker(ctx, promise, reason, is_handled, opaque);
    return;
  }

  msg = js_error_tostring(ctx, reason);

  if(msg && last_msg && str_equal(msg, last_msg)) {
    js_free(ctx, msg);
    return;
  }

  js_free(ctx, last_msg);
  last_msg = msg;
  jsm_exit_code = 1;

  js_std_promise_rejection_tracker(ctx, promise, reason, is_handled, opaque);
}

/*
 * Returns the top-level error to report at shutdown, preferring a rejected
 * jsm_promise over the context's pending exception.
 */
static JSValue
jsm_error_get(JSContext* ctx) {
  if(JS_IsObject(jsm_promise))
    if(JS_PromiseState(ctx, jsm_promise) == JS_PROMISE_REJECTED)
      return JS_PromiseResult(ctx, jsm_promise);

  return JS_GetException(jsm_ctx);
}

/* Prints the context's current pending exception to stderr. */
static void
jsm_error_print(JSContext* ctx) {
  js_error_print(ctx, JS_GetException(ctx));
}

/* Number of entries currently on the module-load stack. */
static size_t
jsm_stack_count(void) {
  return vector_size(&jsm_stack, sizeof(char*));
}

/* Address of the i-th entry of jsm_stack (Python-style negative indexing). */
static char**
jsm_stack_ptr(int i) {
  int size;

  if((size = jsm_stack_count()) > 0) {
    if(i < 0)
      i += size;

    return vector_at(&jsm_stack, sizeof(char*), i);
  }

  return 0;
}

/*
 * Looks for `module` already present on jsm_stack (used for circular-import
 * detection).
 */
static char**
jsm_stack_find(const char* module) {
  char** ptr;

  if(jsm_stack.size == 0)
    return 0;

  vector_foreach_t(&jsm_stack, ptr) if(!path_compare2(*ptr, module)) return ptr;
  return 0;
}

/* Value of the i-th entry of jsm_stack. */
static char*
jsm_stack_at(int i) {
  char** ptr;

  if((ptr = jsm_stack_ptr(i)))
    return *ptr;

  return 0;
}

/* Path of the module/script currently being loaded. */
static inline char*
jsm_stack_top(void) {
  return jsm_stack_at(-1);
}

/*
 * Renders the whole jsm_stack as a newline-separated "i: path" listing, innermost
 * entry first.
 */
static char*
jsm_stack_string(void) {
  int i = jsm_stack_count();
  DynBuf buf;
  dbuf_init2(&buf, 0, vector_realloc);

  while(--i >= 0)
    dbuf_printf(&buf, "%i: %s\n", i, jsm_stack_at(i));

  dbuf_0(&buf);
  return (char*)buf.buf;
}

/* Getter backing the scriptList/scriptFile/scriptDir/__filename/__dirname globals. */
static JSValue
jsm_stack_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case SCRIPT_LIST: {
      char** ptr;
      size_t i = 0;
      ret = JS_NewArray(ctx);

      vector_foreach_t(&jsm_stack, ptr) JS_SetPropertyUint32(ctx, ret, i++, JS_NewString(ctx, *ptr));

      break;
    }
    case SCRIPT_FILE:
    case SCRIPT_FILENAME: {
      char* file;

      if((file = jsm_stack_top()))
        ret = JS_NewString(ctx, file);

      break;
    }
    case SCRIPT_DIRNAME: {
      char* file;

      if((file = jsm_stack_top())) {
        char* dir = path_dirname1(file);
        ret = JS_NewString(ctx, dir);
        free(dir);
      }

      break;
    }
  }

  return ret;
}

/* Pushes a file/module path onto jsm_stack. */
static void
jsm_stack_push(JSContext* ctx, const char* file) {
  DEBUG_MODULE(4, "(file=\"%s\")", file);

  vector_putptr(&jsm_stack, js_strdup(ctx, file));
}

/* Pops and frees the top entry of jsm_stack. */
static void
jsm_stack_pop(JSContext* ctx) {
  char** ptr = vector_pop(&jsm_stack, sizeof(char*));

  DEBUG_MODULE(4, "%s", *ptr);

  js_free(ctx, *ptr);
}

/*
 * Evaluates a top-level script/module file, pushing it on jsm_stack for the
 * duration and printing any resulting exception.
 */
static int
jsm_stack_load(JSContext* ctx, const char* file, BOOL module, BOOL is_main) {
  JSValue val;
  int32_t ret;
  JSValue global_obj = JS_GetGlobalObject(ctx);

  JS_SetPropertyStr(ctx, global_obj, "module", JS_NewObject(ctx));
  jsm_stack_push(ctx, file);

  errno = 0;
  val = js_eval_file(ctx, file, module ? JS_EVAL_TYPE_MODULE : 0);

  jsm_stack_pop(ctx);

#if defined(HAVE_JS_PROMISE_STATE) && defined(HAVE_JS_PROMISE_RESULT)
  if(js_is_promise(ctx, val)) {
    JSPromiseStateEnum state = JS_PromiseState(ctx, val);
    JSValue result = JS_PromiseResult(ctx, val);

    if(state == JS_PROMISE_REJECTED) {
      /* process.on('uncaughtException') consumes the error and lets the loop run on */
      if(jsm_hooks_alive && jsm_hook_bool(ctx, "uncaught", 1, &result)) {
        js_std_promise_rejection_tracker(ctx, val, result, TRUE, 0);
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, val);
        JS_FreeValue(ctx, global_obj);
        return 0;
      }

      /* The std tracker only reports at the end of js_std_loop(), which a failed
         main script never reaches: print here and mark the rejection handled so an
         interactive session does not report it a second time. */
      fprintf(stderr, "Error evaluating '%s':\n", file);
      js_error_print(ctx, result);
      js_std_promise_rejection_tracker(ctx, val, result, TRUE, 0);
      JS_FreeValue(ctx, result);
      JS_FreeValue(ctx, val);
      JS_FreeValue(ctx, global_obj);
      return -1;
    } else if(state == JS_PROMISE_FULFILLED) {
      JS_FreeValue(ctx, val);
      val = JS_DupValue(ctx, result);
    }

    JS_FreeValue(ctx, result);
  }
#endif

  if(JS_IsException(val)) {
    JSValue exception = JS_GetException(ctx);

    if(jsm_hooks_alive && jsm_hook_bool(ctx, "uncaught", 1, &exception)) {
      JS_FreeValue(ctx, exception);
      JS_FreeValue(ctx, global_obj);
      return 0;
    }

    fprintf(stderr, "Error evaluating '%s':\n", file);
    js_error_print(ctx, exception);

    JS_FreeValue(ctx, exception);
    return -1;
  }

  if(JS_IsModule(val) || module) {
    JSModuleDef* m = 0;

    if(!JS_IsModule(val)) {
      m = jsm_module_at(-1);
      val = module_value(ctx, m);
    } else {
      m = JS_VALUE_GET_PTR(val);
    }

  } else {
    JS_ToInt32(ctx, &ret, val);
  }

  if(!JS_IsModule(val))
    JS_FreeValue(ctx, val);

  JS_FreeValue(ctx, global_obj);
  return 0;
}

/* Checks whether a JSModuleDef belongs to one of the builtin modules. */
static BOOL
jsm_is_builtin(JSModuleDef* m) {
  BuiltinModule* rec;

  vector_foreach_t(&jsm_builtins, rec) if(rec->def == m) return TRUE;

  return FALSE;
}

/* is `m` a C module: a native builtin, or a shared object loaded by path.
 *
 * the engine keeps init_func private, so this mirrors how modules get made:
 * builtin records with an init function, and names ending in CONFIG_SHEXT.
 */
static BOOL
jsm_is_native(JSModuleDef* m) {
  BuiltinModule* rec;
  struct list_head* el;

  vector_foreach_t(&jsm_builtins, rec) if(rec->def == m) return rec->module_func != 0;

  list_for_each(el, &loaded_modules) {
    LoadedModule* lm = list_entry(el, LoadedModule, link);

    if(lm->module == m)
      return str_ends(lm->name, CONFIG_SHEXT);
  }

  return FALSE;
}

/*
 * Looks up a builtin module record by name. A leading "node:" is stripped first
 * (Bun/Deno compatibility: `node:fs`/`node:path`/etc. resolve the same as the bare
 * name resolves here, i.e. to *this* engine's own builtin of that name, not Node's
 * actual implementation - the same aliasing Bun/Deno themselves do for their own
 * compat builtins) - except for "os": this engine's own `os` builtin is a low-level
 * POSIX/process-primitives module (exec/pipe/kill/waitpid/...), nothing like Node's
 * `os` (hostname/cpus/homedir/networkInterfaces/...), so aliasing `node:os` to it
 * would silently resolve to the wrong module instead of failing cleanly.
 */
static BuiltinModule*
jsm_builtin_find(const char* name) {
  BuiltinModule* rec;

  if(str_start(name, "node:") && !str_equal(name + 5, "os"))
    name += 5;

  vector_foreach_t(&jsm_builtins, rec) if(str_equal(rec->module_name, name)) return rec;

  return 0;
}

/*
 * Lazily initializes (native-calls or bytecode-loads) a builtin module the first
 * time it's requested, caching the resulting JSModuleDef on `rec`.
 */
static JSModuleDef*
jsm_builtin_init(JSContext* ctx, BuiltinModule* rec) {
  JSModuleDef* m;
  JSValue obj = JS_UNDEFINED;

  DEBUG_MODULE(2, "(module_name: \"%s\")", rec->module_name);

  jsm_stack_push(ctx, rec->module_name);

  if(rec->def == 0) {
    /* C native module */
    if(rec->module_func) {
      m = rec->module_func(ctx, rec->module_name);
      obj = js_value_mkptr(JS_TAG_MODULE, m);

      if(!rec->initialized && !JS_IsUndefined(obj)) {
        JSValue func_obj = JS_DupValue(ctx, obj);
        JS_EvalFunction(ctx, func_obj);
        rec->initialized = TRUE;
      }

      /* bytecode compiled module */
    } else {
      obj = JS_ReadObject(ctx, rec->byte_code, rec->byte_code_len, JS_READ_OBJ_BYTECODE);

      if(JS_IsException(obj)) {
        jsm_stack_pop(ctx);
        return 0;
      }

      m = js_value_ptr(obj);

      if(JS_ResolveModule(ctx, obj) < 0) {
        JS_FreeValue(ctx, obj);
        jsm_stack_pop(ctx);
        return 0;
      }

      JSValue ret = JS_EvalFunction(ctx, obj);
      JS_FreeValue(ctx, ret);
    }

    rec->def = m;
  }

  jsm_stack_pop(ctx);

  return rec->def;
}

/* Loads and parses a JSON file. */
static JSValue
jsm_load_json(JSContext* ctx, const char* file) {
  uint8_t* buf;
  size_t len;

  if(!(buf = js_load_file(ctx, &len, file)))
    return JS_ThrowInternalError(ctx, "Loading '%s' failed", file);

  return JS_ParseJSON(ctx, (const char*)buf, len, file);
}

/*
 * Loads and caches package.json (in the `package_json` global), tolerating a
 * missing/invalid file.
 */
static JSValue
jsm_load_package(JSContext* ctx, const char* file) {
  if(js_is_null_or_undefined(package_json) || JS_VALUE_GET_TAG(package_json) == 0) {
    package_json = jsm_load_json(ctx, file ? file : "package.json");

    if(JS_IsException(package_json)) {
      JS_GetException(ctx);
      package_json = JS_NULL;
    }
  }

  return package_json;
}

/* Searches a ';'/'\n'-separated list of directories for `module_name`. */
static char*
jsm_search_list(JSContext* ctx, const char* module_name, const char* list) {
  char* t;
  size_t i;

  DEBUG_MODULE(4, "(module_name: \"%s\", list: \"%s\")", module_name, list);

  if(!(t = js_malloc(ctx, strlen(list) + 1 + strlen(module_name) + 1)))
    return 0;

  for(; *list; list += i) {
    if((i = str_chrs(list, ";\n", 2)) == 0)
      break;

    str_copyn(t, list, i);
    t[i] = '/';
    str_copy(&t[i + 1], module_name);

    if(path_isfile1(t))
      return t;

    if(list[i])
      ++i;
  }

  js_free(ctx, t);
  return 0;
}

/*
 * ModuleLoader wrapper searching QUICKJS_MODULE_PATH (env var, else the compiled-in
 * default) for `module_name`.
 */
static char*
jsm_search_path(JSContext* ctx, const char* module_name) {
  const char* path;

  assert(is_searchable(module_name));

  if(!(path = getenv("QUICKJS_MODULE_PATH")))
    path = jsm_default_module_path;

  DEBUG_MODULE(4, "(module_name: \"%s\", path: \"%s\")", module_name, path);

  return jsm_search_list(ctx, module_name, path);
}

/*
 * Tries `module_name` with each of module_extensions appended in turn, calling `fn`
 * on each candidate until one succeeds.
 */
static char*
jsm_search_suffix(JSContext* ctx, const char* module_name, ModuleLoader* fn) {
  size_t n, len = strlen(module_name);
  char *s, *t = 0;

  DEBUG_MODULE(4, "(module_name: \"%s\", fn: %s)", module_name, fn == &is_module ? "is_module" : fn == &jsm_search_path ? "jsm_search_path" : "<unknown>");

  if(!(s = js_mallocz(ctx, (len + strlen(module_extensions) + 1))))
    return 0;

  strcpy(s, module_name);

  for(const char* ext = module_extensions; *ext; ext += str_nchrs(ext, ";\n", 2)) {
    s[len] = '\0';

    n = str_chrs(ext, ";\n", 2);
    str_copyn(&s[len], ext, n);

    if((t = fn(ctx, s)))
      break;

    ext += n;
  }

  js_free(ctx, s);
  return t;
}

/*
 * Resolves `module_name` to a file, either directly/via path search (if it already
 * has a recognized suffix) or by trying each module_extensions suffix in turn.
 */
static char*
jsm_search_module(JSContext* ctx, const char* module_name) {
  BOOL search = is_searchable(module_name);
  BOOL suffix = module_has_suffix(module_name);
  ModuleLoader* fn = search ? &jsm_search_path : &is_module;
  char* s = suffix ? fn(ctx, module_name) : jsm_search_suffix(ctx, module_name, fn);

  DEBUG_MODULE(3, "(module_name: \"%s\") search: %s suffix: %s result: %s", module_name, ((search) ? "TRUE" : "FALSE"), ((suffix) ? "TRUE" : "FALSE"), s);

  return s;
}

static int
jsm_module_indexof(JSModuleDef* m) {
  struct list_head* el;
  int i = 0;

  list_for_each(el, &loaded_modules) {
    LoadedModule* lm = list_entry(el, LoadedModule, link);

    if(lm->module == m)
      return i;

    ++i;
  }

  return -1;
}

/* end of "new breed" module loader functions */

static JSModuleDef*
jsm_module_at(int index) {
  struct list_head* el;
  int i = 0;

  if(index < 0) {
    list_for_each_prev(el, &loaded_modules) {
      LoadedModule* lm = list_entry(el, LoadedModule, link);

      if(--i == index)
        return lm->module;
    }
  } else {
    list_for_each(el, &loaded_modules) {
      LoadedModule* lm = list_entry(el, LoadedModule, link);

      if(i++ == index)
        return lm->module;
    }
  }

  return 0;
}

/* the short name inside a loaded module's name: "/a/b/foo.js" -> "foo", "/a/foo/index.js" -> "foo" */
static size_t
jsm_short_name(const char* name, const char** start) {
  const char *end, *p;
  static const char* const exts[] = {".json", ".js", CONFIG_SHEXT};
  size_t i;

  *start = name;
  end = name + strlen(name);

  if(str_start(name, "data:"))
    return end - name;

  for(i = 0; i < countof(exts); i++)
    if(str_ends(name, exts[i])) {
      end -= strlen(exts[i]);
      break;
    }

  for(p = end; p > name && p[-1] != '/' && p[-1] != '\\';)
    --p;

  if(end - p == 5 && !strncmp(p, "index", 5) && p > name) {
    end = p - 1;
    for(p = end; p > name && p[-1] != '/' && p[-1] != '\\';)
      --p;
  }

  *start = p;
  return end - p;
}

static BOOL
jsm_short_match(const char* module_name, const char* name) {
  const char* start;
  size_t len = jsm_short_name(module_name, &start);

  return strlen(name) == len && !strncmp(start, name, len);
}

/*
 * Resolves a JS value (module value, numeric index, or name string) to a
 * JSModuleDef, for the various `*Module` globals.
 */
JSModuleDef*
jsm_module_def(JSContext* ctx, JSValueConst value) {
  JSModuleDef* m;

  if((m = js_module_def(ctx, value)))
    return m;

  struct list_head* el;
  int32_t id = -1, i = 0;
  const char* name = 0;

  if(JS_IsNumber(value))
    JS_ToInt32(ctx, &id, value);
  else
    name = JS_ToCString(ctx, value);

  m = 0;

  list_for_each(el, &loaded_modules) {
    LoadedModule* lm = list_entry(el, LoadedModule, link);

    if(name ? str_equal(lm->name, name) || jsm_short_match(lm->name, name) : id == i) {
      m = lm->module;
      break;
    }

    i++;
  }

  if(name)
    JS_FreeCString(ctx, name);

  return m;
}

/* Resolves `module` through package.json's "_moduleAliases" map, if present. */
static char*
jsm_module_package(JSContext* ctx, const char* module) {
  char *file = 0, *rel = path_isabsolute1(module) ? path_relative1(module) : strdup(module);

  JSValueConst package = jsm_load_package(ctx, "package.json");

  if(JS_IsObject(package)) {
    JSValue aliases = JS_GetPropertyStr(ctx, package, "_moduleAliases");

    if(!JS_IsException(aliases) && JS_IsObject(aliases)) {
      JSValue target = JS_GetPropertyStr(ctx, aliases, path_trimdotslash1(rel));

      if(JS_IsString(target)) {
        file = js_tostring(ctx, target);

        DEBUG_MODULE(1, "(2) %-30s => %s (package.json)", module, file);
      }

      JS_FreeValue(ctx, target);
    }

    JS_FreeValue(ctx, aliases);
  }

  free(rel);
  return file;
}

/*
 * Builds the synthetic "import ... from 'path'; ..." source used by jsm_module_load
 * to bring a module into scope and optionally invoke/assign it.
 */
static void
jsm_module_script(DynBuf* buf, const char* path, const char* name, BOOL star) {
  BOOL exec = FALSE, all = FALSE; /* the last of a leading "!" / "*" wins */

  for(; *path; ++path) {
    switch(*path) {
      case '!': {
        if(!star)
          exec = TRUE, all = FALSE;
        continue;
      }

      case '*': {
        if(!name)
          all = TRUE, exec = FALSE;
        continue;
      }

      case '=': /* bind the default export under the module's name (the default anyway) */ continue;
    }

    break;
  }

  buf->size = 0;

  dbuf_putstr(buf, "import ");

  if(star)
    dbuf_putstr(buf, "* as ");

  size_t pathlen = str_chr(path, '=');

  dbuf_putstr(buf, "tmp from '");
  dbuf_put(buf, (const void*)path, pathlen);
  dbuf_putstr(buf, "';");

  if(exec) {
    dbuf_putstr(buf, "tmp();");
  } else if(all) {
    dbuf_putstr(buf, "Object.assign(globalThis, tmp);");
  } else {
    size_t len = 0;
    char* tmp;

    if(path[pathlen] == '=')
      name = &path[pathlen + 1];

    if(!name)
      name = basename(path);

    if((tmp = strrchr(name, '.')))
      len = tmp - name;
    else
      len = strlen(name);

    dbuf_putstr(buf, "globalThis['");

    if(len)
      dbuf_put(buf, (const uint8_t*)name, len);
    else
      dbuf_putstr(buf, name);

    dbuf_putstr(buf, "'] = tmp;");
  }

  dbuf_0(buf);
}

/* Finds an already-loaded module by name, starting at `start_pos`. */
static JSModuleDef*
jsm_module_find(JSContext* ctx, const char* name, int start_pos) {
  JSModuleDef* m = 0;

  while(*name == '!' || *name == '*')
    ++name;

  DEBUG_MODULE(2, "[1](name: \"%s\", start_pos: %d)", name, start_pos);

  struct list_head* el;
  uint32_t i = 0;

  list_for_each(el, &loaded_modules) {
    LoadedModule* lm = list_entry(el, LoadedModule, link);

    if(i++ < start_pos)
      continue;

    if(str_equal(name, lm->name)) {
      m = lm->module;
      break;
    }
  }

  return m;
}

/*
 * Implements the `loadModule` global: synthesizes and evaluates an
 * import/assignment script for `path`, binding it as `name` (see
 * jsm_module_script).
 */
static JSModuleDef*
jsm_module_load(JSContext* ctx, const char* path, const char* name) {
  DynBuf dbuf;

  DEBUG_MODULE(2, "(path: \"%s\", name: \"%s\")", path, name);

  size_t pos = list_size(&loaded_modules);

  dbuf_init_ctx(ctx, &dbuf);

  jsm_module_script(&dbuf, path, name, FALSE);

  if(*path != '*' && !js_eval_str(ctx, (const char*)dbuf.buf, "<internal>", JS_EVAL_TYPE_MODULE)) {
  } else {
    JS_GetException(ctx);

    jsm_module_script(&dbuf, path, name, TRUE);

    if(js_eval_str(ctx, (const char*)dbuf.buf, "<internal>", JS_EVAL_TYPE_MODULE)) {
      dbuf_free(&dbuf);
      return 0;
    }
  }

  dbuf_free(&dbuf);

  /* jsm_module_loader() registers loaded_modules entries under the *resolved* name
     (e.g. an absolute .so/.js path), not the specifier `path` used here (e.g. "sndobj"),
     so a name-based jsm_module_find(ctx, path, 0) below would never match a module that
     needed resolving. list_add() prepends, and evaluating the synthetic import above adds
     the target module's own entry last (after any of its dependencies), so when the list
     grew it's the new head - grab that directly instead. Falls back to the by-name lookup
     for the case where the module was already loaded/cached and no new entry was added. */
  if(list_size(&loaded_modules) > pos)
    return list_entry(loaded_modules.prev, LoadedModule, link)->module;

  return jsm_module_find(ctx, path, 0);
}

/* Loads a .json file as a synthetic module exporting its parsed content as default. */
static JSModuleDef*
jsm_module_json(JSContext* ctx, const char* path) {
  DynBuf db;
  JSValue ret;
  JSModuleDef* m = 0;
  uint8_t* ptr;
  size_t len, i;

  DEBUG_MODULE(2, "(path: \"%s\")", path);

  if(!(ptr = js_load_file(ctx, &len, path)))
    return 0;

  dbuf_init_ctx(ctx, &db);
  dbuf_putstr(&db, "export default ");

  i = scan_whitenskip((const void*)ptr, len);

  dbuf_put(&db, ptr + i, len - i);
  js_free(ctx, ptr);
  dbuf_0(&db);

  ret = JS_Eval(ctx, (const char*)db.buf, db.size, path, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if(JS_VALUE_GET_TAG(ret) == JS_TAG_MODULE)
    m = JS_VALUE_GET_PTR(ret);
  JS_FreeValue(ctx, ret);

  dbuf_free(&db);
  return m;
}

/*
 * Resolves `module_name` to an existing file path (direct file, or via
 * jsm_search_module).
 */
static char*
jsm_module_locate(JSContext* ctx, const char* module_name, void* opaque) {
  char *tmp, *path = js_strdup(ctx, module_name);
  int i = 0;

  for(;;) {
    DEBUG_MODULE(2, "(i: %d, module_name: \"%s\", opaque: %p) path: \"%s\"", i++, module_name, opaque, path);

    if(has_dot_or_slash(path))
      if(path_isfile1(path))
        break;

    if((tmp = jsm_search_module(ctx, path))) {
      js_free(ctx, path);
      path = tmp;
      break;
    }

    js_free(ctx, path);
    path = 0;
    break;
  }

  return path;
}

/*
 * Decodes a "data:...,<content>[;base64]" URL into module source code (wrapping the
 * payload in JSON.parse(...) for a "/json" media type).
 */
static int
jsm_module_source(JSContext* ctx, const char* name, DynBuf* code) {
  size_t length = strlen(name), offset = str_chr(name, ',');

  if(!name[offset])
    return -1;

  BOOL is_js = byte_finds(name, offset, "/javascript") < offset || byte_finds(name, offset, "/ecmascript") < offset;
  BOOL is_json = !is_js && byte_finds(name, offset, "/json") < offset;
  size_t encoding_offset = byte_rchr(name, offset, ';');
  const char* encoding = encoding_offset > offset ? NULL : &name[encoding_offset + 1];
  BOOL is_base64 = encoding && !strncasecmp(encoding, "base64", 4);

  dbuf_init_ctx(ctx, code);

  ++offset;
  length -= offset;

  if(is_json) {
    if(is_base64)
      dbuf_putstr(code, "import { atos } from 'util';\n");

    dbuf_putstr(code, "export default JSON.parse(");
    if(is_base64)
      dbuf_putstr(code, "atos(");

    dbuf_putc(code, '\'');
    dbuf_put_escaped_table(code, &name[offset], length, escape_singlequote_tab);
    dbuf_putc(code, '\'');

    if(is_base64)
      dbuf_putc(code, ')');

    dbuf_putstr(code, ");");
    dbuf_putc(code, '\n');
  } else if(is_base64) {
    if(dbuf_claim(code, b64url_get_decoded_buffer_size(length)))
      return -1;

    code->size += b64url_decode((const uint8_t*)&name[offset], length, &code->buf[code->size]);
  } else {
    dbuf_put_unescaped_table(code, &name[offset], length, escape_url_tab);
  }

  dbuf_0(code);
  return 0;
}

/*
 * Compiles module source `code` (not run) under the name `name`, installing its
 * import.meta.
 */
static JSModuleDef*
jsm_module_compile(JSContext* ctx, const char* name, const char* code, size_t len, BOOL is_file) {
  JSValue module = JS_Eval(ctx, code, len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  JSModuleDef* m = 0;

  if(!JS_IsException(module)) {
    js_module_set_import_meta(ctx, module, is_file, FALSE);
    m = JS_VALUE_GET_PTR(module);
  }

  JS_FreeValue(ctx, module);
  return m;
}

/* Loads a "data:...,<content>[;base64]" module URL. */
static JSModuleDef*
jsm_module_data(JSContext* ctx, const char* name, void* opaque) {
  DynBuf code;
  JSModuleDef* m = 0;

  if(!jsm_module_source(ctx, name, &code)) {
    m = jsm_module_compile(ctx, name, (const char*)code.buf, code.size, FALSE);
    dbuf_free(&code);
  }

  return m;
}

/* the loaded module an import came from: by full name, else by short name
 * (a builtin is listed as "console" but imports as "/lib/console.js") */
static JSModuleDef*
jsm_module_importer(JSContext* ctx, const char* name) {
  JSModuleDef* m = jsm_module_find(ctx, name, 0);
  struct list_head* el;
  const char* start;
  size_t len;

  if(m)
    return m;

  len = jsm_short_name(name, &start);

  list_for_each(el, &loaded_modules) {
    LoadedModule* lm = list_entry(el, LoadedModule, link);
    const char* s2;

    if(jsm_short_name(lm->name, &s2) == len && !strncmp(start, s2, len))
      return lm->module;
  }

  return 0;
}

/*
 * The engine's JSModuleLoaderFunc: resolves and loads `module_name`, handling
 * data: URLs, registered load hooks, circular-import warnings,
 * package.json aliasing, builtin modules, and filesystem resolution, in that order.
 */
static JSModuleDef*
jsm_module_loader(JSContext* ctx, const char* module_name, void* opaque) {
  char *s = 0, *tmp, *name = js_strdup(ctx, module_name);
  JSModuleDef* m = 0;
  int i = 0;
  BOOL hooked = FALSE;

  DEBUG_MODULE(2, "(i: %d, name: \"%s\", opaque: %p)", i++, name, opaque);

  if(module_hooks_len) {
    hooked = TRUE;
    m = jsm_hook_load(ctx, name, opaque);
    goto end;
  }

  if(str_start(name, "data:")) {
    m = jsm_module_data(ctx, name, opaque);
    goto end;
  }

  /* name must stay the exact js_strdup() block: it is js_free()d or stored in
     LoadedModule below, so prefixes are stripped by re-duplicating, never by `name += n`. */
  if(str_start(name, "file://")) {
    tmp = js_strdup(ctx, name + 7);
    js_free(ctx, name);
    name = tmp;
  }

  /* Bun/Deno compatibility: `node:x` resolves the same as bare `x` would here - this
     engine's own builtin/dynamic-module of that name, not Node's actual
     implementation. Stripped here (not just in jsm_builtin_find(), which only covers
     the static builtin-registry lookup below) so the filesystem/dynamic-.so fallback
     search further down also sees the bare name, for a module that resolves via that
     path rather than being compiled into jsm_builtins (e.g. child_process in a
     build where it's not a static builtin). Excludes "os" - see jsm_builtin_find()'s
     comment on why that one alias would be actively wrong, not just incomplete. */
  if(str_start(name, "node:") && !str_equal(name + 5, "os")) {
    tmp = js_strdup(ctx, name + 5);
    js_free(ctx, name);
    name = tmp;
  }

  if(jsm_stack_find(name) != 0)
    printf("\x1b[1;31mWARNING: circular module dependency '%s' from:\n%s\x1b[0m\n", name, jsm_stack_string());

  if((tmp = jsm_module_package(ctx, name))) {
    js_free(ctx, name);
    name = tmp;
  }

  if(!name[path_component1(name)]) {
    BuiltinModule* rec;

    if((rec = jsm_builtin_find(name))) {
      DEBUG_MODULE(1, "(i: %d) \"%s\" -> \"%s\" (builtin)", i, module_name, rec->module_name);
      m = jsm_builtin_init(ctx, rec);
      goto end;
    }
  }

  if(is_searchable(name) && (tmp = jsm_module_locate(ctx, name, opaque))) {
    js_free(ctx, name);
    name = tmp;
  }

  if(!s)
    s = js_strdup(ctx, name);

  if(s) {
    DEBUG_MODULE(1, "(i: %d) \"%s\" -> \"%s\"", i, module_name, s);

    jsm_stack_push(ctx, s);

    if(str_ends(s, ".json")) {
      m = jsm_module_json(ctx, s);
    } else {
      if(!(m = jsm_module_find(ctx, s, 0)))
        m = js_module_loader(ctx,
                             s,
                             opaque
#ifndef JS_MODULE_LOADER_OLD
                             ,
                             JSM_IMPORT_ATTRIBUTES()
#endif
        );
    }

    jsm_stack_pop(ctx);

    if(!m) {
      JSValue exception = JS_GetException(ctx);

      if(!js_is_null_or_undefined(exception)) {
        const char* msg = JS_ToCString(ctx, exception);
        char* chain = jsm_stack_string();
        BOOL renamed = !str_equal(s, module_name);
        DynBuf db;

        /* JS_Throw*() builds a fresh .stack backtrace automatically; the module import
           chain (jsm_stack, tracked separately from the JS call stack) is appended to the
           message itself since it's the more useful "which module led here" context for a
           load failure, which a plain JS backtrace at the point of `import` won't show. */
        dbuf_init2(&db, 0, vector_realloc);
        dbuf_printf(&db, "could not load module '%s'", s);

        if(renamed)
          dbuf_printf(&db, " (requested as '%s')", module_name);

        if(msg)
          dbuf_printf(&db, ":\n%s", msg);

        if(chain && *chain)
          dbuf_printf(&db, "\nimported from:\n%s", chain);

        dbuf_0(&db);
        JS_ThrowReferenceError(ctx, "%s", (const char*)db.buf);
        dbuf_free(&db);

        if(msg)
          JS_FreeCString(ctx, msg);

        free(chain);
      }

      JS_FreeValue(ctx, exception);
    }

    js_free(ctx, s);

  } else {
    DEBUG_MODULE(1, "\"%s\" -> null", name);
  }

end:
  LoadedModule* lm;

  if((lm = js_malloc(ctx, sizeof(LoadedModule)))) {
    lm->name = name;
    lm->module = m;
    lm->importer = jsm_importer;
    jsm_importer = 0;
    lm->hooked = hooked;
    /* appended, so a module's index never changes */
    list_add_tail(&lm->link, &loaded_modules);
  } else
    js_free(ctx, name);

  if(jsm_importer) {
    js_free(ctx, jsm_importer);
    jsm_importer = 0;
  }

  return m;
}

#ifndef JS_MODULE_LOADER_OLD
/*
 * The module loader with import attributes: `import x from 'a.json' with { type: 'json' }`.
 * The attributes reach js_module_loader() so `type` is honored, and the keys are checked
 * beforehand by js_module_check_attributes() (see jsm_set_module_loader()).
 */
static JSModuleDef*
jsm_module_loader2(JSContext* ctx, const char* module_name, void* opaque, JSValueConst attributes) {
  const JSValue* saved = jsm_import_attributes;
  JSModuleDef* m;

  jsm_import_attributes = &attributes;
  m = jsm_module_loader(ctx, module_name, opaque);
  jsm_import_attributes = saved;
  return m;
}
#endif

/*
 * The engine's module-name normalizer: resolves `name` (as imported from `path`)
 * to a builtin name or absolute file path (the default resolution, no hooks).
 */
static char*
jsm_module_normalize_core(JSContext* ctx, const char* path, const char* name) {
  char* file = 0;
  BuiltinModule* bltin = 0;
  const char* bare = str_start(name, "node:") ? name + 5 : name;

  /* Node's subpath builtins live in flat modules: "fs/promises" -> "fsPromises" */
  if(str_equal(bare, "fs/promises"))
    name = "fsPromises";
  else if(str_equal(bare, "timers/promises"))
    name = "timersPromises";
  else if(str_equal(bare, "assert/strict"))
    name = "assertStrict";
  else if(str_equal(bare, "readline/promises"))
    name = "readlinePromises";

  if(!has_dot_or_slash(name) && (bltin = jsm_builtin_find(name))) {
    if(!file)
      file = js_strdup(ctx, bltin->module_name);
    /* a `data:` importer has no directory (its "path" is the whole source URL),
       so relative specifiers are skipped here and left to the load hook;
       `<...>` importers (eval, REPL) have none either */
  } else if(path[0] != '<' && strncmp(path, "data:", 5) && (path_isdotslash(name) || path_isdotdot(name)) && has_dot_or_slash(name)) {
    DynBuf dir;
    size_t dsl;

    dbuf_init_ctx(ctx, &dir);
    dsl = path_dirlen1(path);

    if(!path[dsl])
      dbuf_putstr(&dir, ".");
    else
      path_append3(path, dsl, &dir);

    path_append2(name, &dir);
    dsl = path_skipdotslash2((const char*)dir.buf, dir.size);

    /* XXX BUG: should use path_normalize* to resolve symlinks */
    dir.size = dsl + path_normalize2((char*)dir.buf + dsl, dir.size - dsl);
    dbuf_0(&dir);

    file = (char*)dir.buf;
  } else if(has_suffix(name, CONFIG_SHEXT) && !name[path_component1(name)]) {
    file = jsm_search_path(ctx, name);
  } else if(has_dot_or_slash(name) && path_exists1(name) && path_isrelative(name)) {
    /* path_absolute1() allocates with libc realloc, but `file` is released with js_free() */
    char* abs = path_absolute1(name);

    path_normalize1(abs);
    file = js_strdup(ctx, abs);
    free(abs);
  }

  if(file == 0)
    if(!bltin && has_dot_or_slash(name) && !module_has_suffix(name))
      file = jsm_search_suffix(ctx, name, &is_module);

  if(file == 0)
    file = js_strdup(ctx, name);

  DEBUG_MODULE(1, "%s: \"%s\" => \"%s\"", path, name, file);
  return file;
}

/* --- module customization hooks: registerHooks() ---
 *
 * Node's synchronous module.registerHooks() (https://nodejs.org/api/module.html):
 *
 * ```js
 * const { deregister } = registerHooks({
 *   resolve(specifier, { parentURL, conditions, importAttributes }, nextResolve) {},
 *   load(url, { format, conditions, importAttributes }, nextLoad) {},
 * });
 * ```
 *
 *   resolve  returns { url, format?, shortCircuit? }
 *   load     returns { format, source, shortCircuit? }
 *
 * hooks run last-registered first; each must call next*() or return
 * `shortCircuit: true`. the engine's own resolution is the last `next`.
 */

/* percent-encodes ' ', '%', '#', '?' and control bytes of a path for a file:// URL;
   returns a js_malloc'd string */
static char*
jsm_url_frompath(JSContext* ctx, const char* path) {
  DynBuf db;
  static const char hex[] = "0123456789ABCDEF";

  if(path[0] != '/')
    return js_strdup(ctx, path);

  dbuf_init_ctx(ctx, &db);
  dbuf_putstr(&db, "file://");

  for(; *path; ++path) {
    uint8_t c = *path;

    if(c <= ' ' || c == '%' || c == '#' || c == '?' || c == 0x7f) {
      dbuf_putc(&db, '%');
      dbuf_putc(&db, hex[c >> 4]);
      dbuf_putc(&db, hex[c & 15]);
    } else {
      dbuf_putc(&db, c);
    }
  }

  dbuf_0(&db);
  return (char*)db.buf;
}

/* maps the module key to the URL hooks see: "/a/b.js" -> "file:///a/b.js",
   "fs" -> "node:fs" ("os", "std" stay bare), anything else unchanged */
static char*
jsm_url_fromkey(JSContext* ctx, const char* key) {
  if(key[0] != '/' && !has_dot_or_slash(key) && jsm_builtin_find(key) && !str_equal(key, "os") && !str_equal(key, "std")) {
    DynBuf db;
    dbuf_init_ctx(ctx, &db);
    dbuf_putstr(&db, "node:");
    dbuf_putstr(&db, key);
    dbuf_0(&db);
    return (char*)db.buf;
  }

  return jsm_url_frompath(ctx, key);
}

/* inverse of jsm_url_fromkey(): "file:///a%20b.js" -> "/a b.js", "node:fs" -> "fs"
   ("node:os" stays); returns a js_malloc'd string */
static char*
jsm_url_tokey(JSContext* ctx, const char* url) {
  DynBuf db;

  if(str_start(url, "node:") && !str_equal(url + 5, "os"))
    return js_strdup(ctx, url + 5);

  if(!str_start(url, "file://"))
    return js_strdup(ctx, url);

  dbuf_init_ctx(ctx, &db);

  for(url += 7; *url; ++url) {
    int h, l;

    if(url[0] == '%' && (h = from_hex(url[1])) >= 0 && (l = from_hex(url[1] ? url[2] : 0)) >= 0) {
      dbuf_putc(&db, h << 4 | l);
      url += 2;
    } else {
      dbuf_putc(&db, *url);
    }
  }

  dbuf_0(&db);
  return (char*)db.buf;
}

/* throws Error with `code` set, e.g. code "ERR_LOADER_CHAIN_INCOMPLETE" */
static JSValue
jsm_throw_code(JSContext* ctx, const char* code, const char* msg) {
  JSValue err = JS_NewError(ctx);

  JS_DefinePropertyValueStr(ctx, err, "message", JS_NewString(ctx, msg), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
  JS_DefinePropertyValueStr(ctx, err, "code", JS_NewString(ctx, code), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
  return JS_Throw(ctx, err);
}

/* the engine's own resolution as the final nextResolve(): returns { url, shortCircuit }
   or an exception */
static JSValue
jsm_default_resolve(JSContext* ctx, JSValueConst specifier, JSValueConst context) {
  JSValue pv = JS_GetPropertyStr(ctx, context, "parentURL"), ret;
  const char* spec = JS_ToCString(ctx, specifier);
  char *base, *file, *tmp, *url;

  if(!spec) {
    JS_FreeValue(ctx, pv);
    return JS_EXCEPTION;
  }

  if(JS_IsString(pv)) {
    const char* pu = JS_ToCString(ctx, pv);
    base = jsm_url_tokey(ctx, pu);
    JS_FreeCString(ctx, pu);
  } else {
    base = js_strdup(ctx, ".");
  }

  JS_FreeValue(ctx, pv);

  tmp = jsm_url_tokey(ctx, spec);
  JS_FreeCString(ctx, spec);

  if(str_start(tmp, "data:") || str_start(tmp, "http:") || str_start(tmp, "https:")) {
    file = tmp;
  } else {
    char* alias;

    file = jsm_module_normalize_core(ctx, base, tmp);
    js_free(ctx, tmp);

    if((alias = jsm_module_package(ctx, file))) {
      js_free(ctx, file);
      file = alias;
    }

    if(!(!file[path_component1(file)] && jsm_builtin_find(file)) && is_searchable(file) && (alias = jsm_module_locate(ctx, file, 0))) {
      js_free(ctx, file);
      file = alias;
    }
  }

  url = jsm_url_fromkey(ctx, file);
  js_free(ctx, file);
  js_free(ctx, base);

  ret = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, ret, "url", JS_NewString(ctx, url));
  JS_SetPropertyStr(ctx, ret, "shortCircuit", JS_TRUE);
  js_free(ctx, url);
  return ret;
}

/* the engine's own loading as the final nextLoad(): returns { format, source, shortCircuit } or an exception.
 *
 *  "builtin" / "addon"  source is null (the engine instantiates them itself)
 *  "json" / "module"    source is the file's text
 */
static JSValue
jsm_default_load(JSContext* ctx, JSValueConst urlv) {
  const char* u = JS_ToCString(ctx, urlv);
  char* name;
  JSValue ret, source = JS_NULL;
  const char* format = "module";

  if(!u)
    return JS_EXCEPTION;

  name = jsm_url_tokey(ctx, u);
  JS_FreeCString(ctx, u);

  if(str_start(name, "data:")) {
    DynBuf code;

    if(jsm_module_source(ctx, name, &code)) {
      js_free(ctx, name);
      return JS_ThrowTypeError(ctx, "invalid data: URL");
    }

    source = JS_NewStringLen(ctx, (const char*)code.buf, code.size);
    dbuf_free(&code);
  } else if(!name[path_component1(name)] && jsm_builtin_find(name)) {
    format = "builtin";
  } else if(has_suffix(name, CONFIG_SHEXT)) {
    format = "addon";
  } else {
    size_t len;
    uint8_t* buf = js_load_file(ctx, &len, name);

    if(!buf) {
      JS_FreeValue(ctx, JS_GetException(ctx));
      JS_ThrowReferenceError(ctx, "could not load module '%s'", name);
      js_free(ctx, name);
      return JS_EXCEPTION;
    }

    if(str_ends(name, ".json"))
      format = "json";

    source = JS_NewStringLen(ctx, (const char*)buf, len);
    js_free(ctx, buf);
  }

  js_free(ctx, name);

  ret = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, ret, "format", JS_NewString(ctx, format));
  JS_SetPropertyStr(ctx, ret, "source", source);
  JS_SetPropertyStr(ctx, ret, "shortCircuit", JS_TRUE);
  return ret;
}

static JSValue jsm_hook_run(JSContext*, int, JSValueConst, int, JSValueConst, JSValueConst);

/* nextResolve(specifier, context?) / nextLoad(url, context?) handed to a hook;
   data = [ next index, hook snapshot, call state, default context ] */
static JSValue
jsm_hook_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValueConst* data) {
  int32_t idx = 0;

  if(argc < 1 || !JS_IsString(argv[0]))
    return JS_ThrowTypeError(ctx, "%s: argument 1 must be a string", magic == HOOK_LOAD ? "nextLoad" : "nextResolve");

  JS_ToInt32(ctx, &idx, data[0]);
  JS_SetPropertyStr(ctx, data[2], "called", JS_TRUE);

  return jsm_hook_run(ctx, magic, data[1], idx, argv[0], argc > 1 && JS_IsObject(argv[1]) ? argv[1] : data[3]);
}

/* runs the hook at the highest index <= idx that defines the method, or the
   engine default when none is left; validates what the hook returned */
static JSValue
jsm_hook_run(JSContext* ctx, int kind, JSValueConst snap, int idx, JSValueConst arg, JSValueConst context) {
  const char* method = kind == HOOK_LOAD ? "load" : "resolve";
  JSValue fn = JS_UNDEFINED, hook, state, next, ret, v;
  JSValueConst data[4], args[3];
  char msg[160];

  for(; idx >= 0; idx--) {
    hook = JS_GetPropertyUint32(ctx, snap, idx);
    fn = JS_GetPropertyStr(ctx, hook, method);
    JS_FreeValue(ctx, hook);

    if(JS_IsFunction(ctx, fn))
      break;

    JS_FreeValue(ctx, fn);
  }

  if(idx < 0)
    return kind == HOOK_LOAD ? jsm_default_load(ctx, arg) : jsm_default_resolve(ctx, arg, context);

  state = JS_NewObject(ctx);
  data[0] = JS_NewInt32(ctx, idx - 1);
  data[1] = snap;
  data[2] = state;
  data[3] = context;
  next = JS_NewCFunctionData(ctx, jsm_hook_next, 2, kind, 4, data);

  args[0] = arg;
  args[1] = context;
  args[2] = next;
  ret = JS_Call(ctx, fn, JS_UNDEFINED, 3, args);

  JS_FreeValue(ctx, fn);
  JS_FreeValue(ctx, next);

  if(JS_IsException(ret)) {
    JS_FreeValue(ctx, state);
    return ret;
  }

  v = JS_GetPropertyStr(ctx, ret, "then");

  if(JS_IsFunction(ctx, v)) {
    JS_FreeValue(ctx, v);
    JS_FreeValue(ctx, state);
    JS_FreeValue(ctx, ret);
    return JS_ThrowTypeError(ctx, "%s hook must be synchronous (got a Promise)", method);
  }

  JS_FreeValue(ctx, v);

  if(!JS_IsObject(ret)) {
    JS_FreeValue(ctx, state);
    JS_FreeValue(ctx, ret);
    return JS_ThrowTypeError(ctx, "%s hook must return an object", method);
  }

  v = JS_GetPropertyStr(ctx, ret, "shortCircuit");

  if(!JS_ToBool(ctx, v)) {
    JS_FreeValue(ctx, v);
    v = JS_GetPropertyStr(ctx, state, "called");

    if(!JS_ToBool(ctx, v)) {
      snprintf(msg, sizeof(msg), "%s hook did not call next%s() and did not return shortCircuit: true", method, kind == HOOK_LOAD ? "Load" : "Resolve");
      JS_FreeValue(ctx, v);
      JS_FreeValue(ctx, state);
      JS_FreeValue(ctx, ret);
      return jsm_throw_code(ctx, "ERR_LOADER_CHAIN_INCOMPLETE", msg);
    }
  }

  JS_FreeValue(ctx, v);
  JS_FreeValue(ctx, state);

  v = JS_GetPropertyStr(ctx, ret, kind == HOOK_LOAD ? "format" : "url");

  if(!JS_IsString(v)) {
    snprintf(msg, sizeof(msg), "%s hook must return { %s: string }", method, kind == HOOK_LOAD ? "format" : "url");
    JS_FreeValue(ctx, v);
    JS_FreeValue(ctx, ret);
    return jsm_throw_code(ctx, "ERR_INVALID_RETURN_PROPERTY_VALUE", msg);
  }

  JS_FreeValue(ctx, v);
  return ret;
}

/* builds { conditions, importAttributes } plus the given extras into a fresh context
   object; `key` is "parentURL" or "format" and `val` its string value or NULL */
static JSValue
jsm_hook_context(JSContext* ctx, const char* key, const char* val) {
  JSValue c = JS_NewObject(ctx), conds = JS_NewArray(ctx);

  JS_SetPropertyUint32(ctx, conds, 0, JS_NewString(ctx, "node"));
  JS_SetPropertyUint32(ctx, conds, 1, JS_NewString(ctx, "import"));
  JS_SetPropertyStr(ctx, c, "conditions", conds);
  JS_SetPropertyStr(ctx, c, "importAttributes", JS_NewObject(ctx));
  JS_SetPropertyStr(ctx, c, key, val ? JS_NewString(ctx, val) : JS_UNDEFINED);
  return c;
}

static JSValue
jsm_hook_snapshot(JSContext* ctx) {
  JSValue snap = JS_NewArray(ctx);
  size_t i;

  for(i = 0; i < module_hooks_len; i++)
    JS_SetPropertyUint32(ctx, snap, i, JS_DupValue(ctx, module_hooks[i]));

  return snap;
}

/* normalize callback while hooks are registered: runs the resolve chain and maps the
   resulting URL back to a module key; returns NULL with an exception pending */
static char*
jsm_hook_normalize(JSContext* ctx, const char* path, const char* name) {
  JSValue snap = jsm_hook_snapshot(ctx), namev = JS_NewString(ctx, name), res, v;
  JSValue context;
  char *url, *key = 0;

  if(path[0] && path[0] != '<' && !str_equal(path, ".")) {
    /* a relative importer ("p.mjs") becomes an absolute file URL, as in Node */
    char* abs = path[0] != '/' && path[str_chr(path, ':')] == 0 ? path_absolute1(path) : 0;

    url = jsm_url_frompath(ctx, abs ? abs : path);
    free(abs);
    context = jsm_hook_context(ctx, "parentURL", url);
    js_free(ctx, url);
  } else {
    context = jsm_hook_context(ctx, "parentURL", 0);
  }

  res = jsm_hook_run(ctx, HOOK_RESOLVE, snap, module_hooks_len - 1, namev, context);

  JS_FreeValue(ctx, snap);
  JS_FreeValue(ctx, namev);
  JS_FreeValue(ctx, context);

  if(JS_IsException(res))
    return 0;

  v = JS_GetPropertyStr(ctx, res, "url");
  if((url = js_tostring(ctx, v))) {
    key = jsm_url_tokey(ctx, url);
    js_free(ctx, url);
  }
  JS_FreeValue(ctx, v);

  v = JS_GetPropertyStr(ctx, res, "format");
  if(key && JS_IsString(v)) {
    if(!hook_formats_set) {
      hook_formats = JS_NewObjectProto(ctx, JS_NULL);
      hook_formats_set = TRUE;
    }
    JS_SetPropertyStr(ctx, hook_formats, key, JS_DupValue(ctx, v));
  }
  JS_FreeValue(ctx, v);
  JS_FreeValue(ctx, res);

  return key;
}

/* bytes of a load hook's `source`: a string, ArrayBuffer or typed array;
 *pfree receives what to JS_FreeCString (NULL for buffers) */
static const char*
jsm_hook_bytes(JSContext* ctx, JSValueConst source, size_t* plen, BOOL* is_str) {
  size_t off = 0, blen = 0, bpe = 0;
  uint8_t* ptr;
  JSValue buf;

  if(JS_IsString(source)) {
    *is_str = TRUE;
    return JS_ToCStringLen(ctx, plen, source);
  }

  *is_str = FALSE;

  if((ptr = JS_GetArrayBuffer(ctx, plen, source)))
    return (const char*)ptr;

  JS_FreeValue(ctx, JS_GetException(ctx));
  buf = JS_GetTypedArrayBuffer(ctx, source, &off, &blen, &bpe);

  if(JS_IsException(buf))
    return 0;

  ptr = JS_GetArrayBuffer(ctx, plen, buf);
  JS_FreeValue(ctx, buf);

  if(!ptr)
    return 0;

  *plen = blen;
  return (const char*)ptr + off;
}

/* load callback while hooks are registered: runs the load chain for `name` and
   compiles what it returns; returns 0 with an exception pending */
static JSModuleDef*
jsm_hook_load(JSContext* ctx, const char* name, void* opaque) {
  JSValue snap = jsm_hook_snapshot(ctx), urlv, context, res, fmt, src;
  JSModuleDef* m = 0;
  const char *format, *bytes;
  size_t len = 0;
  BOOL is_str = FALSE;
  char* url = jsm_url_fromkey(ctx, name);

  urlv = JS_NewString(ctx, url);
  js_free(ctx, url);

  context = jsm_hook_context(ctx, "format", 0);

  if(hook_formats_set) {
    JSValue f = JS_GetPropertyStr(ctx, hook_formats, name);

    if(JS_IsString(f))
      JS_SetPropertyStr(ctx, context, "format", JS_DupValue(ctx, f));

    JS_FreeValue(ctx, f);
  }

  jsm_stack_push(ctx, name);
  res = jsm_hook_run(ctx, HOOK_LOAD, snap, module_hooks_len - 1, urlv, context);

  JS_FreeValue(ctx, snap);
  JS_FreeValue(ctx, urlv);
  JS_FreeValue(ctx, context);

  if(JS_IsException(res))
    goto done;

  fmt = JS_GetPropertyStr(ctx, res, "format");
  src = JS_GetPropertyStr(ctx, res, "source");
  format = JS_ToCString(ctx, fmt);

  if(str_equal(format, "builtin")) {
    BuiltinModule* rec = jsm_builtin_find(name);

    if(rec)
      m = jsm_builtin_init(ctx, rec);
    else
      JS_ThrowReferenceError(ctx, "no builtin module '%s'", name);
  } else if(str_equal(format, "addon")) {
    m = js_module_loader(ctx,
                         name,
                         opaque
#ifndef JS_MODULE_LOADER_OLD
                         ,
                         JS_UNDEFINED
#endif
    );
  } else if(str_equal(format, "module") || str_equal(format, "json")) {
    if(JS_IsNull(src) || JS_IsUndefined(src)) {
      JS_ThrowTypeError(ctx, "load hook returned no source for format '%s'", format);
    } else if((bytes = jsm_hook_bytes(ctx, src, &len, &is_str))) {
      BOOL is_file = name[0] == '/';

      if(str_equal(format, "json")) {
        DynBuf db;
        size_t i = scan_whitenskip((const void*)bytes, len);

        dbuf_init_ctx(ctx, &db);
        dbuf_putstr(&db, "export default ");
        dbuf_put(&db, (const uint8_t*)bytes + i, len - i);
        dbuf_0(&db);
        m = jsm_module_compile(ctx, name, (const char*)db.buf, db.size, is_file);
        dbuf_free(&db);
      } else {
        m = jsm_module_compile(ctx, name, bytes, len, is_file);
      }

      if(is_str)
        JS_FreeCString(ctx, bytes);
    }
  } else {
    JS_ThrowTypeError(ctx, "unsupported module format '%s' for '%s'", format, name);
  }

  JS_FreeCString(ctx, format);
  JS_FreeValue(ctx, fmt);
  JS_FreeValue(ctx, src);

done:
  JS_FreeValue(ctx, res);
  jsm_stack_pop(ctx);
  return m;
}

/* deregister() of a registerHooks() result; data[0] is the hook object */
static JSValue
jsm_hook_deregister(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValueConst* data) {
  size_t i;

  for(i = 0; i < module_hooks_len; i++) {
    if(js_value_obj(module_hooks[i]) != js_value_obj(data[0]))
      continue;

    JS_FreeValue(ctx, module_hooks[i]);
    memmove(&module_hooks[i], &module_hooks[i + 1], (module_hooks_len - i - 1) * sizeof(JSValue));
    --module_hooks_len;
    break;
  }

  return JS_UNDEFINED;
}

/*
 * registerHooks: installs resolve/load hooks (Node's module.registerHooks).
 *
 * ```js
 * const h = registerHooks({ resolve, load });
 * h.deregister();   // or `using h = registerHooks(...)`
 * ```
 *
 *   throws   TypeError for a non-object, or no function member
 *
 * a global of qjsm; lib/module.js re-exports it as `registerHooks`.
 */
static JSValue
jsm_hook_register(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue r, l, ret, dereg, global, symbol, dispose;
  BOOL ok;
  JSValue* p;

  if(argc < 1 || !JS_IsObject(argv[0]))
    return JS_ThrowTypeError(ctx, "registerHooks: argument 1 must be an object { resolve?, load? }");

  r = JS_GetPropertyStr(ctx, argv[0], "resolve");
  l = JS_GetPropertyStr(ctx, argv[0], "load");
  ok = JS_IsFunction(ctx, r) || JS_IsFunction(ctx, l);
  ok = ok && (JS_IsFunction(ctx, r) || JS_IsUndefined(r)) && (JS_IsFunction(ctx, l) || JS_IsUndefined(l));
  JS_FreeValue(ctx, r);
  JS_FreeValue(ctx, l);

  if(!ok)
    return JS_ThrowTypeError(ctx, "registerHooks: expected { resolve?: function, load?: function } with at least one");

  if(!(p = js_realloc(ctx, module_hooks, (module_hooks_len + 1) * sizeof(JSValue))))
    return JS_EXCEPTION;

  module_hooks = p;
  module_hooks[module_hooks_len++] = JS_DupValue(ctx, argv[0]);

  ret = JS_NewObject(ctx);
  dereg = JS_NewCFunctionData(ctx, jsm_hook_deregister, 0, 0, 1, &argv[0]);
  JS_SetPropertyStr(ctx, ret, "deregister", JS_DupValue(ctx, dereg));

  global = JS_GetGlobalObject(ctx);
  symbol = JS_GetPropertyStr(ctx, global, "Symbol");
  dispose = JS_GetPropertyStr(ctx, symbol, "dispose");

  if(JS_IsSymbol(dispose)) {
    JSAtom atom = JS_ValueToAtom(ctx, dispose);
    JS_DefinePropertyValue(ctx, ret, atom, JS_DupValue(ctx, dereg), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, atom);
  }

  JS_FreeValue(ctx, dispose);
  JS_FreeValue(ctx, symbol);
  JS_FreeValue(ctx, global);
  JS_FreeValue(ctx, dereg);
  return ret;
}

static char*
jsm_module_normalize(JSContext* ctx, const char* path, const char* name, void* opaque) {
  if(module_hooks_len)
    return jsm_hook_normalize(ctx, path, name);

  return jsm_module_normalize_core(ctx, path, name);
}

/* the engine's normalize callback: remembers the importer for the load that follows */
static char*
jsm_module_normalize_import(JSContext* ctx, const char* path, const char* name, void* opaque) {
  if(jsm_importer)
    js_free(ctx, jsm_importer);

  jsm_importer = js_strdup(ctx, path);
  return jsm_module_normalize(ctx, path, name, opaque);
}

/* "native" | "data" | "json" | "js" */
static const char*
jsm_module_kind(LoadedModule* lm) {
  if(jsm_is_native(lm->module))
    return "native";
  if(str_start(lm->name, "data:"))
    return "data";
  if(str_ends(lm->name, ".json"))
    return "json";
  return "js";
}

/* the `moduleList` / getModule() item for `lm`, listed at `index` */
static JSValue
jsm_module_item(JSContext* ctx, LoadedModule* lm, int index) {
  JSModuleDef* m = lm->module;
  const char* kind = jsm_module_kind(lm);
  JSValue obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, obj, "index", JS_NewInt32(ctx, index));
  {
    const char* start;
    size_t len = jsm_short_name(lm->name, &start);

    JS_SetPropertyStr(ctx, obj, "name", JS_NewStringLen(ctx, start, len));
  }

  JS_SetPropertyStr(ctx, obj, "kind", JS_NewString(ctx, kind));

  if(jsm_is_builtin(m))
    JS_SetPropertyStr(ctx, obj, "builtin", JS_TRUE);

  if(!jsm_is_builtin(m) && !str_equal(kind, "data"))
    JS_SetPropertyStr(ctx, obj, "path", JS_NewString(ctx, lm->name));

  /* the importer is registered after the modules it imports, so resolve it now */
  if(lm->importer) {
    JSModuleDef* parent = jsm_module_importer(ctx, lm->importer);

    if(parent && parent != m)
      JS_SetPropertyStr(ctx, obj, "parent", JS_NewInt32(ctx, jsm_module_indexof(parent)));
  }

  if(lm->hooked)
    JS_SetPropertyStr(ctx, obj, "hooked", JS_TRUE);

#ifdef HAVE_JS_GETMODULESTATUS
  {
    static const char* const status[] = {
        "unlinked",
        "linking",
        "linked",
        "evaluating",
        "evaluating-async",
        "evaluated",
    };
    int st = JS_GetModuleStatus(ctx, m);

    JS_SetPropertyStr(ctx, obj, "status", JS_NewString(ctx, st >= 0 && st < (int)countof(status) ? status[st] : "unknown"));
  }
#endif

#ifdef HAVE_JS_ISMODULEASYNC
  JS_SetPropertyStr(ctx, obj, "async", JS_NewBool(ctx, JS_IsModuleAsync(ctx, m)));
#endif

  return obj;
}

/* Getter backing the `moduleList` global: lists every currently loaded module. */
static JSValue
jsm_module_array(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue ret = JS_NewArray(ctx);
  struct list_head* el;
  uint32_t i = 0;

  list_for_each(el, &loaded_modules) {
    LoadedModule* lm = list_entry(el, LoadedModule, link);

    JS_SetPropertyUint32(ctx, ret, i, jsm_module_item(ctx, lm, i));
    ++i;
  }

  return ret;
}

/* Getter backing the `builtins` global: a module item for every registered builtin;
   one not loaded yet has no module, so only `name`, `kind` and `builtin`. */
static JSValue
jsm_module_builtins(JSContext* ctx, JSValueConst this_val) {
  JSValue ret = JS_NewArray(ctx);
  BuiltinModule* rec;
  uint32_t i = 0;
  JSAtom delete_props[] = {
      JS_NewAtom(ctx, "index"),
      JS_NewAtom(ctx, "builtin"),
      JS_NewAtom(ctx, "parent"),
  };

  vector_foreach_t(&jsm_builtins, rec) {
    JSValue entry = JS_UNDEFINED;
    struct list_head* el;
    int index = 0;

    if(rec->def)
      list_for_each(el, &loaded_modules) {
        LoadedModule* lm = list_entry(el, LoadedModule, link);

        if(lm->module == rec->def) {
          entry = jsm_module_item(ctx, lm, index);
          break;
        }
        ++index;
      }

    if(JS_IsUndefined(entry)) {
      entry = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, entry, "name", JS_NewString(ctx, rec->module_name));
      JS_SetPropertyStr(ctx, entry, "kind", JS_NewString(ctx, rec->module_func ? "native" : "js"));
    }

    for(int j = 0; j < countof(delete_props); j++)
      JS_DeleteProperty(ctx, entry, delete_props[j], 0);

    JS_SetPropertyUint32(ctx, ret, i++, entry);
  }

  for(int j = 0; j < countof(delete_props); j++)
    JS_FreeAtom(ctx, delete_props[j]);

  return ret;
}

/*
 * Computes a pointer's offset from the tracing allocator's fixed base address, for
 * compact "%p" trace output.
 */
static inline unsigned long long
jsm_trace_malloc_ptr_offset(uint8_t* ptr, struct trace_malloc_data* dp) {
  return ptr - dp->base;
}

/* default memory allocation functions with memory limitation */
/* Platform-specific malloc_usable_size() wrapper used by the tracing allocator. */
static inline size_t
jsm_trace_malloc_usable_size(const void* ptr) {
#if defined(__APPLE__)
  return malloc_size(ptr);
#elif defined(_WIN32)
  return _msize(ptr);
#elif defined(EMSCRIPTEN) || defined(__dietlibc__) || defined(__MSYS__) || defined(ANDROID) || defined(DONT_HAVE_MALLOC_USABLE_SIZE)
  return 0;
#elif defined(__linux__) || defined(HAVE_MALLOC_USABLE_SIZE)
  return malloc_usable_size((void*)ptr);
#else
#warning change this to `return 0;` if compilation fails
  /* change this to `return 0;` if compilation fails */
  return malloc_usable_size(ptr);
#endif
}

/*
 * printf-like tracer for -T/--trace allocator events; only understands "%p" and
 * "%zd" conversions (everything else is copied through verbatim).
 */
static void
FORMAT_STRING(2, 3) jsm_trace_malloc_printf(JSMallocState* s, const char* fmt, ...) {
  va_list ap;
  int c;

  va_start(ap, fmt);

  while((c = *fmt++) != '\0') {
    if(c == '%') {
      /* only handle %p and %zd */
      if(*fmt == 'p') {
        uint8_t* ptr;

        if(!(ptr = va_arg(ap, void*)))
          printf("0");
        else
          printf("H%+06lld.%zd", jsm_trace_malloc_ptr_offset(ptr, s->opaque), jsm_trace_malloc_usable_size(ptr));

        fmt++;
        continue;
      }

      if(fmt[0] == 'z' && fmt[1] == 'd') {
        size_t sz = va_arg(ap, size_t);

        printf("%zd", sz);
        fmt += 2;
        continue;
      }
    }

    putc(c, stdout);
  }

  va_end(ap);
}

/* Initializes the tracing allocator's base pointer for offset computation. */
static void
jsm_trace_malloc_init(struct trace_malloc_data* s) {
  free(s->base = malloc(8));
}

/* Tracing malloc(): enforces the malloc_limit and logs "A size -> ptr". */
static void*
jsm_trace_malloc(JSMallocState* s, size_t size) {
  void* ptr;

  /* Do not allocate zero bytes: behavior is platform dependent */
  assert(size != 0);

  if(unlikely(s->malloc_size + size > s->malloc_limit))
    return 0;

  ptr = malloc(size);
  jsm_trace_malloc_printf(s, "A %zd -> %p\n", size, ptr);

  if(ptr) {
    s->malloc_count++;
    s->malloc_size += jsm_trace_malloc_usable_size(ptr) + MALLOC_OVERHEAD;
  }

  return ptr;
}

/* Tracing free(): updates byte counters and logs "F ptr". */
static void
jsm_trace_free(JSMallocState* s, void* ptr) {
  if(!ptr)
    return;

  jsm_trace_malloc_printf(s, "F %p\n", ptr);
  s->malloc_count--;
  s->malloc_size -= jsm_trace_malloc_usable_size(ptr) + MALLOC_OVERHEAD;
  free(ptr);
}

/* Tracing realloc(): enforces the malloc_limit and logs "R size ptr -> ptr". */
static void*
jsm_trace_realloc(JSMallocState* s, void* ptr, size_t size) {
  size_t old_size;

  if(!ptr) {
    if(size == 0)
      return 0;

    return jsm_trace_malloc(s, size);
  }

  old_size = jsm_trace_malloc_usable_size(ptr);

  if(size == 0) {
    jsm_trace_malloc_printf(s, "R %zd %p\n", size, ptr);
    s->malloc_count--;
    s->malloc_size -= old_size + MALLOC_OVERHEAD;
    free(ptr);
    return 0;
  }

  if(s->malloc_size + size - old_size > s->malloc_limit)
    return 0;

  jsm_trace_malloc_printf(s, "R %zd %p", size, ptr);

  ptr = realloc(ptr, size);
  jsm_trace_malloc_printf(s, " -> %p\n", ptr);

  if(ptr)
    s->malloc_size += jsm_trace_malloc_usable_size(ptr) - old_size;

  return ptr;
}

/* JSMallocFunctions vtable wiring the jsm_trace_* functions above together for
   JS_NewRuntime2(); grouped here with them rather than in the general globals section
   since it's really just their shared "wiring", not independent state. */
static const JSMallocFunctions trace_mf = {
    jsm_trace_malloc,
    jsm_trace_free,
    jsm_trace_realloc,
    jsm_trace_malloc_usable_size,
};

/*
 *
 * Implements the `evalFile`/`evalBuf` globals: evaluates a script/module from a
 * file path or an in-memory string.
 */
static JSValue
jsm_eval_script(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  int32_t flags = 0;
  size_t len;
  const char* str = JS_ToCStringLen(ctx, &len, argv[0]);
  char* file = 0;
  JSValue tmp_global = JS_UNINITIALIZED;

  if(argc > 1 && JS_IsString(argv[1])) {
    file = js_tostring(ctx, argv[1]);
    argc--;
    argv++;
  }

  if(!file)
    if(magic == 0)
      file = js_strdup(ctx, str);

  if(argc > 1) {
    if(!JS_IsNumber(argv[1]) || JS_ToInt32(ctx, &flags, argv[1])) {
      flags = (js_get_propertystr_bool(ctx, argv[1], "backtrace_barrier") ? JS_EVAL_FLAG_BACKTRACE_BARRIER : 0) |
              (js_get_propertystr_bool(ctx, argv[1], "async") ? JS_EVAL_FLAG_ASYNC : 0) |
              (js_get_propertystr_bool(ctx, argv[1], "strict") ? JS_EVAL_FLAG_STRICT : 0)
#ifdef JS_EVAL_FLAG_STRIP
              | (js_get_propertystr_bool(ctx, argv[1], "strip") ? JS_EVAL_FLAG_STRIP : 0)
#endif
          ;

      if(js_has_propertystr(ctx, argv[1], "global"))
        tmp_global = JS_GetPropertyStr(ctx, argv[1], "global");
    }
  } else
    flags = str_ends(file ? file : str, ".mjs") ? JS_EVAL_TYPE_MODULE : 0;

  if(file)
    jsm_stack_push(ctx, file);

  switch(magic) {
    case EVAL_FILE: {
      ret = JS_IsUninitialized(tmp_global) ? js_eval_file(ctx, str, flags) : js_eval_this_file(ctx, tmp_global, str, flags);
      break;
    }
    case EVAL_BUF: {
      ret = JS_IsUninitialized(tmp_global) ? js_eval_buf(ctx, str, len, file, flags) : js_eval_this_buf(ctx, tmp_global, str, len, file, flags);
      break;
    }
  }

  if(!JS_IsUninitialized(tmp_global))
    JS_FreeValue(ctx, tmp_global);

  if(file) {
    jsm_stack_pop(ctx);
    js_free(ctx, file);
  }

  if(JS_IsException(ret)) {
    ret = JS_GetException(ctx);

    if(JS_IsUninitialized(ret))
      ret = JS_UNDEFINED;
  }

  if(JS_VALUE_GET_TAG(ret) == JS_TAG_MODULE) {
    JSModuleDef* m = JS_VALUE_GET_PTR(ret);
    JSValue obj = JS_NewObject(ctx);

    ret = obj;
  }

  JS_FreeCString(ctx, str);
  return ret;
}

/*
 * Evaluates the REPL bootstrap script (imports and starts `repl.REPL`) the first
 * time `interactive` is 1.
 */
static void
jsm_interactive_start(JSContext* ctx, BOOL global) {
  if(interactive == 1) {
    jsm_promise = js_eval_fmt(ctx,
                              JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_ASYNC,
                              "import { REPL } from 'repl';\n"
                              "%srepl = new REPL('%.*s'.replace(/.*\\//g, '').replace(/\\.js$/g, ''), false);\n"
                              "repl.loadSaveOptions();\n"
                              "repl.historyLoad();\n"
                              "await repl.run();\n",
                              global ? "globalThis." : "const ",
                              (int)str_chr(exename, '.'),
                              exename);

    interactive = 2;
  }
}

/* Implements the `startInteractive` global (JS-callable entry point). */
static JSValue
jsm_interactive_func(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  BOOL global = TRUE;

  if(argc > 0)
    global = JS_ToBool(ctx, argv[0]);

  jsm_interactive_start(ctx, global);
  return JS_UNDEFINED;
}

/*
 * JSJobFunc-shaped wrapper around jsm_interactive_func(), used to defer REPL
 * startup to a job (see jsm_signal_handler).
 */
static JSValue
jsm_interactive_job(JSContext* ctx, int argc, JSValueConst argv[]) {
  return jsm_interactive_func(ctx, JS_NULL, argc, argv);
}

/* Magic values for jsm_module_func, the dispatcher behind every `*Module` global
    (findModule, loadModule, normalizeModule, ...). */
enum {
  FIND_MODULE,
  LOAD_MODULE,
  REQUIRE_MODULE,
  LOCATE_MODULE,
  NORMALIZE_MODULE,
  RESOLVE_MODULE,
  GET_MODULE_NAME,
  GET_MODULE_META,
  GET_MODULE_NS,
  GET_MODULE,
};

/*
 * Dispatcher behind every `*Module` global (see the FIND_MODULE... magic enum).
 *
 * Modules are always identified by their index in the loaded-module list
 * (the order of `moduleList`), never by a JS_TAG_MODULE value: findModule()
 * and loadModule() return that index, and resolveModule(),
 * normalizeModule(), getModuleName(), getModuleMetaObject() and getModuleNS()
 * take it (a module name is accepted as well).
 */
static JSValue
jsm_module_func(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  static const char* const func_names[] = {
      "findModule",
      "loadModule",
      "requireModule",
      "locateModule",
      "normalizeModule",
      "resolveModule",
      "getModuleName",
      "getModuleMetaObject",
      "getModuleNS",
      "getModule",
  };
  JSValue ret = JS_EXCEPTION;
  JSModuleDef* m = 0;
  const char* name = 0;

  if(magic == RESOLVE_MODULE || magic == GET_MODULE || magic == GET_MODULE_NAME || magic == GET_MODULE_META || magic == GET_MODULE_NS ||
     (magic == NORMALIZE_MODULE && (JS_IsModule(argv[0]) || JS_IsNumber(argv[0])))) {
    if(!(m = jsm_module_def(ctx, argv[0]))) {
      if(magic == GET_MODULE)
        return JS_NULL;

      return JS_ThrowTypeError(ctx, "%s: argument 1 expecting module index", func_names[magic]);
    }
  } else {
    name = js_tostring(ctx, argv[0]);
  }

  if(magic == LOAD_MODULE || magic == REQUIRE_MODULE) {
    char* path;

    if((path = jsm_module_normalize(ctx, ".", name, 0))) {
      js_free(ctx, (void*)name);
      name = path;
    }
  }

  switch(magic) {
    case FIND_MODULE: {
      int32_t start = 0;

      if(argc > 1)
        JS_ToInt32(ctx, &start, argv[1]);

      /* the full name first, then the short name ("extendArray") */
      if(!(m = jsm_module_find(ctx, name, start))) {
        struct list_head* el;
        int32_t i = 0;

        list_for_each(el, &loaded_modules) {
          LoadedModule* lm = list_entry(el, LoadedModule, link);

          if(i++ >= start && jsm_short_match(lm->name, name)) {
            m = lm->module;
            break;
          }
        }
      }

      ret = JS_NewInt32(ctx, jsm_module_indexof(m));
      break;
    }
    case LOAD_MODULE: {
      const char* key = 0;

      if(argc > 1)
        key = JS_ToCString(ctx, argv[1]);

      if((m = jsm_module_load(ctx, name, key)))
        ret = JS_NewInt32(ctx, jsm_module_indexof(m));
      else
        ret = JS_ThrowInternalError(ctx, "Failed loading module '%s'", name);

      if(key)
        JS_FreeCString(ctx, key);

      break;
    }
    case REQUIRE_MODULE: {
      if((m = jsm_module_load(ctx, name, 0)))
        ret = JS_GetModuleNamespace(ctx, m);
      else
        ret = JS_ThrowInternalError(ctx, "Failed loading module '%s'", name);

      break;
    }
    case LOCATE_MODULE: {
      char* s;

      if((s = jsm_module_locate(ctx, name, 0))) {
        ret = JS_NewString(ctx, s);
        js_free(ctx, s);
      } else
        ret = JS_NULL;

      break;
    }
    case NORMALIZE_MODULE: {
      const char *path, *module, *file;

      path = m ? module_namecstr(ctx, m) : JS_ToCString(ctx, argv[0]);
      module = JS_ToCString(ctx, argv[1]);

      if((file = jsm_module_normalize(ctx, path, module, 0))) {
        ret = JS_NewString(ctx, file);
        js_free(ctx, (char*)file);
      }

      JS_FreeCString(ctx, path);
      JS_FreeCString(ctx, module);
      break;
    }
    case RESOLVE_MODULE: {
      ret = JS_NewInt32(ctx, JS_ResolveModule(ctx, JS_MKPTR(JS_TAG_MODULE, m)));
      break;
    }
    case GET_MODULE_NAME: {
      JSAtom atom = JS_GetModuleName(ctx, m);

      ret = JS_AtomToString(ctx, atom);
      JS_FreeAtom(ctx, atom);
      break;
    }
    case GET_MODULE_META: {
      ret = JS_GetImportMeta(ctx, m);
      break;
    }
    case GET_MODULE_NS: {
      ret = JS_GetModuleNamespace(ctx, m);
      break;
    }
    case GET_MODULE: {
      struct list_head* el;
      uint32_t i = 0;

      ret = JS_NULL;

      list_for_each(el, &loaded_modules) {
        LoadedModule* lm = list_entry(el, LoadedModule, link);

        if(lm->module == m) {
          ret = jsm_module_item(ctx, lm, i);
          break;
        }
        ++i;
      }
      break;
    }
  }

  if(name)
    js_free(ctx, (char*)name);

  return ret;
}

/* Table of global functions/getters installed by main() via JS_SetPropertyFunctionList();
   references EVAL_FILE/EVAL_BUF, the module-func magic enum, and jsm_interactive_func
   (forward-declared above). */
static const JSCFunctionListEntry jsm_global_funcs[] = {
    JS_CFUNC_MAGIC_DEF("evalFile", 1, jsm_eval_script, EVAL_FILE),
    JS_CFUNC_MAGIC_DEF("evalBuf", 1, jsm_eval_script, EVAL_BUF),
    JS_CGETSET_DEF("builtins", jsm_module_builtins, 0),
    JS_CGETSET_MAGIC_DEF("moduleList", jsm_module_array, 0, 0),
    JS_CFUNC_DEF("registerHooks", 1, jsm_hook_register),
    JS_CGETSET_MAGIC_DEF("scriptList", jsm_stack_get, 0, SCRIPT_LIST),
    JS_CGETSET_MAGIC_DEF("scriptFile", jsm_stack_get, 0, SCRIPT_FILE),
    JS_CGETSET_MAGIC_DEF("scriptDir", jsm_stack_get, 0, SCRIPT_DIRNAME),
    JS_CGETSET_MAGIC_DEF("__filename", jsm_stack_get, 0, SCRIPT_FILENAME),
    JS_CGETSET_MAGIC_DEF("__dirname", jsm_stack_get, 0, SCRIPT_DIRNAME),
    JS_CFUNC_MAGIC_DEF("findModule", 1, jsm_module_func, FIND_MODULE),
    JS_CFUNC_MAGIC_DEF("loadModule", 1, jsm_module_func, LOAD_MODULE),
    JS_CFUNC_MAGIC_DEF("resolveModule", 1, jsm_module_func, RESOLVE_MODULE),
    JS_CFUNC_MAGIC_DEF("requireModule", 1, jsm_module_func, REQUIRE_MODULE),
    JS_CFUNC_MAGIC_DEF("normalizeModule", 2, jsm_module_func, NORMALIZE_MODULE),
    JS_CFUNC_MAGIC_DEF("locateModule", 1, jsm_module_func, LOCATE_MODULE),
    JS_CFUNC_MAGIC_DEF("getModuleName", 1, jsm_module_func, GET_MODULE_NAME),
    JS_CFUNC_MAGIC_DEF("getModuleMetaObject", 1, jsm_module_func, GET_MODULE_META),
    JS_CFUNC_MAGIC_DEF("getModuleNS", 1, jsm_module_func, GET_MODULE_NS),
    JS_CFUNC_MAGIC_DEF("getModule", 1, jsm_module_func, GET_MODULE),
    JS_CFUNC_DEF("startInteractive", 0, jsm_interactive_func),
};

#ifndef _WIN32
/* SIGUSR1 handler: schedules a job to enter interactive mode. */
static void
jsm_signal_handler(int arg) {
  switch(arg) {
    case SIGUSR1: {
      interactive = 1;

      JS_EnqueueJob(jsm_ctx, &jsm_interactive_job, 0, 0);
      break;
    }
  }
}
#endif

/* JS_SetInterruptHandler() callback; currently a no-op (never requests interruption). */
static int
jsm_interrupt_handler(JSRuntime* rt, void* opaque) {
  return 0;
}

/*
 * Installs the module loader on a runtime. With import attributes (unless
 * JS_MODULE_LOADER_OLD), an attribute key the engine does not support, i.e. anything
 * but `type`, is a TypeError when the import is resolved, as in js_module_check_attributes().
 */
static void
jsm_set_module_loader(JSRuntime* rt) {
#ifdef JS_MODULE_LOADER_OLD
  JS_SetModuleLoaderFunc(rt, jsm_module_normalize_import, jsm_module_loader, NULL);
#else
  JS_SetModuleLoaderFunc2(rt, jsm_module_normalize_import, jsm_module_loader2, js_module_check_attributes, NULL);
#endif
}

/* Populates jsm_builtins from quickjs-builtins.h (once per process). */
static void
jsm_init(JSContext* ctx) {
  dbuf_init2(&jsm_builtins, 0, &vector_realloc);

#define BUILTIN_NATIVE(name) vector_push(&jsm_builtins, (BuiltinModule)RECORD_NATIVE(name));
#define BUILTIN_COMPILED(name) vector_push(&jsm_builtins, (BuiltinModule)RECORD_COMPILED(name));

  BUILTIN_NATIVE(std)
  BUILTIN_NATIVE(os)

#include "quickjs-builtins.h"

#undef BUILTIN_NATIVE
#undef BUILTIN_COMPILED
}

/*
 * Creates a new JSContext with the bignum extensions and builtin-module registry
 * wired up. Also used as the worker-thread new-context callback.
 */
static JSContext*
jsm_context_new(JSRuntime* rt) {
  JSContext* ctx;

  if(!(ctx = JS_NewContext(rt)))
    return 0;

  /* Main-thread startup sets this on jsm_rt directly (see main()), but a
   * worker thread's JSRuntime (created fresh in worker_func(), see
   * quickjs-libc.c) never goes through that path - this function is also
   * registered as the worker new-context callback via
   * js_std_set_worker_new_context_func(), so it must install the loader
   * itself or bare specifiers (even 'os'/'std', already registered on this
   * same rt) are unresolvable inside every worker. */
  jsm_set_module_loader(rt);

  /* loaded_modules is thread_local (jsm_module_find() walks it via
   * list_for_each) and main() only ever init_list_head()s the main thread's
   * own copy - a worker thread's copy is zero-initialized, not a valid empty
   * circular list, so the first jsm_module_find() call from a worker
   * dereferences NULL. Safe to call again for the main thread too: this
   * always runs before anything has been added to the list. */
  init_list_head(&loaded_modules);

#ifdef QJS_BIGNUM_EXT
  if(bignum_ext) {
    JS_AddIntrinsicBigFloat(ctx);
    JS_AddIntrinsicBigDecimal(ctx);
    JS_AddIntrinsicOperators(ctx);
    JS_EnableBignumExt(ctx, TRUE);
  }

#endif

  jsm_init(ctx);
  return ctx;
}

/* Prints command-line usage and exits(1). */
static void
jsm_help(void) {
  printf("QuickJS version %s\n"
         "usage: %s [options] [file [args]]\n"
         "-h  --help         list options\n"
         "-e  --eval EXPR    evaluate EXPR\n"
         "-i  --interactive  go to interactive mode\n"
         "-m  --module NAME  load an ES6 module\n"
         "-I  --include file include an additional file\n"
         "    --std          make 'std' and 'os' available to the loaded script\n"
#ifdef QJS_BIGNUM_EXT
         "    --no-bignum    disable the bignum extensions (BigFloat, "
         "BigDecimal)\n"
#if HAVE_QJSCALC
         "    --qjscalc      load the QJSCalc runtime (default if invoked as "
         "qjscalc)\n"
#endif
#endif
         "-T  --trace        trace memory allocation\n"
         "-d  --dump         dump the memory usage stats\n"
         "    --memory-limit n       limit the memory usage to 'n' bytes\n"
         "    --stack-size n         limit the stack size to 'n' bytes\n"
         "-q  --quit         just instantiate the interpreter and quit\n"
#ifdef SIGUSR1
         "\n"
         "  USR1 signal starts interactive mode\n"
#endif
         "-l  --list         list modules\n",
         CONFIG_VERSION,
         exename);
  exit(1);
}

/*
 * qjsm entry point: parses CLI options, sets up the runtime/context, loads
 * -I includes and preload modules, then runs -e/a script file/the REPL.
 */
int
main(int argc, char** argv) {
  struct trace_malloc_data trace_data = {0};
  int optind;
  char *expr = 0, dump_memory = 0, trace_memory = 0, empty_run = 0, module = 1, load_std = 0, list_modules = 0;
  const char* include_list[32];
  size_t memory_limit = 0, include_count = 0, stack_size = 0;
#if HAVE_QJSCALC
  int load_jscalc;
#endif

  init_list_head(&loaded_modules);

  package_json = JS_UNDEFINED;

  exename = strdup(argv[0] + path_basename1(argv[0]));
  exelen = strlen(exename);

  /* load jscalc runtime if invoked as 'qjscalc' */
#if HAVE_QJSCALC
  load_jscalc = str_equal(exename, "qjscalc");
#endif

  /* cannot use getopt because we want to pass the command line to the script */
  optind = 1;

  while(optind < argc && *argv[optind] == '-') {
    char* arg = argv[optind] + 1;
    const char *longopt = "", *optarg;

    /* a single - is not an option, it also stops argument scanning */
    if(!*arg)
      break;

    if(arg[1])
      optarg = &arg[1];
    else
      optarg = argv[++optind];

    if(*arg == '-') {
      longopt = arg + 1;
      arg += strlen(arg);

      /* -- stops argument scanning */
      if(!*longopt)
        break;
    }

    for(; *arg || *longopt; longopt = "") {
      char opt;

      if((opt = *arg))
        arg++;

      if(opt == 'h' || opt == '?' || str_equal(longopt, "help")) {
        jsm_help();
        continue;
      }

      if(opt == 'e' || str_equal(longopt, "eval")) {
        if(*arg) {
          expr = arg;
          break;
        }

        if(optind < argc) {
          expr = argv[optind++];
          break;
        }

        fprintf(stderr, "%s: missing expression for -e\n", exename);
        exit(2);
      }

      if(opt == 'I' || str_equal(longopt, "include")) {
        if(optind >= argc) {
          fprintf(stderr, "expecting filename");
          exit(1);
        }

        if(include_count >= countof(include_list)) {
          fprintf(stderr, "too many included files");
          exit(1);
        }

        include_list[include_count++] = optarg;
        break;
      }

      if(opt == 'i' || str_equal(longopt, "interactive")) {
        interactive = 1;
        break;
      }

      if(opt == 'm' || str_equal(longopt, "module")) {
        const char* modules = optarg;
        size_t len;

        for(size_t i = 0; modules[i]; i += len) {
          len = str_chr(&modules[i], ',');
          vector_pushstringlen(&module_list, &modules[i], len);

          if(modules[i + len] == ',')
            len++;
        }

        break;
      }

      if(opt == 'l' || str_equal(longopt, "list")) {
        list_modules++;
        break;
      }

      if(opt == 'd' || str_equal(longopt, "dump")) {
        dump_memory++;
        break;
      }

      if(opt == 'T' || str_equal(longopt, "trace")) {
        trace_memory++;
        break;
      }

      if(str_equal(longopt, "std")) {
        load_std = 1;
        break;
      }

#ifdef QJS_BIGNUM_EXT
      if(str_equal(longopt, "no-bignum")) {
        bignum_ext = 0;
        break;
      }

      if(str_equal(longopt, "bignum")) {
        bignum_ext = 1;
        break;
      }
#if HAVE_QJSCALC
      if(str_equal(longopt, "qjscalc")) {
        load_jscalc = 1;
        break;
      }
#endif
#endif
      if(opt == 'q' || str_equal(longopt, "quit")) {
        empty_run++;
        break;
      }

      if(str_equal(longopt, "memory-limit")) {
        if(optind >= argc) {
          fprintf(stderr, "expecting memory limit");
          exit(1);
        }

        memory_limit = (size_t)strtod(argv[optind++], 0);
        break;
      }

      if(str_equal(longopt, "stack-size")) {
        if(optind + 1 >= argc) {
          fprintf(stderr, "expecting stack size");
          exit(1);
        }

        stack_size = (size_t)strtod(argv[++optind], 0);
        break;
      }

      if(opt) {
        fprintf(stderr, "%s: unknown option '-%c'\n", exename, opt);
      } else {
        fprintf(stderr, "%s: unknown option '--%s'\n", exename, longopt);
      }

      jsm_help();
    }

    optind++;
  }

  /* set once a script/expr/include fails; with -i this no longer means an immediate exit
     (see below), but the process must still report failure via its exit code */
  BOOL had_error = FALSE;
  int exit_code = 0;

  {
    const char* modules;

    if((modules = getenv("DEBUG"))) {
      size_t len;

      for(size_t i = 0; modules[i]; i += len) {
        len = str_chr(&modules[i], ',');
        vector_putptr(&debug_list, str_ndup(&modules[i], len));

        if(modules[i + len] == ',')
          len++;
      }

      debug_module = vector_counts(&debug_list, "modules");
    }
  }

#if HAVE_QJSCALC
  if(load_jscalc)
    bignum_ext = 1;
#endif

  if(trace_memory) {
    jsm_trace_malloc_init(&trace_data);
    jsm_rt = JS_NewRuntime2(&trace_mf, &trace_data);
  } else {
    jsm_rt = JS_NewRuntime();
  }

  if(!jsm_rt) {
    fprintf(stderr, "%s: cannot allocate JS runtime\n", exename);
    exit(2);
  }

  if(memory_limit != 0)
    JS_SetMemoryLimit(jsm_rt, memory_limit);

  if(stack_size != 0)
    JS_SetMaxStackSize(jsm_rt, stack_size);

  js_std_set_worker_new_context_func(jsm_context_new);

  js_std_init_handlers(jsm_rt);

  /* loader for ES6 modules */
  jsm_set_module_loader(jsm_rt);

  jsm_ctx = jsm_context_new(jsm_rt);
  if(!jsm_ctx) {
    fprintf(stderr, "%s: cannot allocate JS context\n", exename);
    exit(2);
  }

  if(list_modules) {
    BuiltinModule* rec;

    printf("Builtin modules:\n");
    vector_foreach_t(&jsm_builtins, rec) {
      printf("  %s%s\n", rec->module_name, rec->module_func ? "" : ".js");
    }

    return 0;
  }

  vector_init(&jsm_stack, jsm_ctx);

  JS_SetHostPromiseRejectionTracker(jsm_rt, jsm_promise_rejection_tracker, 0);

  JS_SetInterruptHandler(jsm_rt, jsm_interrupt_handler, jsm_ctx);

  JSValue sargs = JS_UNDEFINED;

  if(!empty_run) {
    DynBuf db;
    dbuf_init_ctx(jsm_ctx, &db);

#if HAVE_QJSCALC
    if(load_jscalc) {
      js_eval_binary(jsm_ctx, qjsc_qjscalc, qjsc_qjscalc_size, 0);
    }
#endif

    js_std_add_helpers(jsm_ctx, argc - optind, argv + optind);

    dbuf_putstr(&db, "import process from 'process';\nglobalThis.process = process;\n");

    JS_SetPropertyFunctionList(jsm_ctx, JS_GetGlobalObject(jsm_ctx), jsm_global_funcs, countof(jsm_global_funcs));

    if(jsm_builtin_find("bun"))
      dbuf_putstr(&db, "import { plugin } from 'bun';\nglobalThis.Bun = { plugin };\n");

    if(load_std) {
      dbuf_putstr(&db,
                  "import * as std from 'std';\n"
                  "import * as os from 'os';\n"
                  "\n"
                  "globalThis.std = std;\n"
                  "globalThis.os = os;\n"
                  "globalThis.setTimeout = os.setTimeout;\n"
                  "globalThis.clearTimeout = os.clearTimeout;\n");
    }

    sargs = js_global_get_str(jsm_ctx, "scriptArgs");
    JS_DefinePropertyValueStr(jsm_ctx, sargs, "-1", JS_NewString(jsm_ctx, argv[0]), 0);

    if(db.size) {
      dbuf_0(&db);
      js_eval_str(jsm_ctx, (const char*)db.buf, 0, JS_EVAL_TYPE_MODULE);
    }

    dbuf_free(&db);

    jsm_hooks_alive = TRUE;
    atexit(jsm_atexit);

    {
      char** ptr;

      vector_foreach_t(&module_list, ptr) {
        JSModuleDef* m;

        if(!(m = jsm_module_load(jsm_ctx, *ptr, 0))) {
          jsm_error_print(jsm_ctx);
          return 1;
        }
      }

      vector_freestrings(&module_list);
    }

    /* had_error (declared above, before this block) tracks a script/expr/include failure
       across the "should we still drop into an interactive session" decision below: with -i
       (or INTERACTIVE=1, or a USR1 signal already having flipped `interactive`), a failure no
       longer tears the process down immediately - it's reported (see each site below) and the
       runtime is left alive, with whatever got defined before the failure, so
       jsm_interactive_start() further down can still open a REPL on it. Without -i, behavior
       is unchanged: exit immediately. */
    for(size_t i = 0; i < include_count; i++) {
      if(jsm_stack_load(jsm_ctx, include_list[i], FALSE, FALSE) == -1) {
        had_error = TRUE;
        break;
      }
    }

    if(had_error && !interactive)
      goto fail;

    js_eval_str(jsm_ctx,
                "import { Console } from 'console';\n"
                "import { out } from 'std';\n"
                "globalThis.console = new Console(out, { inspectOptions: { customInspect: true } });\n",
                0,
                JS_EVAL_TYPE_MODULE);

    if(!interactive) {
#ifndef _WIN32
#ifdef SIGUSR1
      signal(SIGUSR1, jsm_signal_handler);
#endif
#endif
    }

    if(!had_error) {
      if(expr) {
        if(js_eval_str(jsm_ctx, expr, "<cmdline>", 0) == -1) {
          JSValue exc = JS_GetException(jsm_ctx);

          if(!jsm_hook_bool(jsm_ctx, "uncaught", 1, &exc)) {
            fprintf(stderr, "Error evaluating expression: ");
            js_error_print(jsm_ctx, exc);
            had_error = TRUE;
          }

          JS_FreeValue(jsm_ctx, exc);
        }
      } else if(optind >= argc) {
        /* interactive mode */
        interactive = 1;
      } else {
        const char* filename = argv[optind];

        JS_DefinePropertyValueStr(jsm_ctx, sargs, "$", JS_NewString(jsm_ctx, filename), 0);

        if(jsm_stack_load(jsm_ctx, filename, module, TRUE) == -1)
          had_error = TRUE;
      }
    }

    if(had_error && !interactive)
      goto fail;

    if(getenv("INTERACTIVE") && interactive != 2)
      interactive = 1;

    if(interactive == 1)
      jsm_interactive_start(jsm_ctx, TRUE);

    /* process.on('beforeExit') may schedule more work: run the loop again while it did.
       An idle js_std_loop() returns in a few µs, so a longer one means work was done. */
    for(;;) {
      struct timespec t0, t1;
      BOOL jobs;

      js_std_loop(jsm_ctx);

      if(!jsm_hook_bool(jsm_ctx, "beforeExit", 0, 0))
        break;

      jobs = JS_IsJobPending(jsm_rt);
      clock_gettime(CLOCK_MONOTONIC, &t0);
      js_std_loop(jsm_ctx);
      clock_gettime(CLOCK_MONOTONIC, &t1);

      if(!jobs && (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec) < 10000)
        break;
    }
  }

  JSValue exception = jsm_error_get(jsm_ctx);

  if(!JS_IsNull(exception) && !JS_IsUninitialized(exception))
    js_error_print(jsm_ctx, exception);

  if(jsm_hooks_alive) {
    exit_code = jsm_hook_exit(jsm_ctx, had_error ? 1 : 0);
    jsm_hooks_alive = FALSE;
  } else {
    exit_code = had_error ? 1 : 0;
  }

  if(dump_memory) {
    JSMemoryUsage stats;

    JS_ComputeMemoryUsage(jsm_rt, &stats);
    JS_DumpMemoryUsage(stdout, &stats, jsm_rt);
  }

  JS_FreeValue(jsm_ctx, sargs);

  js_std_free_handlers(jsm_rt);
  JS_FreeContext(jsm_ctx);
  JS_FreeRuntime(jsm_rt);

  if(empty_run && dump_memory) {
    clock_t t[5];
    double best[5];

    for(int i = 0; i < 100; i++) {
      t[0] = clock();
      jsm_rt = JS_NewRuntime();
      t[1] = clock();
      jsm_ctx = JS_NewContext(jsm_rt);
      t[2] = clock();
      JS_FreeContext(jsm_ctx);
      t[3] = clock();
      JS_FreeRuntime(jsm_rt);
      t[4] = clock();

      for(int j = 4; j > 0; j--) {
        double ms = 1000.0 * (t[j] - t[j - 1]) / CLOCKS_PER_SEC;

        if(i == 0 || best[j] > ms)
          best[j] = ms;
      }
    }

    printf("\nInstantiation times (ms): %.3f = %.3f+%.3f+%.3f+%.3f\n", best[1] + best[2] + best[3] + best[4], best[1], best[2], best[3], best[4]);
  }

  return exit_code;

fail:
  exit_code = 1;

  if(jsm_hooks_alive) {
    exit_code = jsm_hook_exit(jsm_ctx, 1);
    jsm_hooks_alive = FALSE;
  }

  js_std_free_handlers(jsm_rt);
  JS_FreeContext(jsm_ctx);
  JS_FreeRuntime(jsm_rt);
  return exit_code;
}
