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

#include "jsmn2.h"

static inline void
jsmn2_init_token(jsmn2tok_t* tok) {
  tok->type = JSMN2_UNDEFINED;
  tok->unclosed = false;
  tok->is_key = false;
  tok->associated = false;
  tok->start = -1;
  tok->size = 0;
#ifdef JSMN2_PARENT_LINKS
  tok->parent = -1;
#endif
}

/**
 * Allocates a fresh unused token from the token pool.
 */
static jsmn2tok_t*
jsmn2_alloc_token(jsmn2_parser* parser, jsmn2tok_t* tokens, const size_t num_tokens) {
  jsmn2tok_t* tok;
  if(parser->toknext >= num_tokens) {
    return NULL;
  }
  tok = &tokens[parser->toknext++];
  jsmn2_init_token(tok);
  return tok;
}

/**
 * Fills token type and boundaries.
 */
static inline void
jsmn2_fill_token(jsmn2tok_t* token, const jsmn2type_t type, const int start, const int end) {
  token->type = type;
  token->start = start;
  token->size = end - start;
}

static inline bool
ishexdigit(unsigned c) {
  unsigned short v1 = c - '0';
  unsigned short v2 = (c | 0x20) - 'a';
  return (v1 <= 9) || (v2 <= 5);
}

/**
 * Fills next available token with JSON primitive.
 */
static inline enum jsmn2err
jsmn2_parse_primitive(
    jsmn2_parser* parser, const char* js, const size_t len, jsmn2tok_t* tokens, const size_t num_tokens) {
  jsmn2tok_t* token;
  int start;
  const char* p;
  const char* q = js + len;
  enum jsmn2err res = JSMN2_SUCCESS;

  start = parser->pos;
  p = js + start;

  for(; p < q && *p != '\0'; p++) {
    switch(*p) {
      case '\v':
      case '\f':
      case '\t':
      case '\r':
      case '\n':
      case ' ':
      case ',':
      case ']':
      case '}': goto found;
      default: break;
    }
  }
  parser->pos = start;
  return JSMN2_ERROR_UNEXPECTED_EOF;

found:
  parser->pos = p - js;
  parser->col += parser->pos - start;
  token = jsmn2_alloc_token(parser, tokens, num_tokens);
  if(token == NULL) {
    token = &parser->tokbuf;
    res = JSMN2_ERROR_NOMEM;
  }
  jsmn2_fill_token(token, JSMN2_PRIMITIVE, start, parser->pos);
#ifdef JSMN2_PARENT_LINKS
  token->parent = parser->toksuper;
#endif
  return res;
}

/**
 * Fills next token with JSON string.
 */
static inline enum jsmn2err
jsmn2_parse_string(
    jsmn2_parser* parser, const char* js, const size_t len, jsmn2tok_t* tokens, const size_t num_tokens) {
  jsmn2tok_t* token;
  int start = parser->pos;
  const char* p = js + start + 1; /* Skip the leading quote */
  const char* q = js + len;
  // unsigned int col = parser->col;
  enum jsmn2err res = JSMN2_SUCCESS;

  for(; p < q && *p != '\0';) {
    /* Quote: end of string */
    if(*p == '\"') {
      token = jsmn2_alloc_token(parser, tokens, num_tokens);
      if(token == NULL) {
        token = &parser->tokbuf;
        res = JSMN2_ERROR_NOMEM;
      }
      parser->pos = p - js;
      jsmn2_fill_token(token, JSMN2_STRING, start + 1, parser->pos);
      parser->pos++;
      parser->col += (p - js - start + 1);
#ifdef JSMN2_PARENT_LINKS
      token->parent = parser->toksuper;
#endif
      if((tokens + parser->toksuper)->type == JSMN2_OBJECT)
        token->is_key = true;
      return res;
    }

    /* Backslash: Quoted symbol expected */
    if(*p == '\\' && p + 1 < q) {
      switch(*++p) {
        /* Allowed escaped symbols */
        case '\"':
        case '/':
        case '\\':
        case 'b':
        case 'f':
        case 'r':
        case 'n':
        case 't': p++; break;
        /* Allows escaped symbol \uXXXX */
        case 'u':
          p++;
          /* FIXME: check for invalid codepoints!! */
          for(int i = 0; i < 4 && *p != '\n'; p++, i++) {
            if(!ishexdigit(*p))
              return JSMN2_ERROR_INVAL;
          }
          break;
        /* Unexpected symbol */
        default: return JSMN2_ERROR_INVAL;
      }
    } else {
      p++;
    }
  }
  return JSMN2_ERROR_UNCLOSED_STRING;
}

static inline int
jamn_skip_whitespaces(jsmn2_parser* parser, const char* js, const size_t len) {
  const char* p = js + parser->pos;
  const char* q = js + len;

  do {
    switch(*p) {
      case '\r':
        if(*++p != '\n')
          return -1; // broken newline
        /* FALLTHROUGH */
      case '\n':
        parser->line++;
        parser->col = 0;
        break;
      case ' ':
      case '\t':
      case '\v':
      case '\f': break;
      default: goto out;
    }
    p++;
    parser->col++;
  } while(p < q && *p != '\0');
out:
  parser->pos = p - js;
  return 0;
}

