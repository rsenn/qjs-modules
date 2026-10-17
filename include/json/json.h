#ifndef JSON_H
#define JSON_H

#include "stream-utils.h"
#include "buffer-utils.h"
#include "bitset.h"
#include "json/jread.h"

/**
 * \defgroup json json: JSON parser
 * @{
 */
/* json_parse() returns a JsonEventType (>= 0) or one of these (< 0), which are not events */
enum {
  /* internal-only json_scan_error_skip() signal: a resync comma was found and consumed -
     not a real token, json_parse() loops back to read the next actual value instead of
     returning this to the caller. */
  JSON_RESYNC = -100,
  JSON_ERROR = -3,     /* malformed input; json->error describes what */
  JSON_NEED_DATA = -2, /* reader ran out mid-token; feed more input and call json_parse() again */
  JSON_NONE = -1,      /* no more events */
};

typedef enum {
  JSON_TOK_NONE = 0,
  JSON_TOK_STRING,
  JSON_TOK_NUMBER,
  JSON_TOK_LITERAL,
  JSON_TOK_IDENT, /* JR_UNQUOTED_KEYS: an object key without quotes */
  JSON_TOK_ERROR_SKIP, /* discarding bytes after a reported error, looking for a resync point */
} JsonTokKind;

typedef enum {
  JSON_STR_NORMAL = 0,
  JSON_STR_ESCAPE,
  JSON_STR_UNICODE,
  JSON_STR_HEX,   /* JR_JSON5_STRINGS: \xHH */
  JSON_STR_NUL,   /* JR_JSON5_STRINGS: after \0, a digit must not follow */
  JSON_STR_CR,    /* JR_JSON5_STRINGS: after `\` CR, an LF ends the continuation */
  JSON_STR_E2,    /* JR_JSON5_STRINGS: after `\` 0xE2, looking for U+2028/2029 */
  JSON_STR_E2_80, /* ... after 0xE2 0x80 */
} JsonStrState;

#define JSON_BLOCK_SIZE 16384

/* where a character or token starts, counted as Location does (0-based line and column) */
typedef struct {
  int32_t line, column;
  int64_t char_offset, byte_offset;
} JsonPos;

typedef struct json_parser {
  Reader reader;
  /* one reader_read() per block instead of per byte: a JS pull callback costs a call each */
  uint8_t block[JSON_BLOCK_SIZE];
  size_t block_pos, block_len;
  DynBuf token;
  int pushback;
  uint32_t pos;
  BitSet stack;
  Location* loc;
  const char* error;

  /* error report: set when json_parse() returns JSON_ERROR. err_at is the start of the
     offending token (a string or literal: its opening byte; otherwise the offending byte),
     err_len the bytes from there through the offending byte, err_code a stable name */
  JsonPos last_start, tok_start, err_at;
  size_t err_len;
  const char* err_code;

  /* resumable low-level token scan: preserved across json_parse() calls so a reader that
     runs dry mid-token (JSON_NEED_DATA) can be resumed correctly once more data arrives */
  const char* literal_text;

  /* json_scan_error_skip() only: '{'/'[' bytes discarded as garbage while resyncing open a
     nesting level that isn't on `stack` (they were never legitimately parsed) - a matching
     '}'/']' later in otherwise-valid input must close *that* first, or it gets mistaken for
     the resync boundary of the container the error actually happened in, eventually popping
     `stack` empty. Always 0 outside of an active skip. */
  int skip_depth;

  /* json_getc_skipws(): comment scan state, 0 outside a comment */
  int cmt_column;

  /* syntax: JR_* flags of jread.h; json_init() sets JR_LEGACY, the historic lax reader
     (comments and stray commas accepted, numbers scanned loosely, many documents) */
  int roots; /* completed top-level values */

  /* bit-fields, bunched and sized to their value range so they share machine words */
  unsigned state : 5;        /* PARSING_* / EXPECTING_* bits of json.c */
  unsigned tok_kind : 3;     /* JsonTokKind */
  unsigned str_state : 3;    /* JsonStrState */
  unsigned quote : 7;        /* closing byte of the string being scanned: '"' or '\'' */
  unsigned num_state : 4;    /* number scan state outside JR_LEGACY (NS_* in json.c) */
  unsigned cmt_state : 3;    /* comment scan state, 0 outside a comment */
  unsigned literal_pos : 4;  /* how far the literal is matched, 0..8 */
  unsigned pending : 4;      /* an event to return on the next json_parse(), valid if has_pending */
  unsigned is_key : 1;
  unsigned comments : 1; /* report comments as JSON_EVENT_COMMENT instead of skipping them */
  unsigned has_pending : 1;
  unsigned after_comma : 1; /* the last token was a separating comma */
  unsigned trailing : 1;    /* data after the document: the rest is discarded */
  unsigned str_unicode_count : 3;

  /* narrow members, kept together so they pack into the same word */
  uint16_t str_unicode_val;
  uint16_t str_surrogate_hi;
  uint8_t flags; /* JR_* syntax flags of jread.h */
} JsonParser;

BOOL json_init(JsonParser*, Reader, const char* filename, JSContext*);
JsonParser* json_new(Reader, const char* filename, JSContext*);
void json_free(JsonParser*, JSRuntime*);
void json_clear(JsonParser*, JSRuntime*);
int json_getc(JsonParser*);
int json_ungetc(JsonParser*, char);
int json_parse(JsonParser*);
int json_end(JsonParser*);

/**
 * @}
 */
#endif /* defined(JSON_H) */
