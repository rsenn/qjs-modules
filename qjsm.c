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
/*#include <sys/poll.h>*/
#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#endif
#if !defined(__wasi__) && !defined(_WIN32)
#include <dlfcn.h>
#endif

#if 1 // def HAVE_QUICKJS_CONFIG_H
#include <quickjs-config.h>
#endif

#ifdef USE_WORKER
#include <pthread.h>
#include <stdatomic.h>

static int
atomic_add_int(int* ptr, int v) {
  return atomic_fetch_add((_Atomic(uint32_t)*)ptr, v) + v;
}
#endif

#include <list.h>
#include <cutils.h>
#include "utils.h"
#include "vector.h"
#include <quickjs-libc.h>
#include "quickjs-internal.h"
#include "buffer-utils.h"

/*typedef struct pollhandler {
  struct pollfd pf;
  void (*handler)(void* opaque, struct pollfd*);
  void* opaque;
  struct list_head link;
} pollhandler_t;

thread_local uint64_t jsm_pending_signals = 0;
struct list_head pollhandlers;*/

void js_std_set_module_loader_func(JSModuleLoaderFunc* func);

#ifdef HAVE_MALLOC_USABLE_SIZE
#ifndef HAVE_MALLOC_USABLE_SIZE_DEFINITION
extern size_t malloc_usable_size();
#endif
#endif

#define trim_dotslash(str) (!strncmp((str), "./", 2) ? (str) + 2 : (str))

struct jsm_module_record {
  const char* module_name;
  JSModuleDef* (*module_func)(JSContext*, const char*);
  uint8_t* byte_code;
  uint32_t byte_code_len;
  JSModuleDef* def;
};

#define jsm_module_extern_compiled(name) \
  extern const uint8_t qjsc_##name[]; \
  extern const uint32_t qjsc_##name##_size;

#define jsm_module_extern_native(name) extern JSModuleDef* js_init_module_##name(JSContext*, const char*)

