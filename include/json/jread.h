/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/jread/blob/master/LICENSE
 */

#ifndef JREAD_H
#define JREAD_H

#include <stdint.h>
#include <stddef.h>
#include "vector.h"
#include "srcpos.h"
#include "json/jevent.h"

/**
 * \defgroup jread jread: JSON push parser
 * @{
 */

/* jr_error_t: why a JSON_EVENT_ERROR was reported.
 * jr_error_name() gives the stable kebab-case name the bindings expose as
 * err.code. */
typedef enum jr_error {
  JR_E_NONE,
  JR_E_UNEXPECTED_TOKEN,
  JR_E_BAD_ESCAPE,
  JR_E_BAD_UNICODE_ESCAPE,
  JR_E_INVALID_UTF8,
  JR_E_CONTROL_CHARACTER,
  JR_E_INVALID_LITERAL,
  JR_E_UNEXPECTED_END,
  JR_E_OUT_OF_MEMORY,
  JR_E_TRAILING_DATA,
} jr_error_t;

/* jr_error_name: the kebab-case name of an error code.
 * never throws: returns a static string. */
const char* jr_error_name(jr_error_t code);

/* jr_flags: which syntax jr_feed() accepts; set with jr_state_init_flags().
 *
 *   jr_state_init_flags(&st, JR_JSON5);
 *
 *   flag                 accepts
 *   JR_COMMENTS          line and block comments wherever whitespace is
 *   JR_TRAILING_COMMAS   [1,] and {"a":1,}
 *   JR_SINGLE_QUOTES     'strings' and 'keys'
 *   JR_UNQUOTED_KEYS     {key: 1}, ASCII identifiers ($ _) and UTF-8 letters
 *   JR_JSON5_NUMBERS     +1  .5  1.  0x1F  Infinity  -Infinity  NaN
 *   JR_JSON5_STRINGS     \' \v \0 \xHH \<any char>, backslash plus newline,
 *                        raw control characters, \v and \f as whitespace
 *   JR_MULTI             several documents in a row (NDJSON, JSON Lines)
 *   JR_LEGACY            the historic lax reader, default of jr_state_init():
 *                        comments, stray or trailing commas, 01, 1., 1.2.3,
 *                        several documents
 *
 * Without JR_LEGACY the reader is strict.
 *   - one document, commas exactly between items, numbers as in RFC 8259
 *   - trailing data is JR_E_TRAILING_DATA
 *   - empty input is JR_E_UNEXPECTED_END
 *
 *   preset        flags
 *   JR_JSON       none: RFC 8259 / JSON.parse
 *   JR_JSONC      JR_COMMENTS | JR_TRAILING_COMMAS (Bun's JSONC, VS Code)
 *   JR_JSON5      everything above except JR_MULTI and JR_LEGACY
 *   JR_JSONL      JR_MULTI: strict values, one or more documents */
enum {
  JR_COMMENTS = 1,
  JR_TRAILING_COMMAS = 2,
  JR_SINGLE_QUOTES = 4,
  JR_UNQUOTED_KEYS = 8,
  JR_JSON5_NUMBERS = 16,
  JR_JSON5_STRINGS = 32,
  JR_MULTI = 64,
  JR_LEGACY = 128,
};

#define JR_JSON 0
#define JR_JSONC (JR_COMMENTS | JR_TRAILING_COMMAS)
#define JR_JSON5 (JR_COMMENTS | JR_TRAILING_COMMAS | JR_SINGLE_QUOTES | JR_UNQUOTED_KEYS | JR_JSON5_NUMBERS | JR_JSON5_STRINGS)
#define JR_JSONL JR_MULTI

/* jr_state_t: everything jr_feed() needs to resume exactly where the previous
 * call stopped, even in the middle of a string, number or keyword.
 *
 *   go, resume   the lexer state to re-enter
 *   go_stack     the array, object and keyword nesting
 *   accum        the token being scanned, copied out as it is read since its
 *                bytes are not contiguous across chunks
 *
 * call jr_state_init() before the first jr_feed(), jr_state_free() when done.
 * zeroing is not enough, go_stack needs its allocator set. */
