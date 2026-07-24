/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/jread/blob/master/LICENSE
 */

#include "jread.h"
#include <stdlib.h>
#include <string.h>

/* Pastes __LINE__ into a label name, so each JR_DISPATCH_*() call site gets its own
 * unique "resume here" label - see the comment above JR_DISPATCH_NEXT() below. */
#define JR_PASTE_(a, b) a##b
#define JR_PASTE(a, b) JR_PASTE_(a, b)
#define JR_RESUME_LABEL JR_PASTE(l_resume_, __LINE__)

#define JR_PUSH() (state->go_stack[state->go_stack_idx++] = go)
#define JR_PUSH_GO(x) \
  do { \
    state->go_stack[state->go_stack_idx++] = go; \
    go = (x); \
  } while(0)
#define JR_POP_GO() (go = state->go_stack[--state->go_stack_idx])

/*
 * Reads and dispatches on the next byte of `go` (or `x`/go_utf8, for the _GO/_MASK
 * variants) - exactly like the original goto*go[(uint8_t)*cstr++], except:
 *
 *  - bounds-checked against `end`: if the chunk is exhausted, this saves everything
 *    needed to resume (go, and a label - JR_RESUME_LABEL - pointing at this exact
 *    dispatch site) into `state` and returns, instead of reading past the buffer.
 *    JR_RESUME_LABEL is unique per call site (via __LINE__), and is planted *inside*
 *    the macro expansion, after any one-time setup code the enclosing l_xxx: label may
 *    have (e.g. l_num_s resetting the accumulator) - so resuming via `goto
 *    *state->resume` at the top of jr_read() re-enters past that setup, without
 *    re-running it.
 *  - accumulates: if state->accumulating, the byte just read is appended to
 *    state->accum, since a token's bytes aren't necessarily contiguous in any one
 *    chunk. l_xxx_s labels turn accumulating on (after resetting the accumulator);
 *    l_xxx_e labels turn it off and trim the trailing (terminator) byte that just
 *    got appended by the very dispatch that landed on l_xxx_e.
 */
#define JR_DISPATCH_NEXT() \
  JR_RESUME_LABEL: \
  if(cstr >= end) { \
    state->go = go; \
    state->resume = &&JR_RESUME_LABEL; \
    return; \
  } \
  { \
    uint8_t jr_c__ = (uint8_t)*cstr++; \
    if(state->accumulating) \
      jr_accum_putc(state, (char)jr_c__); \
    goto* go[jr_c__]; \
  }

#define JR_DISPATCH_NEXT_GO(x) \
  JR_RESUME_LABEL: \
  if(cstr >= end) { \
    state->go = go; \
    state->resume = &&JR_RESUME_LABEL; \
    return; \
  } \
  { \
    uint8_t jr_c__ = (uint8_t)*cstr++; \
    if(state->accumulating) \
      jr_accum_putc(state, (char)jr_c__); \
    goto*(x)[jr_c__]; \
  }

#define JR_DISPATCH_NEXT_MASK() \
  JR_RESUME_LABEL: \
  if(cstr >= end) { \
    state->go = go; \
    state->resume = &&JR_RESUME_LABEL; \
    return; \
  } \
  { \
    uint8_t jr_c__ = (uint8_t)*cstr++; \
    if(state->accumulating) \
      jr_accum_putc(state, (char)jr_c__); \
    goto* go_utf8[jr_c__ & state->utf8_mask]; \
  }

/* Re-dispatch on the already-consumed cstr[-1]: no new byte is read, so this can never
 * block on a chunk boundary and needs no resume handling. */
#define JR_DISPATCH_THIS() goto* go[(uint8_t)cstr[-1]];
#define JR_DISPATCH_THIS_GO(x) goto*(x)[(uint8_t)cstr[-1]];

static void
jr_accum_reset(jr_state_t* state) {
  state->accum_len = 0;
}

static void
jr_accum_putc(jr_state_t* state, char c) {
  if(state->accum_len >= state->accum_cap) {
    size_t cap = state->accum_cap ? state->accum_cap * 2 : 64;
    char* p = realloc(state->accum, cap);

    if(!p)
      return; /* OOM: silently truncate rather than crash */

    state->accum = p;
    state->accum_cap = cap;
  }

  state->accum[state->accum_len++] = c;
}

void
jr_state_init(jr_state_t* state) {
  memset(state, 0, sizeof(*state));
}

