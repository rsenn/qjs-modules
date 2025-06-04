/*
 * MIT License
 *
 * Copyright (c) 2010 Serge Zaitsev
 * Copyright (c) 2024 Weihao Feng
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef JSMN2_H
#define JSMN2_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef JSMN2_STATIC
#define JSMN2_API static
#else
#define JSMN2_API extern
#endif

/**
 * JSON type identifier. Basic types are:
 * 	o Object
 * 	o Array
 * 	o String
 * 	o Other primitive: number, boolean (true/false) or null
 */
typedef enum {
  JSMN2_UNDEFINED = 0,
  JSMN2_OBJECT,
  JSMN2_ARRAY,
  JSMN2_STRING,
  JSMN2_PRIMITIVE,
} jsmn2type_t;

enum jsmn2err {
  JSMN2_SUCCESS = 0,
  /* Not enough tokens were provided */
  JSMN2_ERROR_NOMEM = -1,
  /* Invalid character inside JSON string */
  JSMN2_ERROR_INVAL = -2,
  /* The string is not a full JSON packet, more bytes expected */
  /* \r not followed by a newline */
  JSMN2_ERROR_BROKEN_NEWLINE = -3,
  JSMN2_ERROR_UNEXPECTED_CHAR = JSMN2_ERROR_INVAL,
  JSMN2_ERROR_UNCLOSED_STRING = -4,
  JSMN2_ERROR_UNCLOSED_OBJECT = -5,
  JSMN2_ERROR_UNCLOSED_ARRAY = -6,
  JSMN2_ERROR_UNEXPECTED_EOF = -7,
  JSMN2_ERROR_TRAILING_COMMA = -8,
  JSMN2_ERROR_EXPECTED_EOF = -9,
};

/**
 * JSON token description.
 * type         type (object, array, string etc.)
 * start        start position in JSON data string
 * size         length of this token. For non-literal types, this corresponds to
 *              the number of elements.
 */
typedef struct {
  size_t start;
  int size;
#ifdef JSMN2_PARENT_LINKS
  int parent;
#endif
  jsmn2type_t type : 4;
  bool unclosed : 1;
  bool is_key : 1;
  bool associated : 1;
} jsmn2tok_t;

/**
 * JSON parser. Contains an array of token blocks available. Also stores
 * the string being parsed now and current position in that string.
 */
typedef struct jsmn2_parser {
  unsigned int pos;       /* offset in the JSON string */
  unsigned int toknext;   /* next token to allocate */
  unsigned int line, col; /* current line and col number */
  int toksuper;           /* superior token node, e.g. parent object or array */
  bool __last_is_comma : 1;
  jsmn2tok_t tokbuf;
} jsmn2_parser;

/**
 * Create JSON parser over an array of tokens
 */
JSMN2_API void jsmn2_init(jsmn2_parser* parser);

/**
 * Run JSON parser. It parses a JSON data string into and array of tokens, each
 * describing
 * a single JSON object.
 */
JSMN2_API enum jsmn2err
jsmn2_parse(jsmn2_parser* parser, const char* js, const size_t len, jsmn2tok_t* tokens, const unsigned int num_tokens);

#ifdef __cplusplus
}
#endif

#endif /* JSMN2_H */
