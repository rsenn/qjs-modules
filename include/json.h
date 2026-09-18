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
  /* internal-only json_scan_error_skip() signal: a resync comma was found and consumed -
     not a real token, json_parse() loops back to read the next actual value instead of
     returning this to the caller. Kept outside the -3..9 range js_json_parser_method()
     indexes a type-name table with, since it never escapes json_parse() itself. */
  JSON_RESYNC = -100,
  JSON_ERROR = -3,     /* malformed input; json->error describes what */
  JSON_NEED_DATA = -2, /* reader ran out mid-token; feed more input and call json_parse() again */
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

typedef enum {
  JSON_TOK_NONE = 0,
  JSON_TOK_STRING,
  JSON_TOK_NUMBER,
  JSON_TOK_LITERAL,
  JSON_TOK_ERROR_SKIP, /* discarding bytes after a reported error, looking for a resync point */
} JsonTokKind;

typedef enum {
  JSON_STR_NORMAL = 0,
  JSON_STR_ESCAPE,
  JSON_STR_UNICODE,
} JsonStrState;

struct JsonParser {
  int state;
  Reader reader;
  DynBuf token;
  JsonCallback* callback;
  void* opaque;
  int pushback;
  uint32_t pos;
  BitSet stack;
  Location* loc;
  const char* error;

  /* resumable low-level token scan: preserved across json_parse() calls so a reader that
     runs dry mid-token (JSON_NEED_DATA) can be resumed correctly once more data arrives */
  JsonTokKind tok_kind;
  JsonStrState str_state;
  uint32_t str_unicode_val;
  int str_unicode_count;
  uint32_t str_surrogate_hi;
  const char* literal_text;
  int literal_pos;
  BOOL is_key;

  /* json_scan_error_skip() only: '{'/'[' bytes discarded as garbage while resyncing open a
     nesting level that isn't on `stack` (they were never legitimately parsed) - a matching
     '}'/']' later in otherwise-valid input must close *that* first, or it gets mistaken for
     the resync boundary of the container the error actually happened in, eventually popping
     `stack` empty. Always 0 outside of an active skip. */
  int skip_depth;
};

BOOL json_init(JsonParser*, Reader, const char* filename, JSContext*);
JsonParser* json_new(Reader, const char* filename, JSContext*);
void json_free(JsonParser*, JSRuntime*);
void json_clear(JsonParser*, JSRuntime*);
int json_getc(JsonParser*);
int json_ungetc(JsonParser*, char);
int json_parse(JsonParser*);

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
