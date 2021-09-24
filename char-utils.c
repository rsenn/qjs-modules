#include "char-utils.h"

size_t
token_length(const char* str, size_t len, char delim) {
  const char *s, *e;
  size_t pos;
  for(s = str, e = s + len; s < e; s += pos + 1) {
    pos = byte_chr(s, e - s, delim);
    if(s + pos == e)
      break;

    if(pos == 0 || s[pos - 1] != '\\') {
      s += pos;
      break;
    }
  }
  return s - str;
}

size_t
fmt_ulong(char* dest, unsigned long i) {
  unsigned long len, tmp, len2;
  for(len = 1, tmp = i; tmp > 9; ++len) tmp /= 10;
  if(dest)
    for(tmp = i, dest += len, len2 = len + 1; --len2; tmp /= 10) *--dest = (char)((tmp % 10) + '0');
  return len;
}

size_t
scan_ushort(const char* src, unsigned short* dest) {
  const char* cur;
  unsigned short l;
  for(cur = src, l = 0; *cur >= '0' && *cur <= '9'; ++cur) {
    unsigned long tmp = l * 10ul + *cur - '0';
    if((unsigned short)tmp != tmp)
      break;
    l = tmp;
  }
  if(cur > src)
    *dest = l;
  return (size_t)(cur - src);
}
