#include "bitset.h"
#include <stdlib.h>
#include <stdbool.h>

bool
bitset_resize(BitSet* bs, size_t bits) {
  size_t old = (bs->len + 7) >> 3;
  size_t new = (bits + 7) >> 3;

  if(new > old || bs->ptr == 0) {
    if(!(bs->ptr = realloc(bs->ptr, new)))
      return false;

    if(new > old)
      memset(&bs->ptr[old], 0, new - old);
  }

  bs->len = bits;
  return true;
}

#define bitset_byte(bs, bit) ((bs)->ptr[(bit) >> 3])

#define bitset_index(bs, idx) WRAP_NUM((idx), (signed)(bs)->len)

/*static inline size_t
bitset_index(BitSet* bs, int32_t idx) {
  idx = WRAP_NUM(idx, (signed)bs->len);

  assert(idx >= 0);
  assert(idx < bs->len);

  return idx;
}*/

bool
bitset_at(BitSet* bs, int32_t idx) {
  size_t bit = bitset_index(idx);
  uint8_t b = bitset_byte(bs, bit);

  return (b >> (bit & 7)) & 1;
}

void
bitset_assign(BitSet* bs, int32_t idx, bool value) {
  size_t bit = bitset_index(idx);
  uint8_t* b = &bitset_byte(bs, bit);
  uint8_t mask = 1 << (bit & 7);

  (*b) = value ? (*b) | mask : (*b) & (~mask);
}

bool
bitset_toggle(BitSet* bs, int32_t idx) {
  size_t bit = bitset_index(idx);
  uint8_t* b = &bitset_byte(bs, bit);
  uint8_t mask = 1 << (bit & 7);

  (*b) ^= mask;

  return !!((*b) & mask);
}

void
bitset_free(BitSet* bs) {
  if(bs->ptr) {
    free(bs->ptr);
    bs->ptr = 0;
  }

  bs->len = 0;
}
