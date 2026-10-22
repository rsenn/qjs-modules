/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/jread/blob/master/LICENSE
 */

#include "char-utils.h" /* is_utf16_high_surrogate/is_utf16_low_surrogate, unicode_to_utf8, UTF8_CHAR_LEN_MAX */
#include "json/jread.h"
#include <stdlib.h>

typedef struct jr_str {
  const char* cstr;
  int32_t len;
} jr_str_t;

typedef void (*jr_callback)(JsonEventType type, const jr_str_t* data, void* user_data);

/**
 * \addtogroup jread
 * @{
 */

/* Pastes __LINE__ into a label name, so each JR_DISPATCH_*() call site gets its own
 * unique "resume here" label - see the comment above JR_DISPATCH_NEXT() below. */
#define JR_PASTE_(a, b) a##b
#define JR_PASTE(a, b) JR_PASTE_(a, b)
#define JR_RESUME_LABEL JR_PASTE(l_resume_, __LINE__)

#define JR_PUSH() \
  do { \
    if(!jr_push(state, go)) \
      goto l_oom; \
  } while(0)
#define JR_PUSH_GO(x) \
  do { \
    JR_PUSH(); \
    go = (x); \
  } while(0)
#define JR_POP_GO() (go = jr_pop(state))

/* Like JR_POP_GO(), but for the "a token just fully ended" call sites specifically: if what
 * gets restored is a container's own "expect a value or close" table (go_arr/go_obj), swap
 * it for that container's "expect a comma or close" counterpart (go_arr_sep/go_obj_sep) -
 * so the next byte can't start a fresh value without a separating comma. Popping into
 * anything else (go_doc/go_val: true top level, or go_col: a key was just read, colon comes
 * next) is left untouched - deliberately, since consecutive top-level values with no comma
 * between them is the supported NDJSON/JSON-Lines streaming pattern. */
