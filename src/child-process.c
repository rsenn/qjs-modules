#define _GNU_SOURCE 1
#if defined(__CYGWIN__) || defined(__MSYS__)
#undef __GNU_VISIBLE
#define __GNU_VISIBLE 1
#include <sys/unistd.h>
#endif
#include "child-process.h"
#include "utils.h"
#include "property-enumeration.h"
#include "char-utils.h"
#include "path.h"
#include "debug.h"

#include <stdlib.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#ifdef POSIX_SPAWN
#include <spawn.h>
#else
#include <unistd.h>
#endif
#endif

#ifndef __ANDROID__
#ifndef HAVE_FORK
#if HAVE_VFORK
#define fork() vfork()
#endif
#endif
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

/* If WIFEXITED(STATUS), the low-order 8 bits of the status.  */
#ifndef WEXITSTATUS
#define WEXITSTATUS(status) (((status) & 0xff00) >> 8)
#endif

/* If WIFSIGNALED(STATUS), the terminating signal.  */
#ifndef WTERMSIG
#define WTERMSIG(status) ((status) & 0x7f)
#endif

/* If WIFSTOPPED(STATUS), the signal that stopped the child.  */
#ifndef WSTOPSIG
#define WSTOPSIG(status) WEXITSTATUS(status)
#endif

/* Nonzero if STATUS indicates normal termination.  */
#ifndef WIFEXITED
#define WIFEXITED(status) (((status) & 0x7f) == 0)
#endif

/* Nonzero if STATUS indicates the child is stopped.  */
#ifndef WIFSTOPPED
#define WIFSTOPPED(status) (((status) & 0xff) == 0x7f)
#endif

/* Nonzero if STATUS indicates termination by a signal.  */
#ifndef WIFSIGNALED
#define WIFSIGNALED(status) (!WIFSTOPPED(status) && !WIFEXITED(status))
#endif

/**
 * \addtogroup child-process
 * @{
 */
static struct list_head child_process_list = LIST_HEAD_INIT(child_process_list);
static BOOL child_process_handler;

static void
child_process_signal(JSContext* ctx, JSValueConst handler) {
  JSValue os = js_global_get_str(ctx, "os");
  JSValue sig = JS_GetPropertyStr(ctx, os, "signal");
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

static JSValue
child_process_sigchld(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int status = 0, pid = waitpid(-1, &status, WNOHANG);
  ChildProcess* cp;

  if((cp = child_process_get(pid))) {
    if(child_process_status(cp, status))
      child_process_remove(cp, ctx);
  }

  return JS_UNDEFINED;
}

ChildProcess*
child_process_get(int pid) {
  struct list_head* el;

  list_for_each(el, &child_process_list) {
    ChildProcess* cp = list_entry(el, ChildProcess, link);

    if(cp->pid == pid)
      return cp;
  }

  return 0;
}

ChildProcess*
child_process_new(JSContext* ctx) {
  ChildProcess* child;

  if((child = js_mallocz(ctx, sizeof(ChildProcess)))) {
    child->use_path = true;
    child->exited = child->signaled = child->stopped = child->continued = false;

    child->exitcode = -1;
    child->termsig = -1;
    child->stopsig = -1;
    child->pid = -1;

    child->uid = -1;
    child->gid = -1;

    child->num_fds = 0;

    child->child_fds = child->parent_fds = child->pipe_fds = NULL;

    if(!child_process_handler) {
      JSValue fn = JS_NewCFunction(ctx, child_process_sigchld, "sigchld", 0);
      child_process_signal(ctx, fn);
      JS_FreeValue(ctx, fn);
      child_process_handler = TRUE;
    }
  }

  return child;
}

void
child_process_remove(ChildProcess* cp, JSContext* ctx) {
  list_del(&cp->link);

  if(list_empty(&child_process_list)) {
    child_process_signal(ctx, JS_NULL);
    child_process_handler = FALSE;
  }
}

char**
child_process_environment(JSContext* ctx, JSValueConst object) {
  PropertyEnumeration propenum;
  Vector args;

  if(property_enumeration_init(&propenum, ctx, object, PROPENUM_DEFAULT_FLAGS))
    return 0;

  vector_init(&args, ctx);

  do {
    size_t namelen, valuelen;
    const char* name = property_enumeration_keystrlen(&propenum, &namelen, ctx);
    const char* value = property_enumeration_valuestrlen(&propenum, &valuelen, ctx);
    char* var = js_malloc(ctx, namelen + 1 + valuelen + 1);

    memcpy(var, name, namelen);
    var[namelen] = '=';
    memcpy(&var[namelen + 1], value, valuelen);
    var[namelen + 1 + valuelen] = '\0';

    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, value);

    vector_push(&args, var);

  } while(property_enumeration_next(&propenum));

  vector_emplace(&args, sizeof(char*));

  return (char**)vector_begin(&args);
}

