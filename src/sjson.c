/*
 * Copyright (c) 2021 Firmware Modules Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this softwareand associated documentation files(the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions :
 *
 * The above copyright noticeand this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 * OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "sjson.h"

#include <string.h>

#ifdef SJSON_CONF_DEBUG
#define SJSON_DEBUG SJSON_CONF_DEBUG
#else
#define SJSON_DEBUG 0
#endif

#if SJSON_DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

typedef enum {
  PARSE_STATE_WAITING_KEY,
  PARSE_STATE_COMMENT_START,
  PARSE_STATE_COMMENT,
  PARSE_STATE_KEY,
  PARSE_STATE_KEY_DONE,
  PARSE_STATE_WAITING_VALUE,
  PARSE_STATE_VALUE_STRING,
  PARSE_STATE_VALUE_PRIMITIVE,
  PARSE_STATE_VALUE_DONE,
} parse_state_t;

typedef enum { PARSE_STATE_STR_NORMAL, PARSE_STATE_STR_ESC, PARSE_STATE_STR_UNI } parse_state_string_t;

typedef enum {
  PARSE_COMMENT_SINGLE = 0,
  PARSE_COMMENT_MULTI,

} parse_comment_style_t;

#define SET_ARRAY() \
  do { \
    (ctx->is_array |= ((uint32_t)1 << (ctx->depth))); \
  } while(0)
#define CLEAR_ARRAY() \
  do { \
    (ctx->is_array &= ~((uint32_t)1 << (ctx->depth))); \
  } while(0)
#define IS_ARRAY() (ctx->is_array & ((uint32_t)1 << (ctx->depth)))

int
sjson_init(sjson_ctx_t* ctx, char* buf, uint16_t len, const sjson_cb_t* callbacks) {
  if(ctx) {
    memset(ctx, 0, sizeof(*ctx));
    if(buf && len > 0 && callbacks) {
      ctx->buf = buf;
      ctx->buf_len = len;
      ctx->callbacks = callbacks;
      sjson_reset(ctx);
      return SJSON_STATUS_OK;
    }
  }
  return SJSON_STATUS_INVALID_ARGS;
}

#if SJSON_DEBUG
static const char*
get_state_str(uint8_t s) {
  switch(s) {
    case PARSE_STATE_WAITING_KEY: return "WAITING_KEY";
    case PARSE_STATE_COMMENT_START: return "COMMENT_START";
    case PARSE_STATE_COMMENT: return "COMMENT";
    case PARSE_STATE_KEY: return "KEY";
    case PARSE_STATE_KEY_DONE: return "KEY_DONE";
    case PARSE_STATE_WAITING_VALUE: return "WAITING_VALUE";
    case PARSE_STATE_VALUE_STRING: return "VALUE_STRING";
    case PARSE_STATE_VALUE_PRIMITIVE: return "VALUE_PRIMITIVE";
    case PARSE_STATE_VALUE_DONE: return "VALUE_DONE";
    default: return "UNKNOWN";
  }
}
#endif

/*
 * res is flow-through unless there is an error
 */
static int
add_char(sjson_ctx_t* ctx, char c, int res) {
  if(ctx->buf_len > 0 && ctx->pos < ctx->buf_len - 1) {
    ctx->buf[ctx->pos++] = c;
  } else {
    /* Ensure buffer is always null terminated */
    ctx->buf[ctx->pos] = 0;
    res = SJSON_STATUS_OVERFLOW;
  }
  return res;
}

static int
match_key(sjson_ctx_t* ctx) {
  /* Inside buffer is the key string, null terminated.
   * Match it to all registered handlers.  If a match is found,
   * Set the callback handler as active for invocation when the value is found.
   */
  int res = SJSON_STATUS_OK;
  const sjson_cb_t* cb;

  ctx->value_handler = 0;
  for(cb = ctx->callbacks; cb->key != 0; cb++) {
#if SJSON_DEBUG
    printf("matching: %s -> %s\n", cb->key, ctx->buf);
#endif
    if(strncmp(cb->key, ctx->buf, ctx->buf_len) == 0) {
#if SJSON_DEBUG
      printf("match: [%s] handler:%p\n", cb->key, cb->value_handler);
#endif
      ctx->value_handler = cb->value_handler;
      ctx->opaque = cb->opaque;
      break;
    }
  }

  return res;
}

