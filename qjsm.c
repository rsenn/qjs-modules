#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*
 * QuickJS stand alone interpreter
 *
 * Copyright (c) 2017-2021 Fabrice Bellard
 * Copyright (c) 2017-2021 Charlie Gordon
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following states:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
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
#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#endif

#ifdef HAVE_QUICKJS_CONFIG_H
#include "quickjs-config.h"
#endif

#include "cutils.h"
#include "utils.h"
#include "quickjs-libc.h"
#include "quickjs-internal.h"

#ifdef HAVE_MALLOC_USABLE_SIZE
#ifndef HAVE_MALLOC_USABLE_SIZE_DEFINITION
extern size_t malloc_usable_size();
#endif
#endif

#define trim_dotslash(str) (!strncmp((str), "./", 2) ? (str) + 2 : (str))

extern const uint8_t qjsc_repl[];
extern const uint32_t qjsc_repl_size;
extern const uint8_t qjsc_console[];
extern const uint32_t qjsc_console_size;
extern const uint8_t qjsc_require[];
extern const uint32_t qjsc_require_size;
extern const uint8_t qjsc_fs[];
extern const uint32_t qjsc_fs_size;
extern const uint8_t qjsc_perf_hooks[];
extern const uint32_t qjsc_perf_hooks_size;
extern const uint8_t qjsc_process[];
extern const uint32_t qjsc_process_size;
extern const uint8_t qjsc_util[];
extern const uint32_t qjsc_util_size;
#ifdef CONFIG_BIGNUM
extern const uint8_t qjsc_qjscalc[];
extern const uint32_t qjsc_qjscalc_size;
static int bignum_ext = 1;
#endif

JSModuleDef* js_init_module_child_process(JSContext*, const char*);
JSModuleDef* js_init_module_deep(JSContext*, const char*);
JSModuleDef* js_init_module_inspect(JSContext*, const char*);
JSModuleDef* js_init_module_lexer(JSContext*, const char*);
JSModuleDef* js_init_module_misc(JSContext*, const char*);
JSModuleDef* js_init_module_mmap(JSContext*, const char*);
JSModuleDef* js_init_module_path(JSContext*, const char*);
JSModuleDef* js_init_module_pointer(JSContext*, const char*);
JSModuleDef* js_init_module_predicate(JSContext*, const char*);
JSModuleDef* js_init_module_repeater(JSContext*, const char*);
JSModuleDef* js_init_module_tree_walker(JSContext*, const char*);
JSModuleDef* js_init_module_xml(JSContext*, const char*);

static JSValue
jsm_load_package_json(JSContext* ctx, const char* filename) {
  uint8_t* buf;
  size_t buf_len;
  JSValue ret;

  if(filename == 0)
    filename = "package.json";

  if(!(buf = js_load_file(ctx, &buf_len, filename)))
    return JS_NULL;

  ret = JS_ParseJSON(ctx, buf, buf_len, filename);

  return ret;
}

static JSValue
jsm_module_exports(JSContext* ctx, JSModuleDef* module) {
  JSValue exports = JS_NewObject(ctx);
  size_t i;

  for(i = 0; i < module->export_entries_count; i++) {
    JSExportEntry* entry = &module->export_entries[i];
    JSVarRef* ref = entry->u.local.var_ref;

    if(ref) {
      JSValue export = JS_DupValue(ctx, ref->pvalue ? *ref->pvalue : ref->value);
      JS_SetProperty(ctx, exports, entry->export_name, export);
    }
  }
  return exports;
}

static JSModuleDef*
jsm_module_find(JSContext* ctx, const char* name) {
  struct list_head* el;
  size_t namelen = strlen(name);
  list_for_each(el, &ctx->loaded_modules) {
    JSModuleDef* m = list_entry(el, JSModuleDef, link);
    const char *n, *str = JS_AtomToCString(ctx, m->module_name);
    size_t len;
    n = basename(str);
    len = str_rchr(n, '.');
    // printf("jsm_module_find %s\n", n);
    if(!strcmp(str, name) || !strcmp(n, name) || (len == namelen && !strncmp(n, name, len)))
      return m;

    JS_FreeCString(ctx, str);
  }
  return 0;
}

static JSModuleDef*
jsm_module_get(JSContext* ctx, JSValueConst value) {
  JSModuleDef* m = 0;
  if(JS_IsString(value)) {
    const char* name = JS_ToCString(ctx, value);

    m = jsm_module_find(ctx, name);

    JS_FreeCString(ctx, name);
  } else if(JS_VALUE_GET_TAG(value) == JS_TAG_MODULE) {
    m = JS_VALUE_GET_PTR(value);
  }
  return m;
}

