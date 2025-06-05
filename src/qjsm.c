#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
/*#include <sys/poll.h>*/
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
#endif

#ifndef CONFIG_BIGNUM
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

#include "quickjs-internal.h"

JSModuleDef* js_module_loader_path(JSContext* ctx, const char* module_name, void* opaque);

static JSValue jsm_start_interactive4(JSContext*, JSValueConst, int, JSValueConst[]);

typedef JSModuleDef* ModuleInitFunction(JSContext*, const char*);
typedef char* ModuleLoader(JSContext*, const char*);

typedef struct ModuleLoaderContext {
  JSValue func;
  struct ModuleLoaderContext* next;
} ModuleLoaderContext;

static thread_local int debug_module_loader = 0;
static thread_local Vector module_debug = VECTOR_INIT();
static thread_local Vector module_list = VECTOR_INIT();
static thread_local ModuleLoaderContext* module_loaders = NULL;

#ifndef QUICKJS_MODULE_PATH
#ifdef QUICKJS_PREFIX
#define QUICKJS_MODULE_PATH QUICKJS_PREFIX "/lib/quickjs"
#endif
#endif

static const char jsm_default_module_path[] = QUICKJS_MODULE_PATH;

// static JSModuleLoaderFunc* module_loader = 0;
static JSValue package_json;
static char* exename;
static size_t exelen;
static JSRuntime* jsm_rt;
static JSContext* jsm_ctx;
static int interactive = 0;

static const char* const module_extensions[] = {
    CONFIG_SHEXT, ".js", "/index.js",
    //    "/package.json",
};

static inline BOOL
is_searchable(const char* path) {
  return !path_isexplicit(path);
}

static inline BOOL
has_dot_or_slash(const char* s) {
  return !!s[str_chrs(s, "." PATHSEP_S, 2)];
}

static char*
is_module(JSContext* ctx, const char* module_name) {
  BOOL yes = path_isfile1(module_name);

  if(debug_module_loader > 2)
    printf("%-20s (module_name=\"%s\")=%s\n", __FUNCTION__, module_name, ((yes) ? "TRUE" : "FALSE"));

  return yes ? js_strdup(ctx, module_name) : 0;
}

static int
module_has_suffix(const char* module_name) {
  size_t n = countof(module_extensions);

  for(size_t i = 0; i < n; i++)
    if(has_suffix(module_name, module_extensions[i]))
      return strlen(module_name) - strlen(module_extensions[i]);

  return 0;
}

#ifdef HAVE_GET_MODULE_LOADER_FUNC
JSModuleLoaderFunc* js_std_get_module_loader_func();
void js_std_set_module_loader_func(JSModuleLoaderFunc* func);
#endif

#if !DONT_HAVE_MALLOC_USABLE_SIZE && !defined(ANDROID)
#if HAVE_MALLOC_USABLE_SIZE
#ifndef HAVE_MALLOC_USABLE_SIZE_DEFINITION
extern size_t malloc_usable_size(void*);
#endif
#endif
#endif

#define trim_dotslash(str) (!strncmp((str), "./", 2) ? (str) + 2 : (str))

typedef struct {
  const char* module_name;
  JSModuleDef* (*module_func)(JSContext*, const char*);
  const uint8_t* byte_code;
  uint32_t byte_code_len;
  JSModuleDef* def;
  BOOL initialized;
} BuiltinModule;

#define jsm_module_extern_compiled(name) \
  extern const uint8_t qjsc_##name[]; \
  extern const uint32_t qjsc_##name##_size;

#define jsm_module_extern_native(name) extern JSModuleDef* js_init_module_##name(JSContext*, const char*)

