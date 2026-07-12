#include "json.h"
#include "char-utils.h"

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

static int
json_getc_skipws(JsonParser* json) {
  int c;
  size_t pos = json->token.size;

  for(;;) {
    if((c = json_getc(json)) < 0)
      return c;

    if(!is_whitespace_char(c))
      break;

    json->token.size = pos;
  }

  return c;
}

BOOL
json_init(JsonParser* json, Reader reader, const char* filename, JSContext* ctx) {
  json->reader = reader;
  json->callback = NULL;
  json->opaque = NULL;
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

  dbuf_init2(&json->token, 0, 0);

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
    c = reader_getc(&json->reader);
  }

  if(c >= 0) {
    ++json->pos;
    dbuf_putc(&json->token, c);

    /* a replayed pushback char was already counted once, when it was first read */
    if(from_reader)
      location_nextchar(json->loc, c);
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

/* Resumes/continues scanning a string value's content. Returns a JsonValueType (STRING or
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

      {
        int uc = is_quotable_char(c);

        if(!uc) {
          json->error = "invalid escape sequence in string";
          return JSON_ERROR;
        }

        dbuf_putc(&json->token, uc);
      }

      continue;
    }

    if(json->str_state == JSON_STR_UNICODE) {
      int v;

      if((c = json_getc(json)) < 0)
        return c;

      json->token.size -= 1; /* hex digits aren't part of the decoded text */

      if(!is_xdigit_char(c)) {
        json->error = "invalid unicode escape in string";
        return JSON_ERROR;
      }

      v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10;
      json->str_unicode_val = (json->str_unicode_val << 4) | (uint32_t)v;

      if(++json->str_unicode_count < 4)
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

    if(c == '"') {
      json->token.size -= 1; /* drop the closing quote */
      json->tok_kind = JSON_TOK_NONE;
      return json->is_key ? JSON_TYPE_KEY : JSON_TYPE_STRING;
    }

    /* ordinary byte: already appended by json_getc() as-is */
  }
}

static int
json_scan_number(JsonParser* json) {
  int c;

  for(;;) {
    if((c = json_getc(json)) < 0)
      return c;

    if(!is_number_char(c)) {
      json_ungetc(json, c);
      json->tok_kind = JSON_TOK_NONE;
      return JSON_TYPE_NUMBER;
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
      return JSON_ERROR;
    }

    json->literal_pos++;
  }

  json->tok_kind = JSON_TOK_NONE;

  switch(json->literal_text[0]) {
    case 't': return JSON_TYPE_TRUE;
    case 'f': return JSON_TYPE_FALSE;
    default: return JSON_TYPE_NULL;
  }
}

static int
json_finish(JsonParser* json, int ret) {
  if(ret != JSON_TYPE_OBJECT && ret != JSON_TYPE_ARRAY && ret != JSON_TYPE_OBJECT_END && ret != JSON_TYPE_ARRAY_END)
    if(json->state & PARSING_OBJECT)
      json->state ^= PARSING_OBJECT;

  json->state &= ~(EXPECTING_COLON | EXPECTING_COMMA_OR_END);
  json->state |= ret == JSON_TYPE_KEY ? EXPECTING_COLON : EXPECTING_COMMA_OR_END;

  return ret;
}

/* json_scan_*() return either a completed JsonValueType (>= 0), JSON_ERROR (set directly,
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

  for(;;) {
    if(json->tok_kind == JSON_TOK_STRING)
      return json_finish_scan(json, json_scan_string(json));

    if(json->tok_kind == JSON_TOK_NUMBER)
      return json_finish_scan(json, json_scan_number(json));

    if(json->tok_kind == JSON_TOK_LITERAL)
      return json_finish_scan(json, json_scan_literal(json));

    dbuf_zero(&json->token);

    if((c = json_getc_skipws(json)) < 0)
      return json_need_or_error(c);

    if(json->state & EXPECTING_COMMA_OR_END) {
      json->state &= ~EXPECTING_COMMA_OR_END;

      if(c == ',')
        continue;
    }

    if(json->state & EXPECTING_COLON) {
      json->state &= ~EXPECTING_COLON;

      if(c != ':') {
        json->error = "expected ':'";
        return JSON_ERROR;
      }

      continue;
    }

    switch(c) {
      /* The bit pushed on '{'/'[' is the *enclosing* context to restore once this
         container closes (object-key vs. array), not this container's own type -
         that's already implied by which case branch is handling '}' vs ']'. */
      case '{':
        bitset_push(&json->stack, (json->state & PARSING_OBJECT) != 0, 1);
        json->state = PARSING_OBJECT_KEY;
        return json_finish(json, JSON_TYPE_OBJECT);

      case '[':
        bitset_push(&json->stack, (json->state & PARSING_OBJECT) != 0, 1);
        json->state = PARSING_ARRAY;
        return json_finish(json, JSON_TYPE_ARRAY);

      case '}':
      case ']': {
        int ret = c == '}' ? JSON_TYPE_OBJECT_END : JSON_TYPE_ARRAY_END;

        json->state &= ~(PARSING_OBJECT | PARSING_ARRAY);
        json->state |= bitset_pop(&json->stack, 1) ? PARSING_OBJECT_KEY : PARSING_ARRAY;

        return json_finish(json, ret);
      }

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
        json->tok_kind = JSON_TOK_STRING;
        json->str_state = JSON_STR_NORMAL;
        json->is_key = (json->state & PARSING_OBJECT_KEY) != 0;
        continue;

      default:
        if(!is_number_char(c)) {
          json->error = "expected a value";
          return JSON_ERROR;
        }

        json->tok_kind = JSON_TOK_NUMBER;
        continue;
    }
  }
}
