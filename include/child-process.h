#ifndef CHILD_PROCESS_H
#define CHILD_PROCESS_H

#include <quickjs.h>
#include <list.h>
#include <stdint.h>

/**
 * \defgroup child-process child-process: Child processes
 * @{
 */

typedef struct ChildProcess {
  char* file;
  char* cwd;
  char** args;
  char** env;
  intptr_t pid;
  int exitcode;
  int termsig;
  int stopsig;
  unsigned use_path : 1;
  unsigned signaled : 1, stopped : 1, continued : 1;
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

extern const char* child_process_signals[32];

#ifdef _WIN32
#define WNOWAIT 0x1000000
#define WNOHANG 1
#define WUNTRACED 2
#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#ifndef SIGABRT
#define SIGABRT 6
#endif
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGSTKFLT 16
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGIO 29
#define SIGPWR 30
#define SIGSYS 31
#endif

/**
 * @}
 */
#endif /* defined(CHILD_PROCESS_H) */