#define jsm_module_record_compiled(name) \
  (BuiltinModule) { #name, 0, qjsc_##name, qjsc_##name##_size, 0, FALSE }

#define jsm_module_record_native(name) \
  (BuiltinModule) { #name, js_init_module_##name, 0, 0, 0, FALSE }

jsm_module_extern_native(std);
jsm_module_extern_native(os);
jsm_module_extern_native(child_process);
jsm_module_extern_native(deep);
jsm_module_extern_native(inspect);
jsm_module_extern_native(lexer);
jsm_module_extern_native(misc);
// jsm_module_extern_native(mmap);
jsm_module_extern_native(path);
jsm_module_extern_native(pointer);
jsm_module_extern_native(predicate);
jsm_module_extern_native(repeater);
jsm_module_extern_native(tree_walker);
jsm_module_extern_native(xml);

jsm_module_extern_compiled(console);
jsm_module_extern_compiled(events);
jsm_module_extern_compiled(fs);
jsm_module_extern_compiled(io);
jsm_module_extern_compiled(perf_hooks);
jsm_module_extern_compiled(process);
jsm_module_extern_compiled(repl);
jsm_module_extern_compiled(require);
jsm_module_extern_compiled(tty);
jsm_module_extern_compiled(util);

static thread_local Vector jsm_stack = VECTOR_INIT();
static thread_local Vector jsm_builtin_modules = VECTOR_INIT();
static thread_local BOOL jsm_modules_initialized;

#ifdef CONFIG_BIGNUM
#ifdef HAVE_QJSCALC
jsm_module_extern_compiled(qjscalc);
#endif
static int bignum_ext = 1;
#endif

void js_std_set_worker_new_context_func(JSContext* (*func)(JSRuntime* rt));

static void
jsm_dump_error(JSContext* ctx) {
  js_error_print(ctx, JS_GetException(ctx));
}

enum {
  SCRIPT_LIST,
  SCRIPT_FILE,
  SCRIPT_FILENAME,
  SCRIPT_DIRNAME,
};

char**
jsm_stack_ptr(int i) {
  int size;

  if((size = vector_size(&jsm_stack, sizeof(char*))) > 0) {
    if(i < 0)
      i += size;

    return vector_at(&jsm_stack, sizeof(char*), i);
  }

  return 0;
}

char**
jsm_stack_find(const char* module) {
  char** ptr;

  if(jsm_stack.size == 0)
    return 0;

  vector_foreach_t(&jsm_stack, ptr) if(!path_compare2(*ptr, module)) return ptr;
  return 0;
}

char*
jsm_stack_at(int i) {
  char** ptr;

  if((ptr = jsm_stack_ptr(i)))
    return *ptr;

  return 0;
}

char*
jsm_stack_top(void) {
  return jsm_stack_at(-1);
}

size_t
jsm_stack_count(void) {
  return vector_size(&jsm_stack, sizeof(char*));
}

char*
jsm_stack_string(void) {
  int i = jsm_stack_count();
  DynBuf buf;
  dbuf_init2(&buf, 0, vector_realloc);

  while(--i >= 0)
    dbuf_printf(&buf, "%i: %s\n", i, jsm_stack_at(i));

  dbuf_0(&buf);
  return (char*)buf.buf;
}

JSValue
jsm_stack_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case SCRIPT_LIST: {
      char** ptr;
      size_t i = 0;
      ret = JS_NewArray(ctx);

      vector_foreach_t(&jsm_stack, ptr) {
        JSValue str = JS_NewString(ctx, *ptr);
        JS_SetPropertyUint32(ctx, ret, i++, str);
      }

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

static void
jsm_stack_push(JSContext* ctx, const char* file) {
  vector_putptr(&jsm_stack, js_strdup(ctx, file));
}

static void
jsm_stack_pop(JSContext* ctx) {
  char** ptr = vector_back(&jsm_stack, sizeof(char*));

  js_free(ctx, *ptr);
  vector_pop(&jsm_stack, sizeof(char*));
}

static int
jsm_stack_load(JSContext* ctx, const char* file, BOOL module, BOOL is_main) {
  JSValue val;
  int32_t ret;
  JSValue global_obj = JS_GetGlobalObject(ctx);

  JS_SetPropertyStr(ctx, global_obj, "module", JS_NewObject(ctx));
  jsm_stack_push(ctx, file);

  errno = 0;
  val = js_eval_file(ctx, file, module ? JS_EVAL_TYPE_MODULE : 0);

  if(vector_size(&jsm_stack, sizeof(char*)) > 1)
    jsm_stack_pop(ctx);

#if defined(HAVE_JS_PROMISE_STATE) && defined(HAVE_JS_PROMISE_RESULT)
  if(js_is_promise(ctx, val)) {
    JSPromiseStateEnum state = JS_PromiseState(ctx, val);
    JSValue result = JS_PromiseResult(ctx, val);

    if(state == JS_PROMISE_REJECTED) {
      JS_FreeValue(ctx, val);
      val = JS_Throw(ctx, result);
    } else if(state == JS_PROMISE_FULFILLED) {
      JS_FreeValue(ctx, val);
      val = JS_DupValue(ctx, result);
    }

    JS_FreeValue(ctx, result);
  }
#endif

  if(JS_IsException(val)) {
    JSValue exception = JS_GetException(ctx);

    fprintf(stderr, "Error evaluating '%s': ", file);
    js_error_print(ctx, exception);

    JS_FreeValue(ctx, exception);
    return -1;
  }

  if(JS_IsModule(val) || module) {
    JSModuleDef* m;

    if(!JS_IsModule(val)) {
      m = js_module_at(ctx, -1);
      val = module_value(ctx, m);
    } else {
      m = JS_VALUE_GET_PTR(val);
    }

    module_exports_get(ctx, m, TRUE, global_obj);
  } else {
    JS_ToInt32(ctx, &ret, val);
  }

  if(!JS_IsModule(val))
    JS_FreeValue(ctx, val);

  JS_FreeValue(ctx, global_obj);
  return 0;
}

JSModuleDef* js_init_module_deep(JSContext*, const char*);
JSModuleDef* js_init_module_inspect(JSContext*, const char*);
JSModuleDef* js_init_module_lexer(JSContext*, const char*);
JSModuleDef* js_init_module_misc(JSContext*, const char*);
JSModuleDef* js_init_module_path(JSContext*, const char*);
JSModuleDef* js_init_module_pointer(JSContext*, const char*);
JSModuleDef* js_init_module_predicate(JSContext*, const char*);
JSModuleDef* js_init_module_repeater(JSContext*, const char*);
JSModuleDef* js_init_module_tree_walker(JSContext*, const char*);
JSModuleDef* js_init_module_xml(JSContext*, const char*);

void
jsm_init_modules(JSContext* ctx) {
  if(jsm_modules_initialized)
    return;

  jsm_modules_initialized = TRUE;

  dbuf_init2(&jsm_builtin_modules, 0, &vector_realloc);

#define jsm_builtin_native(name) vector_push(&jsm_builtin_modules, jsm_module_record_native(name));

  jsm_builtin_native(std);
  jsm_builtin_native(os);
  jsm_builtin_native(child_process);
  jsm_builtin_native(deep);
  jsm_builtin_native(inspect);
  jsm_builtin_native(lexer);
  jsm_builtin_native(misc);
  // jsm_builtin_native(mmap);
  jsm_builtin_native(path);
  jsm_builtin_native(pointer);
  jsm_builtin_native(predicate);
  jsm_builtin_native(repeater);
  jsm_builtin_native(tree_walker);
  jsm_builtin_native(xml);

#define jsm_builtin_compiled(name) vector_push(&jsm_builtin_modules, jsm_module_record_compiled(name));

  jsm_builtin_compiled(console);
  jsm_builtin_compiled(events);
  jsm_builtin_compiled(fs);
  jsm_builtin_compiled(io);
  jsm_builtin_compiled(perf_hooks);
  jsm_builtin_compiled(process);
  jsm_builtin_compiled(repl);
  jsm_builtin_compiled(require);
  jsm_builtin_compiled(tty);
  jsm_builtin_compiled(util);
}

static BuiltinModule*
jsm_builtin_find(const char* name) {
  BuiltinModule* rec;

  vector_foreach_t(&jsm_builtin_modules, rec) if(!strcmp(rec->module_name, name)) return rec;

  return 0;
}

static JSModuleDef*
jsm_builtin_init(JSContext* ctx, BuiltinModule* rec) {
  JSModuleDef* m;
  JSValue obj = JS_UNDEFINED;

  jsm_stack_push(ctx, rec->module_name);

  if(rec->def == 0) {
    if(debug_module_loader >= 2)
      printf("(3) %-30s internal\n", rec->module_name);

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
      m = js_value_ptr(obj);

      JS_ResolveModule(ctx, obj);
      JSValue ret = JS_EvalFunction(ctx, obj);
      JS_FreeValue(ctx, ret);

#ifdef DANGEROUS_QJS_INTERNAL
      /* rename module */
      JS_FreeAtom(ctx, m->module_name);
      m->module_name = JS_NewAtom(ctx, rec->module_name);
#endif
    }

    rec->def = m;
  }

  jsm_stack_pop(ctx);

  return rec->def;
}

static JSValue
jsm_load_json(JSContext* ctx, const char* file) {
  uint8_t* buf;
  size_t len;

  if(!(buf = js_load_file(ctx, &len, file)))
    return JS_ThrowInternalError(ctx, "Loading '%s' failed", file);

  return JS_ParseJSON(ctx, (const char*)buf, len, file);
}

static JSValue
jsm_load_package(JSContext* ctx, const char* file) {
  if(JS_IsUndefined(package_json) || JS_VALUE_GET_TAG(package_json) == 0) {
    package_json = jsm_load_json(ctx, file ? file : "package.json");

    if(JS_IsException(package_json)) {
      JS_GetException(ctx);
      package_json = JS_NULL;
    }
  }

  return package_json;
}

static char*
jsm_search_list(JSContext* ctx, const char* module_name, const char* list) {
  const char* s;
  char* t = 0;
  size_t i;

  if(debug_module_loader >= 2)
    printf("%-20s (module_name=\"%s\" list =\"%s\")\n", __FUNCTION__, module_name, list);

  if(!(t = js_malloc(ctx, strlen(list) + 1 + strlen(module_name) + 1)))
    return 0;

  for(s = list; *s; s += i) {
    if((i = str_chrs(s, ";\n", 2)) == 0)
      break;

    strncpy(t, s, i);
    t[i] = '/';
    strcpy(&t[i + 1], module_name);

    if(path_isfile1(t))
      return t;

    if(s[i])
      ++i;
  }

  return 0;
}

static char*
jsm_search_path(JSContext* ctx, const char* module_name) {
  const char* list;

  if(debug_module_loader >= 2)
    printf("%-20s (module_name=\"%s\")\n", __FUNCTION__, module_name);

  assert(is_searchable(module_name));

  if(!(list = getenv("QUICKJS_MODULE_PATH")))
    list = jsm_default_module_path;

  return jsm_search_list(ctx, module_name, list);
}

static char*
jsm_search_suffix(JSContext* ctx, const char* module_name, ModuleLoader* fn) {
  size_t n, len = strlen(module_name);
  char *s, *t = 0;

  if(debug_module_loader > 3)
    printf("%-20s (module_name=\"%s\", fn=%s)\n",
           __FUNCTION__,
           module_name,
           fn == &is_module         ? "is_module"
           : fn == &jsm_search_path ? "jsm_search_path"
                                    : "<unknown>");

  if(!(s = js_mallocz(ctx, (len + 31) & (~0xf))))
    return 0;

  strcpy(s, module_name);
  n = countof(module_extensions);

  for(size_t i = 0; i < n; i++) {
    s[len] = '\0';

    if(has_suffix(s, module_extensions[i]))
      continue;

    strcat(s, module_extensions[i]);

    if((t = fn(ctx, s)))
      break;
  }

  js_free(ctx, s);
  return t;
}

static char*
jsm_search_module(JSContext* ctx, const char* module_name) {
  BOOL search = is_searchable(module_name);
  BOOL suffix = module_has_suffix(module_name);
  ModuleLoader* fn = search ? &jsm_search_path : &is_module;
  char* s = suffix ? fn(ctx, module_name) : jsm_search_suffix(ctx, module_name, fn);

  if(debug_module_loader >= 2)
    printf("%-20s (module_name=\"%s\") search=%s suffix=%s fn=%s result=%s\n",
           __FUNCTION__,
           module_name,
           ((search) ? "TRUE" : "FALSE"),
           ((suffix) ? "TRUE" : "FALSE"),
           search ? "search_module" : "is_module",
           s);

  return s;
}

/* end of "new breed" module loader functions */

BOOL
jsm_module_is_builtin(JSModuleDef* m) {
  BuiltinModule* rec;

  vector_foreach_t(&jsm_builtin_modules, rec) if(rec->def == m) return TRUE;

  return FALSE;
}

char*
jsm_module_package(JSContext* ctx, const char* module) {
  JSValue package;
  char *rel, *file = 0;

  rel = path_isabsolute1(module) ? path_relative1(module) : strdup(module);

  if(!has_suffix(module, CONFIG_SHEXT)) {
    package = jsm_load_package(ctx, "package.json");

    if(JS_IsObject(package)) {
      JSValue target = JS_UNDEFINED, aliases = JS_GetPropertyStr(ctx, package, "_moduleAliases");

      if(!JS_IsException(aliases) && JS_IsObject(aliases)) {
        target = JS_GetPropertyStr(ctx, aliases, path_trimdotslash1(rel));

        if(!JS_IsUndefined(target)) {
          file = js_tostring(ctx, target);

          if(debug_module_loader >= 1)
            printf("%-20s (2) %-30s => %s (package.json)\n", __FUNCTION__, module, file);
        }
      }

      JS_FreeValue(ctx, aliases);
      JS_FreeValue(ctx, target);
    }
  }

  free(rel);
  return file;
}

void
jsm_module_script(DynBuf* buf, const char* path, const char* name, BOOL star) {
  enum { NAMED = 0, ALL, EXEC } mode = NAMED;

  for(; *path; ++path) {
    switch(*path) {
      case '!':
        if(!star)
          mode = EXEC;

        continue;
      case '*':
        if(!name)
          mode = ALL;

        continue;
    }

    break;
  }

  buf->size = 0;

  dbuf_putstr(buf, "import ");

  if(star)
    dbuf_putstr(buf, "* as ");

  dbuf_putstr(buf, "tmp from '");
  dbuf_putstr(buf, path);
  dbuf_putstr(buf, "';\n");

  switch(mode) {
    case EXEC: {
      dbuf_putstr(buf, "tmp();\n");
      break;
    }

    case ALL: {
      dbuf_putstr(buf, "Object.assign(globalThis, tmp);\n");
      break;
    }

    default: {
      size_t len = 0;
      char* tmp;

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

      dbuf_putstr(buf, "'] = tmp;\n");
      break;
    }
  }

  dbuf_0(buf);
}

static JSModuleDef*
jsm_module_find(JSContext* ctx, const char* name, int start_pos) {
  JSModuleDef* m;

  while(*name == '!' || *name == '*')
    ++name;

  if((m = js_module_find_from(ctx, name, start_pos)))
    return m;

  return 0;
}

static JSModuleDef*
jsm_module_load(JSContext* ctx, const char* path, const char* name) {
  JSModuleDef* last_module = module_last(ctx);
  DynBuf dbuf;

  dbuf_init2(&dbuf, 0, 0);

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

  if(module_next(ctx, last_module) == NULL)
    return 0;

  assert(module_next(ctx, last_module));

  JSModuleDef* m = module_next(ctx, module_next(ctx, last_module));

  if(!m)
    m = jsm_module_find(ctx, path, 0);

  return m;
}

JSModuleDef*
jsm_module_json(JSContext* ctx, const char* name) {
  DynBuf db;
  JSValue ret;
  JSModuleDef* m = 0;
  uint8_t* ptr;
  size_t len, i;

  if(!(ptr = js_load_file(ctx, &len, name)))
    return 0;

  js_dbuf_init(ctx, &db);
  dbuf_putstr(&db, "export default ");

  i = scan_whitenskip((const void*)ptr, len);

  dbuf_put(&db, ptr + i, len - i);
  js_free(ctx, ptr);
  dbuf_0(&db);

  ret = JS_Eval(ctx, (const char*)db.buf, db.size, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if(JS_VALUE_GET_TAG(ret) == JS_TAG_MODULE)
    m = JS_VALUE_GET_PTR(ret);
  JS_FreeValue(ctx, ret);

  dbuf_free(&db);
  return m;
}

char*
jsm_module_locate(JSContext* ctx, const char* module_name, void* opaque) {
  char *file = 0, *s = js_strdup(ctx, module_name);

  for(;;) {
    if(debug_module_loader - !strcmp(module_name, s) >= 3)
      printf("%-20s [1](module_name=\"%s\", opaque=%p) s=%s\n", __FUNCTION__, module_name, opaque, s);

    if(has_dot_or_slash(s))
      if(path_isfile1(s))
        break;

    if(is_searchable(s)) {
      if((file = jsm_search_module(ctx, s))) {
        js_free(ctx, s);
        s = js_strdup(ctx, file);
        break;
      }

      /*if(path_component1(s) == 3 && !strncmp(s, "lib", 3)) {
        strcpy(s, &s[3 + path_separator1(&s[3])]);
        continue;
      }*/

    } else {
      if((file = jsm_search_suffix(ctx, s, is_module))) {
        js_free(ctx, s);
        s = js_strdup(ctx, file);
        break;
      }
    }

    break;
  }

  return s;
}

JSValue
jsm_call_loader_method(JSContext* ctx, ModuleLoaderContext* loader, const char* method_name, int argc, JSValue argv[]) {
  argv[argc - 1] = JS_DupValue(ctx, argv[argc - 1]);

  for(; loader; loader = loader->next) {
    JSValue fn = JS_GetPropertyStr(ctx, loader->func, method_name);

    if(!strcmp(method_name, "loader") && !JS_IsFunction(ctx, fn))
      fn = JS_DupValue(ctx, loader->func);

    if(!JS_IsFunction(ctx, fn)) {
      JS_FreeValue(ctx, fn);
      continue;
    }

    JSValue ret = JS_Call(ctx, fn, JS_UNDEFINED, argc, argv);

    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, argv[argc - 1]);
    argv[argc - 1] = ret;

    if(JS_IsException(ret)) {
      fputs("Exception in module loader: ", stderr);
      jsm_dump_error(ctx);
      exit(1);
    }
  }

  return argv[argc - 1];
}

JSValue
jsm_call_loaders(JSContext* ctx, ModuleLoaderContext* loader, JSValueConst arg) {
  JSValue module = arg;
  jsm_call_loader_method(ctx, loader, "loader", 1, &module);
  return module;
}

JSValue
jsm_call_normalizers(JSContext* ctx, ModuleLoaderContext* loader, JSValueConst base, JSValueConst module) {
  JSValue argv[] = {base, module};
  jsm_call_loader_method(ctx, loader, "normalize", countof(argv), argv);
  return argv[1];
}

JSModuleDef*
jsm_module_loader(JSContext* ctx, const char* module_name, void* opaque) {
  char *s = 0, *name = js_strdup(ctx, module_name);
  JSModuleDef* m = 0;
  ModuleLoaderContext** lptr = opaque;

again:
  if(str_start(name, "file://"))
    name += 7;

  if(str_start(name, "data:")) {
    size_t length = strlen(name);
    size_t offset = byte_chr(name, length, ',');

    if(name[offset]) {
      BOOL is_js = byte_finds(name, offset, "/javascript") < offset || byte_finds(name, offset, "/ecmascript") < offset;
      BOOL is_json = !is_js && byte_finds(name, offset, "/json") < offset;
      DynBuf code = DBUF_INIT_CTX(ctx);
      JSValue module;
      size_t encoding_offset = byte_rchr(name, offset, ';');
      const char* encoding = encoding_offset > offset ? NULL : &name[encoding_offset + 1];
      BOOL is_base64 = encoding && !strncasecmp(encoding, "base64", 4);

      ++offset;
      length -= offset;

      if(is_json) {
        if(is_base64)
          dbuf_putstr(&code, "import { atos } from 'util';\n");

        dbuf_putstr(&code, "export default JSON.parse(");
        if(is_base64)
          dbuf_putstr(&code, "atos(");

        dbuf_putc(&code, '\'');
        dbuf_put_escaped_table(&code, &name[offset], length, escape_singlequote_tab);
        dbuf_putc(&code, '\'');

        if(is_base64)
          dbuf_putc(&code, ')');

        dbuf_putstr(&code, ");");
        dbuf_putc(&code, '\n');
      } else if(is_base64) {

        if(dbuf_realloc(&code, code.size + b64url_get_decoded_buffer_size(length)))
          return 0;

        code.size += b64url_decode((const uint8_t*)&name[offset], length, &code.buf[code.size]);
      } else {
        dbuf_put_unescaped_table(&code, &name[offset], length, escape_url_tab);
      }

      dbuf_0(&code);
      module =
          JS_Eval(ctx, (const char*)code.buf, code.size, module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
      dbuf_free(&code);

      if(!JS_IsException(module)) {
        js_module_set_import_meta(ctx, module, FALSE, FALSE);

        m = JS_VALUE_GET_PTR(module);

        /*JSValue meta_obj = JS_GetImportMeta(ctx, m);
        if(!JS_IsException(meta_obj)) {
          JS_DefinePropertyValueStr(ctx, meta_obj, "url", JS_NewString(ctx, name), JS_PROP_C_W_E);
          JS_FreeValue(ctx, meta_obj);
        }*/

        module_rename(ctx, m, JS_NewAtom(ctx, "<data-url>"));
      }

      JS_FreeValue(ctx, module);

      goto end;
    }
  }

  if(!name[path_skip1(name)]) {
    BuiltinModule* rec;

    if((rec = jsm_builtin_find(name))) {
      if(s)
        js_free(ctx, s);

      js_free(ctx, name);

      return jsm_builtin_init(ctx, rec);
    }
  }

  if(lptr) {
    JSValue namev = JS_NewString(ctx, name);
    JSValue module = jsm_call_loaders(ctx, *lptr, namev);
    JS_FreeValue(ctx, namev);

    if(JS_IsString(module)) {
      js_free(ctx, name);
      name = js_tostring(ctx, module);
      lptr = opaque = 0;
      JS_FreeValue(ctx, module);
      goto again;
    } else if(JS_VALUE_GET_TAG(module) == JS_TAG_MODULE) {
      m = JS_VALUE_GET_PTR(module);
      goto end;
    }

    JS_FreeValue(ctx, module);
  }

restart:
  if(jsm_stack_find(name) != 0) {
    printf("\x1b[1;31mWARNING: circular module dependency '%s' from:\n%s\x1b[0m\n", name, jsm_stack_string());
    // exit(1);
  }

  if(s == 0) {
    if((s = jsm_module_package(ctx, name))) {
      if(is_searchable(s)) {
        BuiltinModule* rec;

        if((rec = jsm_builtin_find(s))) {
          free(s);
          js_free(ctx, name);
          return jsm_builtin_init(ctx, rec);
        }
      }
    }

    if(!s)
      s = js_strdup(ctx, name);

    if(s && is_searchable(s)) {
      char* tmp;

      if((tmp = jsm_module_locate(ctx, s, opaque))) {
        js_free(ctx, s);
        s = tmp;
      }
    }

    if(!s)
      s = jsm_module_locate(ctx, name, opaque);

    if(s && strcmp(s, name)) {
      js_free(ctx, name);
      name = s;
      s = NULL;
      goto restart;
    }
  }

  if(s) {
    if(debug_module_loader >= 1)
      printf("%-20s \"%s\" -> \"%s\"\n", __FUNCTION__, name, s);

    JS_GetException(ctx);

    jsm_stack_push(ctx, s);

    if(str_ends(s, ".json"))
      m = jsm_module_json(ctx, s);
    else
      m = js_module_loader(ctx, s, opaque);

    jsm_stack_pop(ctx);

    if(!m) {
      JSValue exception = JS_GetException(ctx);

      if(!js_is_null_or_undefined(exception)) {
        char* top = jsm_stack_top();
        // char* err=js_error_tostring(ctx, exception);

        JS_ThrowInternalError(
            ctx, "%s: %s%scould not load module filename '%s'", __func__, top ? top : "", top ? ": " : "", s /*, err*/);
        // js_free(ctx, err);
      }

      JS_FreeValue(ctx, exception);
    }
    js_free(ctx, s);

  } else {
    if(debug_module_loader)
      printf("%-20s \"%s\" -> null\n", __FUNCTION__, name);
  }

end:
  js_free(ctx, name);
  return m;
}

char*
jsm_module_normalize(JSContext* ctx, const char* path, const char* name, void* opaque) {
  char* file = 0;
  BuiltinModule* bltin = 0;
  ModuleLoaderContext** lptr = opaque;

  if(!has_dot_or_slash(name) && (bltin = jsm_builtin_find(name))) {
    if(bltin->def) {
      const char* str = module_namecstr(ctx, bltin->def);

      file = js_strdup(ctx, str);
      JS_FreeCString(ctx, str);
    }
  } else {
    if(path[0] != '<' && (path_isdotslash(name) || path_isdotdot(name)) && has_dot_or_slash(name)) {
      DynBuf dir;
      size_t dsl;

      js_dbuf_allocator(ctx, &dir);

      if(!(dsl = path_dirlen1(path))[path])
        dbuf_putstr(&dir, ".");
      else
        path_append3(path, dsl, &dir);

      path_append2(name, &dir);
      dsl = path_skipdotslash2((const char*)dir.buf, dir.size);

      /* XXX BUG: should use path_normalize* to resolve symlinks */
      dir.size = dsl + path_normalize2((char*)dir.buf + dsl, dir.size - dsl);
      dbuf_0(&dir);

      file = (char*)dir.buf;
    } else if(has_suffix(name, CONFIG_SHEXT) && !path_isabsolute1(name)) {
      DynBuf db;

      js_dbuf_init(ctx, &db);

      path_append2(QUICKJS_C_MODULE_DIR, &db);
      path_append2(name, &db);
      dbuf_0(&db);

      file = (char*)db.buf;
    } else if(has_dot_or_slash(name) && path_exists1(name) && path_isrelative(name)) {
      file = path_absolute1(name);
      path_normalize1(file);
    }
  }

  if(lptr) {
    JSValue pathv = JS_NewString(ctx, path), namev = JS_NewString(ctx, file ? file : name);
    JSValue module = jsm_call_normalizers(ctx, *lptr, pathv, namev);

    JS_FreeValue(ctx, pathv);
    JS_FreeValue(ctx, namev);

    if(JS_IsString(module)) {
      if(file)
        js_free(ctx, file);

      file = js_tostring(ctx, module);
    }

    JS_FreeValue(ctx, module);
  }

  if(file == 0)
    if(!bltin && has_dot_or_slash(name) && !module_has_suffix(name)) {
      char* tmp;

      if((tmp = jsm_search_suffix(ctx, file ? file : name, &is_module))) {
        // if(file)
        js_free(ctx, file);

        file = tmp;
      }
    }

  if(file == 0)
    file = js_strdup(ctx, name);

  if(debug_module_loader >= 1)
    printf("%-20s %s: \"%s\" => \"%s\"\n", __FUNCTION__, path, name, file);

  return file;
}

/* also used to initialize the worker context */
static JSContext*
jsm_context_new(JSRuntime* rt) {
  JSContext* ctx;

  if(!(ctx = JS_NewContext(rt)))
    return 0;

#ifdef CONFIG_BIGNUM
  if(bignum_ext) {
    JS_AddIntrinsicBigFloat(ctx);
    JS_AddIntrinsicBigDecimal(ctx);
    JS_AddIntrinsicOperators(ctx);
    JS_EnableBignumExt(ctx, TRUE);
  }

#endif

  jsm_init_modules(ctx);

#define jsm_module_native(name) js_init_module_##name(ctx, #name);

  /* jsm_module_native(std);
   jsm_module_native(os);
 #ifndef __wasi__
   //jsm_module_native(child_process);
 #endif
   jsm_module_native(deep);
   jsm_module_native(inspect);
   jsm_module_native(lexer);
   jsm_module_native(misc);
 #ifndef __wasi__
   //jsm_module_native(mmap);
 #endif
   jsm_module_native(path);
   jsm_module_native(pointer);
   jsm_module_native(predicate);
   jsm_module_native(repeater);
   jsm_module_native(tree_walker);
   jsm_module_native(xml);*/

  // printf("Set module loader (rt=%p): %p\n", rt);

  return ctx;
}

JSValue
jsm_modules_array(JSContext* ctx, JSValueConst this_val, int magic) {
  JSModuleDef *m, **list;
  JSValue ret = JS_NewArray(ctx);

  if(!(list = js_modules_vector(ctx)))
    return JS_EXCEPTION;

  for(uint32_t i = 0; (m = list[i]); i++) {
    JSValue obj = JS_NewObject(ctx);

    if(jsm_module_is_builtin(m)) {
      const char* name = module_namecstr(ctx, m);
      size_t namestart = path_basename2(name, strlen(name));
      size_t namelen = str_find(&name[namestart], ".js");

      JS_DefinePropertyValueStr(ctx, obj, "name", JS_NewStringLen(ctx, &name[namestart], namelen), JS_PROP_ENUMERABLE);
      JS_SetPropertyStr(ctx, obj, "builtin", JS_TRUE);

      JS_FreeCString(ctx, name);
    }

    module_make_object(ctx, m, obj);

    JS_SetPropertyUint32(ctx, ret, i, obj);
  }

  return ret;
}

#if defined(__APPLE__)
#define MALLOC_OVERHEAD 0
#else
#define MALLOC_OVERHEAD 8
#endif

struct trace_malloc_data {
  uint8_t* base;
};

static inline unsigned long long
jsm_trace_malloc_ptr_offset(uint8_t* ptr, struct trace_malloc_data* dp) {
  return ptr - dp->base;
}

/* default memory allocation functions with memory limitation */
static inline size_t
jsm_trace_malloc_usable_size(void* ptr) {
#if defined(__APPLE__)
  return malloc_size(ptr);
#elif defined(_WIN32)
  return _msize(ptr);
#elif defined(EMSCRIPTEN) || defined(__dietlibc__) || defined(__MSYS__) || defined(ANDROID) || \
    defined(DONT_HAVE_MALLOC_USABLE_SIZE)
  return 0;
#elif defined(__linux__) || defined(HAVE_MALLOC_USABLE_SIZE)
  return malloc_usable_size(ptr);
#else
#warning change this to `return 0;` if compilation fails
  /* change this to `return 0;` if compilation fails */
  return malloc_usable_size(ptr);
#endif
}

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

static void
jsm_trace_malloc_init(struct trace_malloc_data* s) {
  free(s->base = malloc(8));
}

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

static void
jsm_trace_free(JSMallocState* s, void* ptr) {
  if(!ptr)
    return;

  jsm_trace_malloc_printf(s, "F %p\n", ptr);
  s->malloc_count--;
  s->malloc_size -= jsm_trace_malloc_usable_size(ptr) + MALLOC_OVERHEAD;
  free(ptr);
}

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

static const JSMallocFunctions trace_mf = {
    jsm_trace_malloc,
    jsm_trace_free,
    jsm_trace_realloc,
#if defined(__APPLE__)
    malloc_size,
#elif defined(_WIN32)
    (size_t(*)(const void*))_msize,
#elif defined(EMSCRIPTEN) || defined(__dietlibc__) || defined(__MSYS__) || defined(ANDROID) || \
    defined(DONT_HAVE_MALLOC_USABLE_SIZE_DEFINITION)
    0,
#elif defined(__linux__) || defined(HAVE_MALLOC_USABLE_SIZE)
    (size_t(*)(const void*))malloc_usable_size,
#else
#warning change this to `0,` if compilation fails
    /* change this to `0,` if compilation fails */
    malloc_usable_size,
#endif
};

void
jsm_help(void) {
  printf("QuickJS version " CONFIG_VERSION "\n"
         "usage: %s [options] [file [args]]\n"
         "-h  --help         list options\n"
         "-e  --eval EXPR    evaluate EXPR\n"
         "-i  --interactive  go to interactive mode\n"
         "-m  --module NAME  load an ES6 module\n"
         "-I  --include file include an additional file\n"
         "    --std          make 'std' and 'os' available to the loaded script\n"
#ifdef CONFIG_BIGNUM
         "    --no-bignum    disable the bignum extensions (BigFloat, "
         "BigDecimal)\n"
#ifdef HAVE_QJSCALC
         "    --qjscalc      load the QJSCalc runtime (default if invoked as "
         "qjscalc)\n"
#endif
#endif
         "-T  --trace        trace memory allocation\n"
         "-d  --dump         dump the memory usage stats\n"
         "    --memory-limit n       limit the memory usage to 'n' bytes\n"
         "    --stack-size n         limit the stack size to 'n' bytes\n"
         "    --unhandled-rejection  dump unhandled promise rejections\n"
         "-q  --quit         just instantiate the interpreter and quit\n"
#ifdef SIGUSR1
         "\n"
         "  USR1 signal starts interactive mode\n"
#endif
         ,
         exename);
  exit(1);
}

enum {
  EVAL_FILE,
  EVAL_BUF,
};

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
              (js_get_propertystr_bool(ctx, argv[1], "strict") ? JS_EVAL_FLAG_STRICT : 0) |
              (js_get_propertystr_bool(ctx, argv[1], "strip") ? JS_EVAL_FLAG_STRIP : 0);

      if(js_has_propertystr(ctx, argv[1], "global"))
        tmp_global = JS_GetPropertyStr(ctx, argv[1], "global");
    }
  } else
    flags = str_ends(file ? file : str, ".mjs") ? JS_EVAL_TYPE_MODULE : 0;

  if(file)
    jsm_stack_push(ctx, file);

  switch(magic) {
    case EVAL_FILE: {
      ret = JS_IsUninitialized(tmp_global) ? js_eval_file(ctx, str, flags)
                                           : js_eval_this_file(ctx, tmp_global, str, flags);
      break;
    }

    case EVAL_BUF: {
      ret = JS_IsUninitialized(tmp_global) ? js_eval_buf(ctx, str, len, file, flags)
                                           : js_eval_this_buf(ctx, tmp_global, str, len, file, flags);
      break;
    }
  }

  if(!JS_IsUninitialized(tmp_global))
    JS_FreeValue(ctx, tmp_global);

  if(file) {
    jsm_stack_pop(ctx);
    js_free(ctx, file);
  }

  if(JS_IsException(ret))
    ret = JS_GetException(ctx);

  if(JS_VALUE_GET_TAG(ret) == JS_TAG_MODULE) {
    JSModuleDef* m = JS_VALUE_GET_PTR(ret);
    JSValue obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, obj, "name", module_name(ctx, m));
    JS_SetPropertyStr(ctx, obj, "exports", module_exports(ctx, m));
    ret = obj;
  }

  JS_FreeCString(ctx, str);
  return ret;
}

