#include "char-utils.h"
#include "libutf/include/libutf.h"
#if defined(_WIN32) || defined(__CYGWIN__) || defined(__MSYS__)
#include <winnls.h>
#include <windows.h>
#include <wchar.h>
#endif

/**
 * \addtogroup char-utils
 * @{
 */
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
fmt_ulong(char* dest, uint32_t i) {
  uint32_t len, tmp, len2;

  for(len = 1, tmp = i; tmp > 9; ++len)
    tmp /= 10;

  if(dest)
    for(tmp = i, dest += len, len2 = len + 1; --len2; tmp /= 10)
      *--dest = (char)((tmp % 10) + '0');

  return len;
}

size_t
fmt_longlong(char* dest, int64_t i) {
  if(i < 0) {
    if(dest)
      *dest++ = '-';
    return fmt_ulonglong(dest, (uint64_t)-i) + 1;
  }

  return fmt_ulonglong(dest, (uint64_t)i);
}

size_t
fmt_ulonglong(char* dest, uint64_t i) {
  size_t len;
  uint64_t tmp, len2;

  for(len = 1, tmp = i; tmp > 9ll; ++len)
    tmp /= 10ll;

  if(dest)
    for(tmp = i, dest += len, len2 = len + 1; --len2; tmp /= 10ll)
      *--dest = (tmp % 10ll) + '0';

  return len;
}

#define tohex(c) (char)((c) >= 10 ? (c)-10 + 'a' : (c) + '0')

size_t
fmt_xlonglong(char* dest, uint64_t i) {
  uint64_t len, tmp;

  for(len = 1, tmp = i; tmp > 15ll; ++len)
    tmp >>= 4ll;

  if(dest)
    for(tmp = i, dest += len;;) {
      *--dest = tohex(tmp & 15ll);

      if(!(tmp >>= 4ll))
        break;
    }

  return len;
}

size_t
fmt_xlonglong0(char* dest, uint64_t num, size_t n) {
  size_t i = 0, len;

  if((len = fmt_xlonglong(NULL, num)) < n) {
    len = n - len;

    while(i < len)
      dest[i++] = '0';
  }

  i += fmt_xlonglong(&dest[i], num);
  return i;
}

size_t
fmt_8long(char* dest, uint32_t i) {
  uint32_t len, tmp;

  /* first count the number of bytes needed */
  for(len = 1, tmp = i; tmp > 7; ++len)
    tmp >>= 3;

  if(dest)
    for(tmp = i, dest += len;;) {
      *--dest = (char)((tmp & 7) + '0');

      if(!(tmp >>= 3))
        break;
    }

  return len;
}

#define tohex(c) (char)((c) >= 10 ? (c)-10 + 'a' : (c) + '0')

size_t
fmt_xlong(char* dest, uint32_t i) {
  uint32_t len, tmp;

  /* first count the number of bytes needed */
  for(len = 1, tmp = i; tmp > 15; ++len)
    tmp >>= 4;

  if(dest)
    for(tmp = i, dest += len;;) {
      *--dest = tohex(tmp & 15);

      if(!(tmp >>= 4))
        break;
    }

  return len;
}

size_t
fmt_xlong0(char* dest, uint32_t num, size_t n) {
  size_t i = 0, len;

  if((len = fmt_xlong(NULL, num)) < n) {
    len = n - len;

    while(i < len)
      dest[i++] = '0';
  }

  i += fmt_xlong(&dest[i], num);
  return i;
}

size_t
scan_ushort(const char* src, uint16_t* dest) {
  const char* cur;
  uint16_t l;

  for(cur = src, l = 0; *cur >= '0' && *cur <= '9'; ++cur) {
    uint32_t tmp = l * 10ul + *cur - '0';

    if((uint16_t)tmp != tmp)
      break;

    l = tmp;
  }

  if(cur > src)
    *dest = l;

  return (size_t)(cur - src);
}