#define JR_POP_GO_SEP() \
  do { \
    JR_POP_GO(); \
    if(go == go_arr) \
      go = go_arr_sep; \
    else if(go == go_obj) \
      go = go_obj_sep; \
    else if(go == go_doc) { \
      state->roots++; \
      if(!(state->flags & (JR_MULTI | JR_LEGACY))) \
        go = go_end; \
    } \
  } while(0)

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
 *    *state->resume` at the top of jr_feed() re-enters past that setup, without
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
    state->tab = 0; \
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
    state->tab = (x); \
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
    state->tab = go_utf8; \
    goto* go_utf8[jr_c__ & state->utf8_mask]; \
  }

/* Re-dispatch on the already-consumed cstr[-1]: no new byte is read, so this can never
 * block on a chunk boundary and needs no resume handling. */
#define JR_DISPATCH_THIS() goto* go[(uint8_t)cstr[-1]];
#define JR_DISPATCH_THIS_GO(x) goto*(x)[(uint8_t)cstr[-1]];

static void
jr_accum_reset(jr_state_t* state) {
  state->accum_len = 0;
  state->esc_hi = 0;
  state->surrogate_hi = 0;
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

static int
jr_push(jr_state_t* state, void** go) {
  return vector_push(&state->go_stack, go) != 0;
}

static void**
jr_pop(jr_state_t* state) {
  return *(void***)vector_pop(&state->go_stack, sizeof(void**));
}

void
jr_state_init(jr_state_t* state) {
  memset(state, 0, sizeof(*state));
  state->go_stack = (Vector)VECTOR_INIT();
  srcpos_init(&state->pos, 0);
  state->tok = state->err_pos = state->pos;
  state->flags = JR_LEGACY;
}

/* jr_state_init_flags: jr_state_init() with the syntax `flags` (JR_JSON, JR_JSON5, ...). */
void
jr_state_init_flags(jr_state_t* state, uint8_t flags) {
  jr_state_init(state);
  state->flags = flags;
}

const char*
jr_error_name(jr_error_t code) {
  static const char* const names[] = {
      "", "unexpected-token", "bad-escape", "bad-unicode-escape", "invalid-utf8", "control-character", "invalid-literal", "unexpected-end", "out-of-memory", "trailing-data",
  };

  return (unsigned)code < sizeof(names) / sizeof(names[0]) ? names[code] : "";
}

/* advances `pos` over the chunk bytes in [mark, p). */
static void
jr_sync(jr_state_t* state, const char* p) {
  srcpos_advance(&state->pos, state->mark, p - state->mark);
  state->mark = p;
}

/* a token begins at the byte just read (p[-1]). */
#define JR_TOKEN_START() \
  do { \
    jr_sync(state, cstr - 1); \
    state->tok = state->pos; \
    state->after_comma = 0; \
  } while(0)

/* the byte just read (p[-1]) is a token by itself: [ ] { } */
#define JR_TOKEN_MARK() \
  do { \
    jr_sync(state, cstr - 1); \
    state->tok = state->pos; \
  } while(0)

/* records the error at the byte just read: the token start when inside a token. */
static void
jr_error_at(jr_state_t* state, const char* p, jr_error_t code, int in_token) {
  SourcePos at = state->pos;

  srcpos_advance(&at, state->mark, p - state->mark);

  state->err_code = code;
  state->err_pos = in_token ? state->tok : at;
  state->err_len = at.byte_offset + 1 - state->err_pos.byte_offset;
}

void
jr_state_free(jr_state_t* state) {
  free(state->accum);
  vector_free(&state->go_stack);
  memset(state, 0, sizeof(*state));
}

static void
jr_read_(jr_callback cb, const char* chunk, size_t len, void* user_data, jr_state_t* state) {
  static void* go_doc[] = {
      [0 ... 8] = &&l_err,
      [9 ... 10] = &&l_next,
      [11 ... 12] = &&l_ws5,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,
      [' '] = &&l_next,
      ['!'] = &&l_err,
      ['"'] = &&l_str_s,
      [35 ... 38] = &&l_err,
      ['\''] = &&l_str_sq_s,
      [40 ... 42] = &&l_err,
      ['+'] = &&l_num_s5,
      [','] = &&l_err,
      ['-'] = &&l_num_s,
      ['.'] = &&l_num_s5,
      ['/'] = &&l_cmt,
      ['0' ... '9'] = &&l_num_s,
      [58 ... 72] = &&l_err,
      ['I'] = &&l_kw5_s,
      ['J' ... 'M'] = &&l_err,
      ['N'] = &&l_kw5_s, /* JSON5, gated by flags */
      ['O' ... 'Z'] = &&l_err,
      ['['] = &&l_arr_s,
      [92 ... 101] = &&l_err,
      ['f'] = &&l_false_f,
      ['g' ... 'm'] = &&l_err,
      ['n'] = &&l_null_n,
      ['o' ... 's'] = &&l_err,
      ['t'] = &&l_true_t,
      ['u' ... 'z'] = &&l_err,
      ['{'] = &&l_obj_s,
      [124 ... 255] = &&l_err,
  };

  static void* go_val[] = {
      [0 ... 8] = &&l_err,
      [9 ... 10] = &&l_next,
      [11 ... 12] = &&l_ws5,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,
      [' '] = &&l_next,
      ['!'] = &&l_err,
      ['"'] = &&l_str_s,
      [35 ... 38] = &&l_err,
      ['\''] = &&l_str_sq_s,
      [40 ... 42] = &&l_err,
      ['+'] = &&l_num_s5,
      [','] = &&l_err,
      ['-'] = &&l_num_s,
      ['.'] = &&l_num_s5,
      ['/'] = &&l_cmt,
      ['0' ... '9'] = &&l_num_s,
      [58 ... 72] = &&l_err,
      ['I'] = &&l_kw5_s,
      ['J' ... 'M'] = &&l_err,
      ['N'] = &&l_kw5_s, /* JSON5, gated by flags */
      ['O' ... 'Z'] = &&l_err,
      ['['] = &&l_arr_s,
      [92 ... 101] = &&l_err,
      ['f'] = &&l_false_f,
      ['g' ... 'm'] = &&l_err,
      ['n'] = &&l_null_n,
      ['o' ... 's'] = &&l_err,
      ['t'] = &&l_true_t,
      ['u' ... 'z'] = &&l_err,
      ['{'] = &&l_obj_s,
      [124 ... 255] = &&l_err,
  };

  static void* go_num[] = {
      [0 ... 45] = &&l_num_e,
      ['.'] = &&l_next,
      ['/'] = &&l_num_e,
      ['0' ... '9'] = &&l_next,
      [58 ... 68] = &&l_num_e,
      ['E'] = &&l_num_exp,
      ['F' ... 'd'] = &&l_num_e,
      ['e'] = &&l_num_exp,
      [102 ... 255] = &&l_num_e,
  };

  /* One-shot: the single byte right after 'e'/'E' may be a sign or must be a digit -
   * anything else ends the number (a bare trailing "1e" is invalid JSON, but this parser
   * is already lax elsewhere - e.g. multiple '.'s - so it's simpler and consistent to just
   * end the number early rather than add a new error path for it). Whichever of these
   * matches, dispatch continues through the *stored* `go` (still go_num, untouched by this
   * one-shot table) from the next byte on, so further exponent digits are accepted the same
   * way integer/fraction digits already are. */
  static void* go_num_exp_sign[] = {
      [0 ... 42] = &&l_num_e,
      ['+'] = &&l_next,
      [','] = &&l_num_e,
      ['-'] = &&l_next,
      [46 ... 47] = &&l_num_e,
      ['0' ... '9'] = &&l_next,
      [58 ... 255] = &&l_num_e,
  };

  static void* go_str[] = {
      [0 ... 31] = &&l_str_ctl,
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

  /* Each valid escape lands on its own label instead of a shared l_next: the raw "\X" pair
   * was already auto-accumulated by the dispatches that got here (l_esc, then this table),
   * and it's not what the decoded string should contain - each label backs that out and
   * appends the real decoded byte instead. See l_esc_quote et al. below. */
  static void* go_esc[] = {
      [0 ... 33] = &&l_err,
      ['"'] = &&l_esc_quote,
      [35 ... 46] = &&l_err,
      ['/'] = &&l_esc_slash,
      [48 ... 91] = &&l_err,
      ['\\'] = &&l_esc_bslash,
      [93 ... 97] = &&l_err,
      ['b'] = &&l_esc_b,
      ['c' ... 'e'] = &&l_err,
      ['f'] = &&l_esc_f,
      ['g' ... 'm'] = &&l_err,
      ['n'] = &&l_esc_n,
      ['o' ... 'q'] = &&l_err,
      ['r'] = &&l_esc_r,
      ['s'] = &&l_err,
      ['t'] = &&l_esc_t,
      ['u'] = &&l_esc_u,
      [118 ... 255] = &&l_err,
  };

  /* \uXXXX: reused for all 4 hex-digit positions via state->unicode_count, since they're
   * all validated the same way - only the 4th one (checked in l_esc_u_digit) finishes. */
  static void* go_esc_u[] = {
      [0 ... 47] = &&l_err,
      ['0' ... '9'] = &&l_esc_u_digit,
      [58 ... 64] = &&l_err,
      ['A' ... 'F'] = &&l_esc_u_digit,
      [71 ... 96] = &&l_err,
      ['a' ... 'f'] = &&l_esc_u_digit,
      [103 ... 255] = &&l_err,
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
      [0 ... 8] = &&l_err,
      [9 ... 10] = &&l_next,
      [11 ... 12] = &&l_ws5,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,
      [' '] = &&l_next,
      ['!'] = &&l_err,
      ['"'] = &&l_str_s,
      [35 ... 38] = &&l_err,
      ['\''] = &&l_str_sq_s,
      [40 ... 42] = &&l_err,
      ['+'] = &&l_num_s5,
      [','] = &&l_comma_stray,
      ['-'] = &&l_num_s,
      ['.'] = &&l_num_s5,
      ['/'] = &&l_cmt,
      ['0' ... '9'] = &&l_num_s,
      [58 ... 72] = &&l_err,
      ['I'] = &&l_kw5_s,
      ['J' ... 'M'] = &&l_err,
      ['N'] = &&l_kw5_s, /* JSON5, gated by flags */
      ['O' ... 'Z'] = &&l_err,
      ['['] = &&l_arr_s,
      ['\\'] = &&l_err,
      [']'] = &&l_arr_e,
      [94 ... 101] = &&l_err,
      ['f'] = &&l_false_f,
      ['g' ... 'm'] = &&l_err,
      ['n'] = &&l_null_n,
      ['o' ... 's'] = &&l_err,
      ['t'] = &&l_true_t,
      ['u' ... 'z'] = &&l_err,
      ['{'] = &&l_obj_s,
      [124 ... 255] = &&l_err,
  };

  static void* go_obj[] = {
      [0 ... 8] = &&l_err,
      [9 ... 10] = &&l_next,
      [11 ... 12] = &&l_ws5,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,
      [' '] = &&l_next,
      ['!'] = &&l_err,
      ['"'] = &&l_kvp,
      ['#'] = &&l_err,
      ['$'] = &&l_key_id,
      [37 ... 38] = &&l_err,
      ['\''] = &&l_kvp_sq,
      [40 ... 43] = &&l_err,
      [','] = &&l_comma_stray,
      [45 ... 46] = &&l_err,
      ['/'] = &&l_cmt,
      [48 ... 64] = &&l_err,
      ['A' ... 'Z'] = &&l_key_id,
      [91 ... 94] = &&l_err,
      ['_'] = &&l_key_id,
      ['`'] = &&l_err,
      ['a' ... 'z'] = &&l_key_id,
      [123 ... 124] = &&l_err,
      ['}'] = &&l_obj_e,
      [126 ... 191] = &&l_err,
      [192 ... 247] = &&l_key_id, /* JSON5 keys, gated by flags */
      [248 ... 255] = &&l_err,
  };

  /* Used only right after a value fully ends (see JR_POP_GO_SEP()) - unlike go_arr, which
   * doubles as both "expect value or close" (right after '[' or ',') and, historically,
   * "expect comma or close" too (letting a fresh value start with no separating comma -
   * see BUGS's now-fixed json-parsers-dont-require-commas-between-elements). Only a comma
   * (back to expecting a value - see l_arr_sep_comma) or the closing ']' are legal here. */
  static void* go_arr_sep[] = {
      [0 ... 8] = &&l_err,
      [9 ... 10] = &&l_next,
      [11 ... 12] = &&l_ws5,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,
      [' '] = &&l_next,
      [33 ... 43] = &&l_err,
      [','] = &&l_arr_sep_comma,
      [45 ... 46] = &&l_err,
      ['/'] = &&l_cmt,
      [48 ... 92] = &&l_err,
      [']'] = &&l_arr_e,
      [94 ... 255] = &&l_err,
  };

  /* Same idea as go_arr_sep, for objects: right after a member's value ends, only a comma
   * (back to expecting the next key - l_obj_sep_comma) or the closing '}' are legal. */
  static void* go_obj_sep[] = {
      [0 ... 8] = &&l_err,
      [9 ... 10] = &&l_next,
      [11 ... 12] = &&l_ws5,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,
      [' '] = &&l_next,
      [33 ... 43] = &&l_err,
      [','] = &&l_obj_sep_comma,
      [45 ... 46] = &&l_err,
      ['/'] = &&l_cmt,
      [48 ... 124] = &&l_err,
      ['}'] = &&l_obj_e,
      [126 ... 255] = &&l_err,
  };

  static void* go_col[] = {
      [0 ... 8] = &&l_err,
      [9 ... 10] = &&l_next,
      [11 ... 12] = &&l_ws5,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,
      [' '] = &&l_next,
      [33 ... 46] = &&l_err,
      ['/'] = &&l_cmt,
      ['0' ... '9'] = &&l_err,
      [':'] = &&l_col,
      [59 ... 255] = &&l_err,
  };

  static void* go_obj_val[] = {
      [0 ... 8] = &&l_err,
      [9 ... 10] = &&l_next,
      [11 ... 12] = &&l_ws5,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,
      [' '] = &&l_next,
      [33 ... 46] = &&l_val,
      ['/'] = &&l_cmt,
      [48 ... 255] = &&l_val,
  };

  /* Post-error recovery: every byte is discarded (stays here) except a comma or a closing
   * bracket/brace, which is handed to l_err_resync_boundary instead - see the comment on
   * l_err below for why redispatching that byte through the recovered container-level `go`
   * (not this table) is what actually resumes parsing. */
  static void* go_err_resync[] = {
      [0 ... 43] = &&l_err_resync_skip,
      [','] = &&l_err_resync_boundary,
      [45 ... 92] = &&l_err_resync_skip,
      [']'] = &&l_err_resync_boundary,
      [94 ... 124] = &&l_err_resync_skip,
      ['}'] = &&l_err_resync_boundary,
      [126 ... 255] = &&l_err_resync_skip,
  };

  /* comments (C++ line and C block style) are skipped wherever whitespace is:
   * `go` stays untouched, these tables just eat bytes until the comment ends. */
  static void* go_cmt_start[] = {
      [0 ... 41] = &&l_err,
      ['*'] = &&l_cmt_block,
      [43 ... 46] = &&l_err,
      ['/'] = &&l_cmt_line,
      [48 ... 255] = &&l_err,
  };

  static void* go_cmt_line[] = {
      [0 ... 9] = &&l_cmt_line,
      ['\n'] = &&l_cmt_end,
      [11 ... 255] = &&l_cmt_line,
  };

  static void* go_cmt_block[] = {
      [0 ... 41] = &&l_cmt_block,
      ['*'] = &&l_cmt_star,
      [43 ... 255] = &&l_cmt_block,
  };

  static void* go_cmt_star[] = {
      [0 ... 41] = &&l_cmt_block,
      ['*'] = &&l_cmt_star,
      [43 ... 46] = &&l_cmt_block,
      ['/'] = &&l_cmt_end,
      [48 ... 255] = &&l_cmt_block,
  };

  /* the single-quoted twin of go_str */
  static void* go_str_sq[] = {
      [0 ... 31] = &&l_str_ctl,
      [32 ... 38] = &&l_next,
      ['\''] = &&l_str_e,
      [40 ... 91] = &&l_next,
      ['\\'] = &&l_esc,
      [93 ... 126] = &&l_next,
      [127 ... 191] = &&l_err,
      [192 ... 223] = &&l_utf8_2,
      [224 ... 239] = &&l_utf8_3,
      [240 ... 247] = &&l_utf8_4,
      [248 ... 255] = &&l_err,
  };

  /* JSON5 escapes: \' \v \0 \xHH, a line terminator continues the string, and any other
   * character stands for itself; \1 to \9 stay errors */
  static void* go_esc5[] = {
      [0 ... 9] = &&l_esc_id,
      ['\n'] = &&l_esc_cont,
      [11 ... 12] = &&l_esc_id,
      ['\r'] = &&l_esc_cont_cr,
      [14 ... 33] = &&l_esc_id,
      ['"'] = &&l_esc_quote,
      [35 ... 46] = &&l_esc_id,
      ['/'] = &&l_esc_slash,
      ['0'] = &&l_esc_nul,
      ['1' ... '9'] = &&l_err,
      [58 ... 91] = &&l_esc_id,
      ['\\'] = &&l_esc_bslash,
      [93 ... 97] = &&l_esc_id,
      ['b'] = &&l_esc_b,
      ['c' ... 'e'] = &&l_esc_id,
      ['f'] = &&l_esc_f,
      ['g' ... 'm'] = &&l_esc_id,
      ['n'] = &&l_esc_n,
      ['o' ... 'q'] = &&l_esc_id,
      ['r'] = &&l_esc_r,
      ['s'] = &&l_esc_id,
      ['t'] = &&l_esc_t,
      ['u'] = &&l_esc_u,
      ['v'] = &&l_esc_v,
      ['w'] = &&l_esc_id,
      ['x'] = &&l_esc_x,
      [121 ... 127] = &&l_esc_id,
      [128 ... 191] = &&l_err,
      [192 ... 247] = &&l_esc_hi,
      [248 ... 255] = &&l_err,
  };

  /* after backslash 0: a digit must not follow */
  static void* go_esc_nul[] = {
      [0 ... 47] = &&l_esc_cr_other,
      ['0' ... '9'] = &&l_err,
      [58 ... 255] = &&l_esc_cr_other,
  };

  /* after a backslash and a CR: an LF belongs to the line terminator */
  static void* go_esc_cr[] = {
      [0 ... 9] = &&l_esc_cr_other,
      ['\n'] = &&l_esc_cr_lf,
      [11 ... 255] = &&l_esc_cr_other,
  };

  /* an unquoted key: identifier characters up to a colon, whitespace or comment */
  static void* go_ident[] = {
      [0 ... 8] = &&l_err,
      [9 ... 13] = &&l_key_id_e,
      [14 ... 31] = &&l_err,
      [' '] = &&l_key_id_e,
      [33 ... 35] = &&l_err,
      ['$'] = &&l_next,
      [37 ... 46] = &&l_err,
      ['/'] = &&l_key_id_e,
      ['0' ... '9'] = &&l_next,
      [':'] = &&l_key_id_e,
      [59 ... 64] = &&l_err,
      ['A' ... 'Z'] = &&l_next,
      [91 ... 94] = &&l_err,
      ['_'] = &&l_next,
      ['`'] = &&l_err,
      ['a' ... 'z'] = &&l_next,
      [123 ... 191] = &&l_err,
      [192 ... 223] = &&l_utf8_2,
      [224 ... 239] = &&l_utf8_3,
      [240 ... 247] = &&l_utf8_4,
      [248 ... 255] = &&l_err,
  };

  /* after the last token: whitespace and comments only */
  static void* go_end[] = {
      [0 ... 8] = &&l_err,
      [9 ... 10] = &&l_next,
      [11 ... 12] = &&l_ws5,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,
      [' '] = &&l_next,
      [33 ... 46] = &&l_err,
      ['/'] = &&l_cmt,
      [48 ... 255] = &&l_err,
  };

  /* numbers outside JR_LEGACY: one table per state of the grammar; a table that ends
   * the number sends every other byte to l_num_e, the others report it */
  static void* go_nn_idle[] = {
      [0 ... 255] = &&l_err,
  };

  static void* go_nn_first[] = {
      [0 ... 42] = &&l_err,
      ['+'] = &&l_nn_sign,
      [','] = &&l_err,
      ['-'] = &&l_nn_sign,
      ['.'] = &&l_nn_lead_dot,
      ['/'] = &&l_err,
      ['0'] = &&l_nn_zero,
      ['1' ... '9'] = &&l_nn_int,
      [58 ... 255] = &&l_err,
  };

  static void* go_nn_sign[] = {
      [0 ... 45] = &&l_err,
      ['.'] = &&l_nn_lead_dot5,
      ['/'] = &&l_err,
      ['0'] = &&l_nn_zero,
      ['1' ... '9'] = &&l_nn_int,
      [58 ... 72] = &&l_err,
      ['I'] = &&l_kw5_sign,
      ['J' ... 'M'] = &&l_err,
      ['N'] = &&l_kw5_sign,
      [79 ... 255] = &&l_err,
  };

  static void* go_nn_zero[] = {
      [0 ... 45] = &&l_num_e,
      ['.'] = &&l_nn_dot,
      ['/'] = &&l_num_e,
      ['0' ... '9'] = &&l_err,
      [58 ... 68] = &&l_num_e,
      ['E'] = &&l_nn_exp,
      ['F' ... 'W'] = &&l_num_e,
      ['X'] = &&l_nn_hex_start5,
      ['Y' ... 'd'] = &&l_num_e,
      ['e'] = &&l_nn_exp,
      ['f' ... 'w'] = &&l_num_e,
      ['x'] = &&l_nn_hex_start5,
      [121 ... 255] = &&l_num_e,
  };

  static void* go_nn_int[] = {
      [0 ... 45] = &&l_num_e,
      ['.'] = &&l_nn_dot,
      ['/'] = &&l_num_e,
      ['0' ... '9'] = &&l_nn_int,
      [58 ... 68] = &&l_num_e,
      ['E'] = &&l_nn_exp,
      ['F' ... 'd'] = &&l_num_e,
      ['e'] = &&l_nn_exp,
      [102 ... 255] = &&l_num_e,
  };

  /* "1.": fine in JSON5 only without a digit after it */
  static void* go_nn_dot[] = {
      [0 ... 47] = &&l_nn_dot_end,
      ['0' ... '9'] = &&l_nn_frac,
      [58 ... 68] = &&l_nn_dot_end,
      ['E'] = &&l_nn_dot_exp,
      ['F' ... 'd'] = &&l_nn_dot_end,
      ['e'] = &&l_nn_dot_exp,
      [102 ... 255] = &&l_nn_dot_end,
  };

  static void* go_nn_lead_dot[] = {
      [0 ... 47] = &&l_err,
      ['0' ... '9'] = &&l_nn_frac,
      [58 ... 255] = &&l_err,
  };

  static void* go_nn_frac[] = {
      [0 ... 47] = &&l_num_e,
      ['0' ... '9'] = &&l_nn_frac,
      [58 ... 68] = &&l_num_e,
      ['E'] = &&l_nn_exp,
      ['F' ... 'd'] = &&l_num_e,
      ['e'] = &&l_nn_exp,
      [102 ... 255] = &&l_num_e,
  };

  static void* go_nn_exp[] = {
      [0 ... 42] = &&l_err,
      ['+'] = &&l_nn_exp_sign,
      [','] = &&l_err,
      ['-'] = &&l_nn_exp_sign,
      [46 ... 47] = &&l_err,
      ['0' ... '9'] = &&l_nn_exp_digits,
      [58 ... 255] = &&l_err,
  };

  static void* go_nn_exp_sign[] = {
      [0 ... 47] = &&l_err,
      ['0' ... '9'] = &&l_nn_exp_digits,
      [58 ... 255] = &&l_err,
  };

  static void* go_nn_exp_digits[] = {
      [0 ... 47] = &&l_num_e,
      ['0' ... '9'] = &&l_nn_exp_digits,
      [58 ... 255] = &&l_num_e,
  };

  static void* go_nn_hex_start[] = {
      [0 ... 47] = &&l_err,
      ['0' ... '9'] = &&l_nn_hex,
      [58 ... 64] = &&l_err,
      ['A' ... 'F'] = &&l_nn_hex,
      [71 ... 96] = &&l_err,
      ['a' ... 'f'] = &&l_nn_hex,
      [103 ... 255] = &&l_err,
  };

  static void* go_nn_hex[] = {
      [0 ... 47] = &&l_num_e,
      ['0' ... '9'] = &&l_nn_hex,
      [58 ... 64] = &&l_num_e,
      ['A' ... 'F'] = &&l_nn_hex,
      [71 ... 96] = &&l_num_e,
      ['a' ... 'f'] = &&l_nn_hex,
      [103 ... 255] = &&l_num_e,
  };

  /* Infinity and NaN are matched byte by byte against state->kw */
  static void* go_kw[] = {
      [0 ... 255] = &&l_kw_step,
  };

  static void* go_nn_end[] = {
      [0 ... 255] = &&l_num_e,
  };

  const char* cstr = chunk;
  const char* end = chunk + len;
  void** go = state->go ? state->go : go_doc;
  unsigned was_number; /* l_err: the error hit inside a number or Infinity / NaN */

  if(state->done)
    return;

  state->just_erred = 0;

  if(state->resume)
    goto * state->resume;

