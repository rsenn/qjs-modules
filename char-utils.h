#ifndef CHAR_UTILS_H
#define CHAR_UTILS_H

#include <cutils.h>

#define is_control_char(c) ((c) == '\a' || (c) == '\b' || (c) == '\t' || (c) == '\n' || (c) == '\v' || (c) == '\f' || (c) == '\r')
#define is_alphanumeric_char(c) ((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z')
#define is_digit_char(c) ((c) >= '0' && (c) <= '9')
#define is_newline_char(c) ((c) == '\n')
#define is_identifier_char(c) (is_alphanumeric_char(c) || is_digit_char(c) || (c) == '$' || (c) == '_')
#define is_whitespace_char(c) ((c) == ' ' || (c) == '\t' || (c) == '\v' || (c) == '\n' || (c) == '\r')

static inline int
escape_char_pred(int c) {
  static const unsigned char table[256] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 0x62, 0x74, 0x6e, 0x76, 0x66, 0x72, 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 0, 0, 0, 0, 0, 0, 0, 0x27, 0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                           0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0,    0x5c, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                           0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   'x', 0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0,    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                           0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0,    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                           0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,    0,    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0,    0,    0, 0, 0, 0, 0, 0, 0};

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
  while(*++str) {
    if(!is_identifier_char(*str))
      return 0;
  }
  return 1;
}

static inline int
is_integer(const char* str) {
  if(!(*str >= '1' && *str <= '9') && !(*str == '0' && str[1] == '\0'))
    return 0;
  while(*++str) {
    if(!is_digit_char(*str))
      return 0;
  }
  return 1;
}

static inline size_t
byte_count(const void* s, size_t n, char c) {
  const uint8_t* t;
  uint8_t ch = (uint8_t)c;
  size_t count;
  for(t = (uint8_t*)s, count = 0; n; ++t, --n) {
    if(*t == ch)
      ++count;
  }
  return count;
}

static inline size_t
byte_chr(const char* str, size_t len, char c) {
  const char *s, *t;
  for(s = str, t = s + len; s < t; ++s)
    if(*s == c)
      break;
  return s - str;
}

static inline size_t
byte_rchr(const void* str, size_t len, char c) {
  const char *s, *t;
  for(s = (const char*)str, t = s + len; --t >= s;)
    if(*t == c)
      return (size_t)(t - s);
  return len;
}

static inline size_t
byte_chrs(const char* str, size_t len, const char needle[], size_t nl) {
  const char *s, *t;
  for(s = str, t = str + len; s != t; s++)
    if(byte_chr(needle, nl, *s) < nl)
      break;
  return s - (const char*)str;
}

static inline size_t
byte_charlen(const char* in, size_t len) {
  const uint8_t *pos, *end, *next;
  int cp;
  pos = (const uint8_t*)in;
  end = pos + len;
  cp = unicode_from_utf8(pos, end - pos, &next);
  return next - pos;
}

char* byte_escape(const char*, size_t n);

static inline size_t
str_chr(const char* in, char needle) {
  const char* t = in;
  const char c = needle;
  for(;;) {
    if(!*t || *t == c) {
      break;
    };
    ++t;
  }
  return (size_t)(t - in);
}

static inline size_t
str_chrs(const char* in, const char needles[], size_t nn) {
  const char* t = in;
  size_t i;
  for(;;) {
    if(!*t)
      break;
    for(i = 0; i < nn; i++)
      if(*t == needles[i])
        return (size_t)(t - in);
    ++t;
  }
  return (size_t)(t - in);
}

static inline size_t
str_rchr(const char* s, char needle) {
  const char *in = s, *found = 0;
  for(;;) {
    if(!*in)
      break;
    if(*in == needle)
      found = in;
    ++in;
  }
  return (size_t)((found ? found : in) - s);
}

static inline size_t
str_rchrs(const char* in, const char needles[], size_t nn) {
  const char *s = in, *found = 0;
  size_t i;
  for(;;) {
    if(!*s)
      break;
    for(i = 0; i < nn; ++i) {
      if(*s == needles[i])
        found = s;
    }
    ++s;
  }
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
  for(s = out; (*s = *in); ++s) ++in;
  return (size_t)(s - out);
}

static inline size_t
str_copyn(char* out, const char* in, size_t n) {
  char* s;
  for(s = out; n-- && (*s = *in); ++s) ++in;
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

size_t token_length(const char*, size_t, char delim);

#define FMT_ULONG 40 /* enough space to hold 2^128 - 1 in decimal, plus \0 */

size_t fmt_ulong(char*, unsigned long);

#endif /* defined(CHAR_UTILS_H) */