static JSValue
jsm_module_list(JSContext* ctx, JSValueConst this_val) {
  struct list_head* el;
  JSValue ret = JS_NewArray(ctx);
  uint32_t i = 0;
  list_for_each(el, &ctx->loaded_modules) {
    JSModuleDef* m = list_entry(el, JSModuleDef, link);
    JSValue moduleName = JS_AtomToValue(ctx, m->module_name);
    const char* str = JS_ToCString(ctx, moduleName);

    if(str[0] != '<')
      JS_SetPropertyUint32(ctx, ret, i++, JS_DupValue(ctx, JS_MKPTR(JS_TAG_MODULE, m)));

    JS_FreeCString(ctx, str);
    JS_FreeValue(ctx, moduleName);
  }
  return ret;
}

static void
jsm_dump_obj(JSContext* ctx, FILE* f, JSValueConst val) {
  const char* str;

  str = JS_ToCString(ctx, val);
  if(str) {
    fprintf(f, "%s\n", str);
    JS_FreeCString(ctx, str);
  } else {
    fprintf(f, "[exception]\n");
  }
}

static void
jsm_std_dump_error1(JSContext* ctx, JSValueConst exception_val) {
  JSValue val;
  BOOL is_error;

  is_error = JS_IsError(ctx, exception_val);
  jsm_dump_obj(ctx, stderr, exception_val);
  if(is_error) {
    val = JS_GetPropertyStr(ctx, exception_val, "stack");
    if(!JS_IsUndefined(val)) {
      jsm_dump_obj(ctx, stderr, val);
    }
    JS_FreeValue(ctx, val);
  }
}

void
jsm_std_dump_error(JSContext* ctx, JSValue exception_val) {

  if(!JS_IsNull(exception_val))
    jsm_std_dump_error1(ctx, exception_val);
  JS_FreeValue(ctx, exception_val);
}

/* main loop which calls the user JS callbacks */
void
jsm_loop(JSContext* ctx) {
  JSContext* ctx1;
  int err;

  for(;;) {
    /* execute the pending jobs */
    for(;;) {
      err = JS_ExecutePendingJob(JS_GetRuntime(ctx), &ctx1);
      if(err <= 0) {
        if(err < 0) {
          jsm_std_dump_error(ctx1, JS_GetException(ctx));
        }
        break;
      }
    }

    /*if(!os_poll_func || os_poll_func(ctx))
      break;*/
  }
}
#include "quickjs.h"
#include "quickjs-libc.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const char jsm_default_module_path[] = "."
#ifdef CONFIG_PREFIX
                                       ":" CONFIG_PREFIX "/lib/quickjs"
#endif
    ;

char*
jsm_find_module_ext(JSContext* ctx, const char* module_name, const char* ext) {
  const char *module_path, *p, *q;
  char* filename = NULL;
  size_t n, m;
  struct stat st;

  if((module_path = getenv("QUICKJS_MODULE_PATH")) == NULL)
    module_path = jsm_default_module_path;

  for(p = module_path; *p; p = q) {
    if((q = strchr(p, ':')) == NULL)
      q = p + strlen(p);
    n = q - p;
    filename = js_malloc(ctx, n + 1 + strlen(module_name) + 3 + 1);
    strncpy(filename, p, n);
    filename[n] = '/';
    strcpy(&filename[n + 1], module_name);
    m = strlen(module_name);
    if(!(m >= 3 && !strcmp(&module_name[m - 3], ext)))
      strcpy(&filename[n + 1 + m], ext);
    if(!stat(filename, &st))
      return filename;
    js_free(ctx, filename);
    if(*q == ':')
      ++q;
  }
  return NULL;
}

char*
jsm_find_module(JSContext* ctx, const char* module_name) {
  char* ret = NULL;
  size_t len;

  while(!strncmp(module_name, "./", 2)) module_name = trim_dotslash(module_name);
  len = strlen(module_name);

  if(strchr(module_name, '/') == NULL || (len >= 3 && !strcmp(&module_name[len - 3], ".so")))
    ret = jsm_find_module_ext(ctx, module_name, ".so");

  if(ret == NULL)
    ret = jsm_find_module_ext(ctx, module_name, ".js");
  return ret;
}

