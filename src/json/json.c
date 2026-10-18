#include "char-utils.h"
#include "json/json.h"

/**
 * \addtogroup json
 * @{
 */

enum {
  ERROR = -1,
  PARSING = 0,
  PARSING_OBJECT_KEY = 0b01,
  PARSING_OBJECT_VALUE = 0b10,
  PARSING_OBJECT = 0b11,
  PARSING_ARRAY = 0b100,
  EXPECTING_COMMA_OR_END = 0b1000,
  EXPECTING_COLON = 0b10000,
};

/* internal json_getc_skipws() result: a complete comment is in json->token */
#define C_SIG (-200)

/* skips whitespace and C/C++ comments, returns the next significant byte.
 *
 * `json->cmt_state` keeps a comment open across a NEED_DATA return:
 * ```
 * 0 none | 1 after '/' | 2 in `//` | 3 in `/ *` | 4 in `/ *` after '*'
 * ```
 *
 * with `json->comments` set, a finished comment is kept in the token and
 * C_SIG is returned instead of skipping it.
 *
 * a '/' not followed by '/' or '*' is returned as a plain byte (an error
 * for the caller); the byte after it is pushed back. */
static int
json_getc_skipws(JsonParser* json) {
  int c, ws_column = 0;
  size_t pos = json->token.size;

  enum {
    C_NONE = 0,
    C_START,
    C_ONELINE,
    C_MULTILINE,
    C_MULTILINE_END,
  };

  for(;;) {
    if((c = json_getc(json)) < 0) {
      if(c == STREAM_EOF && json->cmt_state == 2 && json->comments) {
        json->cmt_state = 0;
        return C_SIG;
      }

      return c;
    }

    switch(json->cmt_state) {
      case C_NONE:
        if(c == '/' && ((json->flags & (JR_LEGACY | JR_COMMENTS)) != 0)) {
          json->cmt_state = C_START;
          json->cmt_column = ws_column;
          break;
        }

        if(is_whitespace_char(c) && c != '\n') {
          ws_column++;
          break;
        }

        ws_column = 0;

        if(!is_whitespace_char(c))
          return c;

        break;

      case C_START:
        if(c == '/') {
          json->cmt_state = C_ONELINE;
        } else if(c == '*') {
          json->cmt_state = C_MULTILINE;
        } else {
          json->cmt_state = C_NONE;
          json_ungetc(json, c);
          json->token.size = pos;
          dbuf_putc(&json->token, '/');
          return '/';
        }

        break;

      case C_ONELINE:
        if(c == '\n') {
          json->cmt_state = 0;

          if(json->comments) {
            --json->token.size; /* the newline is not part of the comment */
            return C_SIG;
          }
        }

        break;

      case C_MULTILINE:
        if(c == '*')
          json->cmt_state = C_MULTILINE_END;

        break;

      case C_MULTILINE_END:
        if(c == '/') {
          json->cmt_state = C_NONE;

          if(json->comments)
            return C_SIG;
        } else if(c != '*') {
          json->cmt_state = C_MULTILINE;
        }

        break;
    }

    /* eat leading indentation */
    if(json->cmt_state == C_MULTILINE)
      if(c == '\n') {
        int eat = json->cmt_column;

        while(--eat >= 0) {
          if((c = json_getc(json)) < 0) {
            if(c == STREAM_EOF)
              return C_SIG;

            return c;
          }

          if(is_whitespace_char(c))
            json->token.size--;
        }
      }

    if(!(json->comments && json->cmt_state))
      json->token.size = pos;
  }
}

BOOL
json_init(JsonParser* json, Reader reader, const char* filename, JSContext* ctx) {
  json->reader = reader;
  json->block_pos = json->block_len = 0;
  json->pos = 0;
  json->pushback = -1;
  json->state = PARSING;
  json->stack = BITSET_INIT();
  json->error = NULL;

  json->tok_kind = JSON_TOK_NONE;
  json->str_state = JSON_STR_NORMAL;
  json->str_unicode_val = 0;
  json->str_unicode_count = 0;
  json->str_surrogate_hi = 0;
  json->literal_text = NULL;
  json->literal_pos = 0;
  json->is_key = FALSE;
  json->skip_depth = 0;
  json->cmt_state = 0;
  json->comments = FALSE;
  json->flags = JR_LEGACY;
  json->quote = '"';
  json->num_state = 0;
  json->roots = 0;
  json->pending = 0;
  json->has_pending = 0;
  json->after_comma = 0;
  json->trailing = 0;

  dbuf_init_ctx(ctx, &json->token);

  if(!(json->loc = location_new(ctx)))
    return FALSE;

  location_zero(json->loc);

  if(filename)
    location_set_filename(json->loc, filename, ctx);

  return json->reader.read != NULL;
}

