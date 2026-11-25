/* diet-dlstub.c: dlopen() and friends for a static dietlibc binary.
 * depends on: nothing. linked into qjsm only when built with DIET.
 * rule: loading a shared object always fails, dlerror() says why. */
#include <stddef.h>

void*
dlopen(const char* path, int mode) {
  return NULL;
}

void*
dlsym(void* handle, const char* name) {
  return NULL;
}

int
dlclose(void* handle) {
  return 0;
}

char*
dlerror(void) {
  return (char*)"dlopen is not available in a static dietlibc binary";
}
