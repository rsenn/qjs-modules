#define glob openbsd_glob
#define globfree openbsd_globfree

#include <stdio.h>
#include <sys/stat.h>
#include "glob.h"

int
main() {
  glob_t g = {0};
  int result = glob("*.c", GLOB_BRACE | GLOB_TILDE, 0, &g);

  printf("result = %d\n", result);
}