JSModuleDef*
jsm_module_loader_path(JSContext* ctx, const char* module_name, void* opaque) {
  char *module, *filename = 0;
  JSModuleDef* ret = NULL;
  module = js_strdup(ctx, trim_dotslash(module_name));
  for(;;) {
    if(!strchr(module, '/') && (ret = jsm_module_find(ctx, module))) {
      // printf("jsm_module_loader_path %s %s\n", trim_dotslash(module_name), trim_dotslash(module));
      return ret;
    }
    if(!filename) {
      JSValue package = jsm_load_package_json(ctx, 0);
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
            js_free(ctx, module);
            module = js_strdup(ctx, str);
            JS_FreeCString(ctx, str);
            continue;
          }
        }
      }
    }
    if(!filename)
      filename = module[0] == '/' ? js_strdup(ctx, module) : jsm_find_module(ctx, module);
    break;
  }
  if(filename) {
    // printf("jsm_module_loader_path %s %s\n", trim_dotslash(module_name), trim_dotslash(filename));
    ret = js_module_loader(ctx, filename, opaque);
    js_free(ctx, filename);
  }
  js_free(ctx, module);
  return ret;
}

JSValue
jsm_eval_binary(JSContext* ctx, const uint8_t* buf, size_t buf_len, int load_only) {
  JSValue obj, val;
  obj = JS_ReadObject(ctx, buf, buf_len, JS_READ_OBJ_BYTECODE);

  if(JS_IsException(obj))
    return obj;

  if(JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE) {
    if(!load_only && JS_ResolveModule(ctx, obj) < 0) {
      JS_FreeValue(ctx, obj);
      return JS_ThrowInternalError(ctx, "Failed resolving module");
    }
    js_module_set_import_meta(ctx, obj, FALSE, !load_only);
    val = JS_EvalFunction(ctx, obj);
  }
  return obj;
}

