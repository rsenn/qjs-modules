/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/jread/blob/master/LICENSE
 */

#ifndef JREAD_H
#define JREAD_H

#include <stdint.h>
#include <stddef.h>

#ifndef JREAD_CONFIG_MAX_DEPTH
#define JREAD_CONFIG_MAX_DEPTH 64
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum jr_type {
  jr_type_error,
  jr_type_null,
  jr_type_true,
  jr_type_false,
  jr_type_number,
  jr_type_string,
  jr_type_array_start,
  jr_type_array_end,
  jr_type_object_start,
  jr_type_object_end,
  jr_type_key,
} jr_type_t;

typedef struct jr_str {
  const char* cstr;
  int32_t len;
} jr_str_t;

typedef void (*jr_callback)(jr_type_t type, const jr_str_t* data, void* user_data);

/*
 * Everything jr_read() needs to resume parsing exactly where a previous call left off,
 * including in the middle of a string/number/keyword: which lexer state to re-enter
 * (go/resume), the array/object/keyword nesting stack (go_stack), and an accumulator
 * for the token currently being scanned - a token's bytes aren't necessarily contiguous
 * in any single buffer the caller passes in, so they're copied out incrementally as
 * they're read rather than sliced from the input.
 *
 * Zero-initialize (or use jr_state_init()) before the first jr_read() call.
 */
typedef struct jr_state {
  void** go;
  void* resume;
  void** go_stack[JREAD_CONFIG_MAX_DEPTH];
  int32_t go_stack_idx;
  int32_t utf8_mask;
  jr_type_t str_type;
  char* accum;
  size_t accum_len;
  size_t accum_cap;
  int accumulating;
  int in_number; /* mid-number at the last pause: unlike other tokens, a number has no
                    unambiguous terminator character, so only end-of-input (jr_finish())
                    can close it out */
  int done;
  int error;
} jr_state_t;

void jr_state_init(jr_state_t* state);
void jr_state_free(jr_state_t* state);

/*
 * Feeds one chunk (chunk[0..len)) to the parser, invoking cb for every token that
 * completes - possibly none, if the chunk ends mid-token. May be called any number of
 * times with successive chunks of a stream; a chunk boundary may fall anywhere,
 * including mid-string, mid-number, mid-keyword, or mid-escape-sequence. No-op once
 * state->error or state->done is set.
 */
void jr_read(jr_callback cb, const char* chunk, size_t len, void* user_data, jr_state_t* state);

/*
 * Signals end of input. Flushes a trailing bare number left pending by the last
 * jr_read() call (see jr_state::in_number), or - if parsing was left mid-string,
 * mid-container, or mid-keyword - reports jr_type_error and sets state->error. Sets
 * state->done on a clean finish. No-op once state->error or state->done is already set.
 */
void jr_finish(jr_callback cb, void* user_data, jr_state_t* state);

#ifdef __cplusplus
}
#endif

#endif // #ifndef JREAD_H
