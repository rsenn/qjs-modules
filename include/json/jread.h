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
/* jr_error_t: why a JSON_EVENT_ERROR was reported; jr_error_name() gives the stable
 * kebab-case name the bindings expose as `err.code`. */
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

const char* jr_error_name(jr_error_t code);

/* jr_flags: which syntax jr_read() accepts; set with jr_state_init_flags().
 *
 * ```c
 * jr_state_init_flags(&st, JR_JSON5);
 * ```
 *
 *   flag                 accepts
 *   JR_COMMENTS          line and block comments wherever whitespace is
 *   JR_TRAILING_COMMAS   `[1,]` and `{"a":1,}`
 *   JR_SINGLE_QUOTES     'strings' and 'keys'
 *   JR_UNQUOTED_KEYS     {key: 1}, ASCII identifiers (`$`, `_`) and UTF-8 letters
 *   JR_JSON5_NUMBERS     +1  .5  1.  0x1F  Infinity  -Infinity  NaN
 *   JR_JSON5_STRINGS     \' \v \0 \xHH \<any char>, `\` + newline, raw control characters,
 *                        \v and \f as whitespace
 *   JR_MULTI             several documents, one after another (NDJSON, JSON Lines)
 *   JR_LEGACY            the historic lax reader: comments and stray or trailing commas,
 *                        `01`, `1.`, `1.2.3`, several documents; the default of jr_state_init()
 *
 * Without JR_LEGACY the reader is strict: one document, commas exactly between items,
 * numbers as in RFC 8259, trailing data is JR_E_TRAILING_DATA, empty input JR_E_UNEXPECTED_END.
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

typedef struct jr_str {
  const char* cstr;
  int32_t len;
} jr_str_t;

typedef void (*jr_callback)(JsonEventType type, const jr_str_t* data, void* user_data);

/*
 * Everything jr_read() needs to resume parsing exactly where a previous call left off,
 * including in the middle of a string/number/keyword: which lexer state to re-enter
 * (go/resume), the array/object/keyword nesting stack (go_stack), and an accumulator
 * for the token currently being scanned - a token's bytes aren't necessarily contiguous
 * in any single buffer the caller passes in, so they're copied out incrementally as
 * they're read rather than sliced from the input.
 *
 * Call jr_state_init() before the first jr_read() call (zero-initializing alone is not
 * enough: go_stack needs its allocator set) and jr_state_free() when done.
 */
typedef struct jr_state {
  void** go;
  void* resume;
  Vector go_stack; /* of void**; grows with the nesting depth of the input */
  int32_t utf8_mask;
  char* accum;
  size_t accum_len;
  size_t accum_cap;
  /* bit-fields, bunched and sized to their value range so they share machine words */
  unsigned accumulating : 1;
  unsigned in_number : 1; /* mid-number at the last pause: unlike other tokens, a number has no
                            unambiguous terminator character, so only end-of-input (jr_finish())
                            can close it out */
  unsigned literal_active : 1; /* mid null/true/false at the last pause - see jr_read()'s l_err */
  unsigned after_comma : 1; /* a comma was read and no item has started since */
  unsigned num_ok : 1;   /* the number read so far is a complete one */
  unsigned kw_active : 1; /* mid Infinity / NaN */
  unsigned done : 1;
  unsigned error : 1; /* sticky: "this stream had a parse error at some point" (jr_finish()/
                     close() reporting only) - no longer blocks jr_read() from resyncing */
  unsigned just_erred : 1; /* reset at the top of every jr_read() call; set if *this* call hit an
                     error - lets a caller throw once per bad span instead of on every
                     future call to an already-resynced parser */
  unsigned str_type : 4;   /* JsonEventType of the string being scanned: string or key */
  unsigned cmt : 2;        /* inside a comment: 1 after `/`, 2 `//` line, 3 block */
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
  /* position: `pos` counts every byte fed so far; `tok` is where the token being
   * scanned began; the err_* fields describe the last error, valid when it is reported:
   * err_pos is the token start (the offending byte outside a token), err_len the bytes
   * from there through the offending byte (0 at end of input). Chunk-size independent. */
  SourcePos pos, tok;
  const char* mark; /* chunk byte that `pos` has been advanced up to (jr_read() only) */
  const void* tab;  /* one-shot dispatch table that read the last byte, or NULL */
  SourcePos err_pos;
  size_t err_len;
} jr_state_t;

void jr_state_init(jr_state_t* state);
void jr_state_init_flags(jr_state_t* state, uint8_t flags);
void jr_state_free(jr_state_t* state);

/*
 * Feeds one chunk (chunk[0..len)) to the parser, invoking cb for every token that
 * completes - possibly none, if the chunk ends mid-token. May be called any number of
 * times with successive chunks of a stream; a chunk boundary may fall anywhere,
 * including mid-string, mid-number, mid-keyword, or mid-escape-sequence, or mid-resync.
 * No-op once state->done is set. On malformed input, reports JSON_EVENT_ERROR once (check
 * state->just_erred right after this call to see whether *this* call hit one) and resyncs
 * at the next comma or closing bracket/brace instead of stopping for good - state->error
 * stays set from then on purely as "this stream had an error at some point" bookkeeping
 * for jr_finish() and callers like JsonPushParser.close().
 */
void jr_read(jr_callback cb, const char* chunk, size_t len, void* user_data, jr_state_t* state);

/*
 * Signals end of input. Flushes a trailing bare number left pending by the last
 * jr_read() call (see jr_state::in_number), or - if parsing was left mid-string,
 * mid-container, or mid-keyword - reports JSON_EVENT_ERROR and sets state->error. Sets
 * state->done on a clean finish. No-op once state->done is already set (state->error
 * alone no longer blocks this - a resynced stream can still finish cleanly).
 */
void jr_finish(jr_callback cb, void* user_data, jr_state_t* state);
#endif // #ifndef JREAD_H