static int
invoke_handler(sjson_ctx_t* ctx, sjson_type_t type) {
  int res = SJSON_STATUS_OK;
  if(ctx->value_handler) {
    /* Required to exclude terminating '/0' */
    uint16_t len = ctx->pos > 0 ? ctx->pos - 1 : 0;
#if SJSON_DEBUG
    printf("invoke handler: %p buf:%p pos:%d type:%d\n", ctx->value_handler, ctx->buf, len, type);
#endif
    res = ctx->value_handler(ctx->buf, len, type, ctx->depth, ctx->opaque);
    ctx->stats.num_callbacks++;
  }
  return res;
}

static uint8_t
lookup_hex(const char c) {
  switch(c) {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'A':
    case 'a': return 10;
    case 'B':
    case 'b': return 11;
    case 'C':
    case 'c': return 12;
    case 'D':
    case 'd': return 13;
    case 'E':
    case 'e': return 14;
    case 'F':
    case 'f': return 15;
    default: return -1;
  }
}

/**
 * Collect characters as a primitive.
 *
 * Return OK if done parsing; IN_PROGRESS if parsing; < 0 on error.
 *
 * Note: consumes value-terminating character as well, unlike the string parser.
 */
static int
parse_primitive(sjson_ctx_t* ctx, char c) {
  int res = SJSON_STATUS_IN_PROGRESS;

  /* Check for potentially valid characters */
  if(c >= 32 || c < 127) {
    /* Check for end of primitive */
    switch(c) {
      case '\t':
      case '\r':
      case '\n':
      case ' ':
      case ',':
      case ']':
      case '}':
        /* End  */
        res = add_char(ctx, 0, SJSON_STATUS_OK);
        return res;
    }

    /* Add char to value buffer */
    res = add_char(ctx, c, res);
  } else {
    /* Bad character */
    res = SJSON_STATUS_MALFORMED_VALUE;
  }

  return res;
}

/*
 * Return OK if done parsing; IN_PROGRESS if parsing; < 0 on error.
 */
static int
parse_string(sjson_ctx_t* ctx, char c) {

  int res = SJSON_STATUS_IN_PROGRESS;

  switch(ctx->parse_state_str) {
    case PARSE_STATE_STR_NORMAL:
      switch(c) {
        case '\"':
          /* Done - terminate */
          res = add_char(ctx, 0, SJSON_STATUS_OK);
          break;
        case '\\':
          /* Backslash: Escaped symbol expected */
          ctx->parse_state_str = PARSE_STATE_STR_ESC;
          break;
        default:
          /* everything else gets passed through as the key */
          res = add_char(ctx, c, res);
          break;
      }
      break;
    case PARSE_STATE_STR_ESC:
      switch(c) {
          /* Allowed escaped symbols */
        case '\"':
        case '/':
        case '\\':
          res = add_char(ctx, c, res);
          ctx->parse_state_str = PARSE_STATE_STR_NORMAL;
          break;
        case 'b':
          res = add_char(ctx, '\b', res);
          ctx->parse_state_str = PARSE_STATE_STR_NORMAL;
          break;
        case 'f':
          res = add_char(ctx, '\f', res);
          ctx->parse_state_str = PARSE_STATE_STR_NORMAL;
          break;
        case 'r':
          res = add_char(ctx, '\r', res);
          ctx->parse_state_str = PARSE_STATE_STR_NORMAL;
          break;
        case 'n':
          res = add_char(ctx, '\n', res);
          ctx->parse_state_str = PARSE_STATE_STR_NORMAL;
          break;
        case 't':
          res = add_char(ctx, '\t', res);
          ctx->parse_state_str = PARSE_STATE_STR_NORMAL;
          break;
        case 'u':
          /* Allows escaped symbol \uXXXX */
          ctx->parse_state_str = PARSE_STATE_STR_UNI;
          ctx->str_uni_cnt = 0;
          break;
        default:
          /* Unsupported escape */
          res = SJSON_STATUS_UNEXPECTED_INPUT;
          break;
      }
      break;
    case PARSE_STATE_STR_UNI:
      /* Note!  These limitations apply to our implementation of JSON unicode parsing:
       *  - ASCII only (in and out).
       * If the encoded unicode character can't fit in a byte, then a parsing error is declared.
       * In other words, max unicode character supported is \u00FF
       */

      /* If it isn't a hex character we have an error */
      if(!((c >= 48 && c <= 57) ||   /* 0-9 */
           (c >= 65 && c <= 70) ||   /* A-F */
           (c >= 97 && c <= 102))) { /* a-f */
        res = SJSON_STATUS_UNEXPECTED_INPUT;
      } else {
        if(ctx->str_uni_cnt < 2) {
          /* First two unicode must be 0 due to constraints outlined above. */
          if(c != '0') {
            res = SJSON_STATUS_UNEXPECTED_INPUT;
          }
        } else if(ctx->str_uni_cnt == 2) {
          /* store the (MSB) unicode ASCII character byte */
          res = add_char(ctx, c, res);
        } else if(ctx->str_uni_cnt == 3) {
          /*  Combine the (LSB) unicode ASCII character byte with the stored MSB byte.
           * The token buffer's position will be non-zero if we get here.
           */
          const uint8_t index = ctx->pos - 1;
          uint8_t ascii = lookup_hex(ctx->buf[index]) << 4; /* MSB */
          ascii |= lookup_hex(c) & 0xf;                     /* LSB */
          ctx->buf[index] = ascii;
        }
      }

      if(++ctx->str_uni_cnt >= 4) {
        ctx->parse_state_str = PARSE_STATE_STR_NORMAL;
      }
      break;
    default:
      /* Unexpected symbol */
      res = SJSON_STATUS_INVALID_STATE;
      break;
  }

  return res;
}

