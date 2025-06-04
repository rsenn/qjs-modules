#include <assert.h>
#include "json.h"
#include "char-utils.h"

static int
json_error(JsonParser* json) {
  return -1;
}

static int
json_getc_skipws(JsonParser* json) {
  int c;

  for(;;) {
    if((c = json_getc(json)) < 0)
      return c;

    if(!is_whitespace_char(c))
      break;
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

  json->state = JSON_STATE_PARSING;

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

  return c;
}

int
json_ungetc(JsonParser* json, char c) {
  if(json->pushback != -1)
    return -1;

  json->pushback = (unsigned int)(unsigned char)c;

  --json->pos;
  return 0;
}

int
json_parse(JsonParser* json, JSContext* ctx) {
  int c;

  dbuf_zero(&json->token);

  for(;;) {
    if((c = json_getc_skipws(json)) < 0)
      goto end;

    if(json->state & JSON_STATE_EXPECTING_COLON) {
      if(c != ':') {
        JS_ThrowInternalError(ctx, "JSON parser expects a colon, token = '%c'", (char)c);
        goto fail;
      }
      json->state &= ~JSON_STATE_EXPECTING_COLON;
      continue;
    }

    if(json->state & JSON_STATE_EXPECTING_COLON) {
      if(c != ':') {
        JS_ThrowInternalError(ctx, "JSON parser expects a colon, token = '%c'", (char)c);
        goto fail;
      }
      json->state &= ~JSON_STATE_EXPECTING_COLON;
      continue;
    }

    if(json->state & JSON_STATE_EXPECTING_COMMA_OR_END) {
      if(c == ',') {
        json->state &= ~JSON_STATE_EXPECTING_COMMA_OR_END;
        continue;
      }
    }

    json->state |= JSON_STATE_EXPECTING_COMMA_OR_END;

    if((json->state & JSON_STATE_PARSING_OBJECT) != JSON_STATE_PARSING_OBJECT_KEY)
      switch(c) {
        case '{': {
          json->state = JSON_STATE_PARSING_OBJECT_KEY;
          return JSON_TYPE_OBJECT;
        }

        case '[': {
          json->state = JSON_STATE_PARSING_ARRAY;
          return JSON_TYPE_ARRAY;
        }

        case '}': {
          return JSON_TYPE_OBJECT_END;
        }

        case ']': {
          return JSON_TYPE_ARRAY_END;
        }
      }

    switch(c) {
      case 'n': {
        if(json_getc(json) != 'u')
          goto end;
        if(json_getc(json) != 'l')
          goto end;
        if(json_getc(json) != 'l')
          goto end;

        return JSON_TYPE_NULL;
      }

      case 't': {
        if(json_getc(json) != 'r')
          goto end;
        if(json_getc(json) != 'u')
          goto end;
        if(json_getc(json) != 'e')
          goto end;

        return JSON_TYPE_TRUE;
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

        return JSON_TYPE_FALSE;
      }

      case '"': {
        while((c = json_getc(json)) >= 0) {
          if(c == '\\') {
            dbuf_putc(&json->token, c);

            if((c = json_getc(json)) < 0)
              return c;

          } else if(c == '"') {

            if((json->state & JSON_STATE_PARSING_OBJECT) == JSON_STATE_PARSING_OBJECT_KEY) {
              json->state &= ~JSON_STATE_PARSING_OBJECT;
              json->state |= JSON_STATE_PARSING_OBJECT_VALUE | JSON_STATE_EXPECTING_COLON;

              return JSON_TYPE_KEY;
            }

            if(json->state & JSON_STATE_PARSING_OBJECT) {
              json->state &= ~JSON_STATE_PARSING_OBJECT;
              json->state |= JSON_STATE_PARSING_OBJECT_KEY | JSON_STATE_EXPECTING_COMMA_OR_END;
            }

            return JSON_TYPE_STRING;
          }

          dbuf_putc(&json->token, c);
        }

        break;
      }

        /* Number */
      default: {
        if(!is_number_char(c)) {
          JS_ThrowInternalError(ctx, "JSON parser expects a number, token = '%c'", (char)c);
          goto fail;
        }

        dbuf_putc(&json->token, c);

        while((c = json_getc(json)) >= 0) {
          if(!is_number_char(c)) {
            json_ungetc(json, c);
            return JSON_TYPE_NUMBER;
          }

          dbuf_putc(&json->token, c);
        }

        break;
      }
    }

    break;
  }

  if(c < 0)
    return c;

end:
  return 0;

fail:
  return -1;
}
