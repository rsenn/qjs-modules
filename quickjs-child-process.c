#include "defines.h"
#include "utils.h"
#include "char-utils.h"
#include "buffer-utils.h"
#include "property-enumeration.h"
#include "child-process.h"
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
  CHILD_PROCESS_SPAWNFILE = 0,
  CHILD_PROCESS_SPAWNARGS,
  CHILD_PROCESS_STDIN,
  CHILD_PROCESS_STDOUT,
  CHILD_PROCESS_STDERR,
  CHILD_PROCESS_STDIO,
  CHILD_PROCESS_PID,
  CHILD_PROCESS_EXITCODE,
  CHILD_PROCESS_SIGNALCODE,
  CHILD_PROCESS_KILLED,
  CHILD_PROCESS_ONEXIT,
};

VISIBLE JSClassID js_child_process_class_id = 0;
static JSValue child_process_proto, child_process_ctor;

static BOOL child_process_handler;

/* os.signal(SIGCHLD, handler); a null handler uninstalls. never throws. */
static void
child_process_signal(JSContext* ctx, JSValueConst handler) {
  JSValue os = js_module_namespace_sync(ctx, "os");
  JSValue sig;

  if(JS_IsException(os)) {
    JS_FreeValue(ctx, JS_GetException(ctx));
    return;
  }

  sig = JS_GetPropertyStr(ctx, os, "signal");
  JS_FreeValue(ctx, os);
  JSValueConst args[] = {
      JS_NewInt32(ctx, SIGCHLD),
      handler,
  };

  JSValue ret = JS_Call(ctx, sig, JS_NULL, countof(args), args);
  JS_FreeValue(ctx, sig);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, ret);
}

static void
js_child_process_handler_update(JSContext* ctx, BOOL install);

static JSValue
js_child_process_sigchld(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  child_process_sigchld(SIGCHLD);
  js_child_process_handler_update(ctx, FALSE);
  return JS_UNDEFINED;
}

/* installs the SIGCHLD handler (install) or uninstalls it once no child is left. */
static void
js_child_process_handler_update(JSContext* ctx, BOOL install) {
  if(install && !child_process_handler) {
    JSValue fn = JS_NewCFunction(ctx, js_child_process_sigchld, "sigchld", 0);
    child_process_signal(ctx, fn);
    JS_FreeValue(ctx, fn);
    child_process_handler = TRUE;
  } else if(!install && child_process_handler && child_process_empty()) {
    child_process_signal(ctx, JS_NULL);
    child_process_handler = FALSE;
  }
}

/* child_process_new() plus the SIGCHLD handler. returns NULL with errno set. */
static ChildProcess*
js_child_process_new(JSContext* ctx) {
  ChildProcess* cp;

  if((cp = child_process_new()))
    js_child_process_handler_update(ctx, TRUE);

  return cp;
}

static void
js_child_process_remove(JSContext* ctx, ChildProcess* cp) {
  child_process_remove(cp);
  js_child_process_handler_update(ctx, FALSE);
}

/* trampoline: onexit(opaque) of a ChildProcess calling a JS function.
 *
 * refcount: `func` is owned (dup'd); malloc'd, freed by onexit_clear(). */
typedef struct {
  JSContext* ctx;
  JSValue func;
  ChildProcess* cp;
} ChildProcessExit;

static JSValue
child_process_exitcode(JSContext* ctx, ChildProcess* cp) {
  if(cp->exitcode != -1 && !cp->signaled)
    return JS_NewInt32(ctx, cp->exitcode);

  return JS_NULL;
}

static JSValue
child_process_signalcode(JSContext* ctx, ChildProcess* cp) {
  if(cp->signaled && cp->termsig > 0 && cp->termsig < 32)
    return JS_NewString(ctx, child_process_signals[cp->termsig]);

  return JS_NULL;
}