size_t
scan_uint(const char* src, uint32_t* dest) {
  uint64_t u64;
  size_t r = scan_ulonglong(src, &u64);
  *dest = u64;
  return r;
}

size_t
scan_int(const char* src, int32_t* dest) {
  int64_t i64;
  size_t r = scan_longlong(src, &i64);
  *dest = i64;
  return r;
}

#ifndef MAXLONG
#define MAXLONG (((uint32_t)-1) >> 1)
#endif

size_t
scan_longlong(const char* src, int64_t* dest) {
  size_t i, o;
  uint64_t l;
  char c = src[0];
  unsigned int neg = c == '-';
  o = c == '-' || c == '+';

  if((i = scan_ulonglong(src + o, &l))) {
    if(i > 0ll && l > MAXLONG + neg) {
      l /= 10ll;
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
    n = l << 3ll;

    if((n >> 3ll) != l)
      break;

    if(n + (l << 1ll) < n)
      break;

    n += l << 1ll;

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

size_t
scan_8longn(const char* src, size_t n, uint32_t* dest) {
  const char* tmp = src;
  uint32_t l = 0;
  unsigned char c;

  while(n-- > 0 && (c = (unsigned char)(*tmp - '0')) < 8) {
    if(l >> (sizeof(l) * 8 - 3))
      break;

    l = l * 8 + c;
    ++tmp;
  }

  *dest = l;
  return (size_t)(tmp - src);
}

size_t
scan_whitenskip(const char* s, size_t limit) {
  const char *t, *u;

  for(t = s, u = t + limit; t < u; ++t)
    if(!is_whitespace_char(*t))
      break;

  return (size_t)(t - s);
}

size_t
scan_nonwhitenskip(const char* s, size_t limit) {
  const char *t, *u;

  for(t = s, u = t + limit; t < u; ++t)
    if(is_whitespace_char(*t))
      break;

  return (size_t)(t - s);
}

size_t
scan_line(const char* s, size_t limit) {
  const char *t, *u;

  for(t = s, u = s + limit; t < u; ++t)
    if(*t == '\n' || *t == '\r')
      break;

  return (size_t)(t - s);
}

size_t
scan_lineskip(const char* s, size_t limit) {
  const char *t, *u;

  for(t = s, u = s + limit; t < u; ++t)
    if(*t == '\n') {
      ++t;
      break;
    }

  return (size_t)(t - s);
}

size_t
scan_lineskip_escaped(const char* s, size_t limit) {
  const char *t, *u;

  for(t = s, u = s + limit; t < u; ++t) {
    if(*t == '\\') {
      ++t;
      continue;
    }
    if(*t == '\n') {
      ++t;
      break;
    }
  }

  return (size_t)(t - s);
}

size_t
scan_eolskip(const char* s, size_t limit) {
  size_t n = 0;

  if(n + 1 < limit && s[0] == '\r' && s[1] == '\n')
    n += 2;
  else if(n < limit && s[0] == '\n')
    n += 1;

  return n;
}

size_t
utf8_strlen(const void* in, size_t len) {
  const uint8_t *pos, *end, *next;
  size_t i = 0;

  for(pos = (const uint8_t*)in, end = pos + len; pos < end; pos = next, ++i)
    unicode_from_utf8(pos, end - pos, &next);

  return i;
}

#if defined(_WIN32) || defined(__CYGWIN__) || defined(__MSYS__)
wchar_t*
utf8_towcs(const char* s) {
  int len = (int)strlen(s);
  int n = MultiByteToWideChar(CP_UTF8, 0, s, len, NULL, 0);
  wchar_t* ret;

  if((ret = (wchar_t*)malloc((n + 1) * sizeof(wchar_t)))) {
    MultiByteToWideChar(CP_UTF8, 0, s, len, ret, n);
    ret[n] = L'\0';
  }

  return ret;
}

char*
utf8_fromwcs(const wchar_t* wstr) {
  int len = (int)wcslen(wstr);
  int n = WideCharToMultiByte(CP_UTF8, 0, wstr, len, NULL, 0, NULL, NULL);
  char* ret;

  if((ret = malloc((n + 1)))) {
    WideCharToMultiByte(CP_UTF8, 0, wstr, len, ret, n, NULL, NULL);
    ret[n] = '\0';
  }

  return ret;
}
#endif

bool
utf16_multiword(const void* in) {
  const uint16_t* p16 = in;
  LibutfC16Type type = libutf_c16_type(p16[0]);

  return !((LIBUTF_UTF16_NOT_SURROGATE == type) || (LIBUTF_UTF16_SURROGATE_HIGH != type || LIBUTF_UTF16_SURROGATE_LOW != libutf_c16_type(p16[1])));
}

int
case_lowerc(int c) {
  if(c >= 'A' && c <= 'Z')
    c += 'a' - 'A';

  return c;
}

int
case_starts(const char* a, const char* b) {
  const char *s, *t;

  for(s = a, t = b;; ++s, ++t) {
    unsigned char x, y;

    if(!*t)
      return 1;

    x = case_lowerc(*s);
    y = case_lowerc(*t);

    if(x != y)
      break;

    if(!x)
      break;
  }

  return 0;
}

int
case_diffb(const void* S, size_t len, const void* T) {
  unsigned char x, y;
  const char *s, *t;

  for(s = (const char*)S, t = (const char*)T; len > 0;) {
    --len;
    x = case_lowerc(*s);
    y = case_lowerc(*t);

    ++s;
    ++t;

    if(x != y)
      return ((int)(unsigned int)x) - ((int)(unsigned int)y);
  }

  return 0;
}

size_t
case_findb(const void* haystack, size_t hlen, const void* what, size_t wlen) {
  size_t i, last;
  const char* s = haystack;

  if(hlen < wlen)
    return hlen;

  last = hlen - wlen;

  for(i = 0; i <= last; i++, s++)
    if(!case_diffb(s, wlen, what))
      return i;

  return hlen;
}

size_t
case_finds(const void* haystack, const char* what) {
  return case_findb(haystack, strlen(haystack), what, strlen(what));
}

ssize_t
write_file(const char* file, const void* buf, size_t len) {
  FILE* f;
  ssize_t ret = -1;

  if((f = fopen(file, "w+")))
    switch(fwrite(buf, len, 1, f)) {
      case 1: {
        ret = len;
        break;
      }
    }

  fflush(f);
  ret = ftell(f);
  fclose(f);

  return ret;
}

ssize_t
puts_file(const char* file, const char* s) {
  return write_file(file, s, strlen(s));
}

size_t
u64_to_str(char* x, uint64_t num, int base) {
  size_t len = 0;
  uint64_t n = num;

  do {
    n /= base;
    len++;
    x++;
  } while(n != 0);

  *x-- = '\0';

  do {
    char c = num % base;
    num /= base;

    if(c >= 10)
      c += 'a' - '0' - 10;
    *x-- = c + '0';
  } while(num != 0);

  return len;
}

size_t
i64_to_str(char* x, int64_t num, int base) {
  size_t pos = 0, len;

  if(num < 0) {
    x[pos++] = '-';
    num = -num;
  }

  len = u64_to_str(&x[pos], num, base);

  return pos + len;
}

size_t
str_findb(const char* s1, const char* x, size_t n) {
  const char* b;
  size_t i, j, len = strlen(s1);

  if(len >= n) {
    size_t end = len - n + 1;

    for(i = 0; i < end; i++) {
      b = &s1[i];

      for(j = 0; x[j] == b[j];)
        if(++j == n)
          return i;
    }
  }

  return len;
}

size_t
str_find(const void* s, const void* what) {
  return str_findb(s, what, strlen(what));
}
/**
 * @}
 */
