#ifndef CHAR_UTILS_H
#define CHAR_UTILS_H

#include "defines.h"
#include "debug.h"

#include <string.h>
#include <cutils.h>

/**
 * \defgroup char-utils char-utils: Character Utilities
 * @{
 */
#define is_control_char(c) ((c) >= 0 && (c) <= 0x1f)
#define is_alphanumeric_char(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))
#define is_digit_char(c) ((c) >= '0' && (c) <= '9')
#define is_xdigit_char(c) (is_digit_char((c)) || ((c) >= 'A' && (c) <= 'F') || ((c) >= 'a' && (c) <= 'f'))
#define is_print_char(c) ((c) >= ' ' && (c) <= '\x7f')
#define is_newline_char(c) ((c) == '\n')
#define is_identifier_char(c) (is_alphanumeric_char(c) || is_digit_char(c) || (c) == '$' || (c) == '_')
#define is_whitespace_char(c) ((c) == ' ' || (c) == '\t' || (c) == '\v' || (c) == '\n' || (c) == '\r')

#define str_equal(s, t) (!strcmp((s), (t)))
#define str_contains(s, needle) (!!strchr((s), (needle)))

#define FMT_LONG 41  /* enough space to hold -2^127 in decimal, plus \0 */
#define FMT_ULONG 40 /* enough space to hold 2^128 - 1 in decimal, plus \0 */
#define FMT_8LONG 44 /* enough space to hold 2^128 - 1 in octal, plus \0 */
#define FMT_XLONG 33 /* enough space to hold 2^128 - 1 in hexadecimal, plus \0 */

size_t ansi_skip(const char*, size_t);
size_t ansi_truncate(const char*, size_t, size_t limit);
size_t byte_finds(const void*, size_t, const char* what);
size_t byte_chrs(const void* in, size_t len, const char needle[], size_t nl);
size_t str_copy(char* out, const char* in);
char* str_escape(const char*);
size_t fmt_long(void*, int32_t);
size_t fmt_ulong(void*, uint32_t);
size_t scan_ushort(const char*, uint16_t*);
size_t fmt_longlong(void*, int64_t);
size_t fmt_ulonglong(void*, uint64_t);
size_t fmt_xlonglong(void*, uint64_t);
size_t fmt_xlong(void*, uint32_t num);
size_t fmt_xlong0(void*, uint32_t num, size_t n);
size_t scan_longlong(const char*, int64_t*);
size_t scan_int(const char*, int32_t*);
size_t scan_ulonglong(const char*, uint64_t*);
size_t scan_xlonglong(const char*, uint64_t*);
size_t scan_8longn(const char*, size_t, uint32_t*);
size_t scan_double(const char*, double*);
size_t scan_whitenskip(const char*, size_t);
size_t scan_nonwhitenskip(const char*, size_t);
size_t scan_line(const char*, size_t);
size_t scan_lineskip(const char*, size_t);
size_t scan_charsetnskip(const char*, const char*, size_t);
size_t scan_noncharsetnskip(const char*, const char*, size_t);
int utf8_charlen(const void*, size_t);
int utf8_charcode2(const void*, size_t, size_t* len);
size_t utf8_strlen(const void*, size_t);
size_t utf8_byteoffset(const void*, size_t, int);
int unicode_len_utf8(unsigned int);
wchar_t* utf8_towcs(const char*);
char* utf8_fromwcs(const wchar_t*);
int case_lowerc(int);
int case_diffb(const void*, size_t, const void* T);
size_t case_findb(const void*, size_t, const void* what, size_t wlen);
size_t case_finds(const void*, const char*);
BOOL utf16_multiword(const void*);
size_t u64toa_base(char*, uint64_t num, int base);
size_t i64toa_base(char*, int64_t num, int base);

static inline size_t
byte_chr(const void* in, size_t len, char c) {
  const char* s;

  if((s = memchr(in, c, len)))
    return s - (const char*)in;

  return len;
}

#ifdef HAVE_MEMRCHR
static inline size_t
byte_rchr(const void* haystack, size_t len, char needle) {
  const char* s;

  if((s = memrchr(haystack, needle, len)))
    return s - (const char*)haystack;

  return len;
}
#else
size_t byte_rchr(const void* haystack, size_t len, char needle);
#endif

#ifdef HAVE_MEMMEM
static inline size_t
byte_findb(const void* haystack, size_t hlen, const void* what, size_t wlen) {
  const char* b;

  if((b = memmem(haystack, hlen, what, wlen)))
    return b - (const char*)haystack;

  return hlen;
}
#else
size_t byte_findb(const void* haystack, size_t hlen, const void* what, size_t wlen);
#endif

#define byte_copy(out, len, in) memcpy((out), (in), (len))
#define byte_copyr(out, len, in) memmove((out), (in), (len))
#define byte_diff(a, len, b) memcmp((a), (b), (len))
#define byte_equal(a, len, b) (!memcmp((a), (b), (len)))

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
  const char* s;

  for(s = in; *s; ++s)
    if(*s == needle)
      break;

  return (size_t)(s - in);
}

static inline size_t
str_chrs(const char* in, const char needles[], size_t nn) {
  const char* s;

  for(s = in; *s; s++)
    for(size_t i = 0; i < nn; i++)
      if(*s == needles[i])
        return (size_t)(s - in);

  return (size_t)(s - in);
}

