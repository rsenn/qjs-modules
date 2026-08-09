/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/xread/blob/master/LICENSE
 */

#include "xread.h"
#include "xml_entities.h"
#include <stdlib.h>
#include <string.h>

/* Pastes __LINE__ into a label name, so each XR_DISPATCH_*() call site gets its own
 * unique "resume here" label - see the comment above XR_DISPATCH_NEXT() below. */
#define XR_PASTE_(a, b) a##b
#define XR_PASTE(a, b) XR_PASTE_(a, b)
#define XR_RESUME_LABEL XR_PASTE(l_resume_, __LINE__)

/*
 * Reads and dispatches on the next byte of `go` - exactly like the original
 * goto*go[(uint8_t)*cstr++], except:
 *
 *  - bounds-checked against `end`: if the chunk is exhausted, this saves everything
 *    needed to resume (go, and a label - XR_RESUME_LABEL - pointing at this exact
 *    dispatch site) into `state` and returns, instead of reading past the buffer.
 *    XR_RESUME_LABEL is unique per call site (via __LINE__), and is planted *inside*
 *    the macro expansion, after any one-time setup code the enclosing l_xxx: label
 *    may have (e.g. l_name_begin resetting the accumulator) - so resuming via `goto
 *    *state->resume` at the top of xr_read() re-enters past that setup, without
 *    re-running it.
 *  - accumulates: if state->accumulating, the byte just read is appended to
 *    *state->cur_accum, since a token's bytes aren't necessarily contiguous in any
 *    one chunk. l_xxx_begin-style labels turn accumulating on (after resetting the
 *    accumulator) and pick which of tag_accum/name_accum/val_accum is `cur_accum`;
 *    l_xxx_end-style labels turn it off and trim the trailing (terminator) byte
 *    that just got appended by the very dispatch that landed there.
 */
#define XR_DISPATCH_NEXT() \
  XR_RESUME_LABEL: \
  if(cstr >= end) { \
    state->go = go; \
    state->resume = &&XR_RESUME_LABEL; \
    return; \
  } \
  { \
    uint8_t xr_c__ = (uint8_t)*cstr++; \
    if(state->accumulating) \
      xr_accum_putc(state->cur_accum, (char)xr_c__); \
    goto* go[xr_c__]; \
  }

/* Re-dispatch on the already-consumed cstr[-1]: no new byte is read, so this can
 * never block on a chunk boundary and needs no resume handling. */
#define XR_DISPATCH_THIS() goto* go[(uint8_t)cstr[-1]];

static void
xr_accum_reset(xr_accum_t* a) {
  a->len = 0;
}

static void
xr_accum_putc(xr_accum_t* a, char c) {
  if(a->len >= a->cap) {
    size_t cap = a->cap ? a->cap * 2 : 64;
    char* p = realloc(a->buf, cap);

    if(!p)
      return; /* OOM: silently truncate rather than crash */

    a->buf = p;
    a->cap = cap;
  }

  a->buf[a->len++] = c;
}

void
xr_state_init(xr_state_t* state) {
  memset(state, 0, sizeof(*state));
  state->at_root = 1;
}

void
xr_state_free(xr_state_t* state) {
  free(state->tag_accum.buf);
  free(state->name_accum.buf);
  free(state->val_accum.buf);
  free(state->text_accum.buf);
  memset(state, 0, sizeof(*state));
}