static JSValue
jsm_eval_buf(JSContext* ctx, const char* buf, int buf_len, const char* filename, int module) {
  JSValue val;

  if(module) {
    /* for the modules, we compile then run to be able to set
       import.meta */
    val = JS_Eval(ctx, buf, buf_len, filename, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

    if(JS_IsException(val)) {
      if(JS_IsNull(JS_GetRuntime(ctx)->current_exception)) {
        JS_GetException(ctx);
        val = JS_UNDEFINED;
      }
    }

    if(!JS_IsException(val)) {
      js_module_set_import_meta(ctx, val, FALSE, TRUE);
      /*val =*/JS_EvalFunction(ctx, val);
    }
  } else {
    val = JS_Eval(ctx, buf, buf_len, filename, module & (~(JS_EVAL_TYPE_MODULE)));
  }

  return val;
}

static int
jsm_eval_str(JSContext* ctx, const char* str, const char* filename, int module) {
  JSValue val = jsm_eval_buf(ctx, str, strlen(str), filename, module);
  int32_t ret = -1;

  if(JS_IsNumber(val))
    JS_ToInt32(ctx, &ret, val);
  return ret;
}

static JSValue
jsm_eval_file(JSContext* ctx, const char* filename, int module) {
  uint8_t* buf;
  size_t buf_len;
  int eval_flags;

  if(!(buf = js_load_file(ctx, &buf_len, filename))) {
    return JS_ThrowInternalError(ctx, "Failed loading '%s': %s", filename, strerror(errno));
    /* perror(filename);
     exit(1);*/
  }

  if(module < 0)
    module = (has_suffix(filename, ".mjs") || JS_DetectModule((const char*)buf, buf_len));

  if(module)
    eval_flags = JS_EVAL_TYPE_MODULE;
  else
    eval_flags = JS_EVAL_TYPE_GLOBAL;

  return jsm_eval_buf(ctx, buf, buf_len, filename, eval_flags);
}

static int
jsm_load_script(JSContext* ctx, const char* filename, int module) {
  JSValue val;
  int32_t ret = 0;
  val = jsm_eval_file(ctx, filename, module);
  if(JS_IsException(val)) {
    jsm_std_dump_error(ctx, JS_GetException(ctx));
    return -1;
  }
  if(JS_IsNumber(val))
    JS_ToInt32(ctx, &ret, val);
  if(JS_VALUE_GET_TAG(val) != JS_TAG_MODULE)
    JS_FreeValue(ctx, val);
  return ret;
}

/* also used to initialize the worker context */
static JSContext*
JS_NewCustomContext(JSRuntime* rt) {
  JSContext* ctx;
  ctx = JS_NewContext(rt);
  if(!ctx)
    return NULL;
#ifdef CONFIG_BIGNUM
  if(bignum_ext) {
    JS_AddIntrinsicBigFloat(ctx);
    JS_AddIntrinsicBigDecimal(ctx);
    JS_AddIntrinsicOperators(ctx);
    JS_EnableBignumExt(ctx, TRUE);
  }
#endif
  /* system modules */
  js_init_module_std(ctx, "std");
  js_init_module_os(ctx, "os");
  js_init_module_child_process(ctx, "child_process");
  js_init_module_deep(ctx, "deep");
  js_init_module_inspect(ctx, "inspect");
  js_init_module_lexer(ctx, "lexer");
  js_init_module_misc(ctx, "misc");
  js_init_module_mmap(ctx, "mmap");
  js_init_module_path(ctx, "path");
  js_init_module_pointer(ctx, "pointer");
  js_init_module_predicate(ctx, "predicate");
  js_init_module_repeater(ctx, "repeater");
  js_init_module_tree_walker(ctx, "tree_walker");
  js_init_module_xml(ctx, "xml");

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
        if(ptr == NULL) {
          printf("NULL");
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
    return NULL;
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
      return NULL;
    return jsm_trace_malloc(s, size);
  }
  old_size = jsm_trace_malloc_usable_size(ptr);
  if(size == 0) {
    jsm_trace_malloc_printf(s, "R %zd %p\n", size, ptr);
    s->malloc_count--;
    s->malloc_size -= old_size + MALLOC_OVERHEAD;
    free(ptr);
    return NULL;
  }
  if(s->malloc_size + size - old_size > s->malloc_limit)
    return NULL;

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
#elif defined(EMSCRIPTEN) || defined(__dietlibc__) || defined(__MSYS__) ||                                             \
    defined(DONT_HAVE_MALLOC_USABLE_SIZE_DEFINITION)
    NULL,
#elif defined(__linux__) || defined(HAVE_MALLOC_USABLE_SIZE)
    (size_t(*)(const void*))malloc_usable_size,
#else
    /* change this to `NULL,` if compilation fails */
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
         "-m  --module       load as ES6 module (default=autodetect)\n"
         "    --script       load as ES6 script (default=autodetect)\n"
         "-I  --include file include an additional file\n"
         "    --std          make 'std' and 'os' available to the loaded script\n"
#ifdef CONFIG_BIGNUM
         "    --no-bignum    disable the bignum extensions (BigFloat, BigDecimal)\n"
         "    --qjscalc      load the QJSCalc runtime (default if invoked as qjscalc)\n"
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
js_eval_script(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  const char* str;
  size_t len;
  JSValue ret;
  int32_t module;
  str = JS_ToCStringLen(ctx, &len, argv[0]);
  if(argc > 1)
    JS_ToInt32(ctx, &module, argv[1]);
  else
    module = str_end(str, ".mjs");
  switch(magic) {
    case 0: {
      ret = jsm_eval_file(ctx, str, module);
      break;
    }
    case 1: {
      ret = jsm_eval_buf(ctx, str, len, "<input>", module);
      break;
    }
  }
  if(JS_IsException(ret)) {
    if(JS_IsNull(JS_GetRuntime(ctx)->current_exception)) {
      JS_GetException(ctx);
      ret = JS_UNDEFINED;
    }
  }
  if(JS_VALUE_GET_TAG(ret) == JS_TAG_MODULE) {
    JSModuleDef* module = JS_VALUE_GET_PTR(ret);
    JSValue exports, obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "name", js_module_name(ctx, ret));
    JS_SetPropertyStr(ctx, obj, "exports", jsm_module_exports(ctx, module));
    ret = obj;
  }
  JS_FreeCString(ctx, str);
  return ret;
}

enum {
  FIND_MODULE,
  GET_MODULE_NAME,
  GET_MODULE_OBJECT,
  GET_MODULE_EXPORTS,
  GET_MODULE_NAMESPACE,
  GET_MODULE_FUNCTION,
  GET_MODULE_EXCEPTION,
  GET_MODULE_META_OBJ
};

static JSValue
js_module_func(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_EXCEPTION;
  JSModuleDef* m;
  switch(magic) {

    case FIND_MODULE: {
      const char* name = JS_ToCString(ctx, argv[0]);
      m = jsm_module_find(ctx, name);
      JS_FreeCString(ctx, name);
      ret = JS_DupValue(ctx, JS_MKPTR(JS_TAG_MODULE, m));
      break;
    }
    case GET_MODULE_NAME: {
      if((m = jsm_module_get(ctx, argv[0])))
        ret = js_module_name(ctx, argv[0]);
      break;
    }
    case GET_MODULE_OBJECT: {
      if((m = jsm_module_get(ctx, argv[0]))) {
        ret = JS_NewObject(ctx);

        JS_SetPropertyStr(ctx, ret, "name", js_module_name(ctx, argv[0]));
        JS_SetPropertyStr(ctx, ret, "resolved", JS_NewBool(ctx, m->resolved));
        JS_SetPropertyStr(ctx, ret, "func_created", JS_NewBool(ctx, m->func_created));
        JS_SetPropertyStr(ctx, ret, "instantiated", JS_NewBool(ctx, m->instantiated));
        JS_SetPropertyStr(ctx, ret, "evaluated", JS_NewBool(ctx, m->evaluated));
        if(m->eval_has_exception)
          JS_SetPropertyStr(ctx, ret, "exception", JS_DupValue(ctx, m->eval_exception));
        if(!JS_IsUndefined(m->module_ns))
          JS_SetPropertyStr(ctx, ret, "namespace", JS_DupValue(ctx, m->module_ns));
        if(!JS_IsUndefined(m->func_obj))
          JS_SetPropertyStr(ctx, ret, "func", JS_DupValue(ctx, m->func_obj));
        if(!JS_IsUndefined(m->meta_obj))
          JS_SetPropertyStr(ctx, ret, "meta", JS_DupValue(ctx, m->meta_obj));
      }
      break;
    }
    case GET_MODULE_EXPORTS: {
      if((m = jsm_module_get(ctx, argv[0])))
        ret = jsm_module_exports(ctx, m);
      break;
    }
    case GET_MODULE_NAMESPACE: {
      if((m = jsm_module_get(ctx, argv[0])))
        ret = JS_DupValue(ctx, m->module_ns);
      break;
    }
    case GET_MODULE_FUNCTION: {
      if((m = jsm_module_get(ctx, argv[0]))) {
        if(m->func_created)
          ret = JS_DupValue(ctx, m->func_obj);
        else
          ret = JS_NULL;
      }
      break;
    }
    case GET_MODULE_EXCEPTION: {
      if((m = jsm_module_get(ctx, argv[0]))) {
        if(m->eval_has_exception)
          ret = JS_DupValue(ctx, m->eval_exception);
        else
          ret = JS_NULL;
      }
      break;
    }
    case GET_MODULE_META_OBJ: {
      if((m = jsm_module_get(ctx, argv[0])))
        ret = JS_DupValue(ctx, m->meta_obj);
      break;
    }
  }
  return ret;
}

static const JSCFunctionListEntry jsm_global_funcs[] = {
    JS_CFUNC_MAGIC_DEF("evalFile", 1, js_eval_script, 0),
    JS_CFUNC_MAGIC_DEF("evalScript", 1, js_eval_script, 1),
    JS_CGETSET_DEF("moduleList", jsm_module_list, 0),
    JS_CFUNC_MAGIC_DEF("findModule", 1, js_module_func, FIND_MODULE),
    JS_CFUNC_MAGIC_DEF("getModuleName", 1, js_module_func, GET_MODULE_NAME),
    JS_CFUNC_MAGIC_DEF("getModuleObject", 1, js_module_func, GET_MODULE_OBJECT),
    JS_CFUNC_MAGIC_DEF("getModuleExports", 1, js_module_func, GET_MODULE_EXPORTS),
    JS_CFUNC_MAGIC_DEF("getModuleNamespace", 1, js_module_func, GET_MODULE_NAMESPACE),
    JS_CFUNC_MAGIC_DEF("getModuleFunction", 1, js_module_func, GET_MODULE_FUNCTION),
    JS_CFUNC_MAGIC_DEF("getModuleException", 1, js_module_func, GET_MODULE_EXCEPTION),
    JS_CFUNC_MAGIC_DEF("getModuleMetaObject", 1, js_module_func, GET_MODULE_META_OBJ),
};

int
main(int argc, char** argv) {
  JSRuntime* rt;
  JSContext* ctx;
  struct trace_malloc_data trace_data = {NULL};
  int optind;
  char* expr = NULL;
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
        module = 1;
        break;
      }
      if(!strcmp(longopt, "script")) {
        module = 0;
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
        memory_limit = (size_t)strtod(argv[optind++], NULL);
        break;
      }
      if(!strcmp(longopt, "stack-size")) {
        if(optind >= argc) {
          fprintf(stderr, "expecting stack size");
          exit(1);
        }
        stack_size = (size_t)strtod(argv[optind++], NULL);
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
  if(memory_limit != 0)
    JS_SetMemoryLimit(rt, memory_limit);
  // if (stack_size != 0)
  JS_SetMaxStackSize(rt, stack_size != 0 ? stack_size : 256 * 1048576);
  js_std_set_worker_new_context_func(JS_NewCustomContext);
  js_std_init_handlers(rt);
  ctx = JS_NewCustomContext(rt);
  if(!ctx) {
    fprintf(stderr, "%s: cannot allocate JS context\n", exename);
    exit(2);
  }

  /* loader for ES6 modules */
  JS_SetModuleLoaderFunc(rt, NULL, jsm_module_loader_path, NULL);

  if(dump_unhandled_promise_rejection) {
    JS_SetHostPromiseRejectionTracker(rt, js_std_promise_rejection_tracker, NULL);
  }

  if(!empty_run) {
#ifdef CONFIG_BIGNUM
    if(load_jscalc) {
      jsm_eval_binary(ctx, qjsc_qjscalc, qjsc_qjscalc_size, 0);
    }
#endif
    js_std_add_helpers(ctx, argc - optind, argv + optind);

    jsm_eval_binary(ctx, qjsc_fs, qjsc_fs_size, 0);
    jsm_eval_binary(ctx, qjsc_perf_hooks, qjsc_perf_hooks_size, 0);
    jsm_eval_binary(ctx, qjsc_process, qjsc_process_size, 0);
    {
      const char* str = "import process from 'process';\nglobalThis.process = process;\n";
      jsm_eval_str(ctx, str, "<input>", TRUE);
    }
    jsm_eval_binary(ctx, qjsc_util, qjsc_util_size, 0);
    jsm_eval_binary(ctx, qjsc_console, qjsc_console_size, 0);
    jsm_eval_binary(ctx, qjsc_require, qjsc_require_size, 0);
    /*
        {
          const char* str = "import Console from 'console';\nglobalThis.console = new Console();\n";
          jsm_eval_str(ctx, str, "<input>", TRUE);
        }
        jsm_eval_binary(ctx, qjsc_require, qjsc_require_size, 0);

        {
          const char* str = "import require from 'require';\nglobalThis.require = require;\n";
          jsm_eval_str(ctx, str, "<input>", TRUE);
        }*/

    /* make 'std' and 'os' visible to non module code */

    JS_SetPropertyFunctionList(ctx, JS_GetGlobalObject(ctx), jsm_global_funcs, countof(jsm_global_funcs));
    if(load_std) {
      const char* str = "import * as std from 'std';\nimport * as os from 'os';\nglobalThis.std = std;\nglobalThis.os "
                        "= os;\nglobalThis.setTimeout = os.setTimeout;\nglobalThis.clearTimeout = os.clearTimeout;\n";
      jsm_eval_str(ctx, str, "<input>", TRUE);
    }

    for(i = 0; i < include_count; i++) {
      if(jsm_load_script(ctx, include_list[i], module) == -1)
        goto fail;
    }

    if(expr) {
      if(jsm_eval_str(ctx, expr, "<cmdline>", 0) == -1)
        goto fail;
    } else if(optind >= argc) {
      /* interactive mode */
      interactive = 1;
    } else {
      const char* filename;
      filename = argv[optind];
      if(jsm_load_script(ctx, filename, module) == -1)
        goto fail;
    }
    if(interactive) {
      jsm_eval_binary(ctx, qjsc_repl, qjsc_repl_size, 0);
    }
    js_std_loop(ctx);
  }

  {

    JSValue exception = JS_GetException(ctx);

    if(!JS_IsNull(exception)) {
      jsm_std_dump_error(ctx, exception);
    }
  }

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
    printf("\nInstantiation times (ms): %.3f = %.3f+%.3f+%.3f+%.3f\n",
           best[1] + best[2] + best[3] + best[4],
           best[1],
           best[2],
           best[3],
           best[4]);
  }
  return 0;
fail:
  js_std_free_handlers(rt);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);
  return 1;
}
