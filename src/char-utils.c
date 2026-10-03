#define _GNU_SOURCE
#include <string.h>

#include "char-utils.h"
#include "buffer-utils.h"
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
ansi_skip(const char* str, size_t len) {
  size_t pos = 0;

  if(str[pos] == 0x1b) {
    if(++pos < len && str[pos] == '[') {
      while(++pos < len)
        if(is_alphanumeric_char(str[pos]))
          break;

      if(++pos < len && str[pos] == '~')
        ++pos;

      return pos;
    }
  }

  return 0;
}

size_t
ansi_truncate(const char* str, size_t len, size_t limit) {
  size_t i, n = 0, p;

  for(i = 0; i < len;) {
    if((p = ansi_skip(&str[i], len - i)) > 0) {
      i += p;
      continue;
    }

    n += is_escape_char(str[i]) ? 2 : 1;

    i++;

    if(n > limit)
      break;
  }

  return i;
}



size_t
byte_findb(const void* haystack, size_t hlen, const void* what, size_t wlen) {
  const char* b;

  if((b = memmem(haystack, hlen, what, wlen)))
    return b - (const char*)haystack;

  return hlen;
}

size_t
byte_finds(const void* haystack, size_t hlen, const char* what) {
  return byte_findb(haystack, hlen, what, strlen(what));
}

size_t
byte_equal(const void* s, size_t n, const void* t) {
  return memcmp(s, t, n) == 0;
}

void
byte_copy(void* out, size_t len, const void* in) {
  memcpy(out, in, len);
}




size_t
fmt_long(void* x, int32_t i) {
  char* dest = x;
  if(i < 0) {
    if(dest)
      *dest++ = '-';
    return fmt_ulong(dest, (uint32_t)-i) + 1;
  } else
    return fmt_ulong(dest, (uint32_t)i);
}

size_t
fmt_ulong(void* x, uint32_t i) {
  char* dest = x;
  uint32_t len, tmp, len2;

  for(len = 1, tmp = i; tmp > 9; ++len)
    tmp /= 10;

  if(dest)
    for(tmp = i, dest += len, len2 = len + 1; --len2; tmp /= 10)
      *--dest = (char)((tmp % 10) + '0');

  return len;
}

size_t
fmt_longlong(void* x, int64_t i) {
  char* dest = x;
  if(i < 0) {
    if(dest)
      *dest++ = '-';

    return fmt_ulonglong(dest, (uint64_t)-i) + 1;
  }

  return fmt_ulonglong(dest, (uint64_t)i);
}

size_t
fmt_ulonglong(void* x, uint64_t i) {
  char* dest = x;
  size_t len;
  uint64_t tmp, len2;

  for(len = 1, tmp = i; tmp > 9ll; ++len)
    tmp /= 10ll;

  if(dest)
    for(tmp = i, dest += len, len2 = len + 1; --len2; tmp /= 10ll)
      *--dest = (tmp % 10ll) + '0';

  return len;
}

#define tohex(c) (char)((c) >= 10 ? (c) - 10 + 'a' : (c) + '0')

size_t
fmt_xlonglong(void* x, uint64_t i) {
  char* dest = x;
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



#define tohex(c) (char)((c) >= 10 ? (c) - 10 + 'a' : (c) + '0')

size_t
fmt_xlong(void* x, uint32_t i) {
  char* dest = x;
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
fmt_xlong0(void* x, uint32_t num, size_t n) {
  char* dest = x;
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
scan_int(const char* src, int32_t* dest) {
  int64_t i64 = 0ll;
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
scan_double(const char* s, double* d) {
  const char* p = s;
  long double factor, value = 0.;
  int sign = +1;
  unsigned int expo;

  while(is_whitespace_char(*p))
    p++;

  switch(*p) {
    case '-': sign = -1; /* fall through */
    case '+': p++;
    default: break;
  }

  while((unsigned int)(*p - '0') < 10u)
    value = value * 10 + (*p++ - '0');

  if(*p == '.') {
    factor = 1.;

    p++;

    while((unsigned int)(*p - '0') < 10u) {
      factor *= 0.1;
      value += (*p++ - '0') * factor;
    }
  }

  if((*p | 32) == 'e') {
    expo = 0;
    factor = 10.;

    switch(*++p) {
      case '-': factor = 0.1; /* fall through */
      case '+': p++; break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9': break;
      default:
        value = 0.;
        p = s;
        goto done;
    }

    while((unsigned int)(*p - '0') < 10u)
      expo = 10 * expo + (*p++ - '0');

    while(1) {
      if(expo & 1)
        value *= factor;
      if((expo >>= 1) == 0)
        break;
      factor *= factor;
    }
  }

done:
  *d = value * sign;

  return p - s;
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



int
utf8_charlen(const void* in, size_t len) {
  const uint8_t* next;

  if(unicode_from_utf8(in, len, &next) == -1)
    return 0;

  return next - (const uint8_t*)in;
}

int
utf8_charcode2(const void* in, size_t len, size_t* bytes) {
  const uint8_t* next;
  int cp = unicode_from_utf8(in, len, &next);

  if(bytes)
    *bytes = next - (const uint8_t*)in;

  return cp;
}

size_t
utf8_strlen(const void* in, size_t len) {
  const uint8_t* next;
  size_t i = 0;

  for(const uint8_t *pos = in, *end = in + len; pos < end; pos = next, ++i)
    if(unicode_from_utf8(pos, end - pos, &next) == -1)
      break;

  return i;
}

size_t
utf8_byteoffset(const void* in, size_t len, int pos) {
  const uint8_t *x = in, *y = (const uint8_t*)in + len;

  if(len == 0 || pos == 0)
    return 0;

  if(pos < 0)
    pos = utf8_strlen(in, len) + pos;

  for(int i = 0; x <= y; ++i) {
    if(i >= pos)
      break;

    x += utf8_charlen(x, y - x);
  }

  return x - (const uint8_t*)in;
}

/* Note: at most 31 bits are encoded. At most UTF8_CHAR_LEN_MAX bytes
   are output. */
int
unicode_len_utf8(unsigned int c) {
  int len = 0;

  if(c < 0x80) {
    len++;
  } else {
    if(c < 0x800) {
      len++;
    } else {
      if(c < 0x10000) {
        len++;
      } else {
        if(c < 0x00200000) {
          len++;
        } else {
          if(c < 0x04000000) {
            len++;
          } else if(c < 0x80000000) {
            len++;
            len++;
          } else {
            return 0;
          }
          len++;
        }
        len++;
      }
      len++;
    }
    len++;
  }
  return len;
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


int
case_lowerc(int c) {
  if(c >= 'A' && c <= 'Z')
    c += 'a' - 'A';

  return c;
}


int
case_diffb(const void* S, size_t len, const void* T) {
  unsigned char x, y;

  for(const char *s = (const char*)S, *t = (const char*)T; len > 0;) {
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
  const char* s = haystack;

  if(hlen < wlen)
    return hlen;

  size_t last = hlen - wlen;

  for(size_t i = 0; i <= last; i++, s++)
    if(!case_diffb(s, wlen, what))
      return i;

  return hlen;
}

size_t
case_finds(const void* haystack, const char* what) {
  return case_findb(haystack, strlen(haystack), what, strlen(what));
}


size_t
u64toa_base(char* x, uint64_t num, int base) {
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
i64toa_base(char* x, int64_t num, int base) {
  size_t pos = 0;

  if(num < 0) {
    x[pos++] = '-';
    num = -num;
  }

  return pos + u64toa_base(&x[pos], num, base);
}



/**
 * @}
 */
