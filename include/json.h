#ifndef JSON_H
#define JSON_H

#include "stream-utils.h"
#include "buffer-utils.h"
#include "bitset.h"

/**
 * \defgroup json json: JSON parser
 * @{
 */

typedef enum {
  JSON_TYPE_NONE = -1,
  JSON_TYPE_OBJECT,
  JSON_TYPE_OBJECT_END,
  JSON_TYPE_ARRAY,
  JSON_TYPE_ARRAY_END,
  JSON_TYPE_KEY,
  JSON_TYPE_STRING,
  JSON_TYPE_TRUE,
  JSON_TYPE_FALSE,
  JSON_TYPE_NULL,
  JSON_TYPE_NUMBER,
} JsonValueType;

typedef struct JsonParser JsonParser;

typedef void JsonCallback(JsonParser*, JsonValueType value, void*);

struct JsonParser {
  /*int ref_count;*/
  int state;
  Reader reader;
  DynBuf token;
  JsonCallback* callback;
  void* opaque;
  int pushback;
  uint32_t pos;
  BitSet stack;
};

BOOL json_init(JsonParser*, JSValueConst, JSContext*);
JsonParser* json_new(JSValueConst, JSContext*);
void json_free(JsonParser*, JSRuntime*);
void json_clear(JsonParser*, JSRuntime*);
int json_getc(JsonParser*);
int json_ungetc(JsonParser*, char);
int json_parse(JsonParser*, JSContext*);

static inline int
json_skip(JsonParser* parser, size_t n) {
  while(n-- > 0)
    if(json_getc(parser) < 0)
      return -1;

  return 0;
}

/**
 * @}
 */
#endif /* defined(JSON_H) */
