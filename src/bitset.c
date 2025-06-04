#include "bitset.h"
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>

#define bitset_BYTE(bs, bit) ((bs)->ptr[(bit) >> 3])
#define bitset_INDEX(bs, idx) WRAP_NUM((idx), (signed)(bs)->len)

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

bool
bitset_isset(BitSet* bs, int idx) {
  size_t bit = bitset_INDEX(idx);
  assert(bit < bs->len);
  uint8_t b = bitset_BYTE(bs, bit);

  return (b >> (bit & 7)) & 1;
}

bool
bitset_assign(BitSet* bs, int idx, bool value) {
  size_t bit = bitset_INDEX(idx);

  if(bit >= bs->len)
    if(!bitset_resize(bs, bit + 1))
      return false;

  assert(bit < bs->len);

  uint8_t* b = &bitset_BYTE(bs, bit);
  uint8_t mask = 1 << (bit & 7);

  (*b) = value ? (*b) | mask : (*b) & (~mask);
  return true;
}

bool
bitset_toggle(BitSet* bs, int idx) {
  size_t bit = bitset_INDEX(idx);
  assert(bit < bs->len);
  uint8_t* b = &bitset_BYTE(bs, bit);
  uint8_t mask = 1 << (bit & 7);

  (*b) ^= mask;

  return !!((*b) & mask);
}

bool
bitset_push(BitSet* bs, int bits, size_t num_bits) {
  size_t i = bs->len;

  if(!bitset_resize(bs, bs->len + num_bits))
    return false;

  while(i < bs->len) {
    bitset_assign(bs, i++, bits & 1);
    bits >>= 1;
  }

  return true;
}

void
bitset_free(BitSet* bs) {
  if(bs->ptr) {
    free(bs->ptr);
    bs->ptr = 0;
  }

  bs->len = 0;
}
