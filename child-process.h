#ifndef CHILD_PROCESS_H
#define CHILD_PROCESS_H

#include <stdint.h>

typedef struct ChildProcess {
  char* file;
  char* cwd;
  char** args;
  char** env;
  uint32_t uid, gid;
} ChildProcess;

#endif /* defined(CHILD_PROCESS_H) */