static void
transition(sjson_ctx_t* ctx, parse_state_t new_state) {
  switch(new_state) {
    case PARSE_STATE_KEY:
      /* Ensure value handler is cleared for each new key */
      ctx->value_handler = 0;
      break;
    default: break;
  }

  ctx->parse_state = new_state;
}

/*
 * Returns >= 0 if parsing is working; < 0 on any error as soon as it is detected.
 */
int
sjson_parse(sjson_ctx_t* ctx, const char* buf, int len) {
  int res = SJSON_STATUS_INVALID_ARGS;

  if(ctx && buf && ctx->buf && ctx->callbacks) {

    res = SJSON_STATUS_OK;

    int pc = 0;
    /* Consume all bytes in the buffer */
    while(pc < len) {

      ctx->stats.char_count++;

      char c = buf[pc++];

#if SJSON_DEBUG
      printf("run: %d %d %3d %c %s %d %d %d %d %08x\n",
             len,
             pc,
             c,
             ((c >= 32 && c < 127) ? c : ' '),
             get_state_str(ctx->parse_state),
             ctx->parse_state_str,
             ctx->pos,
             ctx->depth,
             IS_ARRAY() > 0 ? 1 : 0,
             ctx->is_array);
#endif

      switch(ctx->parse_state) {
        case PARSE_STATE_WAITING_KEY: {
          switch(c) {
            case '\"':
              transition(ctx, PARSE_STATE_KEY);
              ctx->pos = 0;
              ctx->parse_state_str = PARSE_STATE_STR_NORMAL;
              /* consume the quotation char */
              break;
              /* whitespace */
            case '\t':
            case '\r':
            case '\n':
            case ' ': break;
            case '/': transition(ctx, PARSE_STATE_COMMENT_START); break;
            case '{': ctx->depth++; break;
            case ',':
              /* Eat commas that arise when exiting objects }, "key" : ... or arrays ], */
              break;
            case '}':
              if(ctx->depth > 0) {
                /* allow closing brace if within object */
                ctx->depth--;
                if(IS_ARRAY()) {
                  /* If previous level is array, wait for value */
                  transition(ctx, PARSE_STATE_WAITING_VALUE);
                }
              } else {
                res = SJSON_STATUS_UNEXPECTED_INPUT;
              }
              break;
            default:
              /* something unexpected. */
              res = SJSON_STATUS_UNEXPECTED_INPUT;
              break;
          }
          break;
        }
        case PARSE_STATE_COMMENT_START: {
          ctx->comment_style = PARSE_COMMENT_SINGLE;
          switch(c) {
            case '*':
              ctx->comment_style = PARSE_COMMENT_MULTI;
              ctx->test_multi_end = 0;
              /* fall-through */
            case '/':
              /* ok - parsing comment */
              transition(ctx, PARSE_STATE_COMMENT);
              break;
            default:
              /* not ok - parsing error */
              res = SJSON_STATUS_UNEXPECTED_INPUT;
              break;
          }
          break;
        }
        case PARSE_STATE_COMMENT: {
          switch(c) {
            case '\r':
            case '\n':
              if(ctx->comment_style == PARSE_COMMENT_SINGLE) {
                /* end of // style */
                transition(ctx, PARSE_STATE_WAITING_KEY);
              }
              break;
            case '*':
              /* might be end of multi-line */
              if(ctx->comment_style == PARSE_COMMENT_MULTI) {
                ctx->test_multi_end = 1;
              }
              break;
            case '/':
              if(ctx->test_multi_end == 1) {
                transition(ctx, PARSE_STATE_WAITING_KEY);
              }
              break;
            default:
              ctx->test_multi_end = 0;
              /* consume rest */
              break;
          }
          break;
        }
        case PARSE_STATE_KEY: {
          res = parse_string(ctx, c);
          if(res == SJSON_STATUS_OK) {
            ctx->stats.num_keys++;
            res = match_key(ctx);
            transition(ctx, PARSE_STATE_KEY_DONE);
          }
          break;
        }
        case PARSE_STATE_KEY_DONE: {
          /* Next state must be transited through ':'
           */
          switch(c) {
            case ':': transition(ctx, PARSE_STATE_WAITING_VALUE); break;
            case '\t':
            case '\r':
            case '\n':
            case ' ':
              /* Whitespace */
              break;
            default:
              /* something unexpected. */
              res = SJSON_STATUS_UNEXPECTED_INPUT;
              break;
          }
          break;
        }
        case PARSE_STATE_WAITING_VALUE: {
          switch(c) {
            case '\"':
              /* Parsing a string value */
              transition(ctx, PARSE_STATE_VALUE_STRING);
              ctx->pos = 0;
              ctx->parse_state_str = PARSE_STATE_STR_NORMAL;
              /* consume the quotation char */
              break;
            case '\t':
            case '\r':
            case '\n':
            case ' ':
              /* Whitespace */
              break;
            case ',':
              /* consume, keep looking */
              break;
            case '[':
              ctx->depth++;
              SET_ARRAY();
              break;
            case '{':
              ctx->depth++;
              transition(ctx, PARSE_STATE_WAITING_KEY);
              break;
            case ']':
              if(IS_ARRAY()) {
                /* Allow closing the array */
                CLEAR_ARRAY();
                ctx->depth--;
                transition(ctx, PARSE_STATE_WAITING_KEY);
              } else {
                res = SJSON_STATUS_UNEXPECTED_INPUT;
              }
              break;
            case ':': res = SJSON_STATUS_UNEXPECTED_INPUT; break;
            default:
              /* Start parsing a primitive number or boolean */
              transition(ctx, PARSE_STATE_VALUE_PRIMITIVE);
              ctx->pos = 0;
              /* include current char */
              res = parse_primitive(ctx, c);
          }
          break;
        }
        case PARSE_STATE_VALUE_STRING: {
          /* String parser consumes the ending quotation but nothing beyond */
          res = parse_string(ctx, c);
          if(res == SJSON_STATUS_OK) {
            ctx->stats.num_strings++;
            res = invoke_handler(ctx, SJSON_TYPE_STRING);
            transition(ctx, PARSE_STATE_VALUE_DONE);
          }
          break;
        }
        case PARSE_STATE_VALUE_DONE: {
          switch(c) {
            case ']':
              if(IS_ARRAY()) {
                CLEAR_ARRAY();
                ctx->depth--;
                if(IS_ARRAY()) {
                  /* Look for next value in this array */
                  transition(ctx, PARSE_STATE_WAITING_VALUE);
                } else {
                  transition(ctx, PARSE_STATE_WAITING_KEY);
                }
              } else {
                res = SJSON_STATUS_UNEXPECTED_INPUT;
              }
              break;
            case ',':
              if(IS_ARRAY()) {
                /* Look for next value in this array */
                transition(ctx, PARSE_STATE_WAITING_VALUE);
              } else {
                transition(ctx, PARSE_STATE_WAITING_KEY);
              }
              break;
            case '}':
              /* End the string token, resume looking for keys or values */
              ctx->depth--;
              if(IS_ARRAY()) {
                /* Look for next value in this array */
                transition(ctx, PARSE_STATE_WAITING_VALUE);
              } else {
                transition(ctx, PARSE_STATE_WAITING_KEY);
              }
              break;
            case '\t':
            case '\r':
            case '\n':
            case ' ':
              /* Whitespace */
              break;
            default:
              /* Something unexpected. */
              res = SJSON_STATUS_UNEXPECTED_INPUT;
              break;
          }
          break;
        }
        case PARSE_STATE_VALUE_PRIMITIVE: {
          /* Primitive parser consumes the end comma or other terminating character */
          res = parse_primitive(ctx, c);
          if(res == SJSON_STATUS_OK) {
            ctx->stats.num_primitives++;
            res = invoke_handler(ctx, SJSON_TYPE_PRIMITIVE);
            /* End the primitive token, resume looking for keys */
            transition(ctx, PARSE_STATE_WAITING_KEY);
            switch(c) {
              case ']':
                if(IS_ARRAY()) {
                  CLEAR_ARRAY();
                  ctx->depth--;
                  if(IS_ARRAY()) {
                    /* Look for next value in this array */
                    transition(ctx, PARSE_STATE_WAITING_VALUE);
                  }
                } else {
                  res = SJSON_STATUS_UNEXPECTED_INPUT;
                }
                break;
              case ',':
                if(IS_ARRAY()) {
                  /* Look for next value in this array */
                  transition(ctx, PARSE_STATE_WAITING_VALUE);
                }
                break;
              case '}':
                ctx->depth--;
                if(IS_ARRAY()) {
                  /* Look for next value in this array */
                  transition(ctx, PARSE_STATE_WAITING_VALUE);
                }
                break;
              default:
                /* Other whitespace: wait for proper finish */
                transition(ctx, PARSE_STATE_VALUE_DONE);
                break;
            }
          }
          break;
        }

        default: res = SJSON_STATUS_INVALID_STATE; break;
      }

      /* Error during procesing- quit. */
      if(res < 0) {
        break;
      }
    }

    /* Update overall parsing status statistic */
    ctx->stats.status = res < ctx->stats.status ? res : ctx->stats.status;
  }

  return res;
}