enum {
  FIND_MODULE,
  FIND_MODULE_INDEX,
  LOAD_MODULE,
  ADD_MODULE,
  REQUIRE_MODULE,
  LOCATE_MODULE,
  NORMALIZE_MODULE,
  RESOLVE_MODULE,
  GET_MODULE_NAME,
  GET_MODULE_VALUE,
  GET_MODULE_INDEX,
  GET_MODULE_OBJECT,
  GET_MODULE_EXPORTS,
  GET_MODULE_IMPORTS,
  GET_MODULE_REQMODULES,
  GET_MODULE_NAMESPACE,
  GET_MODULE_FUNCTION,
  GET_MODULE_EXCEPTION,
  GET_MODULE_META_OBJ,
  MODULE_LOADER,
};

static JSValue
jsm_module_func(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue val = JS_EXCEPTION;
  JSModuleDef* m = 0;
  const char* name = 0;

  if((magic >= RESOLVE_MODULE || (magic == NORMALIZE_MODULE && JS_IsModule(argv[0]))) && magic < MODULE_LOADER) {
    if(!(m = js_module_def(ctx, argv[0])))
      return JS_ThrowTypeError(ctx,
                               "%s: argument 1 expecting module",
                               ((const char* const[]){
                                   "normalizeModule",
                                   "resolveModule",
                                   "getModuleName",
                                   "getModuleValue",
                                   "getModuleObject",
                                   "getModuleExports",
                                   "getModuleImports",
                                   "getModuleReqModules",
                                   "getModuleNamespace",
                                   "getModuleFunction",
                                   "getModuleException",
                                   "getModuleMetaObject",
                               })[magic - NORMALIZE_MODULE]);
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
    case ADD_MODULE: {
      ssize_t i;

      if((i = vector_finds(&module_list, name)) == -1) {
        i = vector_size(&module_list, sizeof(char*));
        vector_pushstring(&module_list, name);
      }

      val = JS_NewInt64(ctx, i);
      break;
    }

    case FIND_MODULE: {
      if((m = jsm_module_find(ctx, name, 0)))
        val = JS_DupValue(ctx, JS_MKPTR(JS_TAG_MODULE, m));
      else
        val = JS_NULL;

      break;
    }

    case FIND_MODULE_INDEX: {
      int32_t start = 0;

      if(argc > 1)
        JS_ToInt32(ctx, &start, argv[1]);

      m = jsm_module_find(ctx, name, start);

      val = JS_NewInt32(ctx, js_module_indexof(ctx, m));

      break;
    }

    case LOAD_MODULE: {
      const char* key = 0;

      if(argc > 1)
        key = JS_ToCString(ctx, argv[1]);

      if((m = jsm_module_load(ctx, name, key)))
        val = module_value(ctx, m);
      else
        val = JS_ThrowInternalError(ctx, "Failed loading module '%s'", name);

      if(key)
        JS_FreeCString(ctx, key);

      break;
    }

    case REQUIRE_MODULE: {
      if((m = jsm_module_loader(ctx, name, NULL)))
        val = module_exports(ctx, m);

      break;
    }

    case LOCATE_MODULE: {
      char* s;

      if((s = jsm_module_locate(ctx, name, 0))) {
        val = JS_NewString(ctx, s);
        js_free(ctx, s);
      }

      break;
    }

    case NORMALIZE_MODULE: {
      const char *path, *module, *file;

      path = m ? module_namecstr(ctx, m) : JS_ToCString(ctx, argv[0]);
      module = JS_ToCString(ctx, argv[1]);

      if((file = jsm_module_normalize(ctx, path, module, 0))) {
        val = JS_NewString(ctx, file);
        js_free(ctx, (char*)file);
      }

      JS_FreeCString(ctx, path);
      JS_FreeCString(ctx, module);
      break;
    }

    case RESOLVE_MODULE: {
      val = JS_NewInt32(ctx, JS_ResolveModule(ctx, JS_MKPTR(JS_TAG_MODULE, m)));
      break;
    }

    case GET_MODULE_NAME: {
      val = module_name(ctx, m);
      break;
    }

    case GET_MODULE_VALUE: {
      val = JS_DupValue(ctx, JS_MKPTR(JS_TAG_MODULE, m));
      break;
    }

    case GET_MODULE_INDEX: {
      val = JS_NewInt32(ctx, js_module_indexof(ctx, m));
      break;
    }

    case GET_MODULE_OBJECT: {
      val = module_object(ctx, m);
      break;
    }

    case GET_MODULE_IMPORTS: {
      val = module_imports(ctx, m);
      break;
    }

    case GET_MODULE_REQMODULES: {
      val = module_reqmodules(ctx, m);
      break;
    }

    case GET_MODULE_EXPORTS: {
      val = module_exports(ctx, m);
      break;
    }

    case GET_MODULE_NAMESPACE: {
      val = module_ns(ctx, m);
      break;
    }

    case GET_MODULE_FUNCTION: {
      val = module_func(ctx, m);
      break;
    }

    case GET_MODULE_EXCEPTION: {
      val = module_exception(ctx, m);
      break;
    }

    case GET_MODULE_META_OBJ: {
      val = module_meta_obj(ctx, m);
      break;
    }

    case MODULE_LOADER: {
      int i, arg;
      ModuleLoaderContext *lc, **lptr;

      if(!argc) {
        val = JS_NewArray(ctx);

        for(i = 0, lc = module_loaders; lc; ++i, lc = lc->next)
          JS_SetPropertyUint32(ctx, val, i, JS_DupValue(ctx, lc->func));
      } else if(JS_IsObject(argv[0])) {
        for(arg = 0; arg < argc; arg++) {
          JSObject* func_obj;
          if(!JS_IsObject(argv[arg])) {
            val = JS_ThrowTypeError(ctx, "argument %d must be an object", arg);
            break;
          }

          func_obj = js_value_obj(argv[arg]);
          val = JS_NewArray(ctx);

          for(i = 0, lptr = &module_loaders; (lc = *lptr); ++i) {
            JSObject* obj = js_value_obj(lc->func);

            if(obj == func_obj) {
              JS_FreeValue(ctx, (*lptr)->func);
              js_free(ctx, *lptr);
              *lptr = (*lptr)->next;
              continue;
            }

            JS_SetPropertyUint32(ctx, val, i, JS_DupValue(ctx, (*lptr)->func));

            lptr = &(*lptr)->next;
          }

          lc = *lptr = js_mallocz(ctx, sizeof(ModuleLoaderContext));
          lc->func = JS_DupValue(ctx, argv[arg]);
          JS_SetPropertyUint32(ctx, val, i++, JS_DupValue(ctx, lc->func));
        }
      } else {
        char* module = js_tostring(ctx, argv[0]);
        BOOL chain = TRUE;

        if(argc > 1)
          chain = JS_ToBool(ctx, argv[1]);

        m = jsm_module_loader(ctx, module, chain ? &module_loaders : 0);
        val = m ? JS_NewInt32(ctx, js_module_indexof(ctx, m)) : JS_NULL;
        // val = m ? JS_DupValue(ctx, JS_MKVAL(JS_TAG_MODULE, m)) : JS_NULL;
      }

      break;
    }
  }

  if(name)
    js_free(ctx, (char*)name);

  return val;
}

