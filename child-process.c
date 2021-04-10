#define _GNU_SOURCE

#include "child-process.h"
#include "utils.h"
#include "property-enumeration.h"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif
#include <errno.h>

ChildProcess*
child_process_new(JSContext* ctx) {
  return js_mallocz(ctx, sizeof(ChildProcess));
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
}

int
child_process_wait(ChildProcess* cp) {
  int pid,status;
  if((pid=waitpid(cp->pid, &status, 0)) == -1)
    return 0;

  cp->exitcode = -1;
  cp->termsig = -1;

if(pid == cp->pid) {
  if(WIFEXITED(status)) {
    cp->exitcode = WEXITSTATUS(status);
    return 1;
  }

  if(WIFSIGNALED(status)) {
    cp->termsig = WTERMSIG(status);
    return 1;
  }
}
  return 0;
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
