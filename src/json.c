#include <assert.h>
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
json_error(JsonParser* json) {
  return -1;
}

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
json_init(JsonParser* json, JSValueConst input, JSContext* ctx) {
  json->ref_count = 1;
  json->reader = reader_from_js(input, ctx);

  json->callback = NULL;
  json->opaque = NULL;
  json->pos = 0;
  json->pushback = -1;

  json->state = PARSING;

  json->stack = BITSET_INIT();

  // js_dbuf_init(ctx, &json->token);
  dbuf_init2(&json->token, 0, 0);

  if(!json->reader.read)
    return FALSE;

  return TRUE;
}

JsonParser*
json_new(JSValueConst input, JSContext* ctx) {
  JsonParser* parser;

  if(!(parser = js_malloc(ctx, sizeof(JsonParser))))
    return 0;

  if(!json_init(parser, input, ctx)) {
    js_free(ctx, parser);
    return 0;
  }

  return parser;
}

void
json_free(JsonParser* json, JSRuntime* rt) {
  if(--json->ref_count == 0) {
    json_clear(json, rt);
    js_free_rt(rt, json);
  }
}

void
json_clear(JsonParser* json, JSRuntime* rt) {
  reader_free(&json->reader);
}

JsonParser*
json_dup(JsonParser* json) {
  ++json->ref_count;
  return json;
}

int
json_getc(JsonParser* json) {
  int c;

  if(json->pushback > -1) {
    c = json->pushback;
    json->pushback = -1;

  } else {
    c = reader_getc(&json->reader);
  }

  ++json->pos;

  dbuf_putc(&json->token, c);
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

int
json_parse(JsonParser* json, JSContext* ctx) {
  int c;
  JsonValueType ret = JSON_TYPE_NONE;

  dbuf_zero(&json->token);

  for(;;) {
    if((c = json_getc_skipws(json)) < 0)
      goto end;

    if(json->state & EXPECTING_COMMA_OR_END) {
      json->state &= ~EXPECTING_COMMA_OR_END;
      if(c == ',') {
        dbuf_zero(&json->token);
        continue;
      }
    }

    if(json->state & EXPECTING_COLON) {
      json->state &= ~EXPECTING_COLON;
      if(c != ':') {
        JS_ThrowInternalError(ctx, "JSON parser expects a colon, token = '%c'", (char)c);
        goto fail;
      }
      dbuf_zero(&json->token);
      continue;
    }

    switch(c) {
      case '{': {
        json->state = PARSING_OBJECT_KEY;
        bitset_push(&json->stack, 1, 1);
        ret = JSON_TYPE_OBJECT;
        goto novalue;
      }

      case '[': {
        json->state = PARSING_ARRAY;
        bitset_push(&json->stack, 0, 1);
        ret = JSON_TYPE_ARRAY;
        goto novalue;
      }

      case '}':
      case ']': {
        ret = c == '}' ? JSON_TYPE_OBJECT_END : JSON_TYPE_ARRAY_END;

        json->state &= ~(PARSING_OBJECT | PARSING_ARRAY);

        json->state |= bitset_pop(&json->stack, 1) ? PARSING_OBJECT_KEY : PARSING_ARRAY;

        goto novalue;
      }

      case 'n': {
        if(json_getc(json) != 'u')
          goto end;
        if(json_getc(json) != 'l')
          goto end;
        if(json_getc(json) != 'l')
          goto end;

        ret = JSON_TYPE_NULL;
        goto end;
      }

      case 't': {
        if(json_getc(json) != 'r')
          goto end;
        if(json_getc(json) != 'u')
          goto end;
        if(json_getc(json) != 'e')
          goto end;

        ret = JSON_TYPE_TRUE;
        goto end;
      }
      case 'f': {
        if(json_getc(json) != 'a')
          goto end;
        if(json_getc(json) != 'l')
          goto end;
        if(json_getc(json) != 's')
          goto end;
        if(json_getc(json) != 'e')
          goto end;

        ret = JSON_TYPE_FALSE;
        goto end;
      }
    }

    switch(c) {
      case '"': {
        // dbuf_zero(&json->token);

        while((c = json_getc(json)) >= 0) {
          if(c == '\\') {

            if((c = json_getc(json)) < 0) {
              ret = c;
              goto end;
            }

          } else if(c == '"') {
            // json->token.size -= 1;

            ret = (json->state & PARSING_OBJECT_KEY) ? JSON_TYPE_KEY : JSON_TYPE_STRING;

            goto end;
          }
        }

        break;
      }

      /* Number */
      default: {
        if(!is_number_char(c)) {
          JS_ThrowInternalError(ctx, "JSON parser expects a number, token = '%c'", (char)c);
          goto fail;
        }

        while((c = json_getc(json)) >= 0) {
          if(!is_number_char(c)) {
            json_ungetc(json, c);

            ret = JSON_TYPE_NUMBER;
            goto end;
          }
        }

        break;
      }
    }

    break;
  }

end:
  if(json->state & PARSING_OBJECT)
    json->state ^= PARSING_OBJECT;

novalue:
  json->state &= ~(EXPECTING_COLON | EXPECTING_COMMA_OR_END);
  json->state |= ret == JSON_TYPE_KEY ? EXPECTING_COLON : EXPECTING_COMMA_OR_END;

  /*if(ret == JSON_TYPE_NONE)
    if(c < 0)
      return c;*/

  return ret;

fail:
  return -1;
}