static inline size_t
str_rchr(const char* in, char needle) {
  const char *s, *found = NULL;

  for(s = in; *s; s++)
    if(*s == needle)
      found = s;

  return (size_t)((found ? found : s) - in);
}

static inline size_t
str_nchrs(const char* in, const char needles[], size_t nn) {
  const char* s;

  for(s = in; *s; s++) {
    size_t i;

    for(i = 0; i < nn; i++)
      if(*s == needles[i])
        break;

    if(i == nn)
      break;
  }

  return (size_t)(s - in);
}

static inline int
str_endb(const char* in, const char* suffix, size_t n) {
  size_t alen;

  if((alen = strlen(in)) < n)
    return 0;

  return byte_equal(in + alen - n, n, suffix);
}

/* str_ends returns 1 if in ends in suffix, 0 otherwise */
static inline int
str_ends(const char* in, const char* suffix) {
  return str_endb(in, suffix, strlen(suffix));
}

static inline int
str_startb(const char* in, const char* prefix, size_t len) {
  return !strncmp(in, prefix, len);
}

static inline int
str_start(const char* in, const char* prefix) {
  return str_startb(in, prefix, strlen(prefix));
}

static inline size_t
str_count(const char* in, char c) {
  size_t i, count = 0;

  for(i = 0; in[i]; i++)
    if(in[i] == c)
      ++count;

  return count;
}

static inline size_t
str_copyn(char* out, const char* in, size_t n) {
  char* t;

  if(!(t = memccpy(out, in, '\0', n))) {
    t = out + n;
    *t = '\0';
  } else {
    t--;
  }

  return (size_t)(t - out);
}

static inline char*
str_ndup(const char* in, size_t n) {
  char* out;

  if(!(out = malloc(n + 1)))
    return out;

  memcpy(out, in, n);
  out[n] = '\0';
  return out;
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

/* Total byte length of a UTF-8 sequence, judged by its lead byte (0 = invalid lead byte) */
static inline size_t
utf8_needed(uint8_t c) {
  if(c < 0x80)
    return 1;
  if(c >= 0xc0 && c < 0xe0)
    return 2;
  if(c >= 0xe0 && c < 0xf0)
    return 3;
  if(c >= 0xf0 && c < 0xf8)
    return 4;
  if(c >= 0xf8 && c < 0xfc)
    return 5;
  if(c >= 0xfc && c < 0xfe)
    return 6;
  return 0;
}

static inline int
utf8_charcode(const void* in, size_t len) {
  return utf8_charcode2(in, len, 0);
}

static inline char
is_quotable_char(char c) {
  switch(c) {
    case '"': return c;
    case '\\': return c;
    case '/': return c;
    case 'b': return '\b';
    case 'f': return '\f';
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
  }

  return 0;
}

static inline BOOL
is_number_char(char c) {
  if(is_digit_char(c))
    return TRUE;

  switch(c) {
    case '.':
    case 'e':
    case 'E':
    case '+':
    case '-': return TRUE;
  }

  return FALSE;
}

static inline BOOL
is_utf16_high_surrogate(const uint32_t c) {
  return c >= 0xd800 && c < 0xdc00;
}

static inline BOOL
is_utf16_low_surrogate(const uint32_t c) {
  return c >= 0xdc00 && c < 0xe000;
}

static inline int
is_escape_char(int c) {
  return is_control_char(c) || c == '\\' || c == '\'' || c == 0x1b || c == 0;
}

/* bare_keys: ES5 IdentifierName, ASCII only; anything else stays quoted. */
static inline BOOL
is_identifier(const char* in, size_t len) {
  if(len == 0 || is_digit_char((unsigned char)in[0]))
    return FALSE;

  for(size_t i = 0; i < len; i++)
    if(!(is_alphanumeric_char((unsigned char)in[i]) || is_digit_char((unsigned char)in[i]) || in[i] == '_' || in[i] == '$'))
      return FALSE;

  return TRUE;
}

static inline BOOL
is_integer(const char* in) {
  if(*in == '-')
    ++in;

  if(!(*in >= '1' && *in <= '9') && !(*in == '0' && in[1] == '\0'))
    return FALSE;

  while(*++in)
    if(!is_digit_char(*in))
      return FALSE;

  return TRUE;
}

/* number text: a digit, '+', '-' or '.' first, then letters, digits and
 * those; keeps ',' quotes and brackets out of a value written verbatim. */
static inline BOOL
is_number(const char* in, size_t len) {
  if(len == 0 || !(is_digit_char((unsigned char)in[0]) || in[0] == '+' || in[0] == '-' || in[0] == '.'))
    return FALSE;

  for(size_t i = 1; i < len; i++)
    if(!(is_digit_char((unsigned char)in[i]) || is_alphanumeric_char((unsigned char)in[i]) || in[i] == '+' || in[i] == '-' || in[i] == '.'))
      return FALSE;

  return TRUE;
}

static inline BOOL
is_xnumber(const char* in, size_t len) {
  if(len < 3 || !str_start(in, "0x"))
    return FALSE;

  for(size_t i = 2; i < len; i++)
    if(!(is_xdigit_char((unsigned char)in[i]) || in[i] == '.'))
      return FALSE;

  return TRUE;
}

/**
 * @}
 */
#endif /* defined(CHAR_UTILS_H) */