l_next:
  JR_DISPATCH_NEXT();

l_err: {
  uint8_t c = (uint8_t)cstr[-1];

  was_number = state->in_number || state->kw_active;
  jr_error_t code = JR_E_UNEXPECTED_TOKEN;
  jr_str_t data = {cstr - 1, 1};

  if(state->tab == go_esc || state->tab == go_esc5 || state->tab == go_esc_nul)
    code = JR_E_BAD_ESCAPE;
  else if(state->tab == go_esc_u)
    code = JR_E_BAD_UNICODE_ESCAPE;
  else if(state->tab == go_utf8)
    code = JR_E_INVALID_UTF8;
  else if(state->literal_active || state->kw_active)
    code = JR_E_INVALID_LITERAL;
  else if(!state->tab && (go == go_str || go == go_str_sq))
    code = c < 32 ? JR_E_CONTROL_CHARACTER : JR_E_INVALID_UTF8;
  else if(go == go_end)
    code = JR_E_TRAILING_DATA;

  jr_error_at(state, cstr - 1, code, state->accumulating || state->literal_active || state->kw_active);
  cb(JSON_EVENT_ERROR, &data, user_data);
}
  /* `error` is sticky (jr_end()/close() report "this stream had an error at some point");
   * `just_erred` is reset at the top of every jr_feed() call and only reflects THIS call, so
   * js_json_pushparser_write() can throw once per bad span instead of on every future write()
   * to an already-resynced parser. */
  state->error = 1;
  state->just_erred = 1;
  state->accumulating = 0; /* abandon whatever partial token was mid-scan */
  state->in_number = 0;
  state->kw_active = 0;
  state->cmt = 0;
  state->after_comma = 0;

  /* Recover the dispatch table for the container we were actually inside (go_arr/go_obj/
   * go_arr_sep/go_obj_sep, or go_doc/go_val at the top level) so the resync boundary below
   * can be handed back to it, instead of duplicating what it already knows how to do with
   * a comma or a closer. `go` only ever drifts away from that table while inside a string
   * (go_str, plus go_esc/go_utf8* which are one-shot dispatches that never change `go`
   * itself) or mid-way through an object's "key: value" phases (go_col, go_obj_val) - both
   * cases leave the container's own table sitting further down go_stack, put there by the
   * same JR_PUSH_GO() that got us here. A literal in progress (go_null_n, go_true_t,
   * go_false_f, etc.) never changes `go` at all - see the literal_active handling below for
   * its own (differently-shaped) cleanup. go_arr_sep/go_obj_sep themselves need no recovery
   * at all - an error reached directly from them (e.g. a missing comma) already has `go`
   * sitting at container level, just in the "expect separator" phase instead of "expect
   * value" - popping here would incorrectly discard it. */
  while(go != go_doc && go != go_val && go != go_arr && go != go_obj && go != go_arr_sep && go != go_obj_sep && go != go_end && !vector_empty(&state->go_stack))
    JR_POP_GO();

  if(state->literal_active) {
    /* l_null_n/l_true_t/l_false_f push `go` (already container-level) onto go_stack purely
     * for symmetry with l_null_ll/l_true_e/l_false_e's matching pop on a *successful*
     * literal - erroring out mid-literal skips that pop, so it must happen here instead, or
     * this stale entry corrupts the level a future real container-close pops into. */
    JR_POP_GO();
    state->literal_active = 0;
  }

  if(!(state->flags & JR_LEGACY)) {
    /* a bad item still was an item: what follows it is a separator or a closer */
    if(was_number || cstr[-1] != ',') {
      if(go == go_arr)
        go = go_arr_sep;
      else if(go == go_obj)
        go = go_obj_sep;
    }

    /* "[1.]": the byte that ended a bad number is one of those, so it is read again, not skipped */
    if(was_number && (cstr[-1] == ',' || cstr[-1] == ']' || cstr[-1] == '}'))
      JR_DISPATCH_THIS();
  }

  JR_DISPATCH_NEXT_GO(go_err_resync);

