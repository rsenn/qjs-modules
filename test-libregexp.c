#include "../libregexp.h"
#include "../quickjs.h"
#include "quickjs-internal.h"
#include <string.h>
/*
int
main(int argc, char* argv[]) {
  uint8_t* re_bytecode;
  int re_bytecode_len;
  char error_msg[64];
  size_t len;
  uint8_t **capture, *str_buf;
  int ret, capture_count, shift;
  int64_t last_index = 0;
  int re_flags = LRE_FLAG_GLOBAL;

  str_buf = (uint8_t*)"^([ABC])(.*)$";
  len = strlen((char*)str_buf);

  re_bytecode = lre_compile(&re_bytecode_len, error_msg, sizeof(error_msg), (const char*)str_buf, len, re_flags, 0);

  capture_count = lre_get_capture_count(re_bytecode);
  capture = NULL;

  if(capture_count > 0) {
    capture = calloc(sizeof(capture[0]), capture_count * 2);
    if(!capture) {
      return 126;
    }
  }

  shift = 0;
  str_buf = (uint8_t*)(argc > 1 ? argv[1] : "BLAH");
  last_index = strlen((char*)str_buf);

  ret = lre_exec(capture, re_bytecode, str_buf, last_index, strlen((char*)str_buf), shift, 0);
  return !ret;
}*/
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
    printf("usage: %s regexp input\n", argv[0]);
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
  printf("ret=%d\n", ret);
  if(ret == 1) {
    capture_count = lre_get_capture_count(bc);
    printf("capture_count: %d\n", capture_count);
    for(i = 0; i < 2 * capture_count; i++) {
      uint8_t* ptr;
      ptr = capture[i];
      printf("%d: ", i);
      if(!ptr)
        printf("<nil>");
      else
        printf("%u", (int)(ptr - (uint8_t*)input));
      printf("\n");
    }
  }
  return 0;
}