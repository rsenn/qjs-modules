#ifndef CHILD_PROCESS_H
#define CHILD_PROCESS_H

#include "quickjs.h"

#include <stdint.h>

typedef struct ChildProcess {
  char* file;
  char* cwd;
  char** args;
  char** env;
  int pid;
  int exitcode;
  int termsig;
  uint32_t uid, gid;
  int num_fds;
  int *child_fds, *parent_fds;
} ChildProcess;

char** child_process_environment(JSContext*, JSValue object);
ChildProcess* child_process_new(JSContext*);
int child_process_spawn(ChildProcess*);
int child_process_wait(ChildProcess*);
int child_process_kill(ChildProcess*, int);

#endif /* defined(CHILD_PROCESS_H) */