static const JSCFunctionListEntry jsm_global_funcs[] = {
    JS_CFUNC_MAGIC_DEF("evalFile", 1, jsm_eval_script, EVAL_FILE),
    JS_CFUNC_MAGIC_DEF("evalBuf", 1, jsm_eval_script, EVAL_BUF),
    JS_CGETSET_MAGIC_DEF("moduleList", jsm_modules_array, 0, 0),
    JS_CGETSET_MAGIC_DEF("moduleObject", js_modules_object, 0, 0),
    JS_CGETSET_MAGIC_DEF("moduleMap", js_modules_map, 0, 0),
    JS_CFUNC_MAGIC_DEF("moduleLoader", 1, jsm_module_func, MODULE_LOADER),
    JS_CGETSET_MAGIC_DEF("scriptList", jsm_stack_get, 0, SCRIPT_LIST),
    JS_CGETSET_MAGIC_DEF("scriptFile", jsm_stack_get, 0, SCRIPT_FILE),
    JS_CGETSET_MAGIC_DEF("scriptDir", jsm_stack_get, 0, SCRIPT_DIRNAME),
    JS_CGETSET_MAGIC_DEF("__filename", jsm_stack_get, 0, SCRIPT_FILENAME),
    JS_CGETSET_MAGIC_DEF("__dirname", jsm_stack_get, 0, SCRIPT_DIRNAME),
    JS_CFUNC_MAGIC_DEF("findModule", 1, jsm_module_func, FIND_MODULE),
    JS_CFUNC_MAGIC_DEF("findModuleIndex", 1, jsm_module_func, FIND_MODULE_INDEX),
    JS_CFUNC_MAGIC_DEF("loadModule", 1, jsm_module_func, LOAD_MODULE),
    JS_CFUNC_MAGIC_DEF("addModule", 1, jsm_module_func, ADD_MODULE),
    JS_CFUNC_MAGIC_DEF("resolveModule", 1, jsm_module_func, RESOLVE_MODULE),
    JS_CFUNC_MAGIC_DEF("requireModule", 1, jsm_module_func, REQUIRE_MODULE),
    JS_CFUNC_MAGIC_DEF("normalizeModule", 2, jsm_module_func, NORMALIZE_MODULE),
    JS_CFUNC_MAGIC_DEF("locateModule", 1, jsm_module_func, LOCATE_MODULE),
    JS_CFUNC_MAGIC_DEF("getModuleName", 1, jsm_module_func, GET_MODULE_NAME),
    JS_CFUNC_MAGIC_DEF("getModuleValue", 1, jsm_module_func, GET_MODULE_VALUE),
    JS_CFUNC_MAGIC_DEF("getModuleIndex", 1, jsm_module_func, GET_MODULE_INDEX),
    JS_CFUNC_MAGIC_DEF("getModuleObject", 1, jsm_module_func, GET_MODULE_OBJECT),
    JS_CFUNC_MAGIC_DEF("getModuleExports", 1, jsm_module_func, GET_MODULE_EXPORTS),
    JS_CFUNC_MAGIC_DEF("getModuleImports", 1, jsm_module_func, GET_MODULE_IMPORTS),
    JS_CFUNC_MAGIC_DEF("getModuleReqModules", 1, jsm_module_func, GET_MODULE_REQMODULES),
    JS_CFUNC_MAGIC_DEF("getModuleNamespace", 1, jsm_module_func, GET_MODULE_NAMESPACE),
    JS_CFUNC_MAGIC_DEF("getModuleFunction", 1, jsm_module_func, GET_MODULE_FUNCTION),
    JS_CFUNC_MAGIC_DEF("getModuleException", 1, jsm_module_func, GET_MODULE_EXCEPTION),
    JS_CFUNC_MAGIC_DEF("getModuleMetaObject", 1, jsm_module_func, GET_MODULE_META_OBJ),
    JS_CFUNC_DEF("startInteractive", 0, jsm_start_interactive4),
};