l_err_resync_skip:
  JR_DISPATCH_NEXT_GO(go_err_resync);

l_err_resync_boundary:
  /* `go` was recovered above to the container we were actually inside when the error hit -
   * redispatching the comma/closer through it does exactly what a well-formed comma or
   * closer would: skip to the next element, or run l_arr_e/l_obj_e to close the container
   * (json->stack pop and all), keeping this file as the single place that knows how. */
  JR_DISPATCH_THIS();

l_cmt:
  if(!(state->flags & (JR_COMMENTS | JR_LEGACY)))
    goto l_err;
  state->cmt = 1;

  if(state->comment_events) {
    jr_sync(state, cstr - 1);
    state->tok = state->pos;
    jr_accum_reset(state);
    jr_accum_putc(state, '/');
    state->accumulating = 1;
  }

  JR_DISPATCH_NEXT_GO(go_cmt_start);

l_cmt_line:
  state->cmt = 2;
  JR_DISPATCH_NEXT_GO(go_cmt_line);

l_cmt_block:
  state->cmt = 3;
  JR_DISPATCH_NEXT_GO(go_cmt_block);

l_cmt_end:
  if(state->comment_events) {
    /* a line comment got its newline appended, it is not part of the text */
    jr_str_t data = {state->accum, (int32_t)(state->accum_len - (state->cmt == 2))};

    state->accumulating = 0;
    cb(JSON_EVENT_COMMENT, &data, user_data);
  }

  state->cmt = 0;
  JR_DISPATCH_NEXT();

