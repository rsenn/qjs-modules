#include "utils.h"
#include "child-process.h"
#include "property-enumeration.h"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#include <signal.h>
#endif
#include <sys/wait.h>

enum { CHILD_PROCESS_FILE = 0, CHILD_PROCESS_CWD, CHILD_PROCESS_ARGS, CHILD_PROCESS_ENV, CHILD_PROCESS_STDIO, CHILD_PROCESS_PID, CHILD_PROCESS_EXITED, CHILD_PROCESS_EXITCODE, CHILD_PROCESS_SIGNALED, CHILD_PROCESS_TERMSIG };

extern char** environ;

thread_local VISIBLE JSClassID js_child_process_class_id = 0;
thread_local JSValue child_process_proto = {JS_TAG_UNDEFINED}, child_process_ctor = {JS_TAG_UNDEFINED};

ChildProcess*
js_child_process_data(JSContext* ctx, JSValueConst value) {
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
  JSValue obj = JS_UNDEFINED, proto = JS_UNDEFINED;

  if(!(cp = js_mallocz(ctx, sizeof(ChildProcess))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
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

static JSValue
js_child_process_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  ChildProcess* cp;

  if(!(cp = js_child_process_data(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectProto(ctx, child_process_proto);

  if(cp->file)
    JS_DefinePropertyValueStr(ctx, obj, "file", JS_NewString(ctx, cp->file), JS_PROP_ENUMERABLE);
  if(cp->cwd)
    JS_DefinePropertyValueStr(ctx, obj, "cwd", JS_NewString(ctx, cp->cwd), JS_PROP_ENUMERABLE);

  JS_DefinePropertyValueStr(ctx, obj, "args", js_strv_to_array(ctx, cp->args), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "env", js_strv_to_array(ctx, cp->env), JS_PROP_ENUMERABLE);

  JS_DefinePropertyValueStr(ctx, obj, "pid", JS_NewUint32(ctx, cp->pid), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "exitcode", JS_NewInt32(ctx, cp->exitcode), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "termsig", JS_NewInt32(ctx, cp->termsig), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "uid", JS_NewUint32(ctx, cp->uid), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "gid", JS_NewUint32(ctx, cp->gid), JS_PROP_ENUMERABLE);

  return obj;
}

static void
js_child_process_finalizer(JSRuntime* rt, JSValue val) {
  ChildProcess* cp = JS_GetOpaque(val, js_child_process_class_id);
  if(cp) {
    child_process_free_rt(cp, rt);
  }
}

static JSValue
js_child_process_exec(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;

  return ret;
}

static JSValue
js_child_process_spawn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  ChildProcess* cp;

  if(!(cp = child_process_new(ctx)))
    return JS_EXCEPTION;

  ret = js_child_process_wrap(ctx, cp);

  cp->file = js_tostring(ctx, argv[0]);

  if(argc > 1) {
    cp->args = js_array_to_argv(ctx, 0, argv[1]);
  } else {
    cp->args = js_malloc(ctx, sizeof(char*) * 2);
    cp->args[0] = js_strdup(ctx, cp->file);
    cp->args[1] = 0;
  }

  if(argc > 2 && JS_IsObject(argv[2])) {
    JSValue env, stdio;
    size_t i, len;
    int *parent_fds, *child_fds;
    env = JS_GetPropertyStr(ctx, argv[2], "env");

    if(JS_IsObject(env)) {
      cp->env = child_process_environment(ctx, env);
    } else {
      cp->env = js_strv_dup(ctx, environ);
    }
    JS_FreeValue(ctx, env);

    stdio = JS_GetPropertyStr(ctx, argv[2], "stdio");
    if(JS_IsException(stdio) || JS_IsUndefined(stdio))
      stdio = JS_NewString(ctx, "pipe");

    if(!JS_IsArray(ctx, stdio)) {
      JSValue a = JS_NewArray(ctx);
      JS_SetPropertyUint32(ctx, a, 0, JS_DupValue(ctx, stdio));
      JS_SetPropertyUint32(ctx, a, 1, JS_DupValue(ctx, stdio));
      JS_SetPropertyUint32(ctx, a, 2, JS_DupValue(ctx, stdio));
      JS_FreeValue(ctx, stdio);
      stdio = a;
    }

    len = js_array_length(ctx, stdio);
    parent_fds = cp->parent_fds = js_malloc(ctx, sizeof(int) * len);
    child_fds = cp->child_fds = js_malloc(ctx, sizeof(int) * len);
    cp->num_fds = len;

    for(i = 0; i < len; i++) {
      JSValue item = JS_GetPropertyUint32(ctx, stdio, i);
      parent_fds[i] = -1;
      child_fds[i] = -1;

      if(JS_IsNumber(item)) {
        int32_t fd;
        JS_ToInt32(ctx, &fd, item);

        child_fds[i] = fd;
      } else if(JS_IsString(item)) {
        const char* s = js_get_propertyint_cstring(ctx, stdio, i);

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

        } else if(!strcmp(s, "inherit")) {
          child_fds[i] = i;
        }

        //        JS_FreeCString(ctx, s);
      }
    }
    JS_FreeValue(ctx, stdio);
  }

  child_process_spawn(cp);

  return ret;
}

static JSValue
js_child_process_get(JSContext* ctx, JSValueConst this_val, int magic) {
  ChildProcess* cp;
  JSValue ret = JS_UNDEFINED;

  if(!(cp = js_child_process_data(ctx, this_val)))
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
      ret = js_strv_to_array(ctx, cp->env);
      break;
    }
    case CHILD_PROCESS_STDIO: {
      ret = cp->parent_fds ? js_intv_to_array(ctx, cp->parent_fds) : JS_NULL;
      break;
    }
    case CHILD_PROCESS_PID: {
      ret = JS_NewInt32(ctx, cp->pid);
      break;
    }
    case CHILD_PROCESS_EXITED: {
      ret = JS_NewBool(ctx, cp->exitcode != -1);
      break;
    }
    case CHILD_PROCESS_EXITCODE: {
      if(cp->exitcode != -1)
        ret = JS_NewInt32(ctx, cp->exitcode);
      break;
    }
    case CHILD_PROCESS_SIGNALED: {
      ret = JS_NewBool(ctx, cp->termsig != -1);
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
  JSValue ret = JS_UNDEFINED;
  int32_t flags = 0;

  if(!(cp = js_child_process_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(argc >= 1)
    JS_ToInt32(ctx, &flags, argv[0]);

  ret = JS_NewInt32(ctx, child_process_wait(cp, flags));

  return ret;
}

static JSValue
js_child_process_kill(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  ChildProcess* cp;
  JSValue ret = JS_UNDEFINED;
  int32_t signum = SIGTERM;

  if(!(cp = js_child_process_data(ctx, this_val)))
    return JS_EXCEPTION;

  if(argc > 0)
    JS_ToInt32(ctx, &signum, argv[0]);

  // if(cp->exitcode == -1 && cp->termsig == -1)
  ret = JS_NewInt32(ctx, child_process_kill(cp, signum));

  return ret;
}

static JSClassDef js_child_process_class = {
    .class_name = "ChildProcess",
    .finalizer = js_child_process_finalizer,
};

static const JSCFunctionListEntry js_child_process_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("file", js_child_process_get, 0, CHILD_PROCESS_FILE),
    JS_CGETSET_MAGIC_DEF("cwd", js_child_process_get, 0, CHILD_PROCESS_CWD),
    JS_CGETSET_MAGIC_DEF("args", js_child_process_get, 0, CHILD_PROCESS_ARGS),
    JS_CGETSET_MAGIC_DEF("env", js_child_process_get, 0, CHILD_PROCESS_ENV),
    JS_CGETSET_MAGIC_DEF("stdio", js_child_process_get, 0, CHILD_PROCESS_STDIO),
    JS_CGETSET_MAGIC_DEF("pid", js_child_process_get, 0, CHILD_PROCESS_PID),
    JS_CGETSET_MAGIC_DEF("exitcode", js_child_process_get, 0, CHILD_PROCESS_EXITCODE),
    JS_CGETSET_MAGIC_DEF("termsig", js_child_process_get, 0, CHILD_PROCESS_TERMSIG),
    JS_CGETSET_MAGIC_DEF("exited", js_child_process_get, 0, CHILD_PROCESS_EXITED),
    JS_CGETSET_MAGIC_DEF("signaled", js_child_process_get, 0, CHILD_PROCESS_SIGNALED),
    JS_CFUNC_DEF("wait", 0, js_child_process_wait),
    JS_CFUNC_DEF("kill", 0, js_child_process_kill),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ChildProcess", JS_PROP_C_W_E),
};

static const JSCFunctionListEntry js_child_process_funcs[] = {
    JS_CFUNC_DEF("exec", 1, js_child_process_exec),
    JS_CFUNC_DEF("spawn", 1, js_child_process_spawn),
    JS_PROP_INT32_DEF("WNOHANG", WNOHANG, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("WNOWAIT", WNOWAIT, JS_PROP_ENUMERABLE),
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
    JS_PROP_INT32_DEF("SIGSTKFLT", SIGSTKFLT, JS_PROP_ENUMERABLE),
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
  JS_SetPropertyFunctionList(ctx, child_process_proto, js_child_process_proto_funcs, countof(js_child_process_proto_funcs));
  JS_SetClassProto(ctx, js_child_process_class_id, child_process_proto);

  child_process_ctor = JS_NewCFunction2(ctx, js_child_process_constructor, "ChildProcess", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, child_process_ctor, child_process_proto);
  JS_SetPropertyFunctionList(ctx, child_process_ctor, js_child_process_funcs, countof(js_child_process_funcs));
  js_set_inspect_method(ctx, child_process_proto, js_child_process_inspect);

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
  m = JS_NewCModule(ctx, module_name, js_child_process_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_child_process_funcs, countof(js_child_process_funcs));
  JS_AddModuleExport(ctx, m, "ChildProcess");
  JS_AddModuleExport(ctx, m, "default");
  return m;
}