static void
jsm_start_interactive(JSContext* ctx, BOOL global) {
  if(interactive == 1) {
    js_eval_fmt(ctx,
                JS_EVAL_TYPE_MODULE,
                "import { REPL } from 'repl';\n"
                "%srepl = new REPL('%.*s'.replace(/.*\\//g, '').replace(/\\.js$/g, ''), false);\n"
                "repl.loadSaveOptions();\n"
                "repl.historyLoad();\n"
                "repl.run();\n",
                global ? "globalThis." : "const ",
                (int)str_chr(exename, '.'),
                exename);

    interactive = 2;
  }
}

static JSValue
jsm_start_interactive4(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  BOOL global = TRUE;

  if(argc > 0)
    global = JS_ToBool(ctx, argv[0]);

  jsm_start_interactive(ctx, global);

  return JS_UNDEFINED;
}

static JSValue
jsm_start_interactive3(JSContext* ctx, int argc, JSValueConst argv[]) {
  return jsm_start_interactive4(ctx, JS_NULL, argc, argv);
}

#ifndef _WIN32
static void
jsm_signal_handler(int arg) {
  switch(arg) {
    case SIGUSR1: {
      interactive = 1;

      JS_EnqueueJob(jsm_ctx, &jsm_start_interactive3, 0, 0);
      break;
    }
  }
}
#endif