l_cmt_star:
  JR_DISPATCH_NEXT_GO(go_cmt_star);

l_num_s:
  JR_TOKEN_START();
  jr_accum_reset(state);
  jr_accum_putc(state, cstr[-1]);
  state->accumulating = 1;
  state->in_number = 1;

  if(!(state->flags & JR_LEGACY)) {
    state->num_ok = 0;
    JR_PUSH_GO(go_nn_idle);
    JR_DISPATCH_THIS_GO(go_nn_first);
  }

  JR_PUSH_GO(go_num);
  JR_DISPATCH_NEXT();

l_num_s5:
  if(!(state->flags & JR_JSON5_NUMBERS))
    goto l_err;
  goto l_num_s;

l_nn_sign:
  state->num_ok = 0;
  JR_DISPATCH_NEXT_GO(go_nn_sign);

l_nn_zero:
  state->num_ok = 1;
  JR_DISPATCH_NEXT_GO(go_nn_zero);

l_nn_int:
  state->num_ok = 1;
  JR_DISPATCH_NEXT_GO(go_nn_int);

l_nn_lead_dot5:
  if(!(state->flags & JR_JSON5_NUMBERS))
    goto l_err;

l_nn_lead_dot:
  state->num_ok = 0;
  JR_DISPATCH_NEXT_GO(go_nn_lead_dot);

l_nn_dot:
  state->num_ok = (state->flags & JR_JSON5_NUMBERS) ? 1 : 0;
  JR_DISPATCH_NEXT_GO(go_nn_dot);

l_nn_dot_end:
  if(!(state->flags & JR_JSON5_NUMBERS))
    goto l_err;
  goto l_num_e;

l_nn_dot_exp:
  if(!(state->flags & JR_JSON5_NUMBERS))
    goto l_err;

l_nn_exp:
  state->num_ok = 0;
  JR_DISPATCH_NEXT_GO(go_nn_exp);

l_nn_frac:
  state->num_ok = 1;
  JR_DISPATCH_NEXT_GO(go_nn_frac);