/* ChildProcess.onexit: calls func(exitcode, signalcode). never throws. */
static void
js_child_process_onexit_call(void* opaque) {
  ChildProcessExit* t = opaque;
  JSValue args[] = {child_process_exitcode(t->ctx, t->cp), child_process_signalcode(t->ctx, t->cp)};
  JSValue ret = JS_Call(t->ctx, t->func, JS_UNDEFINED, countof(args), args);

  JS_FreeValue(t->ctx, ret);
  JS_FreeValue(t->ctx, args[0]);
  JS_FreeValue(t->ctx, args[1]);
}

static void
js_child_process_onexit_clear(JSRuntime* rt, ChildProcess* cp) {
  ChildProcessExit* t = cp->opaque;

  if(t) {
    JS_FreeValueRT(rt, t->func);
    free(t);
  }

  cp->onexit = NULL;
  cp->opaque = NULL;
}

/* installs `func` (borrowed) as onexit; a non-function just clears it.
 * returns 0, or -1 with errno set. */
static int
js_child_process_onexit_set(JSContext* ctx, ChildProcess* cp, JSValueConst func) {
  ChildProcessExit* t = NULL;

  if(JS_IsFunction(ctx, func)) {
    if(!(t = malloc(sizeof(ChildProcessExit))))
      return -1;

    t->ctx = ctx;
    t->func = JS_DupValue(ctx, func);
    t->cp = cp;
  }

  js_child_process_onexit_clear(JS_GetRuntime(ctx), cp);

  if(t) {
    cp->onexit = js_child_process_onexit_call;
    cp->opaque = t;
  }

  return 0;
}

ChildProcess*
js_child_process_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_child_process_class_id);
}

JSValue
js_child_process_wrap(JSContext* ctx, ChildProcess* cp) {
  JSValue obj = JS_NewObjectProtoClass(ctx, child_process_proto, js_child_process_class_id);
  JS_SetOpaque(obj, cp);
  return obj;
}

static JSValue
js_child_process_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  ChildProcess* cp;
  JSValue proto, obj = JS_UNDEFINED;

  if(!(cp = js_child_process_new(ctx)))
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
  free(cp);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
js_child_process_finalizer(JSRuntime* rt, JSValue val) {
  ChildProcess* cp;

  if((cp = JS_GetOpaque(val, js_child_process_class_id))) {
    js_child_process_onexit_clear(rt, cp);
    child_process_free(cp);
  }
}

/* libc-allocated copy of `value` as a string; ChildProcess frees it with free().
 * returns NULL with an exception pending. */
static char*
js_child_process_tostring(JSContext* ctx, JSValueConst value) {
  const char* s;
  char* ret;

  if(!(s = JS_ToCString(ctx, value)))
    return 0;

  ret = strdup(s);
  JS_FreeCString(ctx, s);
  return ret;
}

/* libc-allocated NULL-terminated argv from a JS array of strings. */
static char**
js_child_process_argv(JSContext* ctx, JSValueConst array, size_t skip) {
  size_t i, len = js_array_length(ctx, array);
  char** ret = calloc(skip + len + 1, sizeof(char*));

  for(i = 0; ret && i < len; i++) {
    JSValue item = JS_GetPropertyUint32(ctx, array, i);

    ret[skip + i] = js_child_process_tostring(ctx, item);
    JS_FreeValue(ctx, item);
  }

  return ret;
}

static char**
js_child_process_environ_dup(void) {
  size_t i, len = 0;
  char** ret;

  while(environ[len])
    ++len;

  if((ret = calloc(len + 1, sizeof(char*))))
    for(i = 0; i < len; i++)
      ret[i] = strdup(environ[i]);

  return ret;
}

static char**
js_child_process_environment(JSContext* ctx, JSValueConst object) {
  PropertyEnumeration propenum;
  char** ret = 0;
  size_t n = 0;

  if(property_enumeration_init(&propenum, ctx, object, PROPENUM_DEFAULT_FLAGS))
    return 0;

  do {
    size_t namelen, valuelen;
    const char* name = property_enumeration_keystrlen(&propenum, &namelen, ctx);
    const char* value = property_enumeration_valuestrlen(&propenum, &valuelen, ctx);
    char *var = malloc(namelen + 1 + valuelen + 1), **tmp = realloc(ret, sizeof(char*) * (n + 2));

    if(var && tmp) {
      memcpy(var, name, namelen);
      var[namelen] = '=';
      memcpy(&var[namelen + 1], value, valuelen);
      var[namelen + 1 + valuelen] = '\0';
      (ret = tmp)[n++] = var;
      ret[n] = 0;
    } else {
      free(var);
      if(tmp)
        ret = tmp;
    }

    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, value);
  } while(property_enumeration_next(&propenum));

  if(!ret)
    ret = calloc(1, sizeof(char*));

  return ret;
}

