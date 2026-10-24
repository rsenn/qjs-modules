#ifndef JSON_H
#define JSON_H

#include "stream-utils.h"
#include "json/jread.h"

/**
 * \defgroup json json: JSON pull parser
 * @{
 */

#define JSON_BLOCK_SIZE 16384

/* JsonParser: a pull parser, a Reader in and one JsonEvent per json_next().
 *
 * jread does the parsing: json_next() reads one block, feeds it to jr_feed() and queues the events
 * it makes, so a block may leave many events behind. set flags (JR_*) and comments before the first
 * json_next(). */
typedef struct json_parser {
  Reader reader;
  jr_state_t st;
  Location* loc;         /* where the last event came from, and the file name for errors */
  uint8_t flags;         /* JR_* syntax flags of jread.h; json_init() sets JR_LEGACY */
  unsigned comments : 1; /* deliver comments as JSON_EVENT_COMMENT, else skip them */
  unsigned started : 1;
  unsigned ended : 1;
  uint32_t depth; /* open arrays and objects, as of the last event returned */
  struct json_queue {
    JsonEvent* ev;
    size_t head, len, cap;
    char* text;
    size_t text_len, text_cap;
  } queue;
  uint8_t block[JSON_BLOCK_SIZE];
} JsonParser;

BOOL json_init(JsonParser*, Reader, const char* filename, JSContext*);
JsonParser* json_new(Reader, const char* filename, JSContext*);
void json_free(JsonParser*, JSRuntime*);
void json_clear(JsonParser*, JSRuntime*);

/* json_next: the next event of a pull parser, written to *ev .
 *
 *   JsonParser*  json  the parser, set up by json_init()
 *   JsonEvent*   ev    filled when the result is JSON_PULL_EVENT
 *
 *   returns JsonPull  JSON_PULL_EVENT, JSON_PULL_AGAIN (reader ran dry, or input ended: the parser
 *                     cannot tell them apart) or JSON_PULL_END
 *
 * in the event
 *   - type is a JsonEventType; a malformed span is JSON_EVENT_ERROR with the message in str / len
 *     and the offending token in at, and the next call resyncs as jread does
 *   - str / len is the text of an error, number, string, key or comment; valid until the next call
 *   - at is the start of the token (1-based line and column), has_at is always set
 *
 * an error event also has code (the stable name, "unexpected-token") and span (bytes from at
 * through the offending byte, 0 at the end of input). never throws. */
JsonPull json_next(JsonParser*, JsonEvent* ev);

/* json_next_end: signals the end of the input; the event form of json_end().
 *
 * Call it when json_next() answers JSON_PULL_AGAIN and no more data will come. It delivers what the
 * end completes or breaks, one event per call: a number that nothing terminated, then
 * JSON_EVENT_ERROR with code "unexpected-end" for an unfinished document, then JSON_PULL_END.
 * Call it until it answers JSON_PULL_END. never throws. */
JsonPull json_next_end(JsonParser*, JsonEvent* ev);

/* json_source: the pull parser as a JsonEventSource, for jevent_pump(). */
JsonEventSource json_source(JsonParser*);

/**
 * @}
 */
#endif /* defined(JSON_H) */
