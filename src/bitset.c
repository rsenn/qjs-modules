#include "bitset.h"
#include <stdlib.h>
#include <stdbool.h>

bool
bitset_resize(BitSet* bs, size_t bits) {
  size_t bytes = (bs->len + 7) >> 3;
  size_t need = (bits + 7) >> 3;

  if(need > bytes || bs->ptr == 0) {
    if(!(bs->ptr = realloc(bs->ptr, need)))
      return false;

    if(need > bytes)
      memset(&bs->ptr[bytes], 0, need - bytes);
  }

  bs->len = bits;
  return true;
}

#define bitset_byte(bs, bit) ((bs)->ptr[(bit) >> 3])

#define bitset_index(bs, idx) WRAP_NUM((idx), (signed)(bs)->len)

/*static inline size_t
bitset_index(BitSet* bs, int32_t idx) {
  idx = WRAP_NUM(idx, (signed)bs->len);

  assert(idx >= 0);  assert(idx < bs->len);

  return idx;
}*/

bool
bitset_isset(BitSet* bs, int idx) {
  size_t bit = bitset_index(idx);
  uint8_t b = bitset_byte(bs, bit);

  return (b >> (bit & 7)) & 1;
}

bool
bitset_assign(BitSet* bs, int idx, bool value) {
  size_t bit = bitset_index(idx);

  if(bit >= bs->len) {
    if(!bitset_resize(bs, bit + 1))
      return false;
  }

  uint8_t* b = &bitset_byte(bs, bit);
  uint8_t mask = 1 << (bit & 7);

  (*b) = value ? (*b) | mask : (*b) & (~mask);
  return true;
}

bool
bitset_toggle(BitSet* bs, int idx) {
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
§§