l_nn_exp_sign:
  state->num_ok = 0;
  JR_DISPATCH_NEXT_GO(go_nn_exp_sign);

l_nn_exp_digits:
  state->num_ok = 1;
  JR_DISPATCH_NEXT_GO(go_nn_exp_digits);

l_nn_hex_start5:
  if(!(state->flags & JR_JSON5_NUMBERS))
    goto l_err;
  state->num_ok = 0;
  JR_DISPATCH_NEXT_GO(go_nn_hex_start);

l_nn_hex:
  state->num_ok = 1;
  JR_DISPATCH_NEXT_GO(go_nn_hex);

l_kw5_s:
  if(!(state->flags & JR_JSON5_NUMBERS))
    goto l_err;
  JR_TOKEN_START();
  jr_accum_reset(state);
  jr_accum_putc(state, cstr[-1]);
  state->accumulating = 1;
  state->in_number = 1;
  state->num_ok = 0;
  state->kw = cstr[-1] == 'I' ? "Infinity" : "NaN";
  state->kw_pos = 1;
  state->kw_active = 1;
  JR_PUSH_GO(go_nn_idle);
  JR_DISPATCH_NEXT_GO(go_kw);

l_kw5_sign:
  if(!(state->flags & JR_JSON5_NUMBERS))
    goto l_err;
  state->kw = cstr[-1] == 'I' ? "Infinity" : "NaN";
  state->kw_pos = 1;
  state->kw_active = 1;
  JR_DISPATCH_NEXT_GO(go_kw);

l_kw_step:
  if((uint8_t)state->kw[state->kw_pos] != (uint8_t)cstr[-1])
    goto l_err;
  state->kw_pos++;

  if(state->kw[state->kw_pos]) {
    JR_DISPATCH_NEXT_GO(go_kw);
  }

  state->kw_active = 0;
  state->num_ok = 1;
  JR_DISPATCH_NEXT_GO(go_nn_end);

l_num_exp:
  JR_DISPATCH_NEXT_GO(go_num_exp_sign);

l_num_e:
  state->accumulating = 0;
  state->in_number = 0;
  {
    jr_str_t data = {state->accum, (int32_t)(state->accum_len - 1)};
    cb(JSON_EVENT_NUMBER, &data, user_data);
  }
  JR_POP_GO_SEP();
  JR_DISPATCH_THIS();

l_str_s:
  JR_TOKEN_START();
  jr_accum_reset(state);
  state->accumulating = 1;
  JR_PUSH_GO(go_str);
  state->str_type = JSON_EVENT_STRING;
  JR_DISPATCH_NEXT();

l_str_sq_s:
  if(!(state->flags & JR_SINGLE_QUOTES))
    goto l_err;
  JR_TOKEN_START();
  jr_accum_reset(state);
  state->accumulating = 1;
  JR_PUSH_GO(go_str_sq);
  state->str_type = JSON_EVENT_STRING;
  JR_DISPATCH_NEXT();

l_str_ctl:
  /* JSON5 lets control characters stand in a string, line terminators excepted */
  if(!(state->flags & JR_JSON5_STRINGS) || cstr[-1] == '\n' || cstr[-1] == '\r')
    goto l_err;
  JR_DISPATCH_NEXT();

l_ws5:
  if(!(state->flags & JR_JSON5_STRINGS))
    goto l_err;
  JR_DISPATCH_NEXT();

l_str_e:
  state->accumulating = 0;
  {
    jr_str_t data = {state->accum, (int32_t)(state->accum_len - 1)};
    cb(state->str_type, &data, user_data);
  }
  /* A key string's pop always restores go_col (colon expected next), never go_arr/go_obj
   * directly, so JR_POP_GO_SEP()'s swap is a no-op there - safe to use unconditionally for
   * both a key and a value string. */
  JR_POP_GO_SEP();
  JR_DISPATCH_NEXT();

l_esc:
  JR_DISPATCH_NEXT_GO((state->flags & JR_JSON5_STRINGS) ? go_esc5 : go_esc);

/* accum currently holds [..., '\\', <this escape char>] - both auto-accumulated by the
 * dispatches that got here (go_str's on '\\', go_esc's on this char) - back out those 2
 * raw bytes and append the one real decoded byte instead. */
l_esc_quote:
  state->accum_len -= 2;
  jr_accum_putc(state, '"');
  JR_DISPATCH_NEXT();

l_esc_slash:
  state->accum_len -= 2;
  jr_accum_putc(state, '/');
  JR_DISPATCH_NEXT();

l_esc_bslash:
  state->accum_len -= 2;
  jr_accum_putc(state, '\\');
  JR_DISPATCH_NEXT();

l_esc_b:
  state->accum_len -= 2;
  jr_accum_putc(state, '\b');
  JR_DISPATCH_NEXT();

l_esc_f:
  state->accum_len -= 2;
  jr_accum_putc(state, '\f');
  JR_DISPATCH_NEXT();

l_esc_n:
  state->accum_len -= 2;
  jr_accum_putc(state, '\n');
  JR_DISPATCH_NEXT();

l_esc_r:
  state->accum_len -= 2;
  jr_accum_putc(state, '\r');
  JR_DISPATCH_NEXT();

l_esc_t:
  state->accum_len -= 2;
  jr_accum_putc(state, '\t');
  JR_DISPATCH_NEXT();

l_esc_v:
  state->accum_len -= 2;
  jr_accum_putc(state, '\v');
  JR_DISPATCH_NEXT();

l_esc_id:
  state->accum_len -= 2;
  jr_accum_putc(state, cstr[-1]);
  JR_DISPATCH_NEXT();

l_esc_nul:
  state->accum_len -= 2;
  jr_accum_putc(state, '\0');
  JR_DISPATCH_NEXT_GO(go_esc_nul);

l_esc_cont:
  state->accum_len -= 2;
  JR_DISPATCH_NEXT();

l_esc_cont_cr:
  state->accum_len -= 2;
  JR_DISPATCH_NEXT_GO(go_esc_cr);

l_esc_cr_lf:
  state->accum_len -= 1;
  JR_DISPATCH_NEXT();

l_esc_cr_other:
  JR_DISPATCH_THIS();

l_esc_hi:
  /* a backslash before a multi-byte character: drop the backslash, then validate as usual */
  state->esc_hi = (uint8_t)cstr[-1] == 0xe2;
  state->accum[state->accum_len - 2] = state->accum[state->accum_len - 1];
  state->accum_len -= 1;
  JR_DISPATCH_THIS();

l_esc_x:
  state->unicode_val = 0;
  state->unicode_count = 0;
  state->unicode_need = 2;
  JR_DISPATCH_NEXT_GO(go_esc_u);

l_esc_u:
  state->unicode_val = 0;
  state->unicode_count = 0;
  state->unicode_need = 4;
  JR_DISPATCH_NEXT_GO(go_esc_u);