#ifdef _WIN32
static char*
argv_to_string(char* const* argv, char delim) {
  int i, len;
  char *ptr, *str;

  if(argv == NULL)
    return NULL;

  for(i = 0, len = 0; argv[i]; i++)
    len += strlen(argv[i]) + 1;

  if((str = ptr = (char*)malloc(len + 1)) == NULL)
    return NULL;

  for(i = 0; argv[i]; i++) {
    len = strlen(argv[i]);
    memcpy(ptr, argv[i], len);
    ptr += len;
    *ptr++ = delim;
  }

  *ptr = 0;
  *--ptr = 0;

  return str;
}
#endif

int
child_process_spawn(ChildProcess* cp) {
#ifdef _WIN32
  intptr_t pid;
  DynBuf db;
  char *file, *args, *env;
  PROCESS_INFORMATION pinfo;
  STARTUPINFOA sinfo;
  SECURITY_ATTRIBUTES sattr;
  BOOL search, success;

  sattr.nLength = sizeof(SECURITY_ATTRIBUTES);
  sattr.bInheritHandle = TRUE;
  sattr.lpSecurityDescriptor = NULL;

  ZeroMemory(&pinfo, sizeof(PROCESS_INFORMATION));
  ZeroMemory(&sinfo, sizeof(STARTUPINFO));

  sinfo.cb = sizeof(STARTUPINFO);
  sinfo.hStdError = (HANDLE)_get_osfhandle(cp->child_fds[2]);
  sinfo.hStdOutput = (HANDLE)_get_osfhandle(cp->child_fds[1]);
  sinfo.hStdInput = (HANDLE)_get_osfhandle(cp->child_fds[0]);
  sinfo.dwFlags |= STARTF_USESTDHANDLES;

  search = cp->use_path && path_isname(cp->file);
  file = search ? NULL : cp->file;
  args = argv_to_string(cp->args, ' ');
  env = cp->env ? argv_to_string(cp->env, '\0') : NULL;

  success = CreateProcessA(file, args, &sattr, NULL, TRUE, CREATE_NO_WINDOW, env, cp->cwd, &sinfo, &pinfo);

  free(args);

  if(env)
    free(env);

  if(!success) {
    fprintf(stderr, "CreateProcessA error: %ld\n", (long int)GetLastError());
    pid = -1;
  } else {
    pid = pinfo.dwProcessId;
  }

#elif defined(POSIX_SPAWN)
  int i;
  pid_t pid;
  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attr;

  posix_spawnattr_init(&attr);
  posix_spawnattr_setflags(&attr, 0);

  posix_spawn_file_actions_init(&actions);

  for(i = 0; i <= 2; i++)
    if(cp->child_fds[i] >= 0)
      posix_spawn_file_actions_adddup2(&actions, cp->child_fds[i], i);

  if((cp->use_path ? posix_spawnp : posix_spawn)(&pid, cp->file, &actions, &attr, cp->args, cp->env)) {
    fprintf(stderr, "posix_spawnp error: %s\n", strerror(errno));
    return -1;
  }

#else
  int i;
  pid_t pid;

  if((pid = fork()) == 0) {
    if(cp->parent_fds)
      for(i = 0; i < cp->num_fds; i++)
        if(cp->parent_fds && cp->parent_fds[i] >= 0)
          close(cp->parent_fds[i]);

    if(cp->child_fds) {
      for(i = 0; i < cp->num_fds; i++) {
        if(cp->child_fds[i] >= 0) {
          if(cp->child_fds[i] != i) {
            dup2(cp->child_fds[i], i);
            close(cp->child_fds[i]);
          }
        }
      }
    }

    if(cp->cwd)
      if(chdir(cp->cwd) == -1)
        perror("chdir()");

#ifndef __ANDROID__
    if(cp->uid != -1 && setuid(cp->uid) == -1)
      perror("setuid()");
    if(cp->gid != -1 && setgid(cp->gid) == -1)
      perror("setgid()");
#endif

#if HAVE_EXECVPE
    (cp->use_path ? execvpe : execve)(cp->file, cp->args, cp->env ? cp->env : environ);
    perror("execvp()");
#else
    if(cp->env) {
      size_t i;
      for(i = 0; cp->env[i]; i++)
        putenv(cp->env[i]);
    }

    (cp->use_path ? execvp : execv)(cp->file, cp->args);
    perror("execvp()");
#endif
    exit(errno);
  }

#ifdef DEBUG_OUTPUT
  printf("forked proc %d\n", pid);
#endif
#endif

  if(cp->pipe_fds)
    for(i = 0; i < cp->num_fds; i++)
      if(cp->child_fds[i] >= 0 && cp->pipe_fds[i]) {
        close(cp->child_fds[i]);
        cp->child_fds[i] = -1;
        cp->pipe_fds[i] = -1;
      }

  list_add_tail(&cp->link, &child_process_list);

  return cp->pid = pid;
}