void
sjson_reset(sjson_ctx_t* ctx) {
  if(ctx) {
    ctx->parse_state = PARSE_STATE_WAITING_KEY;
    ctx->pos = 0;
    ctx->value_handler = 0;
    ctx->str_uni_cnt = 0;
    ctx->parse_state_str = PARSE_STATE_STR_NORMAL;
    ctx->is_array = 0;
    ctx->depth = 0;
    memset(&ctx->stats, 0, sizeof(ctx->stats));
  }
}

const char*
sjson_status_to_str(int code) {
  switch(code) {
    case SJSON_STATUS_OK: return "OK";
    case SJSON_STATUS_IN_PROGRESS: return "IN_PROGRESS";
    case SJSON_STATUS_ERROR: return "ERROR";
    case SJSON_STATUS_INVALID_ARGS: return "INVALID_ARGS";
    case SJSON_STATUS_MALFORMED_KEY: return "MALFORMED_KEY";
    case SJSON_STATUS_MALFORMED_VALUE: return "MALFORMED_VALUE";
    case SJSON_STATUS_INVALID_STATE: return "INVALID_STATE";
    case SJSON_STATUS_OVERFLOW: return "OVERFLOW";
    case SJSON_STATUS_UNEXPECTED_INPUT: return "UNEXPECTED_INPUT";
    default: return "<err>";
  }
}

int
sjson_pack_hexstr2bin(const char* hex_str, uint8_t* hex_bin, int len) {
  int res = SJSON_STATUS_INVALID_ARGS;
  if(hex_str && hex_bin && len > 0) {
    res = SJSON_STATUS_OK;
    const char* ptr = hex_str;
    int n = 0;
    while(n < len && *ptr != 0) {
      uint8_t val = 0;
      uint8_t a = lookup_hex(*ptr);
      if(a < 0) {
        return -1;
      }
      val = a << 4;

      ptr++;
      if(*ptr) {
        uint8_t b = lookup_hex(*ptr);
        if(b < 0) {
          return -1;
        }
        ptr++;
        val |= b;
      } else {
        res = SJSON_STATUS_UNEXPECTED_INPUT;
      }

      hex_bin[n++] = val;
    }

    if(n == len && *ptr != 0) {
      /* Buffer would overflow - truncated output */
      res = SJSON_STATUS_OVERFLOW;
    }
  }

  return res;
}
