#ifndef LEXER_H
#define LEXER_H

#include "utils.h"

typedef struct {
  const char* file;
  uint32_t line;
  uint32_t column;
} Location;

typedef struct {
  InputBuffer input;
  Location loc;
} Lexer;

#endif /* defined(LEXER_H) */