JsonParser*
json_new(Reader reader, const char* filename, JSContext* ctx) {
  JsonParser* parser;

  if(!(parser = js_malloc(ctx, sizeof(JsonParser))))
    return 0;

  if(!json_init(parser, reader, filename, ctx)) {
    js_free(ctx, parser);
    return 0;
  }

  return parser;
}

void
json_free(JsonParser* json, JSRuntime* rt) {
  json_clear(json, rt);
  js_free_rt(rt, json);
}

void
json_clear(JsonParser* json, JSRuntime* rt) {
  reader_free(&json->reader);
  dbuf_free(&json->token);
  free(json->stack.ptr); /* bitset_resize() uses libc realloc */
  json->stack = BITSET_INIT();

  if(json->loc)
    location_free(json->loc, rt);
}

int
json_getc(JsonParser* json) {
  int c;
  BOOL from_reader = json->pushback < 0;

  if(!from_reader) {
    c = json->pushback;
    json->pushback = -1;
  } else {
    if(json->block_pos == json->block_len) {
      ssize_t n = reader_read(&json->reader, json->block, sizeof(json->block));

      if(n <= 0) {
        c = n == 0 ? STREAM_EOF : STREAM_ERROR;
        goto done;
      }

      json->block_pos = 0;
      json->block_len = MIN_NUM((size_t)n, sizeof(json->block));
    }

    c = json->block[json->block_pos++];
  }

done:

  if(c >= 0) {
    ++json->pos;
    dbuf_putc(&json->token, c);

    /* a replayed pushback char was already counted once, when it was first read */
    if(from_reader) {
      json->last_start = (JsonPos){json->loc->line, json->loc->column, json->loc->char_offset, json->loc->byte_offset};
      location_nextchar(json->loc, c);
    }
  }

  return c;
}

int
json_ungetc(JsonParser* json, char c) {
  if(json->pushback != -1)
    return -1;

  json->pushback = (unsigned int)(unsigned char)c;

  --json->pos;
  json->token.size -= 1;
  return 0;
}

static int
json_need_or_error(int c) {
  return c == STREAM_ERROR ? JSON_ERROR : JSON_NEED_DATA;
}

/* Common entry point for every "give up on the current token, resync instead" site below.
 * `c` is whatever byte actually triggered the error - if it was itself a '{'/'[' (already
 * consumed, so json_scan_error_skip() never sees it), that opening needs a matching '}'/']'
 * before a *later* one can be trusted as this scan's own resync boundary, or the enclosing
 * container's real close ends up popping json->stack one entry too many. See json.h's
 * skip_depth field and json_scan_error_skip() below. */
static void
json_error_skip_enter(JsonParser* json, int c, const char* code) {
  json->err_code = code;
  json->err_at = json->tok_start;
  json->err_len = json->loc->byte_offset - json->tok_start.byte_offset;
  json->skip_depth = (c == '{' || c == '[') ? 1 : 0;
  json->after_comma = 0;
  json->tok_kind = JSON_TOK_ERROR_SKIP;
}

/* Resumes/continues scanning a string value's content. Returns a JsonEventType (STRING or
 * KEY) once the closing quote is found, JSON_ERROR on malformed escapes, or propagates a
 * negative reader signal (translated by the caller) when input runs out mid-scan - str_state
 * plus whatever's already decoded into json->token let the next call pick back up exactly
 * where this one left off. */
