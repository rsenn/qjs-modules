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

size_t
fmt_longlong(char* dest, int64_t i) {
  if(i < 0) {
    if(dest)
      *dest++ = '-';
    return fmt_ulonglong(dest, (uint64_t)-i) + 1;
  } else
    return fmt_ulonglong(dest, (uint64_t)i);
}

size_t
fmt_ulonglong(char* dest, uint64_t i) {
  size_t len;
  uint64_t tmp, len2;
  for(len = 1, tmp = i; tmp > 9; ++len) tmp /= 10;
  if(dest)
    for(tmp = i, dest += len, len2 = len + 1; --len2; tmp /= 10) *--dest = (tmp % 10) + '0';
  return len;
}

#define tohex(c) (char)((c) >= 10 ? (c)-10 + 'a' : (c) + '0')

size_t
fmt_xlonglong(char* dest, uint64_t i) {
  uint64_t len, tmp;
  for(len = 1, tmp = i; tmp > 15; ++len) tmp >>= 4;
  if(dest)
    for(tmp = i, dest += len;;) {
      *--dest = tohex(tmp & 15);
      if(!(tmp >>= 4)) {
        break;
      };
    }
  return len;
}

#ifndef MAXLONG
#define MAXLONG (((unsigned long)-1) >> 1)
#endif

size_t
scan_longlong(const char* src, int64_t* dest) {
  size_t i, o;
  uint64_t l;
  char c = src[0];
  unsigned int neg = c == '-';
  o = c == '-' || c == '+';
  if((i = scan_ulonglong(src + o, &l))) {
    if(i > 0 && l > MAXLONG + neg) {
      l /= 10;
      --i;
    }
    if(i + o)
      *dest = (int64_t)(c == '-' ? -l : l);
    return i + o;
  }
  return 0;
}

size_t
scan_ulonglong(const char* src, uint64_t* dest) {
  const char* tmp = src;
  uint64_t l = 0;
  unsigned char c;
  while((c = (unsigned char)(*tmp - '0')) < 10) {
    uint64_t n;
    n = l << 3;
    if((n >> 3) != l)
      break;
    if(n + (l << 1) < n)
      break;
    n += l << 1;
    if(n + c < n)
      break;
    l = n + c;
    ++tmp;
  }
  if(tmp - src)
    *dest = l;
  return (size_t)(tmp - src);
}

size_t
scan_xlonglong(const char* src, uint64_t* dest) {
  const char* tmp = src;
  int64_t l = 0;
  unsigned char c;
  while((c = scan_fromhex(*tmp)) < 16) {
    l = (l << 4) + c;
    ++tmp;
  }
  *dest = l;
  return tmp - src;
}
