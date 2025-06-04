#ifndef BITSET_H
#define BITSET_H

#include "buffer-utils.h"

typedef struct {
  uint8_t* buf;
  size_t len;
} BitSet;

#define BITSET_INIT() \
  (BitSet) { 0, 0 }

#define bitset_size(bs) ((bs)->len)

bool bitset_resize(BitSet*, size_t);
bool bitset_get(BitSet*, size_t);
void bitset_set(BitSet*, size_t, bool);
void bitset_free(BitSet*);

#endif /* defined(BITSET) */