static int
js_child_process_options(JSContext* ctx, ChildProcess* cp, JSValueConst obj) {
  size_t len;
  int *parent_fds, *child_fds;
  JSValue value = JS_GetPropertyStr(ctx, obj, "env");

  if(JS_IsObject(value))
    cp->env = js_child_process_environment(ctx, value);
  else
    cp->env = js_child_process_environ_dup();

  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, obj, "cwd");
  if(JS_IsString(value))
    cp->cwd = js_child_process_tostring(ctx, value);

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
  parent_fds = cp->parent_fds = calloc(len + 1, sizeof(int));
  child_fds = cp->child_fds = calloc(len + 1, sizeof(int));
  cp->pipe_fds = NULL;
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

        if(!cp->pipe_fds)
          cp->pipe_fds = calloc(len + 1, sizeof(int));
        cp->pipe_fds[i] = 1;

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

  if(js_has_propertystr(ctx, obj, "usePath")) {
    value = JS_GetPropertyStr(ctx, obj, "usePath");
    cp->use_path = JS_ToBool(ctx, value);
    JS_FreeValue(ctx, value);
  }

  value = JS_GetPropertyStr(ctx, obj, "onExit");
  js_child_process_onexit_set(ctx, cp, value);
  JS_FreeValue(ctx, value);

  return 0;
}

/* Blocks the calling thread until the child has exited or been signaled. */
static void
child_process_wait_blocking(ChildProcess* cp) {
  int pid;

  do {
  } while((pid = child_process_wait(cp, 0)) != -1 && pid != cp->pid);
}

/* Builds a Node.js `spawnSync()`-shaped result: {pid, output, stdout, stderr, status, signal}. */
static JSValue
child_process_result(JSContext* ctx, ChildProcess* cp) {
  JSValue obj = JS_NewObjectProto(ctx, JS_NULL);
  int num = cp->num_fds > 3 ? 3 : cp->num_fds;
  DynBuf db[3];
  JSValue output = JS_NewArray(ctx);

  JS_SetPropertyStr(ctx, obj, "pid", JS_NewInt32(ctx, cp->pid));
  JS_SetPropertyUint32(ctx, output, 0, JS_NULL);

  if(cp->pipe_fds) {
    static const char* names[3] = {
        NULL,
        "stdout",
        "stderr",
    };

    for(int i = 1; i < num; i++)
      if(cp->pipe_fds[i])
        dbuf_init_ctx(ctx, &db[i]);

    for(int i = 1; i < num; i++)
      if(cp->pipe_fds[i]) {
        for(;;) {
          char tmp[1024];
          ssize_t bytes = read(cp->parent_fds[i], tmp, sizeof(tmp));

          if(bytes > 0) {
            dbuf_put(&db[i], (const void*)tmp, bytes);
            continue;
          }

          break;
        }
      }

    for(int i = 1; i < num; i++)
      if(cp->pipe_fds[i]) {
        JSValue str = dbuf_tostring_free(&db[i], ctx);

        JS_DefinePropertyValueStr(ctx, obj, names[i], JS_DupValue(ctx, str), JS_PROP_CONFIGURABLE);
        JS_SetPropertyUint32(ctx, output, i, str);
      }
  }

  JS_DefinePropertyValueStr(ctx, obj, "output", output, JS_PROP_CONFIGURABLE);

  child_process_wait_blocking(cp);
  js_child_process_remove(ctx, cp);

  JS_SetPropertyStr(ctx, obj, "status", child_process_exitcode(ctx, cp));
  JS_SetPropertyStr(ctx, obj, "signal", child_process_signalcode(ctx, cp));

  return obj;
}

