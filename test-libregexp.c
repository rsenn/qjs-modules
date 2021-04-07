#include "../libregexp.h"
#include "../quickjs.h"
#include "quickjs-internal.h"
#include <string.h>

#define CAPTURE_COUNT_MAX 255

int
main(int argc, char** argv) {
  int len, ret, i;
  uint8_t* bc;
  char error_msg[64];
  uint8_t* capture[CAPTURE_COUNT_MAX * 2];
  const char* input;
  int input_len, capture_count;

  if(argc < 3) {
    // printf("usage: %s regexp input\n", argv[0]);
    exit(1);
  }
  bc = lre_compile(&len, error_msg, sizeof(error_msg), argv[1], strlen(argv[1]), 0, NULL);
  if(!bc) {
    fprintf(stderr, "error: %s\n", error_msg);
    exit(1);
  }

  input = argv[2];
  input_len = strlen(input);

  ret = lre_exec(capture, bc, (uint8_t*)input, 0, input_len, 0, NULL);
  // printf("ret=%d\n", ret);
  if(ret == 1) {
    capture_count = lre_get_capture_count(bc);
    // printf("capture_count: %d\n", capture_count);
    for(i = 0; i < 2 * capture_count; i++) {
      uint8_t* ptr;
      ptr = capture[i];
      // printf("%d: ", i);
      if(!ptr)
        // printf("<nil>");
        else
      // printf("%u", (int)(ptr - (uint8_t*)input));
      // printf("\n");
    }
  }
  return 0;
}