/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/xread/blob/master/LICENSE
 */

#ifndef XREAD_H
#define XREAD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xr_type {
  xr_type_attribute,
  xr_type_element_start,
  xr_type_element_end,
  xr_type_error,
} xr_type_t;

typedef struct xr_str {
  const char* cstr;
  int32_t len;
} xr_str_t;

typedef void (*xr_callback)(xr_type_t type, const xr_str_t* name, const xr_str_t* value, void* user_data);

/* A single growable buffer used to accumulate one token's bytes as they're read,
 * since a token isn't necessarily contiguous in any single chunk the caller passes
 * to xr_read(). */
typedef struct xr_accum {
  char* buf;
  size_t len;
  size_t cap;
} xr_accum_t;

/*
 * Everything xr_read() needs to resume parsing exactly where a previous call left
 * off, including in the middle of a tag/attribute name or an attribute value: which
 * lexer state to re-enter (go/resume/name_handle), and three independent
 * accumulators - tag_accum, name_accum, val_accum - since a self-closing tag's
 * element_end event reuses the *tag* name captured for its element_start event
 * (needing its own buffer that outlives attribute scanning), and an attribute
 * callback delivers its name and value together (needing two more, live at once).
 *
 * Zero-initialize (or use xr_state_init()) before the first xr_read() call.
 */
typedef struct xr_state {
  void** go;
  void* resume;
  void* name_handle;
  xr_accum_t tag_accum, name_accum, val_accum;
  xr_accum_t* cur_accum;
  int accumulating;
  int at_root; /* go == go_root: nothing open, a clean place for xr_finish() to land */
  int done;
  int error;
} xr_state_t;

void xr_state_init(xr_state_t* state);
void xr_state_free(xr_state_t* state);

/*
 * Feeds one chunk (chunk[0..len)) to the parser, invoking cb for every
 * attribute/element-start/element-end event that completes - possibly none, if the
 * chunk ends mid-token. May be called any number of times with successive chunks of
 * a stream; a chunk boundary may fall anywhere, including mid-tag-name,
 * mid-attribute-name, or mid-attribute-value. No-op once state->error or
 * state->done is set.
 */
void xr_read(xr_callback cb, const char* chunk, size_t len, void* user_data, xr_state_t* state);

/*
 * Signals end of input. Unlike JSON numbers, every XML token here has an
 * unambiguous terminator character, so there's nothing to flush - this only checks
 * state->at_root (nothing left open) to decide between reporting xr_type_error
 * (truncated input) and setting state->done. No-op once state->error or
 * state->done is already set.
 */
void xr_finish(xr_callback cb, void* user_data, xr_state_t* state);

#ifdef __cplusplus
}
#endif

#endif // #ifndef XREAD_H
