#ifndef CHAR_UTILS_H
#define CHAR_UTILS_H

#include <cutils.h>
#include <string.h>
#include "debug.h"

/**
 * \defgroup char-utils char-utils: Character Utilities
 * @{
 */
#define is_control_char(c) ((c) == '\a' || (c) == '\b' || (c) == '\t' || (c) == '\n' || (c) == '\v' || (c) == '\f' || (c) == '\r')
#define is_alphanumeric_char(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))

#define is_digit_char(c) ((c) >= '0' && (c) <= '9')
#define is_print_char(c) ((c) >= ' ' && (c) <= '\x7f')
#define is_newline_char(c) ((c) == '\n')
#define is_identifier_char(c) (is_alphanumeric_char(c) || is_digit_char(c) || (c) == '$' || (c) == '_')
#define is_whitespace_char(c) ((c) == ' ' || (c) == '\t' || (c) == '\v' || (c) == '\n' || (c) == '\r')

#define str_equal(s, t) (!strcmp((s), (t)))

static inline int
escape_char_pred(int c) {
  static const unsigned char table[256] = {
      'x', 'x', 'x', 'x', 'x', 'x',  'x', 'x', 0x62, 0x74, 0x6e, 0x76, 0x66, 0x72, 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
      'x', 'x', 'x', 0,   0,   0,    0,   0,   0,    0,    0x27, 0,    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0x5c, 0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    'x',  0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
  };

  return table[(unsigned char)c];
}

static inline int
unescape_char_pred(int c) {
  switch(c) {
    case 'b': return 8;
    case 'f': return 12;
    case 'n': return 10;
    case 'r': return 13;
    case 't': return 9;
    case 'v': return 11;
    case '\'': return 39;
    case '\\': return 92;
  }

  return 0;
}

static inline int
is_escape_char(int c) {
  return is_control_char(c) || c == '\\' || c == '\'' || c == 0x1b || c == 0;
}

static inline int
is_backslash_char(int c) {
  return c == '\\';
}

//#define is_dot_char(c) ((c) == '.')0
//#define is_backslash_char(c) ((c) == '\\')

static inline int
is_dot_char(int c) {
  return c == '.';
}

static inline int
is_identifier(const char* str) {
  if(!((*str >= 'A' && *str <= 'Z') || (*str >= 'a' && *str <= 'z') || *str == '$' || *str == '_'))
    return 0;

  while(*++str)
    if(!is_identifier_char(*str))
      return 0;

  return 1;
}

static inline int
is_integer(const char* str) {
  if(*str == '-')
    ++str;

  if(!(*str >= '1' && *str <= '9') && !(*str == '0' && str[1] == '\0'))
    return 0;

  while(*++str)
    if(!is_digit_char(*str))
      return 0;

  return 1;
}

static inline size_t
byte_count(const void* s, size_t n, char c) {
  const uint8_t* t;
  uint8_t ch = (uint8_t)c;
  size_t count;

  for(t = (uint8_t*)s, count = 0; n; ++t, --n)
    if(*t == ch)
      ++count;

  return count;
}

static inline size_t
byte_chr(const void* str, size_t len, char c) {
  const char* s = memchr(str, c, len);

  if(s)
    return s - (const char*)str;

  return len;
}

static inline size_t
byte_rchr(const void* haystack, size_t len, char needle) {
  const char *s, *t;

  for(s = (const char*)haystack, t = s + len;;) {
    --t;

    if(s > t)
      break;

    if(*t == needle)
      return (size_t)(t - s);
  }

  return len;
}

/*size_t
byte_rchr(const void* str, size_t len, char c) {
  const char* s = memrchr(str, c, len);
  if(s)
    return s - (const char*)str;
  return len;
}*/

static inline size_t
byte_chrs(const void* str, size_t len, const char needle[], size_t nl) {
  const char *s, *t;

  for(s = str, t = s + len; s != t; s++)
    if(byte_chr(needle, nl, *s) < nl)
      break;

  return s - (const char*)str;
}

static inline int
byte_diff(const void* a, size_t len, const void* b) {
  size_t i;

  for(i = 0; i < len; ++i) {
    int r = ((unsigned char*)a)[i] - ((unsigned char*)b)[i];

    if(r)
      return r;
  }

  return 0;
}

static inline int
byte_diff2(const char* a, size_t alen, const char* b, size_t blen) {

  if(alen < blen)
    return -b[alen];

  if(blen < alen)
    return a[blen];

  return byte_diff(a, alen, b);
}

static inline size_t
str_chr(const char* in, char needle) {
  const char *t, c = needle;

  for(t = in; *t; ++t)
    if(*t == c)
      break;

  return (size_t)(t - in);
}

static inline size_t
str_chrs(const char* in, const char needles[], size_t nn) {
  const char* t;
  size_t i;

  for(t = in; *t; ++t)
    for(i = 0; i < nn; i++)
      if(*t == needles[i])
        return (size_t)(t - in);

  return (size_t)(t - in);
}

static inline size_t
str_rchr(const char* s, char needle) {
  const char *in, *found = 0;

  for(in = s; *in; ++in)
    if(*in == needle)
      found = in;

  return (size_t)((found ? found : in) - s);
}

static inline size_t
str_rchrs(const char* in, const char needles[], size_t nn) {
  const char *s, *found = 0;
  size_t i;

  for(s = in; *s; ++s)
    for(i = 0; i < nn; ++i)
      if(*s == needles[i])
        found = s;

  return (size_t)((found ? found : s) - in);
}

static inline int
str_endb(const char* a, const char* x, size_t n) {
  size_t alen = strlen(a);
  a += alen - n;

  return alen >= n && !memcmp(a, x, n);
}

