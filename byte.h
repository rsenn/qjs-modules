#ifndef BYTE_H
#define BYTE_H

static inline size_t
byte_chr(const char* str, size_t len, char c) {
  const char *s = str, *t = s + len;
  for(; s < t; ++s)
    if(*s == c)
      break;
  return s - str;
}

#endif