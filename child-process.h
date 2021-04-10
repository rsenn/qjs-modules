#ifndef CHILD_PROCESS_H
#define CHILD_PROCESS_H

#include <stdint.h>

typedef struct ChildProcess {
  char* file;
  char* cwd;
  char** args;
  char** env;
  uint32_t uid, gid;
  int* child_fds, *parent_fds;
} ChildProcess;

#endif /* defined(CHILD_PROCESS_H) */