/* str_ends returns 1 if the b is a suffix of a, 0 otherwise */
static inline int
str_ends(const char* a, const char* b) {
  return str_endb(a, b, strlen(b));
}

static inline int
str_startb(const char* a, const char* x, size_t len) {
  size_t i;

  for(i = 0;; i++) {
    if(i == len)
      return 1;
    if(a[i] != x[i])
      break;
  }

  return 0;
}

static inline int
str_start(const char* a, const char* b) {
  return str_startb(a, b, strlen(b));
}

#define str_contains(s, needle) (!!strchr((s), (needle)))

char* str_escape(const char*);

static inline size_t
str_count(const char* s, char c) {
  size_t i, count = 0;

  for(i = 0; s[i]; i++)
    if(s[i] == c)
      ++count;

  return count;
}

static inline size_t
str_copy(char* out, const char* in) {
  char* s;

  for(s = out; (*s = *in); ++s)
    ++in;

  return (size_t)(s - out);
}

static inline size_t
str_copyn(char* out, const char* in, size_t n) {
  char* s;

  for(s = out; n-- && (*s = *in); ++s)
    ++in;

  *s = '\0';
  return (size_t)(s - out);
}

static inline char*
str_ndup(const char* s, size_t n) {
  char* r = malloc(n + 1);

  if(r == NULL)
    return NULL;

  memcpy(r, s, n);
  r[n] = '\0';
  return r;
}

size_t str_findb(const char*, const char*, size_t);
size_t str_find(const void*, const void*);

static inline size_t
predicate_find(const char* str, size_t len, int (*pred)(int32_t)) {
  size_t pos;

  for(pos = 0; pos < len; pos++)
    if(pred(str[pos]))
      break;

  return pos;
}

static inline size_t
lookup_find(const char* str, size_t len, const char table[256]) {
  size_t pos;

  for(pos = 0; pos < len; pos++)
    if(table[(unsigned char)str[pos]])
      break;

  return pos;
}

static inline char
escape_char_letter(char c) {
  switch(c) {
    case '\0': return '0';
    case '\a': return 'a';
    case '\b': return 'b';
    case '\t': return 't';
    case '\n': return 'n';
    case '\v': return 'v';
    case '\f': return 'f';
    case '\r': return 'r';
    case '\\': return '\\';
    case '\'': return '\'';
  }

  return 0;
}

#define FMT_LONG 41  /* enough space to hold -2^127 in decimal, plus \0 */
#define FMT_ULONG 40 /* enough space to hold 2^128 - 1 in decimal, plus \0 */
#define FMT_8LONG 44 /* enough space to hold 2^128 - 1 in octal, plus \0 */
#define FMT_XLONG 33 /* enough space to hold 2^128 - 1 in hexadecimal, plus \0 */

size_t token_length(const char*, size_t, char delim);
size_t fmt_ulong(char*, uint32_t);
size_t scan_ushort(const char*, uint16_t*);
size_t fmt_longlong(char*, int64_t);
size_t fmt_ulonglong(char*, uint64_t);
size_t fmt_xlonglong(char*, uint64_t);
size_t fmt_xlonglong0(char*, uint64_t, size_t);
size_t fmt_8long(char* dest, uint32_t i);
size_t fmt_xlong(char* dest, uint32_t num);
size_t fmt_xlong0(char* dest, uint32_t num, size_t n);
size_t scan_longlong(const char*, int64_t*);
size_t scan_int(const char*, int32_t*);
size_t scan_uint(const char*, uint32_t*);
size_t scan_ulonglong(const char*, uint64_t*);
size_t scan_xlonglong(const char*, uint64_t*);
size_t scan_8longn(const char*, size_t, uint32_t* dest);
size_t scan_whitenskip(const char*, size_t);
size_t scan_nonwhitenskip(const char*, size_t);
size_t scan_line(const char*, size_t);
size_t scan_lineskip(const char*, size_t);
size_t scan_lineskip_escaped(const char*, size_t);
size_t scan_eolskip(const char*, size_t);
size_t utf8_strlen(const void*, size_t);
wchar_t* utf8_towcs(const char*);
char* utf8_fromwcs(const wchar_t*);
BOOL utf16_multiword(const void*);
int case_lowerc(int);
int case_starts(const char*, const char*);
int case_diffb(const void*, size_t, const void* T);
size_t case_findb(const void*, size_t, const void* what, size_t wlen);
size_t case_finds(const void*, const char*);

static inline int
scan_fromhex(unsigned char c) {
  c -= '0';

  if(c <= 9)
    return c;

  c &= ~0x20;
  c -= 'A' - '0';

  if(c < 6)
    return c + 10;

  return -1;
}

static inline size_t
scan_8long(const char* src, uint32_t* dest) {
  return scan_8longn(src, (size_t)-1, dest);
}

static inline size_t
utf8_charlen(const char* in, size_t len) {
  const uint8_t* next = (const void*)in;
  int r = unicode_from_utf8((const uint8_t*)in, len, &next);
  return r == -1 ? 0 : next - (const uint8_t*)in;
}

static inline int
utf8_charcode(const char* in, size_t len) {
  const uint8_t* next = (const void*)in;
  int r = unicode_from_utf8((const uint8_t*)in, len, &next);
  return next > in ? r : -1;
}

BOOL utf16_multiword(const void*);

ssize_t write_file(const char* file, const void* buf, size_t len);
ssize_t puts_file(const char* file, const char* s);

size_t u64toa(char*, uint64_t num, int base);
size_t i64toa(char*, int64_t num, int base);

/**
 * @}
 */
#endif /* defined(CHAR_UTILS_H) */