void
xr_read(xr_callback cb, const char* chunk, size_t len, void* user_data, xr_state_t* state) {
  static void* go_root[] = {
      [0] = &&l_error,
      [1 ... 8] = &&l_error,
      ['\t'] = &&l_next,
      ['\n'] = &&l_next,
      [11 ... 12] = &&l_error,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_error,
      [' '] = &&l_next,
      [33 ... 59] = &&l_text_begin,
      ['<'] = &&l_tag,
      [61 ... 255] = &&l_text_begin,
  };

  /* Entered once inside a run of text content (l_text_begin), after the first
   * non-whitespace byte - unlike go_root, every byte except '<' (including
   * whitespace) continues accumulating here, so interior whitespace is preserved;
   * only leading whitespace before the first non-whitespace byte is skipped (via
   * go_root's own l_next), matching how insignificant inter-tag whitespace was
   * already silently dropped before this table existed. */
  static void* go_text[] = {
      [0 ... 59] = &&l_next,
      ['<'] = &&l_text_end,
      [61 ... 255] = &&l_next,
  };

  static void* go_name[] = {
      [0 ... 44] = &&l_name_end,
      ['-'] = &&l_next,
      ['.'] = &&l_next,
      [47 ... 47] = &&l_name_end,
      ['0' ... '9'] = &&l_next,
      [58 ... 64] = &&l_name_end,
      ['A' ... 'Z'] = &&l_next,
      [91 ... 94] = &&l_name_end,
      ['_'] = &&l_next,
      [96 ... 96] = &&l_name_end,
      ['a' ... 'z'] = &&l_next,
      [123 ... 255] = &&l_name_end,
  };

  static void* go_stag[] = {
      [0 ... 46] = &&l_error,
      ['/'] = &&l_etag,
      [48 ... 64] = &&l_error,
      ['A' ... 'Z'] = &&l_name_begin,
      [91 ... 94] = &&l_error,
      ['_'] = &&l_name_begin,
      [96 ... 96] = &&l_error,
      ['a' ... 'z'] = &&l_name_begin,
      [123 ... 255] = &&l_error,
  };

  static void* go_etag[] = {
      [0 ... 64] = &&l_error,
      ['A' ... 'Z'] = &&l_name_begin,
      [91 ... 94] = &&l_error,
      ['_'] = &&l_name_begin,
      [96 ... 96] = &&l_error,
      ['a' ... 'z'] = &&l_name_begin,
      [123 ... 255] = &&l_error,
  };

  static void* go_tag_close[] = {
      [0 ... 61] = &&l_error,
      ['>'] = &&l_tag_end,
      [63 ... 255] = &&l_error,
  };

  static void* go_attrib[] = {
      [0 ... 8] = &&l_error,
      ['\t'] = &&l_next,
      ['\n'] = &&l_next,
      [11 ... 12] = &&l_error,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_error,
      [' '] = &&l_next,
      [33 ... 46] = &&l_error,
      ['/'] = &&l_empty_element_tag,
      [48 ... 61] = &&l_error,
      ['>'] = &&l_tag_end,
      [63 ... 64] = &&l_error,
      ['A' ... 'Z'] = &&l_attrib,
      [91 ... 94] = &&l_error,
      ['_'] = &&l_attrib,
      [96 ... 96] = &&l_error,
      ['a' ... 'z'] = &&l_attrib,
      [123 ... 255] = &&l_error,
  };

  /* After an attribute name: '=' begins its value as usual, but '/' (self-closing),
   * '>' (tag end), or the first byte of another attribute's name means the just-
   * scanned name was a valueless/boolean attribute (e.g. `<input disabled>`) -
   * dispatched to l_attrib_bool, which emits it with no value and re-dispatches the
   * same byte through go_attrib, same as go_attrib itself handles those bytes. */
  static void* go_attrib_eq[] = {
      [0 ... 8] = &&l_error,     ['\t'] = &&l_next,
      ['\n'] = &&l_next,         [11 ... 12] = &&l_error,
      ['\r'] = &&l_next,         [14 ... 31] = &&l_error,
      [' '] = &&l_next,          [33 ... 46] = &&l_error,
      ['/'] = &&l_attrib_bool,   [48 ... 60] = &&l_error,
      ['='] = &&l_attrib_eq,     ['>'] = &&l_attrib_bool,
      [63 ... 64] = &&l_error,   ['A' ... 'Z'] = &&l_attrib_bool,
      [91 ... 94] = &&l_error,   ['_'] = &&l_attrib_bool,
      [96] = &&l_error,          ['a' ... 'z'] = &&l_attrib_bool,
      [123 ... 255] = &&l_error,
  };

  static void* go_attrib_val_begin[] = {
      [0 ... 8] = &&l_error,
      ['\t'] = &&l_next,
      ['\n'] = &&l_next,
      [11 ... 12] = &&l_error,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_error,
      [' '] = &&l_next,
      [33] = &&l_error,
      ['"'] = &&l_attrib_val_double,
      [35 ... 38] = &&l_error,
      ['\''] = &&l_attrib_val_single,
      [40 ... 255] = &&l_error,
  };

  static void* go_attrib_val_single[] = {
      [0 ... 31] = &&l_error,
      [32 ... 38] = &&l_next,
      ['\''] = &&l_attrib_val,
      [40 ... 255] = &&l_next,
  };

  static void* go_attrib_val_double[] = {
      [0 ... 31] = &&l_error,
      [32 ... 33] = &&l_next,
      ['"'] = &&l_attrib_val,
      [35 ... 255] = &&l_next,
  };

  const char* cstr = chunk;
  const char* end = chunk + len;
  void** go = state->go ? state->go : go_root;

  if(state->error || state->done)
    return;

  if(state->resume)
    goto * state->resume;

l_next:
  XR_DISPATCH_NEXT();

l_error: {
  xr_str_t name = {"Error!", 6};
  xr_str_t val = {cstr - 1, 1};
  cb(xr_type_error, &name, &val, user_data);
}
  state->error = 1;
  return;

l_name_begin:
  xr_accum_reset(&state->tag_accum);
  xr_accum_putc(&state->tag_accum, cstr[-1]);
  state->cur_accum = &state->tag_accum;
  state->accumulating = 1;
  go = go_name;
  XR_DISPATCH_NEXT();

l_name_end:
  goto * state->name_handle;

l_text_begin:
  xr_accum_reset(&state->text_accum);
  xr_accum_putc(&state->text_accum, cstr[-1]);
  state->cur_accum = &state->text_accum;
  state->accumulating = 1;
  go = go_text;
  XR_DISPATCH_NEXT();

l_text_end:
  state->accumulating = 0;
  {
    xr_str_t text = {state->text_accum.buf, (int32_t)(state->text_accum.len - 1)};
    text.len = (int32_t)xml_decode_entities(state->text_accum.buf, (size_t)text.len);
    cb(xr_type_text, 0, &text, user_data);
  }
  goto l_tag;

l_tag:
  state->at_root = 0;
  state->name_handle = &&l_stag_name;
  go = go_stag;
  XR_DISPATCH_NEXT();

l_etag:
  state->name_handle = &&l_etag_name;
  go = go_etag;
  XR_DISPATCH_NEXT();

l_tag_end:
  state->at_root = 1;
  go = go_root;
  XR_DISPATCH_NEXT();

l_stag_name:
  state->accumulating = 0;
  {
    xr_str_t tag = {state->tag_accum.buf, (int32_t)(state->tag_accum.len - 1)};
    cb(xr_type_element_start, &tag, 0, user_data);
  }
  go = go_attrib;
  XR_DISPATCH_THIS();

l_etag_name:
  state->accumulating = 0;
  {
    xr_str_t tag = {state->tag_accum.buf, (int32_t)(state->tag_accum.len - 1)};
    cb(xr_type_element_end, &tag, 0, user_data);
  }
  go = go_tag_close;
  XR_DISPATCH_THIS();

l_empty_element_tag: {
  xr_str_t tag = {state->tag_accum.buf, (int32_t)(state->tag_accum.len - 1)};
  cb(xr_type_element_end, &tag, 0, user_data);
}
  go = go_tag_close;
  XR_DISPATCH_NEXT();

l_attrib:
  xr_accum_reset(&state->name_accum);
  xr_accum_putc(&state->name_accum, cstr[-1]);
  state->cur_accum = &state->name_accum;
  state->accumulating = 1;
  state->name_handle = &&l_attrib_name;
  go = go_name;
  XR_DISPATCH_NEXT();

l_attrib_name:
  state->accumulating = 0;
  go = go_attrib_eq;
  XR_DISPATCH_THIS();

l_attrib_eq:
  go = go_attrib_val_begin;
  XR_DISPATCH_NEXT();

l_attrib_val_single:
  xr_accum_reset(&state->val_accum);
  state->cur_accum = &state->val_accum;
  state->accumulating = 1;
  go = go_attrib_val_single;
  XR_DISPATCH_NEXT();

l_attrib_val_double:
  xr_accum_reset(&state->val_accum);
  state->cur_accum = &state->val_accum;
  state->accumulating = 1;
  go = go_attrib_val_double;
  XR_DISPATCH_NEXT();

l_attrib_val:
  state->accumulating = 0;
  {
    xr_str_t name = {state->name_accum.buf, (int32_t)(state->name_accum.len - 1)};
    xr_str_t val = {state->val_accum.buf, (int32_t)(state->val_accum.len - 1)};
    val.len = (int32_t)xml_decode_entities(state->val_accum.buf, (size_t)val.len);
    cb(xr_type_attribute, &name, &val, user_data);
  }
  go = go_attrib;
  XR_DISPATCH_NEXT();

l_attrib_bool: {
  xr_str_t name = {state->name_accum.buf, (int32_t)(state->name_accum.len - 1)};
  cb(xr_type_attribute, &name, 0, user_data);
}
  go = go_attrib;
  XR_DISPATCH_THIS();
}

void
xr_finish(xr_callback cb, void* user_data, xr_state_t* state) {
  if(state->error || state->done)
    return;

  if(!state->at_root) {
    xr_str_t name = {"Error!", 6};
    xr_str_t val = {"", 0};

    cb(xr_type_error, &name, &val, user_data);
    state->error = 1;
    return;
  }

  state->done = 1;
}
