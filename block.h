#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>

typedef struct block {
  uint8_t* base;
  size_t size;
} block_t;

#endif /* defined(BLOCK_H) */
