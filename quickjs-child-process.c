#include "defines.h"
#include "utils.h"
#include "char-utils.h"
#include "child-process.h"
#include "property-enumeration.h"
#include "debug.h"

/**
 * \defgroup quickjs-child-process quickjs-child_process: Child process
 * @{
 */
#ifdef _WIN32
#include <io.h>
#define pipe(fds) _pipe(fds, 4096, 0)
#else
#include <unistd.h>
#include <sys/wait.h>
#endif
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>

enum {
  CHILD_PROCESS_FILE = 0,
  CHILD_PROCESS_CWD,
  CHILD_PROCESS_ARGS,
  CHILD_PROCESS_ENV,
  CHILD_PROCESS_STDIO,
  CHILD_PROCESS_PID,
  CHILD_PROCESS_EXITED,
  CHILD_PROCESS_EXITCODE,
  CHILD_PROCESS_TERMSIG,
  CHILD_PROCESS_SIGNALED,
  CHILD_PROCESS_STOPPED,
  CHILD_PROCESS_CONTINUED,
};

VISIBLE JSClassID js_child_process_class_id = 0;
VISIBLE JSValue child_process_proto = {{0}, JS_TAG_UNDEFINED}, child_process_ctor = {{0}, JS_TAG_UNDEFINED};

ChildProcess*
js_child_process_data(JSValueConst value) {
  return JS_GetOpaque(value, js_child_process_class_id);
}

ChildProcess*
js_child_process_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_child_process_class_id);
}

JSValue
js_child_process_wrap(JSContext* ctx, ChildProcess* cp) {
  JSValue obj;

  obj = JS_NewObjectProtoClass(ctx, child_process_proto, js_child_process_class_id);
  JS_SetOpaque(obj, cp);
  return obj;
}

static JSValue
js_child_process_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  ChildProcess* cp;
  JSValue proto, obj = JS_UNDEFINED;

  if(!(cp = js_mallocz(ctx, sizeof(ChildProcess))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  obj = JS_NewObjectProtoClass(ctx, proto, js_child_process_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, cp);
  return obj;

fail:
  js_free(ctx, cp);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_child_process_finalizer(JSRuntime* rt, JSValue val) {
  ChildProcess* cp;

  if((cp = JS_GetOpaque(val, js_child_process_class_id)))
    child_process_free_rt(cp, rt);
}

static JSValue
js_child_process_exec(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_UNDEFINED;
}

static int
js_child_process_options(JSContext* ctx, ChildProcess* cp, JSValueConst obj) {
  size_t len;
  int *parent_fds, *child_fds;
  JSValue value = JS_GetPropertyStr(ctx, obj, "env");

  if(JS_IsObject(value))
    cp->env = child_process_environment(ctx, value);
  else
    cp->env = js_strv_dup(ctx, environ);

  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, obj, "cwd");
  if(JS_IsString(value))
    cp->cwd = js_tostring(ctx, value);

  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, obj, "stdio");
  if(JS_IsException(value) || JS_IsUndefined(value))
    value = JS_NewString(ctx, "pipe");

  if(!JS_IsArray(ctx, value)) {
    JSValue a = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, a, 0, JS_DupValue(ctx, value));
    JS_SetPropertyUint32(ctx, a, 1, JS_DupValue(ctx, value));
    JS_SetPropertyUint32(ctx, a, 2, JS_DupValue(ctx, value));
    JS_FreeValue(ctx, value);
    value = a;
  }

  len = js_array_length(ctx, value);
  parent_fds = cp->parent_fds = js_mallocz(ctx, sizeof(int) * (len + 1));
  child_fds = cp->child_fds = js_mallocz(ctx, sizeof(int) * (len + 1));
  cp->num_fds = len;

  for(size_t i = 0; i < len; i++) {
    JSValue item = JS_GetPropertyUint32(ctx, value, i);
    parent_fds[i] = -1;
    child_fds[i] = -1;

    if(JS_IsNumber(item)) {
      int32_t fd;

      JS_ToInt32(ctx, &fd, item);
      child_fds[i] = fd;
    } else if(JS_IsString(item)) {
      const char* s = JS_ToCString(ctx, item);

      if(!strcmp(s, "pipe")) {
        int fds[2];

        if(pipe(fds) == -1)
          fds[0] = fds[1] = -1;

        if(i == 0) {
          child_fds[i] = fds[0];
          parent_fds[i] = fds[1];
        } else {
          child_fds[i] = fds[1];
          parent_fds[i] = fds[0];
        }

      } else if(!strcmp(s, "ignore")) {
        child_fds[i] = open("/dev/null", O_RDWR);
      } else if(!strcmp(s, "inherit")) {
        child_fds[i] = i;
      }

      JS_FreeCString(ctx, s);
    }
  }

  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, obj, "usePath");
  if(JS_IsBool(value))
    cp->use_path = JS_ToBool(ctx, value);

  JS_FreeValue(ctx, value);
  return 0;
}