static int
json_scan_string(JsonParser* json) {
  int c;

  for(;;) {
    if(json->str_state == JSON_STR_ESCAPE) {
      if((c = json_getc(json)) < 0)
        return c;

      json->token.size -= 1; /* raw escaped char; the decoded form (if any) is appended below */
      json->str_state = JSON_STR_NORMAL;

      if(c == 'u') {
        json->str_state = JSON_STR_UNICODE;
        json->str_unicode_val = 0;
        json->str_unicode_count = 0;
        continue;
      }

      if(json->flags & JR_JSON5_STRINGS) {
        /* escapes of JSON5: `\x41` `\v` `\0` `\'` `\<any char>` and line continuations */
        switch(c) {
          case 'x':
            json->str_state = JSON_STR_HEX;
            json->str_unicode_val = 0;
            json->str_unicode_count = 0;
            continue;
          case 'v': dbuf_putc(&json->token, '\v'); continue;
          case '0':
            dbuf_putc(&json->token, 0);
            json->str_state = JSON_STR_NUL;
            continue;
          case '\r': json->str_state = JSON_STR_CR; continue;
          case '\n': continue;
          case 0xe2: json->str_state = JSON_STR_E2; continue;
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
          case '6':
          case '7':
          case '8':
          case '9':
            json->error = "invalid escape sequence in string";
            json_error_skip_enter(json, c, "bad-escape");
            return JSON_ERROR;
        }

        {
          int uc = is_quotable_char(c);

          dbuf_putc(&json->token, uc ? uc : c);
        }

        continue;
      }

      {
        int uc = is_quotable_char(c);

        if(!uc) {
          json->error = "invalid escape sequence in string";
          json_error_skip_enter(json, c, "bad-escape");
          return JSON_ERROR;
        }

        dbuf_putc(&json->token, uc);
      }

      continue;
    }

    if(json->str_state == JSON_STR_NUL) {
      if((c = json_getc(json)) < 0)
        return c;

      json->str_state = JSON_STR_NORMAL;

      if(c >= '0' && c <= '9') {
        json->token.size -= 1;
        json->error = "invalid escape sequence in string";
        json_error_skip_enter(json, c, "bad-escape");
        return JSON_ERROR;
      }

      json_ungetc(json, c);
      continue;
    }

    if(json->str_state == JSON_STR_CR) {
      if((c = json_getc(json)) < 0)
        return c;

      json->str_state = JSON_STR_NORMAL;

      if(c == '\n')
        json->token.size -= 1;
      else
        json_ungetc(json, c);

      continue;
    }

    if(json->str_state == JSON_STR_E2) {
      /* `\` + a character starting with 0xE2: U+2028 and U+2029 continue the line, any
         other is itself */
      if((c = json_getc(json)) < 0)
        return c;

      if(c == 0x80) {
        json->token.size -= 1;
        json->str_state = JSON_STR_E2_80;
      } else {
        json_ungetc(json, c);
        dbuf_putc(&json->token, 0xe2);
        json->str_state = JSON_STR_NORMAL;
      }

      continue;
    }

    if(json->str_state == JSON_STR_E2_80) {
      if((c = json_getc(json)) < 0)
        return c;

      json->str_state = JSON_STR_NORMAL;

      if(c == 0xa8 || c == 0xa9) {
        json->token.size -= 1;
      } else {
        json_ungetc(json, c);
        dbuf_putc(&json->token, 0xe2);
        dbuf_putc(&json->token, 0x80);
      }

      continue;
    }

    if(json->str_state == JSON_STR_UNICODE || json->str_state == JSON_STR_HEX) {
      int v;

      if((c = json_getc(json)) < 0)
        return c;

      json->token.size -= 1; /* hex digits aren't part of the decoded text */

      if(!is_xdigit_char(c)) {
        json->error = "invalid unicode escape in string";
        json_error_skip_enter(json, c, "bad-unicode-escape");
        return JSON_ERROR;
      }

      v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10;
      json->str_unicode_val = (json->str_unicode_val << 4) | (uint32_t)v;

      if(++json->str_unicode_count < (json->str_state == JSON_STR_HEX ? 2 : 4))
        continue;

      json->str_state = JSON_STR_NORMAL;

      {
        uint32_t cp = json->str_unicode_val;

        if(is_utf16_high_surrogate(cp)) {
          json->str_surrogate_hi = cp;
        } else {
          uint8_t buf[UTF8_CHAR_LEN_MAX];
          int n;

          if(is_utf16_low_surrogate(cp) && json->str_surrogate_hi)
            cp = 0x10000 + ((json->str_surrogate_hi - 0xd800) << 10) + (cp - 0xdc00);

          json->str_surrogate_hi = 0;
          n = unicode_to_utf8(buf, cp);
          dbuf_put(&json->token, buf, n);
        }
      }

      continue;
    }

    /* JSON_STR_NORMAL */
    if((c = json_getc(json)) < 0)
      return c;

    if(c == '\\') {
      json->token.size -= 1; /* strip the backslash now: atomic with reading it, so a pause
                                 right after (waiting for the escaped char) can't double-strip */
      json->str_state = JSON_STR_ESCAPE;
      continue;
    }

    if(c == json->quote) {
      json->token.size -= 1; /* drop the closing quote */
      json->tok_kind = JSON_TOK_NONE;
      return json->is_key ? JSON_EVENT_KEY : JSON_EVENT_STRING;
    }

    /* JSON5 lets other control characters stand in a string, not a line terminator */
    if(c < 0x20 && !(json->flags & JR_LEGACY) && (!(json->flags & JR_JSON5_STRINGS) || c == '\n' || c == '\r')) {
      json->token.size -= 1;
      json->error = "control character in string";
      json_error_skip_enter(json, c, "control-character");
      return JSON_ERROR;
    }

    /* ordinary byte: already appended by json_getc() as-is */
  }
}

