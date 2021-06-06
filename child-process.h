#ifndef CHILD_PROCESS_H
#define CHILD_PROCESS_H

#include "quickjs.h"
#include "list.h"

#include <stdint.h>

typedef struct ChildProcess {
  char* file;
  char* cwd;
  char** args;
  char** env;
  int pid;
  int exitcode;
  int termsig;
  int stopsig;
  uint32_t uid, gid;
  int num_fds;
  int *child_fds, *parent_fds;
  struct list_head link;
} ChildProcess;

char** child_process_environment(JSContext*, JSValue object);
ChildProcess* child_process_new(JSContext*);
ChildProcess* child_process_get(int pid);
void child_process_sigchld(int pid);
int child_process_spawn(ChildProcess*);
int child_process_wait(ChildProcess*, int);
int child_process_kill(ChildProcess*, int);
void child_process_free(ChildProcess*, JSContext*);
void child_process_free_rt(ChildProcess*, JSRuntime*);

#endif /* defined(CHILD_PROCESS_H) */