/**
 * Parse JSON string and fill tokens.
 */
#define JSMN2_PARSER_ADVANCE(p, n) \
  do { \
    (p)->pos += (n); \
    (p)->col += (n); \
  } while(0)
JSMN2_API enum jsmn2err
jsmn2_parse(jsmn2_parser* parser, const char* js, const size_t len, jsmn2tok_t* tokens, const unsigned int num_tokens) {
  enum jsmn2err r;
  int i;
  jsmn2tok_t* token;

  assert(tokens != NULL);

  if(parser->tokbuf.type != JSMN2_UNDEFINED) {
    token = jsmn2_alloc_token(parser, tokens, num_tokens);
    if(token == NULL)
      return JSMN2_ERROR_NOMEM;
    (void)memcpy(token, &parser->tokbuf, sizeof(*token));
    parser->tokbuf.type = JSMN2_UNDEFINED;
    parser->tokbuf.unclosed = false;
    parser->tokbuf.is_key = false;
#ifdef JSMN2_NO_TRAILING_COMMAS
    parser->__last_is_comma = false;
#endif
  }

  for(; parser->pos < len && js[parser->pos] != '\0';) {
    char c;
    jsmn2type_t type;
    unsigned int line = parser->line;
    unsigned int col = parser->col;
    unsigned int start = parser->pos;

    c = js[parser->pos];
    switch(c) {
      case '{':
      case '[':
#ifndef JSMN2_TESTMODE
        if(!parser->toknext && c != '{')
          return JSMN2_ERROR_UNEXPECTED_CHAR;
#endif
        token = jsmn2_alloc_token(parser, tokens, num_tokens);
        if(token == NULL) {
          return JSMN2_ERROR_NOMEM;
        }
        if(parser->toksuper != -1) {
          jsmn2tok_t* t = &tokens[parser->toksuper];

          if(t->type == JSMN2_OBJECT) {
            return JSMN2_ERROR_INVAL;
          }
          if(t->type == JSMN2_ARRAY)
            t->size++;
#ifdef JSMN2_PARENT_LINKS
          token->parent = parser->toksuper;
#endif
        } else if(parser->toknext > 1) {
          // This is the case where a root object/array (if in non-strict mode)
          // is already defined.
          return JSMN2_ERROR_EXPECTED_EOF;
        }
        token->type = (c == '{' ? JSMN2_OBJECT : JSMN2_ARRAY);
        token->unclosed = true;
        token->start = parser->pos;
        parser->toksuper = parser->toknext - 1;
        JSMN2_PARSER_ADVANCE(parser, 1);
        break;
      case '}':
      case ']': type = (c == '}' ? JSMN2_OBJECT : JSMN2_ARRAY);
#ifdef JSMN2_PARENT_LINKS
        if(parser->toknext < 1) {
          return JSMN2_ERROR_INVAL;
        }
        token = &tokens[parser->toknext - 1];
        if(token->is_key)
          return JSMN2_ERROR_UNEXPECTED_CHAR;
#ifdef JSMN2_NO_TRAILING_COMMAS
        if(parser->__last_is_comma)
          return JSMN2_ERROR_TRAILING_COMMA;
#endif
        for(;;) {
          if(token->start != -1 && token->unclosed) {
            if(token->type != type) {
              return JSMN2_ERROR_UNEXPECTED_CHAR;
            }
            token->unclosed = false;
            parser->toksuper = token->parent;
            break;
          }
          if(token->parent == -1) {
            if(token->type != type || parser->toksuper == -1) {
              return JSMN2_ERROR_UNEXPECTED_CHAR;
            }
            break;
          }
          token = &tokens[token->parent];
        }
#else
        token = &tokens[parser->toknext - 1];
        if(token->is_key)
          return JSMN2_ERROR_UNEXPECTED_CHAR;
        for(i = parser->toknext - 1; i >= 0; i--) {
          token = &tokens[i];
          if(token->start != -1 && token->unclosed) {
            if(token->type != type) {
              return JSMN2_ERROR_UNEXPECTED_CHAR;
            }
            parser->toksuper = -1;
            token->unclosed = false;
            break;
          }
        }
        /* Error if unmatched closing bracket */
        if(i == -1) {
          return JSMN2_ERROR_UNEXPECTED_CHAR;
        }
        for(; i >= 0; i--) {
          token = &tokens[i];
          if(token->start != -1 && token->unclosed) {
            parser->toksuper = i;
            break;
          }
        }
#endif
        JSMN2_PARSER_ADVANCE(parser, 1);
        break;
      case '\"':
        r = jsmn2_parse_string(parser, js, len, tokens, num_tokens);
        switch(r) {
          case JSMN2_SUCCESS:
          case JSMN2_ERROR_NOMEM:
            if(parser->toksuper != -1 && tokens != NULL) {
              jsmn2tok_t* t = &tokens[parser->toksuper];
              switch(t->type) {
                case JSMN2_OBJECT:
                case JSMN2_ARRAY: t->size++; break;
                case JSMN2_STRING:
                  if(t->is_key && !t->associated) {
                    t->associated = true;
                    break;
                  }
                  /* FALLTHROUGH */
                default:
                  parser->line = line;
                  parser->col = col;
                  parser->pos = start;
                  parser->tokbuf.type = JSMN2_UNDEFINED;
                  // expected ‘,‘, or '}' or ']'!
                  // TODO: More detailed error messages could be provided here!
                  return JSMN2_ERROR_UNEXPECTED_CHAR;
              }
            }
          default:
            if(r != JSMN2_SUCCESS)
              return r;
        }
        break;
      case '\r':
      case '\n':
      case ' ':
      case '\t':
      case '\f':
      case '\v':
        if(jamn_skip_whitespaces(parser, js, len))
          return JSMN2_ERROR_BROKEN_NEWLINE;
        break;
      case ':':
        if(!tokens[parser->toknext - 1].is_key) {
          return JSMN2_ERROR_UNEXPECTED_CHAR;
        }
        parser->toksuper = parser->toknext - 1;
        JSMN2_PARSER_ADVANCE(parser, 1);
        break;
      case ',':
        if(tokens != NULL) {
          if(tokens[parser->toknext - 1].is_key || parser->__last_is_comma)
            return JSMN2_ERROR_UNEXPECTED_CHAR;
#ifdef JSMN2_NO_TRAILING_COMMAS
          else if(tokens[parser->toknext - 1].type <= JSMN2_ARRAY)
            return JSMN2_ERROR_TRAILING_COMMA;
#endif

          if(parser->toksuper != -1 && tokens[parser->toksuper].type > JSMN2_ARRAY) {
#ifdef JSMN2_PARENT_LINKS
            parser->toksuper = tokens[parser->toksuper].parent;
#else
            for(i = parser->toknext - 1; i >= 0; i--) {
              if(tokens[i].type == JSMN2_ARRAY || tokens[i].type == JSMN2_OBJECT) {
                if(tokens[i].start != -1 && tokens[i].unclosed) {
                  parser->toksuper = i;
                  break;
                }
              }
            }
#endif
          }
        }
        JSMN2_PARSER_ADVANCE(parser, 1);
        break;

      case '-':
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case 't':
      case 'f':
      case 'n':
        /* And they must not be keys of the object */
        if(tokens != NULL && parser->toksuper != -1) {
          const jsmn2tok_t* t = &tokens[parser->toksuper];
          if(t->type == JSMN2_OBJECT || (t->type == JSMN2_STRING && !t->is_key)) {
            return JSMN2_ERROR_INVAL;
          }
        }
        r = jsmn2_parse_primitive(parser, js, len, tokens, num_tokens);
        switch(r) {
          case JSMN2_SUCCESS:
          case JSMN2_ERROR_NOMEM:
            if(parser->toksuper != -1 && tokens != NULL) {
              jsmn2tok_t* t = &tokens[parser->toksuper];
              switch(t->type) {
                case JSMN2_OBJECT:
                case JSMN2_ARRAY: t->size++; break;
                case JSMN2_STRING:
                  if(t->is_key && !t->associated) {
                    t->associated = true;
                    break;
                  }
                default:
                  // UNRECHABLE
                  parser->line = line;
                  parser->col = col;
                  parser->pos = start;
                  parser->tokbuf.type = JSMN2_UNDEFINED;
                  // expected ‘,‘, or '}' or ']'!
                  // TODO: More detailed error messages could be provided here!
                  return JSMN2_ERROR_UNEXPECTED_CHAR;
              }
            }
          default:
            if(r != JSMN2_SUCCESS)
              return r;
        }
        break;

      default: return JSMN2_ERROR_INVAL;
    }
    parser->__last_is_comma = (c == ',');
  }

  if(tokens != NULL) {
    for(i = parser->toknext - 1; i >= 0; i--) {
      /* Unmatched opened object or array */
      if(tokens[i].start != -1 && tokens[i].unclosed) {
        return tokens[i].type == JSMN2_OBJECT ? JSMN2_ERROR_UNCLOSED_OBJECT : JSMN2_ERROR_UNCLOSED_ARRAY;
      }
    }
  }
  return JSMN2_SUCCESS;
}
#undef JSMN2_PARSER_ADVANCE

/**
 * Creates a new parser based over a given buffer with an array of tokens
 * available.
 */
JSMN2_API void
jsmn2_init(jsmn2_parser* parser) {
  parser->pos = 0;
  parser->toknext = 0;
  parser->col = 1;
  parser->line = 1;
  parser->toksuper = -1;
  parser->__last_is_comma = false;
  jsmn2_init_token(&parser->tokbuf);
}
