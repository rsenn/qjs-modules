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
/* json_parse() returns a JsonEventType (>= 0) or one of these (< 0).
 * these are statuses, not events. */
enum {
  /* internal to json_scan_error_skip(): a resync comma was consumed, so
     json_parse() reads the next value instead of returning this */
  JSON_RESYNC = -100,
  JSON_ERROR = -3,     /* malformed input; see json->error */
  JSON_NEED_DATA = -2, /* reader ran dry; feed more input, call again */
  JSON_NONE = -1,      /* no more events */
};

typedef enum {
  JSON_TOK_NONE = 0,
  JSON_TOK_STRING,
  JSON_TOK_NUMBER,
  JSON_TOK_LITERAL,
  JSON_TOK_IDENT, /* JR_UNQUOTED_KEYS: object key without quotes */
  JSON_TOK_ERROR_SKIP, /* discarding bytes after an error, looking for a resync point */
} JsonTokKind;

typedef enum {
  JSON_STR_NORMAL = 0,
  JSON_STR_ESCAPE,
  JSON_STR_UNICODE,
  JSON_STR_HEX,   /* JR_JSON5_STRINGS: \xHH */
  JSON_STR_NUL,   /* JR_JSON5_STRINGS: after \0, a digit must not follow */
  JSON_STR_CR,    /* JR_JSON5_STRINGS: after backslash CR, an LF ends the continuation */
  JSON_STR_E2,    /* JR_JSON5_STRINGS: after backslash 0xE2, looking for U+2028/2029 */
  JSON_STR_E2_80, /* ... after 0xE2 0x80 */
} JsonStrState;

#define JSON_BLOCK_SIZE 16384

/* where a character or token starts; line and column are 0-based */
typedef struct {
  int32_t line, column;
  int64_t char_offset, byte_offset;
} JsonPos;

typedef struct json_parser {
  Reader reader;
  /* one reader_read() per block, not per byte: a JS pull callback costs a call */
  uint8_t block[JSON_BLOCK_SIZE];
  size_t block_pos, block_len;
  DynBuf token;
  int pushback;
  uint32_t pos;
  BitSet stack;
  Location* loc;
  const char* error;

  /* error report, set when json_parse() returns JSON_ERROR.
     err_at: start of the offending token (opening byte of a string or
     literal, else the offending byte); err_len: bytes from there through
     the offending byte; err_code: stable name */
  JsonPos last_start, tok_start, err_at;
  size_t err_len;
  const char* err_code;

  /* resumable token scan, kept across json_parse() calls so a reader that
     runs dry mid-token (JSON_NEED_DATA) resumes once data arrives */
  const char* literal_text;

  /* json_scan_error_skip() only: count of { and [ discarded while
     resyncing; they open levels that are not on stack, so a later } or ]
     must close those first. 0 outside a skip */
  int skip_depth;

  int cmt_column; /* json_getc_skipws(): column where the open comment started */

  int roots; /* completed top-level values */

  /* bit-fields, sized to their value range and bunched into shared words */
  unsigned state : 5;        /* PARSING_* / EXPECTING_* bits of json.c */
  unsigned tok_kind : 3;     /* JsonTokKind */
  unsigned str_state : 3;    /* JsonStrState */
  unsigned quote : 7;        /* closing byte of the string being scanned: '"' or '\'' */
  unsigned num_state : 4;    /* number scan state outside JR_LEGACY (NS_* in json.c) */
  unsigned cmt_state : 3;    /* comment scan state, 0 outside a comment */
  unsigned literal_pos : 4;  /* how far the literal is matched, 0..8 */
  unsigned pending : 4;      /* event for the next json_parse(), valid if has_pending */
  unsigned is_key : 1;
  unsigned comments : 1; /* return comments as JSON_EVENT_COMMENT, do not skip them */
  unsigned has_pending : 1;
  unsigned after_comma : 1; /* the last token was a separating comma */
  unsigned trailing : 1;    /* data after the document: the rest is discarded */
  unsigned str_unicode_count : 3;

  /* narrow members, kept together so they pack into the same word */
  uint16_t str_unicode_val;
  uint16_t str_surrogate_hi;
  uint8_t flags; /* JR_* syntax flags of jread.h; json_init() sets JR_LEGACY */
} JsonParser;

BOOL json_init(JsonParser*, Reader, const char* filename, JSContext*);
JsonParser* json_new(Reader, const char* filename, JSContext*);
void json_free(JsonParser*, JSRuntime*);
void json_clear(JsonParser*, JSRuntime*);
int json_getc(JsonParser*);
int json_ungetc(JsonParser*, char);

/* json_parse: reads up to the next event; the token text is in json->token.
 *
 * after a key named id:
 *
 *   int r = json_parse(&json);   r is JSON_EVENT_KEY, json->token is id
 *
 *   JsonParser*  json  the parser, set up by json_init()
 *
 *   returns int  a JsonEventType (>= 0), or a status (< 0) that is no event:
 *
 *     JSON_NONE       -1  no more events
 *     JSON_NEED_DATA  -2  reader ran dry; feed more and call again
 *     JSON_ERROR      -3  malformed input; see json->error, err_code, err_at
 *
 * never throws; JSON_RESYNC stays inside json.c and is never returned. */
int json_parse(JsonParser*);
int json_end(JsonParser*);

/* json_next: the next event of a pull parser, written to *ev (the Event model v2 form of json_parse()).
 *
 *   JsonParser*  json  the parser, set up by json_init()
 *   JsonEvent*   ev    filled when the result is JSON_PULL_EVENT
 *
 *   returns JsonPull  JSON_PULL_EVENT, JSON_PULL_AGAIN (reader ran dry, or input ended: the parser
 *                     cannot tell them apart) or JSON_PULL_END
 *
 * in the event
 *   - type is a JsonEventType; a malformed span is JSON_EVENT_ERROR with the message in str / len
 *     and the offending token in at, and the next call resyncs as json_parse() does
 *   - str / len is the text of an error, number, string, key or comment; valid until the next call
 *   - at is the start of the token (1-based line and column), has_at is always set
 *
 * never throws; the error code and length stay in json->err_code and json->err_len. */
JsonPull json_next(JsonParser*, JsonEvent* ev);

/* json_source: the pull parser as a JsonEventSource, for jevent_pump(). */
JsonEventSource json_source(JsonParser*);

/**
 * @}
 */
#endif /* defined(JSON_H) */