static JSValue
js_child_process_spawn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret;
  ChildProcess* cp;

  if(!(cp = js_child_process_new(ctx)))
    return JS_EXCEPTION;

  ret = js_child_process_wrap(ctx, cp);

  if(JS_IsArray(ctx, argv[0])) {
    cp->args = js_child_process_argv(ctx, argv[0], 0);

    if(cp->args && cp->args[0])
      cp->file = strdup(cp->args[0]);
  } else {
    cp->file = js_child_process_tostring(ctx, argv[0]);

    if(argc > 1) {
      cp->args = js_child_process_argv(ctx, argv[1], 1);
      cp->args[0] = strdup(cp->file);

      --argc;
      ++argv;
    } else {
      cp->args = malloc(sizeof(char*) * 2);
      cp->args[0] = strdup(cp->file);
      cp->args[1] = 0;
    }
  }

  if(argc > 1 && JS_IsObject(argv[1]))
    js_child_process_options(ctx, cp, argv[1]);

  child_process_spawn(cp);

  /* spawnSync */
  if(magic) {
    JSValue result = child_process_result(ctx, cp);
    JS_FreeValue(ctx, ret);
    ret = result;
  }

  return ret;
}

static JSValue
js_child_process_exec(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret;
  ChildProcess* cp;

  if(!(cp = js_child_process_new(ctx)))
    return JS_EXCEPTION;

  ret = js_child_process_wrap(ctx, cp);

  const char* shell = getenv("SHELL");

  if(!shell)
    shell = "/bin/sh";

  cp->file = strdup(shell);
  cp->args = realloc(cp->args, sizeof(char*) * 4);
  cp->args[0] = strdup("sh");
  cp->args[1] = strdup("-c");
  cp->args[2] = js_child_process_tostring(ctx, argv[0]);
  cp->args[3] = 0;

  if(argc > 1 && JS_IsObject(argv[1]))
    js_child_process_options(ctx, cp, argv[1]);

  child_process_spawn(cp);

  /* execSync */
  if(magic) {
    JSValue result = child_process_result(ctx, cp);
    JS_FreeValue(ctx, ret);
    ret = result;
  }

  return ret;
}

static JSValue
child_process_fd(JSContext* ctx, ChildProcess* cp, int idx) {
  if(cp->parent_fds && idx < cp->num_fds && cp->parent_fds[idx] >= 0)
    return JS_NewInt32(ctx, cp->parent_fds[idx]);

  return JS_NULL;
}

