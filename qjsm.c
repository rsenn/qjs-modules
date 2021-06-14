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
#include <dlfcn.h>
#include <time.h>
#include <threads.h>
#include <signal.h>
#include <sys/poll.h>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#endif

#if 1 // def HAVE_QUICKJS_CONFIG_H
#include "quickjs-config.h"
#endif

#ifdef USE_WORKER
#include <pthread.h>
#include <stdatomic.h>

static int
atomic_add_int(int* ptr, int v) {
  return atomic_fetch_add((_Atomic(uint32_t)*)ptr, v) + v;
}
#endif

#include "list.h"
#include "cutils.h"
#include "utils.h"
#include "vector.h"
#include "quickjs-libc.h"
#include "quickjs-internal.h"

typedef struct pollhandler {
  struct pollfd pf;
  void (*handler)(void* opaque, struct pollfd*);
  void* opaque;
  struct list_head link;
} pollhandler_t;

thread_local uint64_t jsm_pending_signals = 0;
struct list_head pollhandlers;

void js_std_set_module_loader_func(JSModuleLoaderFunc* func);

#ifdef HAVE_MALLOC_USABLE_SIZE
#ifndef HAVE_MALLOC_USABLE_SIZE_DEFINITION
extern size_t malloc_usable_size();
#endif
#endif

#define trim_dotslash(str) (!strncmp((str), "./", 2) ? (str) + 2 : (str))

#define jsm_declare_module(name)                                                                                       \
  extern const uint8_t qjsc_##name[];                                                                                  \
  extern const uint32_t qjsc_##name##_size;                                                                            \
  JSModuleDef* js_init_module_##name(JSContext*, const char*);

jsm_declare_module(console);
jsm_declare_module(events);
jsm_declare_module(fs);
jsm_declare_module(perf_hooks);
jsm_declare_module(process);
jsm_declare_module(repl);
jsm_declare_module(require);
jsm_declare_module(tty);
jsm_declare_module(util);

#ifdef CONFIG_BIGNUM
jsm_declare_module(qjscalc);
static int bignum_ext = 1;
#endif

void js_std_set_worker_new_context_func(JSContext* (*func)(JSRuntime* rt));

void jsm_std_dump_error(JSContext* ctx, JSValue exception_val);

static Vector module_list = VECTOR_INIT();
static Vector builtins = VECTOR_INIT();

static int64_t
jsm_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

int
jsm_interrupt_handler(JSRuntime* rt, void* opaque) {
  return (jsm_pending_signals >> SIGINT) & 1;
}

void
jsm_unlink_timer(JSRuntime* rt, JSOSTimer* th) {
  if(th->link.prev) {
    list_del(&th->link);
    th->link.prev = th->link.next = NULL;
  }
}

void
jsm_free_timer(JSRuntime* rt, JSOSTimer* th) {
  JS_FreeValueRT(rt, th->func);
  js_free_rt(rt, th);
}

void
jsm_call_handler(JSContext* ctx, JSValueConst func) {
  JSValue ret, func1;
  /* 'func' might be destroyed when calling itself (if it frees the
     handler), so must take extra care */
  func1 = JS_DupValue(ctx, func);
  ret = JS_Call(ctx, func1, JS_UNDEFINED, 0, NULL);
  JS_FreeValue(ctx, func1);
  if(JS_IsException(ret))
    jsm_std_dump_error(ctx, JS_GetException(ctx));
  JS_FreeValue(ctx, ret);
}

void
jsm_sab_free(void* opaque, void* ptr) {
  JSSABHeader* sab;
  int ref_count;
  sab = (JSSABHeader*)((uint8_t*)ptr - sizeof(JSSABHeader));
  ref_count = atomic_add_int(&sab->ref_count, -1);
  assert(ref_count >= 0);
  if(ref_count == 0) {
    free(sab);
  }
}

void
jsm_free_message(JSWorkerMessage* msg) {
  size_t i;
  /* free the SAB */
  for(i = 0; i < msg->sab_tab_len; i++) { jsm_sab_free(NULL, msg->sab_tab[i]); }
  free(msg->sab_tab);
  free(msg->data);
  free(msg);
}

