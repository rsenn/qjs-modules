#include <assert.h>
#include "json.h"
#include "char-utils.h"

static int
json_error(JsonParser* json) {
  //json->stack->state = JSON_STATE_ERROR;
  return -1;
}

static ReaderStatus
json_skip_ws(JsonParser* json, JSContext* ctx) {
  int c;

  for(;;) {
    if((c = json_getc(json)) < 0)
      return c;

    if(is_whitespace_char(c))
      continue;

    json_ungetc(json, c, ctx);
    break;
  }

  return 0;
}

/**
 * \addtogroup json json: JSON parser
 * @{
 */

BOOL
json_init(JsonParser* json, JSValueConst input, JSContext* ctx) {
  json->ref_count = 1;
  /*  json->state = 0;
    json->type = -1;*/
  json->reader = reader_from_js(input, ctx);

  block_init(&json->buf);
  json->unget = 0;

  json->callback = NULL;
  json->opaque = NULL;
  json->pos = 0;
  json->stack = 0;

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
  block_free(&json->buf, rt);
}

JsonParser*
json_dup(JsonParser* json) {
  ++json->ref_count;
  return json;
}

int
json_getc(JsonParser* json) {
  int c;

  if(json->unget > 0) {
    c = ((uint8_t*)block_data(&json->buf))[--json->unget];
  } else {
    c = reader_getc(&json->reader);

    //if(c == READER_EOF)
      //json->stack->state = JSON_STATE_EOF;

    if(c == READER_ERROR)
      return json_error(json);
  }

  ++json->pos;

  return c;
}

uint8_t*
json_peek(JsonParser* json, size_t n, JSContext* ctx) {
  size_t new_size = n + json->unget;

  if(new_size > block_length(&json->buf)) {
    if(block_realloc(&json->buf, new_size, ctx))
      return 0;
  }

  uint8_t* ptr = &((uint8_t*)block_data(&json->buf))[json->unget];

  for(size_t i = 0; i < n; ++i)
    if(reader_read(&json->reader, &ptr[i], 1) <= 0)
      return 0;

  json->unget += n;

  return ptr;
}

BOOL
json_ungetc(JsonParser* json, int c, JSContext* ctx) {
  uint8_t ch = c;

  if(json->unget + 1 > block_length(&json->buf))
    if(block_realloc(&json->buf, json->unget + 1, ctx))
      return FALSE;

  ((uint8_t*)block_data(&json->buf))[json->unget] = ch;

  ++json->unget;
  --json->pos;

  return TRUE;
}

int
json_parse(JsonParser* json, JSContext* ctx) {
  int c;
  JsonParseState prev_state = /*json->stack ? json->stack->state : */JSON_STATE_WAITINGFIRSTCHAR;
  JsonValueType type = JSON_TYPE_NONE;
  uint8_t* ptr;

  if((c = json_skip_ws(json, ctx)) < 0)
    return c;

  DynBuf db = DBUF_INIT_CTX(ctx);

again:

      if((c = json_getc(json)) < 0)
        goto end;

      switch(c) {
        case '{':
        case '[': {
          json_push(json,
                    c == '{' ? JSON_STATE_PARSING_OBJECT : JSON_STATE_PARSING_ARRAY,
                    c == '{' ? JSON_TYPE_OBJECT : JSON_TYPE_ARRAY);
          goto end;
          break;
        }
        case '"': {
          json_push(json, JSON_STATE_PARSING_STRING, JSON_TYPE_STRING);
          goto again;
          break;
        }

        case 't':
        case 'f': {
          size_t len = c == 't' ? 3 : 4;

          if(!(ptr = json_peek(json, len + 1, ctx)))
            goto end;

          if(byte_diff((char*)ptr, c == 't' ? 3 : 4, c == 't' ? "rue" : "alse") || is_alphanumeric_char(ptr[len])) {
            goto end;
          }

          json_skip(json, len);

          // type = c == 't' ? JSON_TYPE_TRUE : JSON_TYPE_FALSE;
          json_push(json, JSON_STATE_EXPECTING_COMMA_OR_END, c == 't' ? JSON_TYPE_TRUE : JSON_TYPE_FALSE);
          break;
        }

        case 'n': {
          if(!(ptr = json_peek(json, 4, ctx)))
            goto end;

          if(byte_diff((char*)ptr, 3, "ull") || is_alphanumeric_char(ptr[3]))
            goto end;

          json_skip(json, 3);

          /*type = JSON_TYPE_NULL;
          json->state = JSON_STATE_EXPECTING_COMMA_OR_END;*/
          json_push(json, JSON_STATE_EXPECTING_COMMA_OR_END, JSON_TYPE_NULL);
          break;
        }

        default: {
          break;
        }
      }

   /*   break;
    }
    case JSON_STATE_PARSING_OBJECT: {

      if((c = json_getc(json)) < 0)
        goto end;

      if(c == '"') {
        json_push(json, JSON_STATE_PARSING_STRING, JSON_TYPE_KEY);
        goto again;
      }

      break;
    }
    case JSON_STATE_PARSING_ARRAY: {
      break;
    }
    case JSON_STATE_PARSING: {
      //switch(json->stack->type) {
        case JSON_TYPE_OBJECT: {
          break;
        }

        case JSON_TYPE_ARRAY: {
          break;
        }

        case JSON_TYPE_NUMBER: {
          break;
        }

        case JSON_TYPE_KEY:
        default: {
          break;
        }
      }

      break;
    }
    case JSON_STATE_PARSING_STRING: {
      int prev = -1;

      for(;;) {
        if((c = json_getc(json)) < 0)
          break;

        if(c == '"' && prev != '\\') {
          //type = json->stack->type;

          if(type == JSON_TYPE_KEY)
            //json->stack->state = JSON_STATE_EXPECTING_COLON;

          //// json_push(json, json->stack->type == JSON_TYPE_KEY ? JSON_STATE_EXPECTING_COLON :
          // JSON_STATE_EXPECTING_COMMA_OR_END, JSON_TYPE_STRING);
          break;
        }

        dbuf_putc(&db, c);
        prev = c;
      }

      break;
    }

    case JSON_STATE_PARSING_PRIMITIVE: {
      break;
    }

    case JSON_STATE_EXPECTING_COMMA_OR_END: {
      c = json_getc(json);

      if(c == READER_EOF) {
        json_pop(json);
      } else if(c == ',') {
        //json_push(json, JSON_STATE_PARSING, json->stack->type);
      } else {
        json_error(json);
      }
      break;
    }
    case JSON_STATE_EXPECTING_COLON: {
      if((c = json_getc(json)) >= 0 && c == ':') {

        //json->stack->state = JSON_STATE_PARSING_OBJECT;
        goto again;
      } else {
        json_error(json);
      }

      break;
    }
  }*/

end:
  // if(json->state != JSON_STATE_ERROR)
  // if(prev_state != json->state)
  // if(prev_state != JSON_STATE_WAITINGFIRSTCHAR)

  if(type == JSON_TYPE_KEY)
    //json->stack->state = JSON_STATE_EXPECTING_COLON;

  if(json->callback) {
    if(db.size)
      dbuf_0(&db);

    json->callback(json, type, db.size ? db.buf : 0);
  }

  dbuf_free(&db);

  //return json->stack->state;
}

/**
 * @}
 */