bool
child_process_status(ChildProcess* cp, int status) {
  cp->status = status;

  cp->exited = WIFEXITED(status);
  cp->signaled = WIFSIGNALED(status);
  cp->stopped = WIFSTOPPED(status);

#ifdef WIFCONTINUED
  if((cp->continued = WIFCONTINUED(status)))
    cp->stopsig = -1;
#else
  cp->continued = 0;
#endif

  if(cp->exited)
    cp->exitcode = WEXITSTATUS(status);

  if(cp->signaled || cp->exited)
    cp->termsig = WTERMSIG(status);

  if(cp->stopped)
    cp->stopsig = WSTOPSIG(status);

  return cp->signaled || cp->exited;
}

int
child_process_wait(ChildProcess* cp, int flags) {
#ifdef _WIN32
  DWORD exitcode = 0;
  HANDLE hproc = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, cp->pid);

  for(;;) {
    DWORD ret = WaitForSingleObject(hproc, INFINITE);

    if(ret == WAIT_TIMEOUT)
      continue;

    if(ret == WAIT_FAILED)
      return -1;

    if(ret == WAIT_OBJECT_0) {
      GetExitCodeProcess(hproc, &exitcode);
      CloseHandle(hproc);

      if(exitcode == STILL_ACTIVE)
        return -1;

      cp->exitcode = exitcode;
      return cp->pid;
    }
  }

  return -1;

/*#elif defined(POSIX_SPAWN)

  return -1;*/
#else
  int status = 0, pid = waitpid(cp ? cp->pid : -1, &status, flags);

  if((cp && pid == cp->pid) || (cp = child_process_get(pid)))
    child_process_status(cp, status);

  return pid;
#endif
}

int
child_process_kill(ChildProcess* cp, int signum) {
#ifdef _WIN32
  if(TerminateProcess((HANDLE)cp->pid, 0))
    return 0;
  return -1;
#else
  return kill(cp->pid, signum);
#endif
}

void
child_process_free(ChildProcess* cp, JSContext* ctx) {
  child_process_free_rt(cp, JS_GetRuntime(ctx));
}

void
child_process_free_rt(ChildProcess* cp, JSRuntime* rt) {
  if(cp->file)
    js_free_rt(rt, cp->file);

  if(cp->cwd)
    js_free_rt(rt, cp->cwd);

  if(cp->args)
    js_strv_free_rt(rt, cp->args);

  if(cp->env)
    js_strv_free_rt(rt, cp->env);

  if(cp->child_fds) {
    for(int i = 0; i < cp->num_fds; i++)
      if(cp->pipe_fds && cp->pipe_fds[i])
        close(cp->child_fds[i]);

    js_free_rt(rt, cp->child_fds);
  }

  if(cp->parent_fds) {
    for(int i = 0; i < cp->num_fds; i++)
      if(cp->pipe_fds && cp->pipe_fds[i])
        close(cp->parent_fds[i]);

    js_free_rt(rt, cp->parent_fds);
  }

  if(cp->pipe_fds)
    js_free_rt(rt, cp->pipe_fds);

  js_free_rt(rt, cp);
}

const char* child_process_signals[32] = {
    0,           "SIGHUP",  "SIGINT",    "SIGQUIT", "SIGILL",   "SIGTRAP", "SIGABRT", "SIGBUS",
    "SIGFPE",    "SIGKILL", "SIGUSR1",   "SIGSEGV", "SIGUSR2",  "SIGPIPE", "SIGALRM", "SIGTERM",
    "SIGSTKFLT", "SIGCHLD", "SIGCONT",   "SIGSTOP", "SIGTSTP",  "SIGTTIN", "SIGTTOU", "SIGURG",
    "SIGXCPU",   "SIGXFSZ", "SIGVTALRM", "SIGPROF", "SIGWINCH", "SIGIO",   "SIGPWR",  "SIGSYS",
};
/**
 * @}
 */