void
jr_state_free(jr_state_t* state) {
  free(state->accum);
  memset(state, 0, sizeof(*state));
}

void
jr_read(jr_callback cb, const char* chunk, size_t len, void* user_data, jr_state_t* state) {
  static void* go_doc[] = {
      [1 ... 8] = &&l_err,    ['\t'] = &&l_next,       ['\n'] = &&l_next,         [11 ... 12] = &&l_err,   ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,  [' '] = &&l_next,        [0] = &&l_err,             [33 ... 33] = &&l_err,   ['"'] = &&l_str_s,
      [35 ... 44] = &&l_err,  ['-'] = &&l_num_s,       [46 ... 47] = &&l_err,     ['0' ... '9'] = &&l_num_s, [58 ... 90] = &&l_err,
      ['['] = &&l_arr_s,      [92 ... 101] = &&l_err,  ['f'] = &&l_false_f,       [103 ... 109] = &&l_err, ['n'] = &&l_null_n,
      [111 ... 115] = &&l_err, ['t'] = &&l_true_t,     [117 ... 122] = &&l_err,   ['{'] = &&l_obj_s,       [124 ... 255] = &&l_err,
  };

  static void* go_val[] = {
      [0 ... 8] = &&l_err,       ['\t'] = &&l_next,       ['\n'] = &&l_next,  [11 ... 12] = &&l_err,   ['\r'] = &&l_next,   [14 ... 31] = &&l_err,
      [' '] = &&l_next,          [33 ... 33] = &&l_err,   ['"'] = &&l_str_s,  [35 ... 44] = &&l_err,   ['-'] = &&l_num_s,   [46 ... 47] = &&l_err,
      ['0' ... '9'] = &&l_num_s, [58 ... 90] = &&l_err,   ['['] = &&l_arr_s,  [92 ... 101] = &&l_err,  ['f'] = &&l_false_f, [103 ... 109] = &&l_err,
      ['n'] = &&l_null_n,        [111 ... 115] = &&l_err, ['t'] = &&l_true_t, [117 ... 122] = &&l_err, ['{'] = &&l_obj_s,   [124 ... 255] = &&l_err,
  };

  static void* go_num[] = {
      [0 ... 45] = &&l_num_e,
      ['.'] = &&l_next,
      [47 ... 47] = &&l_num_e,
      ['0' ... '9'] = &&l_next,
      [58 ... 255] = &&l_num_e,
  };

  static void* go_str[] = {
      [0 ... 31] = &&l_err,
      [32 ... 33] = &&l_next,
      ['"'] = &&l_str_e,
      [35 ... 91] = &&l_next,
      ['\\'] = &&l_esc,
      [93 ... 126] = &&l_next,
      [127 ... 191] = &&l_err,
      [192 ... 223] = &&l_utf8_2,
      [224 ... 239] = &&l_utf8_3,
      [240 ... 247] = &&l_utf8_4,
      [248 ... 255] = &&l_err,
  };

  static void* go_esc[] = {
      [0 ... 33] = &&l_err,
      ['"'] = &&l_next,
      [35 ... 46] = &&l_err,
      ['/'] = &&l_next,
      [48 ... 91] = &&l_err,
      ['\\'] = &&l_next,
      [93 ... 97] = &&l_err,
      ['b'] = &&l_next,
      [99 ... 101] = &&l_err,
      ['f'] = &&l_next,
      [103 ... 109] = &&l_err,
      ['n'] = &&l_next,
      [111 ... 113] = &&l_err,
      ['r'] = &&l_next,
      [115 ... 115] = &&l_err,
      ['t'] = &&l_next,
      [117 ... 255] = &&l_err,
  };

  static void* go_utf8[] = {
      ['\0'] = &&l_utf8_valid,
      [1 ... 127] = &&l_err,
      [128 ... 191] = &&l_utf8,
      [192 ... 255] = &&l_err,
  };

  static void* go_null_n[] = {
      [0 ... 116] = &&l_err,
      ['u'] = &&l_null_u,
      [118 ... 255] = &&l_err,
  };

  static void* go_null_u[] = {
      [0 ... 107] = &&l_err,
      ['l'] = &&l_null_l,
      [109 ... 255] = &&l_err,
  };

  static void* go_null_l[] = {
      [0 ... 107] = &&l_err,
      ['l'] = &&l_null_ll,
      [109 ... 255] = &&l_err,
  };

  static void* go_true_t[] = {
      [0 ... 113] = &&l_err,
      ['r'] = &&l_true_r,
      [115 ... 255] = &&l_err,
  };

  static void* go_true_r[] = {
      [0 ... 116] = &&l_err,
      ['u'] = &&l_true_u,
      [118 ... 255] = &&l_err,
  };

  static void* go_true_u[] = {
      [0 ... 100] = &&l_err,
      ['e'] = &&l_true_e,
      [102 ... 255] = &&l_err,
  };

  static void* go_false_f[] = {
      [0 ... 96] = &&l_err,
      ['a'] = &&l_false_a,
      [98 ... 255] = &&l_err,
  };

  static void* go_false_a[] = {
      [0 ... 107] = &&l_err,
      ['l'] = &&l_false_l,
      [109 ... 255] = &&l_err,
  };

  static void* go_false_l[] = {
      [0 ... 114] = &&l_err,
      ['s'] = &&l_false_s,
      [116 ... 255] = &&l_err,
  };

  static void* go_false_s[] = {
      [0 ... 100] = &&l_err,
      ['e'] = &&l_false_e,
      [102 ... 255] = &&l_err,
  };

  static void* go_arr[] = {
      [0 ... 8] = &&l_err,     ['\t'] = &&l_next,         ['\n'] = &&l_next,       [11 ... 12] = &&l_err, ['\r'] = &&l_next,       [14 ... 31] = &&l_err,
      [' '] = &&l_next,        [33 ... 33] = &&l_err,     ['"'] = &&l_str_s,       [35 ... 43] = &&l_err, [','] = &&l_next,        ['-'] = &&l_num_s,
      [46 ... 47] = &&l_err,   ['0' ... '9'] = &&l_num_s, [58 ... 90] = &&l_err,   ['['] = &&l_arr_s,     [92 ... 92] = &&l_err,   [']'] = &&l_arr_e,
      [94 ... 101] = &&l_err,  ['f'] = &&l_false_f,       [103 ... 109] = &&l_err, ['n'] = &&l_null_n,    [111 ... 115] = &&l_err, ['t'] = &&l_true_t,
      [117 ... 122] = &&l_err, ['{'] = &&l_obj_s,         [124 ... 255] = &&l_err,
  };

  static void* go_obj[] = {
      [0 ... 8] = &&l_err,
      ['\t'] = &&l_next,
      ['\n'] = &&l_next,
      [11 ... 12] = &&l_err,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,
      [' '] = &&l_next,
      [33 ... 33] = &&l_err,
      ['"'] = &&l_kvp,
      [35 ... 43] = &&l_err,
      [','] = &&l_next,
      [45 ... 124] = &&l_err,
      ['}'] = &&l_obj_e,
      [126 ... 255] = &&l_err,
  };

  static void* go_col[] = {
      [0 ... 8] = &&l_err,
      ['\t'] = &&l_next,
      ['\n'] = &&l_next,
      [11 ... 12] = &&l_err,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,
      [' '] = &&l_next,
      [33 ... 57] = &&l_err,
      [':'] = &&l_col,
      [59 ... 255] = &&l_err,
  };

  static void* go_obj_val[] = {
      [0 ... 8] = &&l_err,
      ['\t'] = &&l_next,
      ['\n'] = &&l_next,
      [11 ... 12] = &&l_err,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,
      [' '] = &&l_next,
      [33 ... 255] = &&l_val,
  };

  const char* cstr = chunk;
  const char* end = chunk + len;
  void** go = state->go ? state->go : go_doc;

  if(state->error || state->done)
    return;

  if(state->resume)
    goto* state->resume;

l_next:
  JR_DISPATCH_NEXT();

l_err:
  {
    jr_str_t data = {cstr - 1, 1};
    cb(jr_type_error, &data, user_data);
  }
  state->error = 1;
  return;

l_num_s:
  jr_accum_reset(state);
  jr_accum_putc(state, cstr[-1]);
  state->accumulating = 1;
  state->in_number = 1;
  JR_PUSH_GO(go_num);
  JR_DISPATCH_NEXT();

l_num_e:
  state->accumulating = 0;
  state->in_number = 0;
  {
    jr_str_t data = {state->accum, (int32_t)(state->accum_len - 1)};
    cb(jr_type_number, &data, user_data);
  }
  JR_POP_GO();
  JR_DISPATCH_THIS();

l_str_s:
  jr_accum_reset(state);
  state->accumulating = 1;
  JR_PUSH_GO(go_str);
  state->str_type = jr_type_string;
  JR_DISPATCH_NEXT();

l_str_e:
  state->accumulating = 0;
  {
    jr_str_t data = {state->accum, (int32_t)(state->accum_len - 1)};
    cb(state->str_type, &data, user_data);
  }
  JR_POP_GO();
  JR_DISPATCH_NEXT();

l_esc:
  JR_DISPATCH_NEXT_GO(go_esc);

l_utf8:
  state->utf8_mask >>= 8;
  JR_DISPATCH_NEXT_MASK();

l_utf8_2:
  state->utf8_mask = 0x000000FF;
  JR_DISPATCH_NEXT_GO(go_utf8);

l_utf8_3:
  state->utf8_mask = 0x0000FFFF;
  JR_DISPATCH_NEXT_GO(go_utf8);

l_utf8_4:
  state->utf8_mask = 0x00FFFFFF;
  JR_DISPATCH_NEXT_GO(go_utf8);

l_utf8_valid:
  JR_DISPATCH_THIS();

l_null_n:
  JR_PUSH();
  JR_DISPATCH_NEXT_GO(go_null_n);

l_null_u:
  JR_DISPATCH_NEXT_GO(go_null_u);

l_null_l:
  JR_DISPATCH_NEXT_GO(go_null_l);

l_null_ll:
  cb(jr_type_null, 0, user_data);
  JR_POP_GO();
  JR_DISPATCH_NEXT();

l_true_t:
  JR_PUSH();
  JR_DISPATCH_NEXT_GO(go_true_t);

l_true_r:
  JR_DISPATCH_NEXT_GO(go_true_r);

l_true_u:
  JR_DISPATCH_NEXT_GO(go_true_u);

l_true_e:
  cb(jr_type_true, 0, user_data);
  JR_POP_GO();
  JR_DISPATCH_NEXT();

l_false_f:
  JR_PUSH();
  JR_DISPATCH_NEXT_GO(go_false_f);

l_false_a:
  JR_DISPATCH_NEXT_GO(go_false_a);

l_false_l:
  JR_DISPATCH_NEXT_GO(go_false_l);

l_false_s:
  JR_DISPATCH_NEXT_GO(go_false_s);

l_false_e:
  cb(jr_type_false, 0, user_data);
  JR_POP_GO();
  JR_DISPATCH_NEXT();

l_arr_s:
  cb(jr_type_array_start, 0, user_data);
  JR_PUSH_GO(go_arr);
  JR_DISPATCH_NEXT();

l_arr_e:
  cb(jr_type_array_end, 0, user_data);
  JR_POP_GO();
  JR_DISPATCH_NEXT();

l_obj_s:
  cb(jr_type_object_start, 0, user_data);
  JR_PUSH_GO(go_obj);
  JR_DISPATCH_NEXT();

l_obj_e:
  cb(jr_type_object_end, 0, user_data);
  JR_POP_GO();
  JR_DISPATCH_NEXT();

l_kvp:
  jr_accum_reset(state);
  state->accumulating = 1;
  JR_PUSH_GO(go_obj_val);
  JR_PUSH_GO(go_col);
  JR_PUSH_GO(go_str);
  state->str_type = jr_type_key;
  JR_DISPATCH_NEXT();

l_val:
  JR_POP_GO();
  JR_DISPATCH_THIS_GO(go_val);

l_col:
  JR_POP_GO();
  JR_DISPATCH_NEXT();
}

void
jr_finish(jr_callback cb, void* user_data, jr_state_t* state) {
  if(state->error || state->done)
    return;

  if(state->in_number) {
    jr_str_t data = {state->accum, (int32_t)state->accum_len};

    cb(jr_type_number, &data, user_data);
    state->in_number = 0;
    state->accumulating = 0;

    if(state->go_stack_idx > 0)
      state->go = state->go_stack[--state->go_stack_idx];
  }

  if(state->go_stack_idx > 0 || state->accumulating) {
    jr_str_t data = {0, 0};

    cb(jr_type_error, &data, user_data);
    state->error = 1;
    return;
  }

  state->done = 1;
}