/* number scan states outside JR_LEGACY, in json->num_state:
 * ```
 * FIRST start | SIGN after - or + | ZERO after 0 | INT digits | DOT after 5.
 * LDOT after .  | FRAC fraction | EXP after e | EXPSIGN after e+ | EXPDIG exponent
 * HEX0 after 0x | HEX hex digits | KW inside Infinity or NaN
 * ``` */
enum {
  NS_FIRST,
  NS_SIGN,
  NS_ZERO,
  NS_INT,
  NS_DOT,
  NS_LDOT,
  NS_FRAC,
  NS_EXP,
  NS_EXPSIGN,
  NS_EXPDIG,
  NS_HEX0,
  NS_HEX,
  NS_KW,
};

/* whether a number may end in `state` (at a delimiter or the end of input) */
static int
json_num_complete(const JsonParser* json) {
  switch(json->num_state) {
    case NS_ZERO:
    case NS_INT:
    case NS_FRAC:
    case NS_EXPDIG:
    case NS_HEX: return 1;
    case NS_DOT: return (json->flags & JR_JSON5_NUMBERS) != 0;
  }

  return 0;
}

/* one byte of a number: 1 = part of it, 0 = it ends before `c`, -1 = `c` is wrong. */
static int
json_num_step(JsonParser* json, int c) {
  int n5 = (json->flags & JR_JSON5_NUMBERS) != 0;
  int digit = c >= '0' && c <= '9';

  switch(json->num_state) {
    case NS_FIRST:
    case NS_SIGN:
      if(json->num_state == NS_FIRST && c == '-') {
        json->num_state = NS_SIGN;
      } else if(json->num_state == NS_FIRST && c == '+' && n5) {
        json->num_state = NS_SIGN;
      } else if(c == '0') {
        json->num_state = NS_ZERO;
      } else if(digit) {
        json->num_state = NS_INT;
      } else if(c == '.' && n5) {
        json->num_state = NS_LDOT;
      } else if((c == 'I' || c == 'N') && n5) {
        json->literal_text = c == 'I' ? "Infinity" : "NaN";
        json->literal_pos = 1;
        json->num_state = NS_KW;
      } else {
        return -1;
      }

      return 1;

    case NS_ZERO:
      if(digit)
        return -1;

      if(c == '.') {
        json->num_state = NS_DOT;
        return 1;
      }

      if(c == 'e' || c == 'E') {
        json->num_state = NS_EXP;
        return 1;
      }

      if(c == 'x' || c == 'X') {
        if(!n5)
          return -1;

        json->num_state = NS_HEX0;
        return 1;
      }

      return 0;

    case NS_INT:
      if(digit)
        return 1;

      if(c == '.') {
        json->num_state = NS_DOT;
        return 1;
      }

      if(c == 'e' || c == 'E') {
        json->num_state = NS_EXP;
        return 1;
      }

      return 0;

    case NS_DOT:
      if(digit) {
        json->num_state = NS_FRAC;
        return 1;
      }

      if(c == 'e' || c == 'E') {
        if(!n5)
          return -1;

        json->num_state = NS_EXP;
        return 1;
      }

      return n5 ? 0 : -1;

    case NS_LDOT:
      if(!digit)
        return -1;

      json->num_state = NS_FRAC;
      return 1;

    case NS_FRAC:
      if(digit)
        return 1;

      if(c == 'e' || c == 'E') {
        json->num_state = NS_EXP;
        return 1;
      }

      return 0;

    case NS_EXP:
      if(c == '+' || c == '-') {
        json->num_state = NS_EXPSIGN;
        return 1;
      }
      /* fall through */
    case NS_EXPSIGN:
      if(!digit)
        return -1;

      json->num_state = NS_EXPDIG;
      return 1;

    case NS_EXPDIG: return digit ? 1 : 0;

    case NS_HEX0:
      if(!is_xdigit_char(c))
        return -1;

      json->num_state = NS_HEX;
      return 1;

    case NS_HEX: return is_xdigit_char(c) ? 1 : 0;
  }

  return -1;
}