static JSValue
js_child_process_get(JSContext* ctx, JSValueConst this_val, int magic) {
  ChildProcess* cp;
  JSValue ret = JS_UNDEFINED;

  if(!(cp = js_child_process_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case CHILD_PROCESS_SPAWNFILE: {
      ret = cp->file ? JS_NewString(ctx, cp->file) : JS_NULL;
      break;
    }
    case CHILD_PROCESS_SPAWNARGS: {
      ret = cp->args ? js_strv_to_array(ctx, cp->args) : JS_NULL;
      break;
    }
    case CHILD_PROCESS_STDIN: {
      ret = child_process_fd(ctx, cp, 0);
      break;
    }
    case CHILD_PROCESS_STDOUT: {
      ret = child_process_fd(ctx, cp, 1);
      break;
    }
    case CHILD_PROCESS_STDERR: {
      ret = child_process_fd(ctx, cp, 2);
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
    case CHILD_PROCESS_EXITCODE: {
      ret = child_process_exitcode(ctx, cp);
      break;
    }
    case CHILD_PROCESS_SIGNALCODE: {
      ret = child_process_signalcode(ctx, cp);
      break;
    }
    case CHILD_PROCESS_KILLED: {
      ret = JS_NewBool(ctx, cp->killed);
      break;
    }
    case CHILD_PROCESS_ONEXIT: {
      ret = cp->opaque ? JS_DupValue(ctx, ((ChildProcessExit*)cp->opaque)->func) : JS_NULL;
      break;
    }
  }

  return ret;
}

static JSValue
js_child_process_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  ChildProcess* cp;

  if(!(cp = js_child_process_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case CHILD_PROCESS_ONEXIT: {
      js_child_process_onexit_set(ctx, cp, value);
      break;
    }
  }

  return JS_UNDEFINED;
}

/* Blocking wait for the child to change state; callers wanting async notification should use `onExit` instead. */
static JSValue
js_child_process_wait(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  ChildProcess* cp;
  int32_t flags = 0;

  if(!(cp = js_child_process_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(argc >= 1)
    JS_ToInt32(ctx, &flags, argv[0]);

  if(!cp->exited && !cp->signaled) {
    int pid;

    if((pid = child_process_wait(cp, flags)) != -1 && pid == cp->pid) {
      js_child_process_remove(ctx, cp);
      child_process_notify(cp);
    }
  }

  if(!cp->exited && !cp->signaled)
    return JS_NULL;

  JSValue ret = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetPropertyStr(ctx, ret, "exitCode", child_process_exitcode(ctx, cp));
  JS_SetPropertyStr(ctx, ret, "signalCode", child_process_signalcode(ctx, cp));
  return ret;
}

static JSValue
js_child_process_kill(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  ChildProcess* cp;
  int32_t signum = SIGTERM;
  JSValueConst sig = argv[magic], child = magic ? argv[0] : this_val;
  int ret;

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

  ret = child_process_kill(cp, signum);

  if(ret == 0)
    cp->killed = TRUE;

  return JS_NewBool(ctx, ret == 0);
}

enum {
  CHILD_PROCESS_TOPRIMITIVE,
};

static JSValue
js_child_process_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  ChildProcess* cp;
  JSValue ret = JS_UNDEFINED;

  if(!(cp = js_child_process_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case CHILD_PROCESS_TOPRIMITIVE: {
      const char* hint = 0;

      if(argc > 0)
        hint = JS_ToCString(ctx, argv[0]);

      ret = JS_NewInt32(ctx, cp->pid);

      if(hint)
        JS_FreeCString(ctx, hint);
      break;
    }
  }

  return ret;
}

static JSClassDef js_child_process_class = {
    .class_name = "ChildProcess",
    .finalizer = js_child_process_finalizer,
};

static const JSCFunctionListEntry js_child_process_proto_funcs[] = {
    JS_CGETSET_ENUMERABLE_DEF("spawnfile", js_child_process_get, 0, CHILD_PROCESS_SPAWNFILE),
    JS_CGETSET_ENUMERABLE_DEF("spawnargs", js_child_process_get, 0, CHILD_PROCESS_SPAWNARGS),
    JS_CGETSET_ENUMERABLE_DEF("stdin", js_child_process_get, 0, CHILD_PROCESS_STDIN),
    JS_CGETSET_ENUMERABLE_DEF("stdout", js_child_process_get, 0, CHILD_PROCESS_STDOUT),
    JS_CGETSET_ENUMERABLE_DEF("stderr", js_child_process_get, 0, CHILD_PROCESS_STDERR),
    JS_CGETSET_ENUMERABLE_DEF("stdio", js_child_process_get, 0, CHILD_PROCESS_STDIO),
    JS_CGETSET_ENUMERABLE_DEF("pid", js_child_process_get, 0, CHILD_PROCESS_PID),
    JS_CGETSET_ENUMERABLE_DEF("exitCode", js_child_process_get, 0, CHILD_PROCESS_EXITCODE),
    JS_CGETSET_ENUMERABLE_DEF("signalCode", js_child_process_get, 0, CHILD_PROCESS_SIGNALCODE),
    JS_CGETSET_ENUMERABLE_DEF("killed", js_child_process_get, 0, CHILD_PROCESS_KILLED),
    JS_CGETSET_ENUMERABLE_DEF("onExit", js_child_process_get, js_child_process_set, CHILD_PROCESS_ONEXIT),
    JS_CFUNC_DEF("wait", 0, js_child_process_wait),
    JS_CFUNC_MAGIC_DEF("kill", 0, js_child_process_kill, 0),
    JS_CFUNC_MAGIC_DEF("[Symbol.toPrimitive]", 0, js_child_process_method, CHILD_PROCESS_TOPRIMITIVE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ChildProcess", 0),
};

static const JSCFunctionListEntry js_child_process_funcs[] = {
    JS_CFUNC_MAGIC_DEF("exec", 1, js_child_process_exec, 0),
    JS_CFUNC_MAGIC_DEF("execSync", 1, js_child_process_exec, 1),
    JS_CFUNC_MAGIC_DEF("spawn", 1, js_child_process_spawn, 0),
    JS_CFUNC_MAGIC_DEF("spawnSync", 1, js_child_process_spawn, 1),
    JS_CFUNC_MAGIC_DEF("kill", 1, js_child_process_kill, 1),

    JS_PROP_INT32_DEF("WNOHANG", WNOHANG, JS_PROP_ENUMERABLE),
#ifdef WNOWAIT
    JS_PROP_INT32_DEF("WNOWAIT", WNOWAIT, JS_PROP_ENUMERABLE),
#endif
    JS_PROP_INT32_DEF("WUNTRACED", WUNTRACED, JS_PROP_ENUMERABLE),
 #ifdef SIGHUP
    JS_CONSTANT(SIGHUP),
#endif
#ifdef SIGINT
    JS_CONSTANT(SIGINT),
#endif
#ifdef SIGQUIT
    JS_CONSTANT(SIGQUIT),
#endif
#ifdef SIGILL
    JS_CONSTANT(SIGILL),
#endif
#ifdef SIGTRAP
    JS_CONSTANT(SIGTRAP),
#endif
#ifdef SIGABRT
    JS_CONSTANT(SIGABRT),
#endif
#ifdef SIGBUS
    JS_CONSTANT(SIGBUS),
#endif
#ifdef SIGFPE
    JS_CONSTANT(SIGFPE),
#endif
#ifdef SIGKILL
    JS_CONSTANT(SIGKILL),
#endif
#ifdef SIGUSR1
    JS_CONSTANT(SIGUSR1),
#endif
#ifdef SIGSEGV
    JS_CONSTANT(SIGSEGV),
#endif
#ifdef SIGUSR2
    JS_CONSTANT(SIGUSR2),
#endif
#ifdef SIGPIPE
    JS_CONSTANT(SIGPIPE),
#endif
#ifdef SIGALRM
    JS_CONSTANT(SIGALRM),
#endif
#ifdef SIGTERM
    JS_CONSTANT(SIGTERM),
#endif
#ifdef SIGSTKFLT
    JS_CONSTANT(SIGSTKFLT),
#endif
#ifdef SIGCHLD
    JS_CONSTANT(SIGCHLD),
#endif
#ifdef SIGCONT
    JS_CONSTANT(SIGCONT),
#endif
#ifdef SIGSTOP
    JS_CONSTANT(SIGSTOP),
#endif
#ifdef SIGTSTP
    JS_CONSTANT(SIGTSTP),
#endif
#ifdef SIGTTIN
    JS_CONSTANT(SIGTTIN),
#endif
#ifdef SIGTTOU
    JS_CONSTANT(SIGTTOU),
#endif
#ifdef SIGURG
    JS_CONSTANT(SIGURG),
#endif
#ifdef SIGXCPU
    JS_CONSTANT(SIGXCPU),
#endif
#ifdef SIGXFSZ
    JS_CONSTANT(SIGXFSZ),
#endif
#ifdef SIGVTALRM
    JS_CONSTANT(SIGVTALRM),
#endif
#ifdef SIGPROF
    JS_CONSTANT(SIGPROF),
#endif
#ifdef SIGWINCH
    JS_CONSTANT(SIGWINCH),
#endif
#ifdef SIGIO
    JS_CONSTANT(SIGIO),
#endif
#ifdef SIGPOLL
    JS_CONSTANT(SIGPOLL),
#endif
#ifdef SIGPWR
    JS_CONSTANT(SIGPWR),
#endif
#ifdef SIGSYS
    JS_CONSTANT(SIGSYS),
#endif
#ifdef SIGBREAK
    JS_CONSTANT(SIGBREAK),
#endif
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