#define jsm_module_record_compiled(name) \
  (struct jsm_module_record) { #name, 0, qjsc_##name, qjsc_##name##_size, 0 }

#define jsm_module_record_native(name) \
  (struct jsm_module_record) { #name, js_init_module_##name, 0, 0, 0 }

jsm_module_extern_native(std);
jsm_module_extern_native(os);
jsm_module_extern_native(child_process);
jsm_module_extern_native(deep);
jsm_module_extern_native(inspect);
jsm_module_extern_native(lexer);
jsm_module_extern_native(misc);
jsm_module_extern_native(mmap);
jsm_module_extern_native(path);
jsm_module_extern_native(pointer);
jsm_module_extern_native(predicate);
jsm_module_extern_native(repeater);
jsm_module_extern_native(tree_walker);
jsm_module_extern_native(xml);

jsm_module_extern_compiled(console);
jsm_module_extern_compiled(events);
jsm_module_extern_compiled(fs);
jsm_module_extern_compiled(perf_hooks);
jsm_module_extern_compiled(process);
jsm_module_extern_compiled(repl);
jsm_module_extern_compiled(require);
jsm_module_extern_compiled(tty);
jsm_module_extern_compiled(util);

static thread_local Vector jsm_modules;
static thread_local BOOL jsm_modules_initialized;

#ifdef CONFIG_BIGNUM
jsm_module_extern_compiled(qjscalc);
static int bignum_ext = 1;
#endif

void js_std_set_worker_new_context_func(JSContext* (*func)(JSRuntime* rt));

static void
jsm_dump_error(JSContext* ctx) {
  /*JSRuntime* rt = JS_GetRuntime(ctx);
  JSValue error = rt->current_exception;*/
  /*printf("qjsm: current_exception 0x%08x\n", offsetof(JSRuntime, current_exception));
  printf("qjsm: sizeof(struct list_head) 0x%08x\n", sizeof(struct list_head));*/

  js_error_print(ctx, JS_GetException(ctx));
}

static int debug_module_loader = 0;
static Vector module_debug = VECTOR_INIT();
static thread_local Vector module_list = VECTOR_INIT();
// static Vector builtins = VECTOR_INIT();

static const char jsm_default_module_path[] = "."
#ifdef QUICKJS_MODULE_PATH
                                              ":" QUICKJS_MODULE_PATH
#elif defined(CONFIG_PREFIX)
                                              ":" CONFIG_PREFIX "/lib/quickjs"
#endif
    ;

JSValue package_json;

static JSValue
eval_buf(JSContext* ctx, const void* buf, int buf_len, const char* filename, int eval_flags) {
  JSValue val;

  if((eval_flags & JS_EVAL_TYPE_MASK) == JS_EVAL_TYPE_MODULE) {
    /* for the modules, we compile then run to be able to set import.meta */
    val = JS_Eval(ctx, buf, buf_len, filename, eval_flags | JS_EVAL_FLAG_COMPILE_ONLY);
    if(!JS_IsException(val)) {
      js_module_set_import_meta(ctx, val, TRUE, TRUE);
      JS_EvalFunction(ctx, val);
    }
  } else {
    val = JS_Eval(ctx, buf, buf_len, filename, eval_flags);
  }
  if(JS_IsException(val))
    js_error_print(ctx, JS_GetRuntime(ctx)->current_exception);

  jsm_dump_error(ctx);
  return val;
}

void
jsm_init_modules(JSContext* ctx) {
  js_dbuf_init(ctx, &jsm_modules);

#define jsm_builtin_native(name) vector_push(&jsm_modules, jsm_module_record_native(name));

  jsm_builtin_native(std);
  jsm_builtin_native(os);
  jsm_builtin_native(child_process);
  jsm_builtin_native(deep);
  jsm_builtin_native(inspect);
  jsm_builtin_native(lexer);
  jsm_builtin_native(misc);
  jsm_builtin_native(mmap);
  jsm_builtin_native(path);
  jsm_builtin_native(pointer);
  jsm_builtin_native(predicate);
  jsm_builtin_native(repeater);
  jsm_builtin_native(tree_walker);
  jsm_builtin_native(xml);

  // printf("native builtins: "); dump_vector(&builtins, 0);

#define jsm_builtin_compiled(name) vector_push(&jsm_modules, jsm_module_record_compiled(name));

  jsm_builtin_compiled(console);
  jsm_builtin_compiled(events);
  jsm_builtin_compiled(fs);
  jsm_builtin_compiled(perf_hooks);
  jsm_builtin_compiled(process);
  jsm_builtin_compiled(repl);
  jsm_builtin_compiled(require);
  jsm_builtin_compiled(tty);
  jsm_builtin_compiled(util);
}

static JSValue
jsm_load_package(JSContext* ctx, const char* file) {
  if(JS_IsUndefined(package_json)) {
    uint8_t* buf;
    size_t len;
    if(file == 0)
      file = "package.json";
    if(!(buf = js_load_file(ctx, &len, file)))
      package_json = JS_NULL;
    else
      package_json = JS_ParseJSON(ctx, buf, len, file);
  }
  return JS_DupValue(ctx, package_json);
}

static struct jsm_module_record*
jsm_module_find(const char* name) {
  struct jsm_module_record* rec;
  vector_foreach_t(&jsm_modules, rec) {
    if(!strcmp(rec->module_name, name))
      return rec;
  }
  return 0;
}

static JSModuleDef*
jsm_module_init(JSContext* ctx, struct jsm_module_record* rec) {
  JSModuleDef* m;
  if(rec->def == 0) {
    if(debug_module_loader)
      printf("\x1b[48;5;214m(3)\x1b[0m %-30s internal\n", rec->module_name);
    if(rec->module_func) {
      m = rec->module_func(ctx, rec->module_name);

      // m->init_func(ctx, m);

    } else {
      JSValue obj = js_eval_binary(ctx, rec->byte_code, rec->byte_code_len, 0);
      m = JS_VALUE_GET_PTR(obj);
    }
    rec->def = m;
  }
  return rec->def;
}

JSModuleDef*
jsm_module_loader(JSContext* ctx, const char* name, void* opaque) {
  char *module, *file = 0;
  JSModuleDef* m = 0;

  if(!jsm_modules_initialized) {
    jsm_init_modules(ctx);
    jsm_modules_initialized = TRUE;
  }

  module = js_strdup(ctx, trim_dotslash(name));
  for(;;) {
    if(debug_module_loader > 1) {
      if(file)
        printf("\x1b[48;5;214m(1)\x1b[0m %-30s '%s'\n", name, file);
      /*  else  printf("jsm_module_loader[%x] \x1b[48;5;124m(1)\x1b[0m %-20s ->
       * %s\n", pthread_self(), trim_dotslash(name), trim_dotslash(module));*/
    }
    if(!strchr(module, '/')) {
      struct jsm_module_record* rec;

      if((rec = jsm_module_find(module))) {
        m = jsm_module_init(ctx, rec);
        goto end;
      }
    }

    if(!has_suffix(name, ".so") && !file) {
      JSValue package = jsm_load_package(ctx, 0);
      if(!JS_IsNull(package)) {
        JSValue aliases = JS_GetPropertyStr(ctx, package, "_moduleAliases");
        JSValue target = JS_UNDEFINED;
        if(!JS_IsUndefined(aliases)) {
          target = JS_GetPropertyStr(ctx, aliases, module);
        }
        JS_FreeValue(ctx, aliases);
        JS_FreeValue(ctx, package);
        if(!JS_IsUndefined(target)) {
          const char* str = JS_ToCString(ctx, target);
          if(str) {
            if(debug_module_loader)
              printf("\x1b[48;5;28m(2)\x1b[0m %-30s => %s\n", module, str);

            js_free(ctx, module);

            module = js_strdup(ctx, str);
            JS_FreeCString(ctx, str);
            continue;
          }
        }
      }
    }
    if(!file) {
      if(strchr("./", module[0]))
        file = js_strdup(ctx, module);
      else if(!(file = js_module_search(ctx, jsm_default_module_path, module)))
        break;
      continue;
    }
    break;
  }
  if(file) {
    if(debug_module_loader)
      if(strcmp(trim_dotslash(module), trim_dotslash(file)))
        printf("\x1b[48;5;21m(3)\x1b[0m %-30s -> %s\n", module, file);

    m = js_module_loader(ctx, file, opaque);
  }
end:
  if(vector_finds(&module_debug, "import") != -1) {
    fprintf(stderr, (!file || strcmp(module, file)) ? "!!! IMPORT %s -> %s\n" : "!!! IMPORT %s\n", module, file);
  }
  if(!m)
    printf("jsm_module_loader(\"%s\") = %p\n", name, m);
  if(module)
    js_free(ctx, module);
  if(file)
    js_free(ctx, file);
  return m;
}

static JSValue
jsm_eval_file(JSContext* ctx, const char* file, int module) {
  uint8_t* buf;
  size_t len;
  int flags;

  buf = js_load_file(ctx, &len, file);
  if(!buf) {
    return JS_ThrowInternalError(ctx, "Failed loading '%s': %s", file, strerror(errno));
  }

  if(module < 0)
    module = (has_suffix(file, ".mjs") || JS_DetectModule((const char*)buf, len));
  flags = module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
  return eval_buf(ctx, buf, len, file, flags);
}

static int
jsm_load_script(JSContext* ctx, const char* file, BOOL module) {
  JSValue val;
  int32_t ret = 0;
  val = jsm_eval_file(ctx, file, module);
  if(JS_IsException(val)) {
    fprintf(stderr, "Failed loading '%s': %s\n", file, strerror(errno));
    js_value_fwrite(ctx, val, stderr);
    return -1;
  } else if(JS_IsModule(val)) {
    JSValue global = JS_GetGlobalObject(ctx);
    module_exports_get(ctx, JS_VALUE_GET_PTR(val), TRUE, global);
  } else {
    JS_ToInt32(ctx, &ret, val);
    JS_FreeValue(ctx, val);
  }
  return ret;
}

/* also used to initialize the worker context */
static JSContext*
jsm_context_new(JSRuntime* rt) {
  JSContext* ctx;
  ctx = JS_NewContext(rt);
  if(!ctx)
    return 0;
#ifdef CONFIG_BIGNUM
  if(bignum_ext) {
    JS_AddIntrinsicBigFloat(ctx);
    JS_AddIntrinsicBigDecimal(ctx);
    JS_AddIntrinsicOperators(ctx);
    JS_EnableBignumExt(ctx, TRUE);
  }
#endif

#define jsm_module_native(name) js_init_module_##name(ctx, #name);

  /* jsm_module_native(std);
   jsm_module_native(os);
 #ifndef __wasi__
   jsm_module_native(child_process);
 #endif
   jsm_module_native(deep);
   jsm_module_native(inspect);
   jsm_module_native(lexer);
   jsm_module_native(misc);
 #ifndef __wasi__
   jsm_module_native(mmap);
 #endif
   jsm_module_native(path);
   jsm_module_native(pointer);
   jsm_module_native(predicate);
   jsm_module_native(repeater);
   jsm_module_native(tree_walker);
   jsm_module_native(xml);*/
  return ctx;
}

#if defined(__APPLE__)
#define MALLOC_OVERHEAD 0
#else
#define MALLOC_OVERHEAD 8
#endif

struct trace_malloc_data {
  uint8_t* base;
};

static void
dump_vector(const Vector* vec, size_t start) {
  size_t i, len = vector_size(vec, sizeof(char*));
  for(i = start; i < len; i++) {
    const char* str = *(char**)vector_at(vec, sizeof(char*), i);
    fputs(i > start ? "',\n  '" : "[\n  '", stdout);
    fputs(str, stdout);
    if(i + 1 == len)
      puts("'\n]");
  }
}

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
#elif defined(EMSCRIPTEN) || defined(__dietlibc__) || defined(__MSYS__) || defined(DONT_HAVE_MALLOC_USABLE_SIZE)
  return 0;
#elif defined(__linux__) || defined(HAVE_MALLOC_USABLE_SIZE)
  return malloc_usable_size(ptr);
#else
  /* change this to `return 0;` if compilation fails */
  return malloc_usable_size(ptr);
#endif
}

static void
#ifdef _WIN32
    /* mingw printf is used */
    __attribute__((format(gnu_printf, 2, 3)))
#else
    __attribute__((format(printf, 2, 3)))
#endif
    jsm_trace_malloc_printf(JSMallocState* s, const char* fmt, ...) {
  va_list ap;
  int c;

  va_start(ap, fmt);
  while((c = *fmt++) != '\0') {
    if(c == '%') {
      /* only handle %p and %zd */
      if(*fmt == 'p') {
        uint8_t* ptr = va_arg(ap, void*);
        if(ptr == 0) {
          printf("0");
        } else {
          printf("H%+06lld.%zd", jsm_trace_malloc_ptr_offset(ptr, s->opaque), jsm_trace_malloc_usable_size(ptr));
        }
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
  if(ptr) {
    s->malloc_size += jsm_trace_malloc_usable_size(ptr) - old_size;
  }
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
#elif defined(EMSCRIPTEN) || defined(__dietlibc__) || defined(__MSYS__) || defined(DONT_HAVE_MALLOC_USABLE_SIZE_DEFINITION)
    0,
#elif defined(__linux__) || defined(HAVE_MALLOC_USABLE_SIZE)
    (size_t(*)(const void*))malloc_usable_size,
#else
    /* change this to `0,` if compilation fails */
    malloc_usable_size,
#endif
};

#define PROG_NAME "qjsm"

void
jsm_help(void) {
  printf("QuickJS version " CONFIG_VERSION "\n"
         "usage: " PROG_NAME " [options] [file [args]]\n"
         "-h  --help         list options\n"
         "-e  --eval EXPR    evaluate EXPR\n"
         "-i  --interactive  go to interactive mode\n"
         "-m  --module NAME  load an ES6 module\n"
         "-I  --include file include an additional file\n"
         "    --std          make 'std' and 'os' available to the loaded script\n"
#ifdef CONFIG_BIGNUM
         "    --no-bignum    disable the bignum extensions (BigFloat, "
         "BigDecimal)\n"
         "    --qjscalc      load the QJSCalc runtime (default if invoked as "
         "qjscalc)\n"
#endif
         "-T  --trace        trace memory allocation\n"
         "-d  --dump         dump the memory usage stats\n"
         "    --memory-limit n       limit the memory usage to 'n' bytes\n"
         "    --stack-size n         limit the stack size to 'n' bytes\n"
         "    --unhandled-rejection  dump unhandled promise rejections\n"
         "-q  --quit         just instantiate the interpreter and quit\n");
  exit(1);
}

static JSValue
jsm_eval_script(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  const char* str;
  size_t len;
  JSValue ret;
  int32_t module;
  str = JS_ToCStringLen(ctx, &len, argv[0]);
  if(argc > 1)
    JS_ToInt32(ctx, &module, argv[1]);
  else
    module = str_ends(str, ".mjs");
  switch(magic) {
    case 0: {
      ret = jsm_eval_file(ctx, str, module);
      break;
    }
    case 1: {
      ret = eval_buf(ctx, str, len, 0, module);
      break;
    }
  }
  if(JS_IsException(ret)) {
    if(JS_IsNull(JS_GetRuntime(ctx)->current_exception)) {
      ret = JS_GetException(ctx);
      // ret = JS_UNDEFINED;
    }
  }
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
  LOAD_MODULE,
  RESOLVE_MODULE,
  GET_MODULE_NAME,
  GET_MODULE_OBJECT,
  GET_MODULE_EXPORTS,
  GET_MODULE_NAMESPACE,
  GET_MODULE_FUNCTION,
  GET_MODULE_EXCEPTION,
  GET_MODULE_META_OBJ,
};

static JSValue
jsm_module_func(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue val = JS_EXCEPTION;
  JSModuleDef* m = 0;
  const char* name = 0;

  if(magic >= GET_MODULE_NAME) {
    if(!(m = js_module_def(ctx, argv[0])))
      return JS_ThrowTypeError(ctx,
                               "%s: argument 1 expecting module",
                               ((const char* const[]){
                                   "getModuleName",
                                   "getModuleObject",
                                   "getModuleExports",
                                   "getModuleNamespace",
                                   "getModuleFunction",
                                   "getModuleException",
                                   "getModuleMetaObject",
                               })[magic - 3]);
  } else {
    name = JS_ToCString(ctx, argv[0]);
  }

  switch(magic) {
    case FIND_MODULE: {
      if((m = js_module_find(ctx, name)))
        val = JS_DupValue(ctx, JS_MKPTR(JS_TAG_MODULE, m));
      else
        val = JS_NULL;
      break;
    }
    case LOAD_MODULE: {
      ImportDirective imp;
      memset(&imp, 0, sizeof(imp));
      int r, n = countof(imp.args);
      r = js_strv_copys(ctx, argc, argv, n, imp.args);
      // printf("LOAD_MODULE r=%i argc=%i\n", r, argc);

      val = js_import_eval(ctx, imp);

      /*   if(JS_IsModule(val))
           m = JS_VALUE_GET_PTR(val);
         else*/
      m = js_module_find(ctx, imp.path);

      if(m)
        val = module_object(ctx, m);

      js_strv_free_n(ctx, n, imp.args);
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
    case GET_MODULE_OBJECT: {
      val = module_object(ctx, m);
      break;
    }
    case GET_MODULE_EXPORTS: {
      val = module_exports(ctx, m);
      break;
    }
    case GET_MODULE_NAMESPACE: {
      val = JS_DupValue(ctx, m->module_ns);
      break;
    }
    case GET_MODULE_FUNCTION: {

      val = module_func(ctx, m);
      break;
    }
    case GET_MODULE_EXCEPTION: {
      if(m->eval_has_exception)
        val = JS_DupValue(ctx, m->eval_exception);
      else
        val = JS_NULL;
      break;
    }
    case GET_MODULE_META_OBJ: {
      val = JS_DupValue(ctx, m->meta_obj);
      break;
    }
  }
  if(name)
    JS_FreeCString(ctx, name);

  return val;
}

static const JSCFunctionListEntry jsm_global_funcs[] = {
    JS_CFUNC_MAGIC_DEF("evalFile", 1, jsm_eval_script, 0),
    JS_CFUNC_MAGIC_DEF("evalScript", 1, jsm_eval_script, 1),
    JS_CGETSET_MAGIC_DEF("moduleList", js_modules_array, 0, 0),
    JS_CGETSET_MAGIC_DEF("moduleObject", js_modules_object, 0, 0),
    JS_CGETSET_MAGIC_DEF("moduleMap", js_modules_map, 0, 0),
    JS_CFUNC_MAGIC_DEF("findModule", 1, jsm_module_func, FIND_MODULE),
    JS_CFUNC_MAGIC_DEF("loadModule", 1, jsm_module_func, LOAD_MODULE),
    JS_CFUNC_MAGIC_DEF("resolveModule", 1, jsm_module_func, RESOLVE_MODULE),
    JS_CFUNC_MAGIC_DEF("getModuleName", 1, jsm_module_func, GET_MODULE_NAME),
    JS_CFUNC_MAGIC_DEF("getModuleObject", 1, jsm_module_func, GET_MODULE_OBJECT),
    JS_CFUNC_MAGIC_DEF("getModuleExports", 1, jsm_module_func, GET_MODULE_EXPORTS),
    JS_CFUNC_MAGIC_DEF("getModuleNamespace", 1, jsm_module_func, GET_MODULE_NAMESPACE),
    JS_CFUNC_MAGIC_DEF("getModuleFunction", 1, jsm_module_func, GET_MODULE_FUNCTION),
    JS_CFUNC_MAGIC_DEF("getModuleException", 1, jsm_module_func, GET_MODULE_EXCEPTION),
    JS_CFUNC_MAGIC_DEF("getModuleMetaObject", 1, jsm_module_func, GET_MODULE_META_OBJ),
};

int
main(int argc, char** argv) {
  JSRuntime* rt;
  JSContext* ctx;
  struct trace_malloc_data trace_data = {0};
  int optind;
  char* expr = 0;
  int interactive = 0;
  int dump_memory = 0;
  int trace_memory = 0;
  int empty_run = 0;
  int module = 1;
  int load_std = 1;
  int dump_unhandled_promise_rejection = 0;
  size_t memory_limit = 0;
  char* include_list[32];
  int i, include_count = 0;
#ifdef CONFIG_BIGNUM
  int load_jscalc;
#endif
  size_t stack_size = 0;
  const char* exename;

  package_json = JS_UNDEFINED;

  /*init_list_head(&pollhandlers);*/

  {
    const char* p;
    exename = argv[0];
    p = strrchr(exename, '/');
    if(p)
      exename = p + 1;
    /* load jscalc runtime if invoked as 'qjscalc' */
    load_jscalc = !strcmp(exename, "qjscalc");
  }

  /* cannot use getopt because we want to pass the command line to
     the script */
  optind = 1;
  while(optind < argc && *argv[optind] == '-') {
    char* arg = argv[optind] + 1;
    const char* longopt = "";
    const char* optarg;

    /* a single - is not an option, it also stops argument scanning */
    if(!*arg)
      break;

    if(arg[1]) {
      optarg = &arg[1];
    } else {
      optarg = argv[++optind];
    }

    if(*arg == '-') {
      longopt = arg + 1;
      arg += strlen(arg);
      /* -- stops argument scanning */
      if(!*longopt)
        break;
    }
    for(; *arg || *longopt; longopt = "") {
      char opt = *arg;
      if(opt)
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
        interactive++;
        break;
      }
      if(opt == 'm' || !strcmp(longopt, "module")) {
        const char* modules = argv[optind];
        size_t i, len;

        for(i = 0; modules[i]; i += len) {
          len = str_chr(&modules[i], ',');
          vector_putptr(&module_list, str_ndup(&modules[i], len));

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
      if(!strcmp(longopt, "qjscalc")) {
        load_jscalc = 1;
        break;
      }
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

  {
    const char* modules;

    if((modules = getenv("DEBUG"))) {
      size_t i, len;
      for(i = 0; modules[i]; i += len) {
        len = str_chr(&modules[i], ',');
        vector_putptr(&module_debug, str_ndup(&modules[i], len));

        if(modules[i + len] == ',')
          len++;
      }

      debug_module_loader = vector_counts(&module_debug, "modules");
    }
  }

  if(load_jscalc)
    bignum_ext = 1;

  if(trace_memory) {
    jsm_trace_malloc_init(&trace_data);
    rt = JS_NewRuntime2(&trace_mf, &trace_data);
  } else {
    rt = JS_NewRuntime();
  }
  if(!rt) {
    fprintf(stderr, "%s: cannot allocate JS runtime\n", exename);
    exit(2);
  }

  JS_SetModuleLoaderFunc(rt, 0, jsm_module_loader, 0);

  if(memory_limit != 0)
    JS_SetMemoryLimit(rt, memory_limit);
  // if (stack_size != 0)
  JS_SetMaxStackSize(rt, stack_size != 0 ? stack_size : 256 * 1048576);

  js_std_set_worker_new_context_func(jsm_context_new);

  js_std_init_handlers(rt);
  ctx = jsm_context_new(rt);
  if(!ctx) {
    fprintf(stderr, "%s: cannot allocate JS context\n", exename);
    exit(2);
  }

  /* loader for ES6 modules */
  JS_SetModuleLoaderFunc(rt, js_module_normalize, jsm_module_loader, 0);
  // js_std_set_module_loader_func(jsm_module_loader);

  if(dump_unhandled_promise_rejection) {
    JS_SetHostPromiseRejectionTracker(rt, js_std_promise_rejection_tracker, 0);
  }

  if(!empty_run) {
#ifdef CONFIG_BIGNUM
    if(load_jscalc) {
      js_eval_binary(ctx, qjsc_qjscalc, qjsc_qjscalc_size, 0);
    }
#endif
    js_std_add_helpers(ctx, argc - optind, argv + optind);

    {
      const char* str = "import process from 'process';\nglobalThis.process = process;\n";
      js_eval_str(ctx, str, 0, JS_EVAL_TYPE_MODULE);
    }

    JS_SetPropertyFunctionList(ctx, JS_GetGlobalObject(ctx), jsm_global_funcs, countof(jsm_global_funcs));
    if(load_std) {
      const char* str = "import * as std from 'std';\nimport * as os from "
                        "'os';\nglobalThis.std = "
                        "std;\nglobalThis.os "
                        "= os;\nglobalThis.setTimeout = "
                        "os.setTimeout;\nglobalThis.clearTimeout = "
                        "os.clearTimeout;\n";
      js_eval_str(ctx, str, 0, JS_EVAL_TYPE_MODULE);
    }

    // jsm_list_modules(ctx);

    {
      char** name;
      JSModuleDef* m;
      vector_foreach_t(&module_list, name) {
        if(!(m = js_module_import_namespace(ctx, *name, 0))) {
          /* if((m = jsm_module_loader(ctx, *name, 0))) {
                    JSValue exports = module_exports(ctx, m);
                    JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), *name, exports);
                  } else {*/
          fprintf(stderr, "error loading module '%s'\n", *name);
          jsm_dump_error(ctx);
          exit(1);
        }
        free(*name);
      }
      vector_free(&module_list);
    }

    for(i = 0; i < include_count; i++) {
      if(jsm_load_script(ctx, include_list[i], module) == -1)
        goto fail;
    }

    if(expr) {
      if(js_eval_str(ctx, expr, "<cmdline>", 0) == -1)
        goto fail;
    } else if(optind >= argc) {
      /* interactive mode */
      interactive = 1;
    } else {
      const char* filename;
      filename = argv[optind];
      if(jsm_load_script(ctx, filename, module) == -1) {
        js_value_fwrite(ctx, JS_GetException(ctx), stderr);
        goto fail;
      }
    }
    if(interactive) {
      char str[512];
      const char* home = getenv("HOME");

      snprintf(str,
               sizeof(str),
               "import REPL from 'repl';\n"
               "import fs from 'fs';\n"
               "import { Console } from 'console';\n"
               "const history = '%s/.%s_history';\n"
               "globalThis.console = new Console({ inspectOptions: { customInspect: true } });\n"
               "globalThis.repl = new REPL('qjsm');\n"
               "repl.fs = fs;\n"
               "repl.show = console.log;\n"
               "repl.historyLoad(history);\n"
               "repl.addCleanupHandler(() => repl.historySave(history));\n"
               "repl.runSync();\n",
               home,
               exename);
      printf("str: %s\n", str);
      js_eval_binary(ctx, qjsc_repl, qjsc_repl_size, 0);
      js_eval_str(ctx, str, 0, JS_EVAL_TYPE_MODULE);
    }

    js_std_loop(ctx);
  }

  if(!JS_IsNull(ctx->rt->current_exception))
    jsm_dump_error(ctx);

  if(dump_memory) {
    JSMemoryUsage stats;
    JS_ComputeMemoryUsage(rt, &stats);
    JS_DumpMemoryUsage(stdout, &stats, rt);
  }
  js_std_free_handlers(rt);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  if(empty_run && dump_memory) {
    clock_t t[5];
    double best[5];
    int i, j;
    for(i = 0; i < 100; i++) {
      t[0] = clock();
      rt = JS_NewRuntime();
      t[1] = clock();
      ctx = JS_NewContext(rt);
      t[2] = clock();
      JS_FreeContext(ctx);
      t[3] = clock();
      JS_FreeRuntime(rt);
      t[4] = clock();
      for(j = 4; j > 0; j--) {
        double ms = 1000.0 * (t[j] - t[j - 1]) / CLOCKS_PER_SEC;
        if(i == 0 || best[j] > ms)
          best[j] = ms;
      }
    }
    printf("\nInstantiation times (ms): %.3f = %.3f+%.3f+%.3f+%.3f\n", best[1] + best[2] + best[3] + best[4], best[1], best[2], best[3], best[4]);
  }
  return 0;
fail:
  js_std_free_handlers(rt);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);
  return 1;
}