static int
json_scan_number(JsonParser* json) {
  int c;

  if(!(json->flags & JR_LEGACY)) {
    for(;;) {
      int r;

      if((c = json_getc(json)) < 0)
        return c;

      if(json->num_state == NS_KW) {
        if((uint8_t)json->literal_text[json->literal_pos] != c) {
          json->error = "invalid literal";
          json_error_skip_enter(json, c, "invalid-literal");
          return JSON_ERROR;
        }

        if(!json->literal_text[++json->literal_pos]) {
          json->tok_kind = JSON_TOK_NONE;
          return JSON_EVENT_NUMBER;
        }

        continue;
      }

      if((r = json_num_step(json, c)) > 0)
        continue;

      if(r == 0 && json_num_complete(json)) {
        json_ungetc(json, c);
        json->tok_kind = JSON_TOK_NONE;
        return JSON_EVENT_NUMBER;
      }

      /* the byte stays for the resync to look at */
      json_ungetc(json, c);
      json->error = "invalid number";
      json_error_skip_enter(json, 0, "unexpected-token");
      return JSON_ERROR;
    }
  }

  for(;;) {
    if((c = json_getc(json)) < 0)
      return c;

    if(!is_number_char(c)) {
      json_ungetc(json, c);
      json->tok_kind = JSON_TOK_NONE;
      return JSON_EVENT_NUMBER;
    }
  }
}