/* return 1 if a message was handled, 0 if no message */
static int
jsm_handle_posted_message(JSRuntime* rt, JSContext* ctx, JSWorkerMessageHandler* port) {
  JSWorkerMessagePipe* ps = port->recv_pipe;
  int ret;
  struct list_head* el;
  JSWorkerMessage* msg;
  JSValue obj, data_obj, func, retval;

  pthread_mutex_lock(&ps->mutex);
  if(!list_empty(&ps->msg_queue)) {
    el = ps->msg_queue.next;
    msg = list_entry(el, JSWorkerMessage, link);

    /* remove the message from the queue */
    list_del(&msg->link);

    if(list_empty(&ps->msg_queue)) {
      uint8_t buf[16];
      int ret;
      for(;;) {
        ret = read(ps->read_fd, buf, sizeof(buf));
        if(ret >= 0)
          break;
        if(errno != EAGAIN && errno != EINTR)
          break;
      }
    }

    pthread_mutex_unlock(&ps->mutex);

    data_obj = JS_ReadObject(ctx, msg->data, msg->data_len, JS_READ_OBJ_SAB | JS_READ_OBJ_REFERENCE);

    jsm_free_message(msg);

    if(JS_IsException(data_obj))
      goto fail;
    obj = JS_NewObject(ctx);
    if(JS_IsException(obj)) {
      JS_FreeValue(ctx, data_obj);
      goto fail;
    }
    JS_DefinePropertyValueStr(ctx, obj, "data", data_obj, JS_PROP_C_W_E);

    /* 'func' might be destroyed when calling itself (if it frees the
       handler), so must take extra care */
    func = JS_DupValue(ctx, port->on_message_func);
    retval = JS_Call(ctx, func, JS_UNDEFINED, 1, (JSValueConst*)&obj);
    JS_FreeValue(ctx, obj);
    JS_FreeValue(ctx, func);
    if(JS_IsException(retval)) {
    fail:
      js_std_dump_error(ctx);
    } else {
      JS_FreeValue(ctx, retval);
    }
    ret = 1;
  } else {
    pthread_mutex_unlock(&ps->mutex);
    ret = 0;
  }
  return ret;
}

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
char*
jsm_normalize_module(JSContext* ctx, const char* base_name, const char* name, void* opaque) {
  size_t p;
  const char* r;
  DynBuf file = {0, 0, 0};
  size_t n;
  if(name[0] != '.')
    return js_strdup(ctx, name);

  js_dbuf_init(ctx, &file);

  n = base_name[(p = str_rchr(base_name, '/'))] ? p : 0;

  dbuf_put(&file, base_name, n);
  dbuf_0(&file);

  for(r = name;;) {
    if(r[0] == '.' && r[1] == '/') {
      r += 2;
    } else if(r[0] == '.' && r[1] == '.' && r[2] == '/') {
      /* remove the last path element of file, except if "." or ".." */
      if(file.size == 0)
        break;
      if((p = byte_rchr(file.buf, file.size, '/')) < file.size)
        p++;
      else
        p = 0;
      if(!strcmp(&file.buf[p], ".") || !strcmp(&file.buf[p], ".."))
        break;
      if(p > 0)
        p--;
      file.size = p;
      r += 3;
    } else {
      break;
    }
  }
  if(file.size == 0)
    dbuf_putc(&file, '.');

  dbuf_putc(&file, '/');
  dbuf_putstr(&file, r);
  dbuf_0(&file);

  // printf("jsm_normalize_module\x1b[1;48;5;27m(1)\x1b[0m %-40s %-40s -> %s\n", base_name, name, file.buf);

  return file.buf;
}
static JSModuleDef*
jsm_module_loader_so(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  void* hd;
  JSModuleDef* (*init)(JSContext*, const char*);
  char* filename;

  if(!strchr(module_name, '/')) {
    /* must add a '/' so that the DLL is not searched in the
       system library paths */
    filename = js_malloc(ctx, strlen(module_name) + 2 + 1);
    if(!filename)
      return NULL;
    strcpy(filename, "./");
    strcpy(filename + 2, module_name);
  } else {
    filename = (char*)module_name;
  }
  /* C module */
  hd = dlopen(filename, RTLD_NOW | RTLD_LOCAL);
  if(filename != module_name)
    js_free(ctx, filename);
  if(!hd) {
    JS_ThrowReferenceError(ctx, "could not load module filename '%s' as shared library: %s", module_name, dlerror());
    goto fail;
  }

  init = dlsym(hd, "js_init_module");
  if(!init) {
    JS_ThrowReferenceError(ctx, "could not load module filename '%s': js_init_module not found", module_name);
    goto fail;
  }

  m = init(ctx, module_name);
  if(!m) {
    JS_ThrowReferenceError(ctx, "could not load module filename '%s': initialization error", module_name);
  fail:
    if(hd)
      dlclose(hd);
    return NULL;
  }
  return m;
}