static JSValue
js_child_process_spawn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  ChildProcess* cp;

  if(!(cp = child_process_new(ctx)))
    return JS_EXCEPTION;

  ret = js_child_process_wrap(ctx, cp);

  if(JS_IsArray(ctx, argv[0])) {
    cp->args = js_array_to_argv(ctx, NULL, argv[0]);

    if(cp->args[0])
      cp->file = js_strdup(ctx, cp->args[0]);
  } else {
    cp->file = js_tostring(ctx, argv[0]);

    if(argc > 1) {
      int n = js_array_length(ctx, argv[1]);

      cp->args = js_mallocz(ctx, sizeof(char*) * (n + 2));
      cp->args[0] = js_strdup(ctx, cp->file);

      js_array_copys(ctx, argv[1], n, &cp->args[1]);

      --argc;
      ++argv;
    } else {
      cp->args = js_malloc(ctx, sizeof(char*) * 2);
      cp->args[0] = js_strdup(ctx, cp->file);
      cp->args[1] = 0;
    }
  }

  if(argc > 1 && JS_IsObject(argv[1]))
    js_child_process_options(ctx, cp, argv[1]);

  child_process_spawn(cp);

  return ret;
}

static JSValue
js_child_process_get(JSContext* ctx, JSValueConst this_val, int magic) {
  ChildProcess* cp;
  JSValue ret = JS_UNDEFINED;

  if(!(cp = js_child_process_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case CHILD_PROCESS_FILE: {
      ret = JS_NewString(ctx, cp->file);
      break;
    }

    case CHILD_PROCESS_CWD: {
      ret = cp->cwd ? JS_NewString(ctx, cp->cwd) : JS_NULL;
      break;
    }

    case CHILD_PROCESS_ARGS: {
      ret = js_strv_to_array(ctx, cp->args);
      break;
    }

    case CHILD_PROCESS_ENV: {
      ret = JS_NewObject(ctx);

      for(char** ptr = cp->env; *ptr; ptr++) {
        size_t namelen = str_chr(*ptr, '=');
        JSAtom key = JS_NewAtomLen(ctx, *ptr, namelen);

        JS_DefinePropertyValue(ctx, ret, key, JS_NewString(ctx, *ptr + namelen + 1), JS_PROP_ENUMERABLE);
        JS_FreeAtom(ctx, key);
      }

      break;
    }

    case CHILD_PROCESS_STDIO: {
      ret = cp->parent_fds ? js_intv_to_array(ctx, cp->parent_fds, cp->num_fds) : JS_NULL;
      break;
    }

    case CHILD_PROCESS_PID: {
      ret = JS_NewInt32(ctx, cp->pid);
      break;
    }

    case CHILD_PROCESS_EXITED: {
      ret = JS_NewBool(ctx, cp->exitcode != -1 || cp->signaled);
      break;
    }

    case CHILD_PROCESS_EXITCODE: {
      if(cp->exitcode != -1)
        ret = JS_NewInt32(ctx, cp->exitcode);

      break;
    }

    case CHILD_PROCESS_SIGNALED: {
      ret = JS_NewBool(ctx, cp->signaled);
      break;
    }

    case CHILD_PROCESS_STOPPED: {
      ret = JS_NewBool(ctx, cp->stopped);
      break;
    }

    case CHILD_PROCESS_CONTINUED: {
      ret = JS_NewBool(ctx, cp->continued);
      break;
    }

    case CHILD_PROCESS_TERMSIG: {
      if(cp->termsig != -1)
        ret = JS_NewInt32(ctx, cp->termsig);

      break;
    }
  }

  return ret;
}

static JSValue
js_child_process_wait(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  ChildProcess* cp;
  int32_t flags = 0;

  if(!(cp = js_child_process_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(argc >= 1)
    JS_ToInt32(ctx, &flags, argv[0]);

  return JS_NewInt32(ctx, child_process_wait(cp, flags));
}

static JSValue
js_child_process_kill(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  ChildProcess* cp;
  int32_t signum = SIGTERM;
  JSValueConst sig = argv[magic], child = magic ? argv[0] : this_val;

  if(!(cp = js_child_process_data2(ctx, child)))
    return JS_EXCEPTION;

  if(argc >= 1 + magic) {
    if(JS_IsString(sig)) {
      const char* str = JS_ToCString(ctx, sig);
      int n = str_start(str, "SIG") ? 0 : 3;

      for(int i = 1; i < 32; i++) {
        if(!strcmp(child_process_signals[i] + n, str)) {
          signum = i;
          break;
        }
      }

      JS_FreeCString(ctx, str);
    } else {
      JS_ToInt32(ctx, &signum, sig);
    }
  }

  return JS_NewInt32(ctx, child_process_kill(cp, signum));
}

static JSClassDef js_child_process_class = {
    .class_name = "ChildProcess",
    .finalizer = js_child_process_finalizer,
};

static const JSCFunctionListEntry js_child_process_proto_funcs[] = {
    JS_CGETSET_ENUMERABLE_DEF("file", js_child_process_get, 0, CHILD_PROCESS_FILE),
    JS_CGETSET_MAGIC_DEF("cwd", js_child_process_get, 0, CHILD_PROCESS_CWD),
    JS_CGETSET_ENUMERABLE_DEF("args", js_child_process_get, 0, CHILD_PROCESS_ARGS),
    JS_CGETSET_MAGIC_DEF("env", js_child_process_get, 0, CHILD_PROCESS_ENV),
    JS_CGETSET_ENUMERABLE_DEF("stdio", js_child_process_get, 0, CHILD_PROCESS_STDIO),
    JS_CGETSET_ENUMERABLE_DEF("pid", js_child_process_get, 0, CHILD_PROCESS_PID),
    JS_CGETSET_ENUMERABLE_DEF("exitcode", js_child_process_get, 0, CHILD_PROCESS_EXITCODE),
    JS_CGETSET_ENUMERABLE_DEF("termsig", js_child_process_get, 0, CHILD_PROCESS_TERMSIG),
    JS_CGETSET_ENUMERABLE_DEF("exited", js_child_process_get, 0, CHILD_PROCESS_EXITED),
    JS_CGETSET_ENUMERABLE_DEF("signaled", js_child_process_get, 0, CHILD_PROCESS_SIGNALED),
    JS_CGETSET_ENUMERABLE_DEF("stopped", js_child_process_get, 0, CHILD_PROCESS_STOPPED),
    JS_CGETSET_ENUMERABLE_DEF("continued", js_child_process_get, 0, CHILD_PROCESS_CONTINUED),
    JS_CFUNC_DEF("wait", 0, js_child_process_wait),
    JS_CFUNC_MAGIC_DEF("kill", 0, js_child_process_kill, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ChildProcess", 0),
};

static const JSCFunctionListEntry js_child_process_funcs[] = {
    JS_CFUNC_DEF("exec", 1, js_child_process_exec),
    JS_CFUNC_DEF("spawn", 1, js_child_process_spawn),
    JS_CFUNC_MAGIC_DEF("kill", 1, js_child_process_kill, 1),

    JS_PROP_INT32_DEF("WNOHANG", WNOHANG, JS_PROP_ENUMERABLE),
#ifdef WNOWAIT
    JS_PROP_INT32_DEF("WNOWAIT", WNOWAIT, JS_PROP_ENUMERABLE),
#endif
    JS_PROP_INT32_DEF("WUNTRACED", WUNTRACED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGHUP", SIGHUP, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGINT", SIGINT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGQUIT", SIGQUIT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGILL", SIGILL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGTRAP", SIGTRAP, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGABRT", SIGABRT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGBUS", SIGBUS, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGFPE", SIGFPE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGKILL", SIGKILL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGUSR1", SIGUSR1, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGSEGV", SIGSEGV, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGUSR2", SIGUSR2, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGPIPE", SIGPIPE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGALRM", SIGALRM, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGTERM", SIGTERM, JS_PROP_ENUMERABLE),
#ifdef SIGSTKFLT
    JS_PROP_INT32_DEF("SIGSTKFLT", SIGSTKFLT, JS_PROP_ENUMERABLE),
#endif
    JS_PROP_INT32_DEF("SIGCHLD", SIGCHLD, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGCONT", SIGCONT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGSTOP", SIGSTOP, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGTSTP", SIGTSTP, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGTTIN", SIGTTIN, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGTTOU", SIGTTOU, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGURG", SIGURG, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGXCPU", SIGXCPU, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGXFSZ", SIGXFSZ, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGVTALRM", SIGVTALRM, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGPROF", SIGPROF, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGWINCH", SIGWINCH, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGIO", SIGIO, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGPWR", SIGPWR, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SIGSYS", SIGSYS, JS_PROP_ENUMERABLE),
};

static int
js_child_process_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_child_process_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_child_process_class_id, &js_child_process_class);

  child_process_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx,
                             child_process_proto,
                             js_child_process_proto_funcs,
                             countof(js_child_process_proto_funcs));
  JS_SetClassProto(ctx, js_child_process_class_id, child_process_proto);

  child_process_ctor = JS_NewCFunction2(ctx, js_child_process_constructor, "ChildProcess", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, child_process_ctor, child_process_proto);
  JS_SetPropertyFunctionList(ctx, child_process_ctor, js_child_process_funcs, countof(js_child_process_funcs));

  if(m) {
    JS_SetModuleExportList(ctx, m, js_child_process_funcs, countof(js_child_process_funcs));
    JS_SetModuleExport(ctx, m, "ChildProcess", child_process_ctor);
    JS_SetModuleExport(ctx, m, "default", child_process_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_child_process
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if(!(m = JS_NewCModule(ctx, module_name, js_child_process_init)))
    return NULL;

  JS_AddModuleExportList(ctx, m, js_child_process_funcs, countof(js_child_process_funcs));
  JS_AddModuleExport(ctx, m, "ChildProcess");
  JS_AddModuleExport(ctx, m, "default");
  return m;
}

/**
 * @}
 */
