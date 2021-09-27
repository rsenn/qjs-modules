#include "child-process.h"
#include "utils.h"
#include "property-enumeration.h"

#include <errno.h>

#define POSIX_SPAWN 1

#ifdef _WIN32
#include <io.h>
#else
#if POSIX_SPAWN
#include <spawn.h>
#else
#include <unistd.h>
#endif
#include <sys/wait.h>
#endif

static struct list_head child_process_list = LIST_HEAD_INIT(child_process_list);

void
child_process_sigchld(int pid) {
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
  child = js_mallocz(ctx, sizeof(ChildProcess));
  list_add_tail(&child->link, &child_process_list);
  child->exitcode = -1;
  child->termsig = -1;
  child->stopsig = -1;
  child->pid = -1;
  return child;
}

char**
child_process_environment(JSContext* ctx, JSValueConst object) {
  PropertyEnumeration propenum;
  Vector args;

  if(property_enumeration_init(&propenum, ctx, object, PROPENUM_DEFAULT_FLAGS))
    return 0;

  vector_init(&args, ctx);

  do {
    char* var;
    const char *name, *value;
    size_t namelen, valuelen;

    name = property_enumeration_keystrlen(&propenum, &namelen, ctx);
    value = property_enumeration_valuestrlen(&propenum, &valuelen, ctx);

    var = js_malloc(ctx, namelen + 1 + valuelen + 1);

    memcpy(var, name, namelen);
    var[namelen] = '=';
    memcpy(&var[namelen + 1], value, valuelen);
    var[namelen + 1 + valuelen] = '\0';

    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, value);

    vector_push(&args, var);

  } while(property_enumeration_next(&propenum));

  vector_emplace(&args, sizeof(char*));
  return (char**)args.data;
}

int
child_process_spawn(ChildProcess* cp) {
#if POSIX_SPAWN
  pid_t pid;
  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attr;

  posix_spawnattr_setflags(&attr, 0);
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, cp->child_fds[0], 0);
  posix_spawn_file_actions_adddup2(&actions, cp->child_fds[1], 1);
  posix_spawn_file_actions_adddup2(&actions, cp->child_fds[2], 2);

  if(posix_spawnp(&pid, cp->file, &actions, &attr, cp->args, NULL)) {
    fprintf(stderr, "posix_spawnp error: %s\n", strerror(errno));
    return -1;
  }

  return cp->pid = pid;
#else
  int pid, i;

  if((pid = fork()) == 0) {

    if(cp->parent_fds) {
      for(i = 0; i < cp->num_fds; i++) {
        if(cp->parent_fds && cp->parent_fds[i] >= 0)
          close(cp->parent_fds[i]);
      }
    }

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
      chdir(cp->cwd);

    setuid(cp->uid);
    setgid(cp->gid);

    execvpe(cp->file, cp->args, cp->env);
    perror("execvp()");
    exit(errno);
  }
  if(cp->child_fds) {
    for(i = 0; i < cp->num_fds; i++) {
      if(cp->child_fds[i] >= 0 && cp->child_fds[i] != i)
        close(cp->child_fds[i]);
    }
  }
  return cp->pid = pid;
#endif
}

int
child_process_wait(ChildProcess* cp, int flags) {
  int pid, status;

  if((pid = waitpid(cp->pid, &status, flags)) != cp->pid)
    return pid;

  cp->signaled = WIFSIGNALED(status);
  cp->stopped = WIFSTOPPED(status);
  cp->continued = WIFCONTINUED(status);

  if(WIFEXITED(status))
    cp->exitcode = WEXITSTATUS(status);

  if(WIFSIGNALED(status) || WIFEXITED(status))
    cp->termsig = WTERMSIG(status);
  if(WIFSTOPPED(status))
    cp->stopsig = WSTOPSIG(status);
  if(WIFCONTINUED(status))
    cp->stopsig = -1;

  return pid;
}

int
child_process_kill(ChildProcess* cp, int signum) {
  int ret;
  int status;
  ret = kill(cp->pid, signum);

  if(ret != -1 && waitpid(cp->pid, &status, WNOHANG) == cp->pid) {
    if(WIFEXITED(status))
      cp->exitcode = WEXITSTATUS(status);

    if(WIFSIGNALED(status))
      cp->termsig = WTERMSIG(status);
  }
  return ret;
}

void
child_process_free(ChildProcess* cp, JSContext* ctx) {
  list_del(&cp->link);
  if(cp->file)
    js_free(ctx, cp->file);
  if(cp->cwd)
    js_free(ctx, cp->cwd);
  if(cp->args)
    js_strv_free(ctx, cp->args);
  if(cp->env)
    js_strv_free(ctx, cp->env);

  js_free(ctx, cp);
}
void
child_process_free_rt(ChildProcess* cp, JSRuntime* rt) {
  list_del(&cp->link);
  if(cp->file)
    js_free_rt(rt, cp->file);
  if(cp->cwd)
    js_free_rt(rt, cp->cwd);
  if(cp->args)
    js_strv_free_rt(rt, cp->args);
  if(cp->env)
    js_strv_free_rt(rt, cp->env);

  js_free_rt(rt, cp);
}