JSModuleDef*
jsm_module_loader_path(JSContext* ctx, const char* module_name, void* opaque) {
  char *module, *filename = 0;
  JSModuleDef* ret = NULL;
  module = js_strdup(ctx, trim_dotslash(module_name));
  for(;;) {
    if(!strchr(module, '/') && (ret = jsm_module_find(ctx, module))) {
       printf("jsm_module_loader_path[%x] %s -> %s\n", pthread_self(), trim_dotslash(module_name), trim_dotslash(module));
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

    if(!filename) {
      if(strchr("./", module[0]))
        filename = js_strdup(ctx, module);
      else
        filename = jsm_find_module(ctx, module);
      continue;
    }

    break;
  }

  if(filename) {
    if(strcmp(trim_dotslash(module_name), trim_dotslash(filename))) printf("jsm_module_loader_path[%x] \x1b[1;48;5;124m(3)\x1b[0m %-40s -> %s\n", pthread_self(), module, filename);
    ret = has_suffix(filename, ".so") ? jsm_module_loader_so(ctx, filename) : js_module_loader(ctx, filename, opaque);
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

static JSModuleDef*
jsm_load_module(JSContext* ctx, const char* name) {
  DynBuf buf;
  JSModuleDef* m;
  js_dbuf_init(ctx, &buf);
  dbuf_printf(&buf, "import * as %s from '%s'; globalThis.%s = %s;", name, name, name, name);
  dbuf_0(&buf);
  jsm_eval_buf(ctx, buf.buf, buf.size, "<input>", TRUE);
  m = jsm_module_find(ctx, name);

  return m;
}

static void
jsm_list_modules(JSContext* ctx) {
  struct list_head* el;
  list_for_each(el, &ctx->loaded_modules) {
    JSModuleDef* m = list_entry(el, JSModuleDef, link);
    const char *n, *str = JS_AtomToCString(ctx, m->module_name);
    size_t len = strlen(str);

    JS_FreeCString(ctx, str);
  }
}

/* also used to initialize the worker context */
static JSContext*
jsm_context_new(JSRuntime* rt) {
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

#define jsm_module_native(name) js_init_module_##name(ctx, #name);

  jsm_module_native(std);
  jsm_module_native(os);
  jsm_module_native(child_process);
  jsm_module_native(deep);
  jsm_module_native(inspect);
  jsm_module_native(lexer);
  jsm_module_native(misc);
  jsm_module_native(mmap);
  jsm_module_native(path);
  jsm_module_native(pointer);
  jsm_module_native(predicate);
  jsm_module_native(repeater);
  jsm_module_native(tree_walker);
  jsm_module_native(xml);
  return ctx;
}

static int
jsm_os_poll(JSContext* ctx, uint32_t timeout) {
  JSRuntime* rt = JS_GetRuntime(ctx);
  JSThreadState* ts = JS_GetRuntimeOpaque(rt);
  int ret, fd_max, min_delay;
  int64_t cur_time, delay;
  fd_set rfds, wfds;
  JSOSRWHandler* rh;
  struct list_head* el;
  struct timeval tv, *tvp;

  /* only check signals in the main thread */
  /* if(!ts->recv_pipe && unlikely(jsm_pending_signals != 0)) {
     JSOSSignalHandler* sh;
     uint64_t mask;

     list_for_each(el, &ts->os_signal_handlers) {
       sh = list_entry(el, JSOSSignalHandler, link);
       mask = (uint64_t)1 << sh->sig_num;
       if(jsm_pending_signals & mask) {
         jsm_pending_signals &= ~mask;
         jsm_call_handler(ctx, sh->func);
         return 0;
       }
     }
   }*/

  if(list_empty(&ts->os_rw_handlers) && list_empty(&ts->os_timers) && list_empty(&ts->port_list) &&
     list_empty(&pollhandlers))
    return -1; /* no more events */

  if(!list_empty(&pollhandlers)) {
    list_for_each(el, &pollhandlers) {
      pollhandler_t* ph = list_entry(el, pollhandler_t, link);
      if(ph->pf.events) {
        if(ph->pf.events & POLLIN)
          FD_SET(ph->pf.fd, &rfds);
        if(ph->pf.events & POLLOUT)
          FD_SET(ph->pf.fd, &wfds);
      }
    }
  }

  if(!list_empty(&ts->os_timers)) {
    cur_time = jsm_time_ms();
    min_delay = 10000;
    list_for_each(el, &ts->os_timers) {
      JSOSTimer* th = list_entry(el, JSOSTimer, link);
      delay = th->timeout - cur_time;
      if(delay <= 0) {
        JSValue func;
        /* the timer expired */
        func = th->func;
        th->func = JS_UNDEFINED;
        jsm_unlink_timer(rt, th);
        if(!th->has_object)
          jsm_free_timer(rt, th);
        jsm_call_handler(ctx, func);
        JS_FreeValue(ctx, func);
        return 0;
      } else if(delay < min_delay) {
        min_delay = delay;
      }
    }
    tv.tv_sec = min_delay / 1000;
    tv.tv_usec = (min_delay % 1000) * 1000;
    tvp = &tv;
  } else {
    if(timeout) {
      tv.tv_sec = timeout / 1000;
      tv.tv_usec = (timeout % 1000) * 1000;
      tvp = &tv;
    } else {
      tvp = NULL;
    }
  }

  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  fd_max = -1;
  list_for_each(el, &ts->os_rw_handlers) {
    rh = list_entry(el, JSOSRWHandler, link);
    fd_max = max_int(fd_max, rh->fd);
    if(!JS_IsNull(rh->rw_func[0]))
      FD_SET(rh->fd, &rfds);
    if(!JS_IsNull(rh->rw_func[1]))
      FD_SET(rh->fd, &wfds);
  }

  list_for_each(el, &ts->port_list) {
    JSWorkerMessageHandler* port = list_entry(el, JSWorkerMessageHandler, link);
    if(!JS_IsNull(port->on_message_func)) {
      JSWorkerMessagePipe* ps = port->recv_pipe;
      fd_max = max_int(fd_max, ps->read_fd);
      FD_SET(ps->read_fd, &rfds);
    }
  }

  ret = select(fd_max + 1, &rfds, &wfds, NULL, tvp);
  if(ret > 0) {

    if(!list_empty(&pollhandlers)) {
      list_for_each(el, &pollhandlers) {
        pollhandler_t* ph = list_entry(el, pollhandler_t, link);
        if(ph->pf.events) {
          ph->pf.revents = (FD_ISSET(ph->pf.fd, &rfds) ? POLLIN : 0) | (FD_ISSET(ph->pf.fd, &wfds) ? POLLOUT : 0);

          if(ph->pf.revents && ph->handler)
            ph->handler(ph->opaque, &ph->pf);
        }
      }
    }
    list_for_each(el, &ts->os_rw_handlers) {
      rh = list_entry(el, JSOSRWHandler, link);
      if(!JS_IsNull(rh->rw_func[0]) && FD_ISSET(rh->fd, &rfds)) {
        jsm_call_handler(ctx, rh->rw_func[0]);
        /* must stop because the list may have been modified */
        goto done;
      }
      if(!JS_IsNull(rh->rw_func[1]) && FD_ISSET(rh->fd, &wfds)) {
        jsm_call_handler(ctx, rh->rw_func[1]);
        /* must stop because the list may have been modified */
        goto done;
      }
    }

    list_for_each(el, &ts->port_list) {
      JSWorkerMessageHandler* port = list_entry(el, JSWorkerMessageHandler, link);
      if(!JS_IsNull(port->on_message_func)) {
        JSWorkerMessagePipe* ps = port->recv_pipe;
        if(FD_ISSET(ps->read_fd, &rfds)) {
          if(jsm_handle_posted_message(rt, ctx, port))
            goto done;
        }
      }
    }
  }
done:
  return 0;
}

/* main loop which calls the user JS callbacks */
void
jsm_std_loop(JSContext* ctx, uint32_t timeout) {
  JSContext* ctx1;
  int err;
  uint64_t t = jsm_time_ms();

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

    if(jsm_os_poll(ctx, timeout))
      break;

    if(timeout > 0 && jsm_time_ms() - t >= timeout)
      break;
  }
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
         "-m  --module NAME  load an ES6 module\n"
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
  LOAD_MODULE,
  RESOLVE_MODULE,
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
    case LOAD_MODULE: {
      const char* name = JS_ToCString(ctx, argv[0]);
      JSModuleDef* m;

      if((m = jsm_load_module(ctx, name)))
        ret = JS_MKPTR(JS_TAG_MODULE, m);

      JS_FreeCString(ctx, name);
      break;
    }
    case RESOLVE_MODULE: {
      ret = JS_NewInt32(ctx, JS_ResolveModule(ctx, argv[0]));
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
        if(TRUE || m->func_created)
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
    JS_CFUNC_MAGIC_DEF("loadModule", 1, js_module_func, LOAD_MODULE),
    JS_CFUNC_MAGIC_DEF("resolveModule", 1, js_module_func, RESOLVE_MODULE),
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

  init_list_head(&pollhandlers);
  js_std_set_module_loader_func(jsm_module_loader_path);

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
        const char* modules = argv[optind++];
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

  js_std_set_worker_new_context_func(jsm_context_new);

  js_std_init_handlers(rt);
  ctx = jsm_context_new(rt);
  if(!ctx) {
    fprintf(stderr, "%s: cannot allocate JS context\n", exename);
    exit(2);
  }

  /* loader for ES6 modules */
  JS_SetModuleLoaderFunc(rt, jsm_normalize_module, jsm_module_loader_path, NULL);

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

    int num_native, num_compiled;

#define jsm_builtin_native(name) vector_putptr(&builtins, #name)

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
    num_native = vector_size(&builtins, sizeof(char*));

    // printf("native builtins: "); dump_vector(&builtins, 0);

#define jsm_builtin_compiled(name)                                                                                     \
  jsm_eval_binary(ctx, qjsc_##name, qjsc_##name##_size, 0);                                                            \
  vector_putptr(&builtins, #name)

    jsm_builtin_compiled(console);
    jsm_builtin_compiled(events);
    jsm_builtin_compiled(fs);
    jsm_builtin_compiled(perf_hooks);
    jsm_builtin_compiled(process);
    // jsm_builtin_compiled(repl);
    jsm_builtin_compiled(require);
    jsm_builtin_compiled(tty);
    jsm_builtin_compiled(util);

    num_compiled = vector_size(&builtins, sizeof(char*)) - num_native;

    {
      const char* str = "import process from 'process';\nglobalThis.process = process;\n";
      jsm_eval_str(ctx, str, "<input>", TRUE);
    }

    JS_SetPropertyFunctionList(ctx, JS_GetGlobalObject(ctx), jsm_global_funcs, countof(jsm_global_funcs));
    if(load_std) {
      const char* str = "import * as std from 'std';\nimport * as os from 'os';\nglobalThis.std = std;\nglobalThis.os "
                        "= os;\nglobalThis.setTimeout = os.setTimeout;\nglobalThis.clearTimeout = os.clearTimeout;\n";
      jsm_eval_str(ctx, str, "<input>", TRUE);
    }

    // jsm_list_modules(ctx);

    {
      char** name;
      JSModuleDef* m;
      vector_foreach_t(&module_list, name) {
        if(!(m = jsm_load_module(ctx, *name))) {
          fprintf(stderr, "error loading module '%s'\n", *name);
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

    jsm_std_loop(ctx, 0);
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
