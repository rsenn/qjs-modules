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