/* an unquoted key: [A-Za-z0-9$_] and bytes >= 0x80 up to the first other byte */
static int
json_scan_ident(JsonParser* json) {
  int c;

  for(;;) {
    if((c = json_getc(json)) < 0)
      return c;

    if(!(((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) || c == '$' || c == '_' || c >= 0x80)) {
      json_ungetc(json, c);
      json->tok_kind = JSON_TOK_NONE;
      return JSON_EVENT_KEY;
    }
  }
}

static int
json_scan_literal(JsonParser* json) {
  int c;
  size_t len = strlen(json->literal_text);

  while(json->literal_pos < (int)len) {
    if((c = json_getc(json)) < 0)
      return c;

    if((uint8_t)json->literal_text[json->literal_pos] != c) {
      json->error = "invalid literal";
      json_error_skip_enter(json, c, "invalid-literal");
      return JSON_ERROR;
    }

    json->literal_pos++;
  }

  json->tok_kind = JSON_TOK_NONE;

  switch(json->literal_text[0]) {
    case 't': return JSON_EVENT_TRUE;
    case 'f': return JSON_EVENT_FALSE;
    default: return JSON_EVENT_NULL;
  }
}

/* Resumes/continues discarding bytes after a reported error, looking for a resync point:
 * a comma, or a closing bracket/brace while some container is actually open (state's
 * PARSING_OBJECT/PARSING_ARRAY bits, mirroring the exact same check json_parse()'s own
 * '}'/']' case relies on). Consuming through a comma just clears the expectation bits and
 * lets json_parse()'s main loop continue into the next value, in the same container. A
 * matched closer is handled exactly like json_parse()'s own '}'/']' case (json_finish()
 * below still runs on it), since it really does close that container. Only the *first*
 * bad byte (in json_parse()'s switch/colon-check) is reported as JSON_ERROR - every byte
 * skipped here is silent, so an arbitrarily long bad span costs one thrown exception, not
 * one per byte. */
static int
json_scan_error_skip(JsonParser* json) {
  int c;

  for(;;) {
    if((c = json_getc(json)) < 0)
      return c;

    json->token.size = 0; /* discarded, not a token - keep memory use bounded regardless of span length */

    /* A '{'/'[' hit while skipping opens a nesting level that was never pushed onto the
     * real `stack` (it's garbage, not legitimately parsed) - its matching '}'/']' later in
     * otherwise-valid input must close *that* first, or it gets mistaken for this scan's
     * own resync boundary, and the *real* enclosing container's later close then pops an
     * empty `stack`. */
    if(c == '{' || c == '[') {
      json->skip_depth++;
      continue;
    }

    if(json->skip_depth > 0) {
      if(c == '}' || c == ']')
        json->skip_depth--;

      continue;
    }

    if(c == ',') {
      /* An abandoned "key: <bad value>" member never reached json_finish(), so the
       * object-value-phase bit it set is still up - flip it back to key-phase (the same
       * toggle a completed value's json_finish() would have done) or the next member's
       * key gets misread as a value, and the real value after it as fresh garbage. */
      if((json->state & PARSING_OBJECT) == PARSING_OBJECT_VALUE)
        json->state ^= PARSING_OBJECT;

      json->tok_kind = JSON_TOK_NONE;
      return JSON_RESYNC;
    }

    /* json->stack.len (actual nesting depth), not the state bits: closing a depth-0
     * container leaves a leftover PARSING_ARRAY bit set (see json_parse()'s own '}'/']'
     * case - it always "restores" *something*, even when there's truly nothing enclosing),
     * which would otherwise make a stray '}'/']' right after that look like a legitimate
     * close and pop json->stack while it's already empty. */
    if((c == '}' || c == ']') && json->stack.len > 0) {
      int ret = c == '}' ? JSON_EVENT_OBJECT_END : JSON_EVENT_ARRAY_END;

      json->state &= ~(PARSING_OBJECT | PARSING_ARRAY);
      json->state |= bitset_pop(&json->stack, 1) ? PARSING_OBJECT_KEY : PARSING_ARRAY;
      json->tok_kind = JSON_TOK_NONE;

      return ret;
    }

    /* ordinary garbage byte: discard and keep scanning */
  }
}

static int
json_finish(JsonParser* json, int ret) {
  if(ret != JSON_EVENT_OBJECT_START && ret != JSON_EVENT_ARRAY_START && ret != JSON_EVENT_OBJECT_END && ret != JSON_EVENT_ARRAY_END)
    if(json->state & PARSING_OBJECT)
      json->state ^= PARSING_OBJECT;

  json->state &= ~(EXPECTING_COLON | EXPECTING_COMMA_OR_END);
  json->after_comma = 0;

  if(ret != JSON_EVENT_KEY && json->stack.len == 0)
    json->roots++;

  if(ret == JSON_EVENT_KEY)
    json->state |= EXPECTING_COLON;
  else if(ret != JSON_EVENT_OBJECT_START && ret != JSON_EVENT_ARRAY_START)
    /* Right after *opening* a container, a value/key or an immediate close is expected,
     * not "comma or end" - json_parse()'s switch(c) already handles both of those cases
     * directly with no flag needed. Setting this bit here was harmless under the old
     * fully-permissive EXPECTING_COMMA_OR_END check (a non-',' byte just fell through
     * regardless), but became a live bug once that check started actually enforcing commas:
     * it made every array/object's very first element look like a missing-comma error. */
    json->state |= EXPECTING_COMMA_OR_END;

  return ret;
}

/* json_scan_*() return either a completed JsonEventType (>= 0), JSON_ERROR (set directly,
 * e.g. for a malformed escape), or a raw reader signal (STREAM_EOF/STREAM_ERROR) when input
 * ran out mid-scan. Only the latter needs translating via json_need_or_error() - checking
 * merely "< 0" would also catch an already-JSON_ERROR result and misreport it as NEED_DATA. */
static int
json_finish_scan(JsonParser* json, int r) {
  if(r == JSON_ERROR)
    return JSON_ERROR;

  if(r < 0)
    return json_need_or_error(r);

  return json_finish(json, r);
}

int
json_parse(JsonParser* json) {
  int c;

  json->error = NULL;

  if(json->has_pending) {
    json->has_pending = 0;
    return json->pending;
  }

  if(json->trailing) {
    /* data after the document: discarded */
    for(;;) {
      int c = json_getc(json);

      if(c < 0)
        return json_need_or_error(c);

      json->token.size = 0;
    }
  }

  for(;;) {
    if(json->tok_kind == JSON_TOK_IDENT)
      return json_finish_scan(json, json_scan_ident(json));

    if(json->tok_kind == JSON_TOK_STRING)
      return json_finish_scan(json, json_scan_string(json));

    if(json->tok_kind == JSON_TOK_NUMBER)
      return json_finish_scan(json, json_scan_number(json));

    if(json->tok_kind == JSON_TOK_LITERAL)
      return json_finish_scan(json, json_scan_literal(json));

    if(json->tok_kind == JSON_TOK_ERROR_SKIP) {
      int r = json_scan_error_skip(json);

      if(r == JSON_RESYNC)
        continue;

      return json_finish_scan(json, r);
    }

    if(!json->cmt_state)
      dbuf_zero(&json->token);

    c = json_getc_skipws(json);

    if(c == C_SIG)
      return JSON_EVENT_COMMENT;

    if(c < 0)
      return json_need_or_error(c);

    json->tok_start = json->last_start;

    if(json->state & EXPECTING_COMMA_OR_END) {
      json->state &= ~EXPECTING_COMMA_OR_END;

      if(c == ',' && ((json->flags & JR_LEGACY) || json->stack.len > 0)) {
        json->after_comma = 1;
        continue;
      }

      /* A container close is always legal here regardless (that's the "OR_END" part) -
       * anything else needs an explicit comma, except at the very top level (depth 0,
       * nothing open), where a fresh value starting right after the previous one closed
       * is the supported NDJSON/JSON-Lines streaming pattern (see doc/native/json.md).
       * json->stack.len (actual depth), not the state bits - closing a depth-0 container
       * leaves a leftover PARSING_ARRAY bit set (see json_parse()'s '}'/']' case), which
       * would otherwise make this misfire right after the very first NDJSON line. */
      if(c != '}' && c != ']' && json->stack.len > 0) {
        json->error = "expected ',' or closing bracket";
        json_error_skip_enter(json, c, "unexpected-token");
        return JSON_ERROR;
      }
    }

    if(json->state & EXPECTING_COLON) {
      json->state &= ~EXPECTING_COLON;

      if(c != ':') {
        json->error = "expected ':'";
        json_error_skip_enter(json, c, "unexpected-token");
        return JSON_ERROR;
      }

      continue;
    }

    if(!(json->flags & (JR_LEGACY | JR_MULTI)) && json->stack.len == 0 && json->roots > 0) {
      json->error = "unexpected data after the document";
      json->err_code = "trailing-data";
      json->err_at = json->tok_start;
      json->err_len = 1;
      json->trailing = 1;
      return JSON_ERROR;
    }

    if((json->flags & JR_UNQUOTED_KEYS) && (json->state & PARSING_OBJECT_KEY) &&
       ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '$' || c == '_' || c >= 0xc0)) {
      json->tok_kind = JSON_TOK_IDENT;
      continue;
    }

    /* the lax reader skips a comma with no value before it: [1,,2] is [1,2] */
    if(c == ',' && (json->flags & JR_LEGACY) && json->stack.len > 0)
      continue;

    switch(c) {
      /* The bit pushed on '{'/'[' is the *enclosing* context to restore once this
         container closes (object-key vs. array), not this container's own type -
         that's already implied by which case branch is handling '}' vs ']'. */
      case '{':
        bitset_push(&json->stack, (json->state & PARSING_OBJECT) != 0, 1);
        json->state = PARSING_OBJECT_KEY;
        return json_finish(json, JSON_EVENT_OBJECT_START);

      case '[':
        bitset_push(&json->stack, (json->state & PARSING_OBJECT) != 0, 1);
        json->state = PARSING_ARRAY;
        return json_finish(json, JSON_EVENT_ARRAY_START);

      case '}':
      case ']': {
        int ret = c == '}' ? JSON_EVENT_OBJECT_END : JSON_EVENT_ARRAY_END;
        const char* bad = NULL;

        if(!(json->flags & JR_LEGACY)) {
          int in_obj = (json->state & PARSING_OBJECT) != 0;

          if(json->stack.len == 0) {
            json->error = "unexpected closing bracket";
            json_error_skip_enter(json, c, "unexpected-token");
            return JSON_ERROR;
          }

          if(in_obj != (c == '}'))
            bad = "mismatched closing bracket";
          else if(in_obj && (json->state & PARSING_OBJECT) == PARSING_OBJECT_VALUE)
            bad = "expected a value";
          else if(json->after_comma && !(json->flags & JR_TRAILING_COMMAS))
            bad = "trailing comma";
        }

        json->state &= ~(PARSING_OBJECT | PARSING_ARRAY);
        json->state |= bitset_pop(&json->stack, 1) ? PARSING_OBJECT_KEY : PARSING_ARRAY;
        ret = json_finish(json, ret);

        if(bad) {
          /* the error comes first, the closing event right after */
          json->error = bad;
          json->err_code = "unexpected-token";
          json->err_at = json->tok_start;
          json->err_len = 1;
          json->pending = ret;
          json->has_pending = 1;
          return JSON_ERROR;
        }

        return ret;
      }

      case '\'':
        if(!(json->flags & JR_SINGLE_QUOTES)) {
          json->error = "expected a value";
          json_error_skip_enter(json, c, "unexpected-token");
          return JSON_ERROR;
        }

        dbuf_zero(&json->token);
        json->quote = '\'';
        json->tok_kind = JSON_TOK_STRING;
        json->str_state = JSON_STR_NORMAL;
        json->is_key = (json->state & PARSING_OBJECT_KEY) != 0;
        continue;

      case 'n':
        json->tok_kind = JSON_TOK_LITERAL;
        json->literal_text = "null";
        json->literal_pos = 1;
        continue;

      case 't':
        json->tok_kind = JSON_TOK_LITERAL;
        json->literal_text = "true";
        json->literal_pos = 1;
        continue;

      case 'f':
        json->tok_kind = JSON_TOK_LITERAL;
        json->literal_text = "false";
        json->literal_pos = 1;
        continue;

      case '"':
        dbuf_zero(&json->token);
        json->quote = '"';
        json->tok_kind = JSON_TOK_STRING;
        json->str_state = JSON_STR_NORMAL;
        json->is_key = (json->state & PARSING_OBJECT_KEY) != 0;
        continue;

      default:
        if(!(json->flags & JR_LEGACY)) {
          /* the first byte is scanned again by the number state machine */
          json->num_state = NS_FIRST;
          json_ungetc(json, c);
          json->tok_kind = JSON_TOK_NUMBER;
          continue;
        }

        if(!is_number_char(c)) {
          json->error = "expected a value";
          json_error_skip_enter(json, c, "unexpected-token");
          return JSON_ERROR;
        }

        json->tok_kind = JSON_TOK_NUMBER;
        continue;
    }
  }
}