typedef struct jr_state {
  void** go;
  void* resume;
  Vector go_stack; /* of void**, grows with the nesting depth */
  int32_t utf8_mask;
  char* accum;
  size_t accum_len;
  size_t accum_cap;
  /* bit-fields, bunched and sized to their value range so they share machine words */
  unsigned accumulating : 1;
  /* mid-number at the last pause, a number has no terminator character so only
   * end of input (jr_end()) can close it */
  unsigned in_number : 1;
  unsigned literal_active : 1; /* mid null/true/false at the last pause */
  unsigned after_comma : 1; /* a comma was read and no item has started since */
  unsigned num_ok : 1;   /* the number read so far is a complete one */
  unsigned kw_active : 1; /* mid Infinity / NaN */
  unsigned esc_hi : 1;    /* a backslash came before a 0xE2 byte, U+2028 and U+2029 then continue the line */
  unsigned done : 1;
  /* set by the caller after jr_state_init_flags(): comments arrive as JSON_EVENT_COMMENT, the text with its // or slash-star */
  unsigned comment_events : 1;
  /* sticky: the stream had an error at some point, reported by jr_end().
   * it does not stop jr_feed() from resyncing */
  unsigned error : 1;
  /* reset at the top of every jr_feed(), set if this call hit an error, so a
   * caller can throw once per bad span */
  unsigned just_erred : 1;
  unsigned str_type : 4;   /* JsonEventType of the string being scanned: string or key */
  unsigned cmt : 2;        /* inside a comment: 1 after a slash, 2 line, 3 block */
  unsigned err_code : 4;   /* jr_error_t of the last error */
  unsigned kw_pos : 4;     /* how far the keyword is matched, 0..8 */
  unsigned unicode_count : 3; /* how many of the 4 hex digits have been read */
  unsigned unicode_need : 3;  /* hex digits the current escape takes: 4 for \u, 2 for \x */
  /* narrow members, kept together so they pack into the same word */
  uint16_t unicode_val;  /* \uXXXX hex digits decoded so far, mid-escape */
  uint16_t surrogate_hi; /* a buffered UTF-16 high surrogate, waiting for its low half */
  uint8_t flags;         /* JR_* syntax flags */
  int roots;             /* documents completed so far */
  const char* kw;        /* the keyword being matched, and how far */
  SourcePos pos, tok; /* pos: bytes fed so far, tok: start of the current token */
  const char* mark; /* chunk byte that pos has been advanced up to (jr_feed() only) */
  const void* tab;  /* one-shot dispatch table that read the last byte, or NULL */
  SourcePos err_pos; /* token start of the last error, or its offending byte outside a token */
  size_t err_len; /* bytes from err_pos through the offending byte, 0 at end of input */
} jr_state_t;

/* jr_state_init: sets up state with the JR_LEGACY syntax. never throws. */
void jr_state_init(jr_state_t* state);

/* jr_state_init_flags: sets up state with the given JR_* flags. never throws. */
void jr_state_init_flags(jr_state_t* state, uint8_t flags);

/* jr_state_free: releases accum and go_stack of state. never throws. */
void jr_state_free(jr_state_t* state);

/**
 * jr_feed: feeds one chunk to the parser, calling the handler per completed token.
 *
 *   jr_state_t*         state    parser state, set up with jr_state_init()
 *   const char*         chunk    next bytes of the stream, chunk[0..len)
 *   size_t              len      number of bytes in chunk
 *   JsonEventHandler*   handler  receives each event; the first negative result stops delivery
 *   void*               user     passed to handler unchanged
 *
 *   returns ssize_t  0, or the first negative handler result
 *
 * call it any number of times with successive chunks of one stream.
 *   - a chunk boundary may fall anywhere, also mid-string, mid-number, mid-keyword, mid-escape
 *   - no-op once state->done is set
 *   - malformed input reports one error event, then resyncs at the next comma or closing
 *     bracket or brace instead of stopping for good
 *   - state->just_erred tells whether this call hit an error, state->error stays set as a
 *     record for jr_end()
 *
 * an error event carries the offending token in at, has_at set; other events carry the
 * position of the token start the parser had when it completed (state->tok). To feed a sink:
 *
 *   jr_feed(&st, chunk, n, jevent_handler, &sink);
 *
 * never throws. */
ssize_t jr_feed(jr_state_t* state, const char* chunk, size_t len, JsonEventHandler* handler, void* user);

/* jr_end: signals the end of the input, the twin of jr_feed().
 *
 * a bare number left pending is flushed, parsing left mid-string, mid-container or mid-keyword
 * reports an error event and sets state->error, state->done is set on a clean end. A no-op once
 * state->done is set. */
ssize_t jr_end(jr_state_t* state, JsonEventHandler* handler, void* user);

/* jr_flush: delivers a line comment that the data ended in, as jr_end() does, but leaves the
 * parser open for more data. Only with comment_events, else a no-op. */
ssize_t jr_flush(jr_state_t* state, JsonEventHandler* handler, void* user);

/**
 * jr_flags_from_name: the JR_* flags of a dialect name.
 *
 *   "json" JR_JSON, "jsonc" JR_JSONC, "json5" JR_JSON5, "jsonl" JR_JSONL, "legacy" JR_LEGACY
 *
 *   const char*  name  dialect name, name[0..len)
 *   size_t       len   length of name
 *   int*         flags receives the flags
 *
 *   returns int  0, or -1 for an unknown name; never throws */
int jr_flags_from_name(const char* name, size_t len, int* flags);

/**
 * @}
 */

#endif // #ifndef JREAD_H