l_esc_u_digit: {
  uint8_t c = (uint8_t)cstr[-1];
  int v = (c <= '9') ? c - '0' : (c | 0x20) - 'a' + 10; /* c|0x20 lowercases A-F */

  state->unicode_val = (state->unicode_val << 4) | (uint32_t)v;

  if(++state->unicode_count < state->unicode_need) {
    JR_DISPATCH_NEXT_GO(go_esc_u);
  }
}
  /* 4th digit just read: accum holds [..., '\\', 'u', h1, h2, h3, h4] (6 raw bytes) - drop
   * them and append the decoded code point's UTF-8 bytes instead, exactly mirroring
   * src/json.c's json_scan_string() JSON_STR_UNICODE handling (same surrogate-pair rule). */
  state->accum_len -= 2 + state->unicode_need;
  {
    uint32_t cp = state->unicode_val, unit = cp;
    uint8_t buf[UTF8_CHAR_LEN_MAX];
    int n, i;

    /* a high surrogate is written alone (3 bytes, kept like JSON.parse keeps it),
     * a low one right after it replaces those 3 bytes with the pair */
    if(is_utf16_low_surrogate(cp) && state->surrogate_hi && state->accum_len == state->surrogate_end) {
      cp = 0x10000 + ((state->surrogate_hi - 0xd800) << 10) + (cp - 0xdc00);
      state->accum_len -= 3;
    }

    state->surrogate_hi = 0;
    n = unicode_to_utf8(buf, cp);

    for(i = 0; i < n; i++)
      jr_accum_putc(state, (char)buf[i]);

    if(is_utf16_high_surrogate(unit)) {
      state->surrogate_hi = unit;
      state->surrogate_end = state->accum_len;
    }
  }
  JR_DISPATCH_NEXT();

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
  /* backslash, U+2028 or U+2029: a line continuation, drop its three
   * bytes, they end just before the byte that reached this label */
  if(state->esc_hi) {
    uint8_t* t = (uint8_t*)state->accum + state->accum_len - 4;

    state->esc_hi = 0;

    if(state->accum_len >= 4 && t[0] == 0xe2 && t[1] == 0x80 && (t[2] == 0xa8 || t[2] == 0xa9)) {
      t[0] = t[3];
      state->accum_len -= 3;
    }
  }
  JR_DISPATCH_THIS();

l_null_n:
  JR_TOKEN_START();
  JR_PUSH();
  state->literal_active = 1;
  JR_DISPATCH_NEXT_GO(go_null_n);

l_null_u:
  JR_DISPATCH_NEXT_GO(go_null_u);

l_null_l:
  JR_DISPATCH_NEXT_GO(go_null_l);

l_null_ll:
  cb(JSON_EVENT_NULL, 0, user_data);
  state->literal_active = 0;
  JR_POP_GO_SEP();
  JR_DISPATCH_NEXT();

l_true_t:
  JR_TOKEN_START();
  JR_PUSH();
  state->literal_active = 1;
  JR_DISPATCH_NEXT_GO(go_true_t);

l_true_r:
  JR_DISPATCH_NEXT_GO(go_true_r);

l_true_u:
  JR_DISPATCH_NEXT_GO(go_true_u);

l_true_e:
  cb(JSON_EVENT_TRUE, 0, user_data);
  state->literal_active = 0;
  JR_POP_GO_SEP();
  JR_DISPATCH_NEXT();

l_false_f:
  JR_TOKEN_START();
  JR_PUSH();
  state->literal_active = 1;
  JR_DISPATCH_NEXT_GO(go_false_f);

l_false_a:
  JR_DISPATCH_NEXT_GO(go_false_a);

l_false_l:
  JR_DISPATCH_NEXT_GO(go_false_l);

l_false_s:
  JR_DISPATCH_NEXT_GO(go_false_s);

l_false_e:
  cb(JSON_EVENT_FALSE, 0, user_data);
  state->literal_active = 0;
  JR_POP_GO_SEP();
  JR_DISPATCH_NEXT();

l_arr_s:
  state->after_comma = 0;
  JR_TOKEN_MARK();
  cb(JSON_EVENT_ARRAY_START, 0, user_data);
  JR_PUSH_GO(go_arr);
  JR_DISPATCH_NEXT();

l_arr_e:
  if(state->after_comma && !(state->flags & (JR_TRAILING_COMMAS | JR_LEGACY))) {
    /* "[1,]": reported, but the closer still closes so the structure stays balanced */
    jr_str_t data = {cstr - 1, 1};

    jr_error_at(state, cstr - 1, JR_E_UNEXPECTED_TOKEN, 0);
    cb(JSON_EVENT_ERROR, &data, user_data);
    state->error = 1;
    state->just_erred = 1;
  }

  state->after_comma = 0;
  JR_TOKEN_MARK();
  cb(JSON_EVENT_ARRAY_END, 0, user_data);
  /* Closing this array is itself "a value ending" from the parent's point of view - same
   * comma-required rule applies to whatever comes after it there. */
  JR_POP_GO_SEP();
  JR_DISPATCH_NEXT();

l_obj_s:
  state->after_comma = 0;
  JR_TOKEN_MARK();
  cb(JSON_EVENT_OBJECT_START, 0, user_data);
  JR_PUSH_GO(go_obj);
  JR_DISPATCH_NEXT();

l_obj_e:
  if(state->after_comma && !(state->flags & (JR_TRAILING_COMMAS | JR_LEGACY))) {
    /* "{\"a\":1,}": reported, but the closer still closes so the structure stays balanced */
    jr_str_t data = {cstr - 1, 1};

    jr_error_at(state, cstr - 1, JR_E_UNEXPECTED_TOKEN, 0);
    cb(JSON_EVENT_ERROR, &data, user_data);
    state->error = 1;
    state->just_erred = 1;
  }

  state->after_comma = 0;
  JR_TOKEN_MARK();
  cb(JSON_EVENT_OBJECT_END, 0, user_data);
  JR_POP_GO_SEP();
  JR_DISPATCH_NEXT();

l_kvp:
  JR_TOKEN_START();
  jr_accum_reset(state);
  state->accumulating = 1;
  JR_PUSH_GO(go_obj_val);
  JR_PUSH_GO(go_col);
  JR_PUSH_GO(go_str);
  state->str_type = JSON_EVENT_KEY;
  JR_DISPATCH_NEXT();

l_kvp_sq:
  if(!(state->flags & JR_SINGLE_QUOTES))
    goto l_err;
  JR_TOKEN_START();
  jr_accum_reset(state);
  state->accumulating = 1;
  JR_PUSH_GO(go_obj_val);
  JR_PUSH_GO(go_col);
  JR_PUSH_GO(go_str_sq);
  state->str_type = JSON_EVENT_KEY;
  JR_DISPATCH_NEXT();

l_key_id:
  if(!(state->flags & JR_UNQUOTED_KEYS))
    goto l_err;
  JR_TOKEN_START();
  jr_accum_reset(state);
  jr_accum_putc(state, cstr[-1]);
  state->accumulating = 1;
  JR_PUSH_GO(go_obj_val);
  JR_PUSH_GO(go_col);
  JR_PUSH_GO(go_ident);
  JR_DISPATCH_THIS();