/* the reader is exhausted: finishes a number that ends with the input and reports an
 * unfinished document. Returns JSON_EVENT_NUMBER, JSON_ERROR (json->error set) or
 * JSON_NONE; call again after an error or a number for the end. */
int
json_end(JsonParser* json) {
  json->error = NULL;

  if(json->tok_kind == JSON_TOK_NUMBER && (json->flags & JR_LEGACY || (json->num_state != NS_KW && json_num_complete(json)))) {
    json->tok_kind = JSON_TOK_NONE;
    return json_finish(json, JSON_EVENT_NUMBER);
  }

  if(json->has_pending) {
    json->has_pending = 0;
    return json->pending;
  }

  if(json->trailing || json->tok_kind == JSON_TOK_ERROR_SKIP) {
    json->tok_kind = JSON_TOK_NONE;
    return JSON_NONE;
  }

  if(!(json->flags & JR_LEGACY) && (json->tok_kind != JSON_TOK_NONE || json->cmt_state == 1 || json->cmt_state == 3 || json->cmt_state == 4 ||
                                    json->stack.len > 0 || (json->roots == 0 && !(json->flags & JR_MULTI)))) {
    json->tok_kind = JSON_TOK_NONE;
    json->cmt_state = 0;
    json->stack.len = 0;
    json->roots = 1;
    json->error = "unexpected end of input";
    json->err_code = "unexpected-end";
    json->err_at = (JsonPos){json->loc->line, json->loc->column, json->loc->char_offset, json->loc->byte_offset};
    json->err_len = 0;
    return JSON_ERROR;
  }

  return JSON_NONE;
}

/**
 * @}
 */