int
jsm_interrupt_handler(JSRuntime* rt, void* opaque) {
  /*JSContext* ctx = opaque;*/

  return 0;
}

int
main(int argc, char** argv) {
  struct trace_malloc_data trace_data = {0};
  int optind;
  char *expr = 0, dump_memory = 0, trace_memory = 0, empty_run = 0, module = 1, load_std = 1,
       dump_unhandled_promise_rejection = 0;
  const char* include_list[32];
  size_t /*i,*/ memory_limit = 0, include_count = 0, stack_size = 0;
#ifdef HAVE_QJSCALC
  int load_jscalc;
#endif

  package_json = JS_UNDEFINED;
  // replObj = JS_UNDEFINED;

  exename = strdup(argv[0] + path_basename1(argv[0]));
  exelen = strlen(exename);

  // printf("n = %zu, exename = %s, exelen = %d\n", n, exename, (int)exelen);

  /* load jscalc runtime if invoked as 'qjscalc' */
#ifdef HAVE_QJSCALC
  load_jscalc = !strcmp(exename, "qjscalc");
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

      if(opt == 'h' || opt == '?' || !strcmp(longopt, "help")) {
        jsm_help();
        continue;
      }

      if(opt == 'e' || !strcmp(longopt, "eval")) {
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

      if(opt == 'I' || !strcmp(longopt, "include")) {
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

      if(opt == 'i' || !strcmp(longopt, "interactive")) {
        interactive = 1;
        break;
      }

      if(opt == 'm' || !strcmp(longopt, "module")) {
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

      if(opt == 'd' || !strcmp(longopt, "dump")) {
        dump_memory++;
        break;
      }

      if(opt == 'T' || !strcmp(longopt, "trace")) {
        trace_memory++;
        break;
      }

      if(!strcmp(longopt, "std")) {
        load_std = 1;
        break;
      }

      if(!strcmp(longopt, "unhandled-rejection")) {
        dump_unhandled_promise_rejection = 1;
        break;
      }

#ifdef CONFIG_BIGNUM
      if(!strcmp(longopt, "no-bignum")) {
        bignum_ext = 0;
        break;
      }

      if(!strcmp(longopt, "bignum")) {
        bignum_ext = 1;
        break;
      }
#ifdef HAVE_QJSCALC
      if(!strcmp(longopt, "qjscalc")) {
        load_jscalc = 1;
        break;
      }
#endif
#endif
      if(opt == 'q' || !strcmp(longopt, "quit")) {
        empty_run++;
        break;
      }

      if(!strcmp(longopt, "memory-limit")) {
        if(optind >= argc) {
          fprintf(stderr, "expecting memory limit");
          exit(1);
        }

        memory_limit = (size_t)strtod(argv[optind++], 0);
        break;
      }

      if(!strcmp(longopt, "stack-size")) {
        if(optind >= argc) {
          fprintf(stderr, "expecting stack size");
          exit(1);
        }

        stack_size = (size_t)strtod(argv[optind++], 0);
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

  jsm_init_modules(jsm_ctx);

#ifdef HAVE_GET_MODULE_LOADER_FUNC
  module_loader = js_std_get_module_loader_func();
#endif

  {
    const char* modules;

    if((modules = getenv("DEBUG"))) {
      size_t len;

      for(size_t i = 0; modules[i]; i += len) {
        len = str_chr(&modules[i], ',');
        vector_putptr(&module_debug, str_ndup(&modules[i], len));

        if(modules[i + len] == ',')
          len++;
      }

      debug_module_loader = vector_counts(&module_debug, "modules");
    }
  }

#ifdef HAVE_QJSCALC
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
  JS_SetModuleLoaderFunc(jsm_rt, jsm_module_normalize, jsm_module_loader, &module_loaders);

  jsm_ctx = jsm_context_new(jsm_rt);
  if(!jsm_ctx) {
    fprintf(stderr, "%s: cannot allocate JS context\n", exename);
    exit(2);
  }

  vector_init(&jsm_stack, jsm_ctx);

  if(dump_unhandled_promise_rejection)
    JS_SetHostPromiseRejectionTracker(jsm_rt, js_std_promise_rejection_tracker, 0);

  JS_SetInterruptHandler(jsm_rt, jsm_interrupt_handler, jsm_ctx);

  JSValue sargs = JS_UNDEFINED;

  if(!empty_run) {
    DynBuf db;
    js_dbuf_init(jsm_ctx, &db);

#ifdef HAVE_QJSCALC
    if(load_jscalc) {
      js_eval_binary(jsm_ctx, qjsc_qjscalc, qjsc_qjscalc_size, 0);
    }
#endif

    js_std_add_helpers(jsm_ctx, argc - optind, argv + optind);

    dbuf_putstr(&db, "import process from 'process';\nglobalThis.process = process;\n");

    // dbuf_putstr(&db, "import require from 'require';\nglobalThis.require = require;\n");

    JS_SetPropertyFunctionList(jsm_ctx, JS_GetGlobalObject(jsm_ctx), jsm_global_funcs, countof(jsm_global_funcs));

    if(load_std) {
      const char* str = "import * as std from 'std';\nimport * as os from 'os';\nglobalThis.std = "
                        "std;\nglobalThis.os = os;\nglobalThis.setTimeout = "
                        "os.setTimeout;\nglobalThis.clearTimeout = os.clearTimeout;\n";
      dbuf_putstr(&db, str);
    }

    sargs = js_global_get_str(jsm_ctx, "scriptArgs");
    JS_DefinePropertyValueStr(jsm_ctx, sargs, "-1", JS_NewString(jsm_ctx, argv[0]), 0);

    if(db.size) {
      dbuf_0(&db);
      js_eval_str(jsm_ctx, (const char*)db.buf, 0, JS_EVAL_TYPE_MODULE);
    }

    dbuf_free(&db);

    {
      char** ptr;

      vector_foreach_t(&module_list, ptr) {
        JSModuleDef* m;

        if(!(m = jsm_module_load(jsm_ctx, *ptr, 0))) {
          jsm_dump_error(jsm_ctx);
          return 1;
        }
      }

      vector_freestrings(&module_list);
    }

    for(size_t i = 0; i < include_count; i++) {
      if(jsm_stack_load(jsm_ctx, include_list[i], FALSE, FALSE) == -1)
        goto fail;
    }

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

    if(expr) {
      if(js_eval_str(jsm_ctx, expr, "<cmdline>", 0) == -1)
        goto fail;
    } else if(optind >= argc) {
      /* interactive mode */
      interactive = 1;
    } else {
      const char* filename = argv[optind];

      JS_DefinePropertyValueStr(jsm_ctx, sargs, "$", JS_NewString(jsm_ctx, filename), 0);

      if(jsm_stack_load(jsm_ctx, filename, module, TRUE) == -1)
        goto fail;
    }

    if(interactive == 1)
      jsm_start_interactive(jsm_ctx, TRUE);

    js_std_loop(jsm_ctx);
  }

  JSValue exception = JS_GetException(jsm_ctx);

  if(!JS_IsNull(exception))
    js_error_print(jsm_ctx, exception);

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
    printf("\nInstantiation times (ms): %.3f = %.3f+%.3f+%.3f+%.3f\n",
           best[1] + best[2] + best[3] + best[4],
           best[1],
           best[2],
           best[3],
           best[4]);
  }

  return 0;
fail:
  js_std_free_handlers(jsm_rt);
  JS_FreeContext(jsm_ctx);
  JS_FreeRuntime(jsm_rt);
  return 1;
}