l_key_id_e:
  /* the byte that ended the name is the last of accum; redispatch it through go_col */
  state->accumulating = 0;
  {
    jr_str_t data = {state->accum, (int32_t)(state->accum_len - 1)};
    cb(JSON_EVENT_KEY, &data, user_data);
  }
  JR_POP_GO();
  JR_DISPATCH_THIS();

l_comma_stray:
  /* a comma where an item or a closer belongs: [,] [1,,2] {,} */
  if(!(state->flags & JR_LEGACY))
    goto l_err;
  JR_DISPATCH_NEXT();

l_val:
  JR_POP_GO();
  JR_DISPATCH_THIS_GO(go_val);

l_col:
  JR_POP_GO();
  JR_DISPATCH_NEXT();

l_arr_sep_comma:
  state->after_comma = 1;
  go = go_arr; /* comma consumed - back to expecting a value for the next element */
  JR_DISPATCH_NEXT();

l_obj_sep_comma:
  state->after_comma = 1;
  go = go_obj; /* comma consumed - back to expecting a key for the next member */
  JR_DISPATCH_NEXT();

l_oom: {
  jr_error_at(state, cstr, JR_E_OUT_OF_MEMORY, 0);
  /* Unlike jr_accum_putc()'s truncation, a lost go_stack entry can't be papered over: every
   * later pop would restore the wrong table. So stop for good instead of resyncing. */
  jr_str_t data = {0, 0};
  cb(JSON_EVENT_ERROR, &data, user_data);
  state->error = 1;
  state->just_erred = 1;
  state->accumulating = 0;
  state->done = 1;
}
}

static void
jr_read(jr_callback cb, const char* chunk, size_t len, void* user_data, jr_state_t* state) {
  state->mark = chunk;
  jr_read_(cb, chunk, len, user_data, state);
  jr_sync(state, chunk + len);
  state->mark = 0;
}

static void
jr_finish(jr_callback cb, void* user_data, jr_state_t* state) {
  /* `error` is sticky and no longer means "dead" (see jr_feed()'s l_err) - a resynced
   * stream must still be able to reach `done` normally here if it's actually complete. */
  if(state->done)
    return;

  if(state->in_number && !(state->flags & JR_LEGACY) && !state->num_ok) {
    /* "-", "1e", "0x", "Infini": the input ends inside a number */
    jr_str_t data = {0, 0};

    state->err_code = state->kw_active ? JR_E_INVALID_LITERAL : JR_E_UNEXPECTED_END;
    state->err_pos = state->kw_active ? state->tok : state->pos;
    state->err_len = state->kw_active ? state->pos.byte_offset - state->tok.byte_offset : 0;
    state->in_number = 0;
    state->accumulating = 0;
    cb(JSON_EVENT_ERROR, &data, user_data);
    state->error = 1;
    return;
  }

  if(state->in_number) {
    jr_str_t data = {state->accum, (int32_t)state->accum_len};

    cb(JSON_EVENT_NUMBER, &data, user_data);
    state->in_number = 0;
    state->accumulating = 0;

    if(!vector_empty(&state->go_stack)) {
      state->go = jr_pop(state);

      if(vector_empty(&state->go_stack))
        state->roots++; /* the number was the whole document */
    }
  }

  if(state->cmt == 2 && state->comment_events) {
    /* a line comment at the end of the input has no newline */
    jr_str_t data = {state->accum, (int32_t)state->accum_len};

    state->cmt = 0;
    state->accumulating = 0;
    cb(JSON_EVENT_COMMENT, &data, user_data);
  }

  if(state->cmt == 3 || state->cmt == 1) {
    jr_str_t data = {0, 0};

    state->err_code = JR_E_UNEXPECTED_END;
    state->err_pos = state->pos;
    state->err_len = 0;
    cb(JSON_EVENT_ERROR, &data, user_data);
    state->error = 1;
    return;
  }

  if(!(state->flags & (JR_MULTI | JR_LEGACY)) && state->roots == 0 && vector_empty(&state->go_stack) && !state->accumulating && !state->error) {
    jr_str_t data = {0, 0};

    state->err_code = JR_E_UNEXPECTED_END;
    state->err_pos = state->pos;
    state->err_len = 0;
    cb(JSON_EVENT_ERROR, &data, user_data);
    state->error = 1;
    return;
  }

  if(!vector_empty(&state->go_stack) || state->accumulating) {
    jr_str_t data = {0, 0};

    state->err_code = JR_E_UNEXPECTED_END;
    state->err_pos = state->pos;
    state->err_len = 0;
    cb(JSON_EVENT_ERROR, &data, user_data);
    state->error = 1;
    return;
  }

  state->done = 1;
}

typedef struct {
  JsonEventHandler* handler;
  void* user;
  const jr_state_t* state;
  ssize_t failure; /* first negative result of the handler, 0 if none */
} JsonFeed;

static void
jfeed_callback(JsonEventType type, const jr_str_t* data, void* user_data) {
  JsonFeed* f = user_data;
  JsonEvent ev;
  ssize_t r;

  if(f->failure)
    return;

  ev = (JsonEvent){
      .type = type,
      .str = data ? data->cstr : NULL,
      .len = data ? data->len : 0,
      .at = type == JSON_EVENT_ERROR ? f->state->err_pos : f->state->tok,
      .has_at = 1,
  };

  if(type == JSON_EVENT_ERROR) {
    ev.code = jr_error_name(f->state->err_code);
    ev.span = f->state->err_len;
  }

  if((r = f->handler(f->user, &ev)) < 0)
    f->failure = r;
}

ssize_t
jr_feed(jr_state_t* state, const char* chunk, size_t len, JsonEventHandler* handler, void* user) {
  JsonFeed f = {handler, user, state, 0};

  jr_read(jfeed_callback, chunk, len, &f, state);
  return f.failure;
}

ssize_t
jr_end(jr_state_t* state, JsonEventHandler* handler, void* user) {
  JsonFeed f = {handler, user, state, 0};

  jr_finish(jfeed_callback, &f, state);
  return f.failure;
}

ssize_t
jr_flush(jr_state_t* state, JsonEventHandler* handler, void* user) {
  JsonFeed f = {handler, user, state, 0};

  if(state->cmt == 2 && state->comment_events && !state->done) {
    jr_str_t data = {state->accum, (int32_t)state->accum_len};

    state->cmt = 0;
    state->accumulating = 0;
    jfeed_callback(JSON_EVENT_COMMENT, &data, &f);
  }

  return f.failure;
}

int
jr_flags_from_name(const char* name, size_t len, int* flags) {
  static const struct {
    const char* name;
    int flags;
  } dialects[] = {
      {"json", JR_JSON},
      {"jsonc", JR_JSONC},
      {"json5", JR_JSON5},
      {"jsonl", JR_JSONL},
      {"legacy", JR_LEGACY},
  };

  for(size_t i = 0; i < sizeof(dialects) / sizeof(dialects[0]); i++) {
    if(strlen(dialects[i].name) == len && !memcmp(dialects[i].name, name, len)) {
      *flags = dialects[i].flags;
      return 0;
    }
  }

  return -1;
}

/**
 * @}
 */
