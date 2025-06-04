#ifndef BITSET_H
#define BITSET_H

#include "buffer-utils.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint8_t* ptr;
  size_t len;
} BitSet;

#define BITSET_INIT() \
  (BitSet) { 0, 0 }

#define bitset_size(bs) ((bs)->len)

bool bitset_resize(BitSet*, size_t);
bool bitset_isset(BitSet*, int);
bool bitset_assign(BitSet*, int, bool);
bool bitset_toggle(BitSet*, int);
void bitset_free(BitSet*);
bool bitset_push(BitSet*, int, size_t);
int bitset_pop(BitSet*, size_t);

#endif /* defined(BITSET) */
