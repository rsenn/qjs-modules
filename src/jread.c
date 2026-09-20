/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/jread/blob/master/LICENSE
 */

#include "jread.h"
#include "char-utils.h" /* is_utf16_high_surrogate/is_utf16_low_surrogate, unicode_to_utf8, UTF8_CHAR_LEN_MAX */
#include <stdlib.h>
#include <string.h>

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
}

void
jr_state_free(jr_state_t* state) {
  free(state->accum);
  vector_free(&state->go_stack);
  memset(state, 0, sizeof(*state));
}

void
jr_read(jr_callback cb, const char* chunk, size_t len, void* user_data, jr_state_t* state) {
  static void* go_doc[] = {
      [1 ... 8] = &&l_err,     ['\t'] = &&l_next,      ['\n'] = &&l_next,       [11 ... 12] = &&l_err,     ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,   [' '] = &&l_next,       [0] = &&l_err,           [33 ... 33] = &&l_err,     ['"'] = &&l_str_s,
      [35 ... 44] = &&l_err,   ['-'] = &&l_num_s,      [46 ... 47] = &&l_err,   ['0' ... '9'] = &&l_num_s, [58 ... 90] = &&l_err,
      ['['] = &&l_arr_s,       [92 ... 101] = &&l_err, ['f'] = &&l_false_f,     [103 ... 109] = &&l_err,   ['n'] = &&l_null_n,
      [111 ... 115] = &&l_err, ['t'] = &&l_true_t,     [117 ... 122] = &&l_err, ['{'] = &&l_obj_s,         [124 ... 255] = &&l_err,
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
      ['E'] = &&l_num_exp, /* placed after the ranges above so these two override them */
      ['e'] = &&l_num_exp,
  };

  /* One-shot: the single byte right after 'e'/'E' may be a sign or must be a digit -
   * anything else ends the number (a bare trailing "1e" is invalid JSON, but this parser
   * is already lax elsewhere - e.g. multiple '.'s - so it's simpler and consistent to just
   * end the number early rather than add a new error path for it). Whichever of these
   * matches, dispatch continues through the *stored* `go` (still go_num, untouched by this
   * one-shot table) from the next byte on, so further exponent digits are accepted the same
   * way integer/fraction digits already are. */
  static void* go_num_exp_sign[] = {
      [0 ... 255] = &&l_num_e,
      ['+'] = &&l_next,
      ['-'] = &&l_next,
      ['0' ... '9'] = &&l_next,
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
      [99 ... 101] = &&l_err,
      ['f'] = &&l_esc_f,
      [103 ... 109] = &&l_err,
      ['n'] = &&l_esc_n,
      [111 ... 113] = &&l_err,
      ['r'] = &&l_esc_r,
      [115 ... 115] = &&l_err,
      ['t'] = &&l_esc_t,
      [117 ... 255] = &&l_err,
      ['u'] = &&l_esc_u, /* placed after the range above so this one entry overrides it */
  };

  /* \uXXXX: reused for all 4 hex-digit positions via state->unicode_count, since they're
   * all validated the same way - only the 4th one (checked in l_esc_u_digit) finishes. */
  static void* go_esc_u[] = {
      [0 ... 255] = &&l_err,
      ['0' ... '9'] = &&l_esc_u_digit,
      ['A' ... 'F'] = &&l_esc_u_digit,
      ['a' ... 'f'] = &&l_esc_u_digit,
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

  /* Used only right after a value fully ends (see JR_POP_GO_SEP()) - unlike go_arr, which
   * doubles as both "expect value or close" (right after '[' or ',') and, historically,
   * "expect comma or close" too (letting a fresh value start with no separating comma -
   * see BUGS's now-fixed json-parsers-dont-require-commas-between-elements). Only a comma
   * (back to expecting a value - see l_arr_sep_comma) or the closing ']' are legal here. */
  static void* go_arr_sep[] = {
      [0 ... 8] = &&l_err,
      ['\t'] = &&l_next,
      ['\n'] = &&l_next,
      [11 ... 12] = &&l_err,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,
      [' '] = &&l_next,
      [33 ... 255] = &&l_err,
      [','] = &&l_arr_sep_comma,
      [']'] = &&l_arr_e,
  };

  /* Same idea as go_arr_sep, for objects: right after a member's value ends, only a comma
   * (back to expecting the next key - l_obj_sep_comma) or the closing '}' are legal. */
  static void* go_obj_sep[] = {
      [0 ... 8] = &&l_err,
      ['\t'] = &&l_next,
      ['\n'] = &&l_next,
      [11 ... 12] = &&l_err,
      ['\r'] = &&l_next,
      [14 ... 31] = &&l_err,
      [' '] = &&l_next,
      [33 ... 255] = &&l_err,
      [','] = &&l_obj_sep_comma,
      ['}'] = &&l_obj_e,
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

  /* Post-error recovery: every byte is discarded (stays here) except a comma or a closing
   * bracket/brace, which is handed to l_err_resync_boundary instead - see the comment on
   * l_err below for why redispatching that byte through the recovered container-level `go`
   * (not this table) is what actually resumes parsing. */
  static void* go_err_resync[] = {
      [0 ... 255] = &&l_err_resync_skip,
      [','] = &&l_err_resync_boundary,
      [']'] = &&l_err_resync_boundary,
      ['}'] = &&l_err_resync_boundary,
  };

  const char* cstr = chunk;
  const char* end = chunk + len;
  void** go = state->go ? state->go : go_doc;

  if(state->done)
    return;

  state->just_erred = 0;

  if(state->resume)
    goto * state->resume;

l_next:
  JR_DISPATCH_NEXT();

l_err: {
  jr_str_t data = {cstr - 1, 1};
  cb(jr_type_error, &data, user_data);
}
  /* `error` is sticky (jr_finish()/close() report "this stream had an error at some point");
   * `just_erred` is reset at the top of every jr_read() call and only reflects THIS call, so
   * js_json_pushparser_write() can throw once per bad span instead of on every future write()
   * to an already-resynced parser. */
  state->error = 1;
  state->just_erred = 1;
  state->accumulating = 0; /* abandon whatever partial token was mid-scan */

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
  while(go != go_doc && go != go_val && go != go_arr && go != go_obj && go != go_arr_sep && go != go_obj_sep
        && !vector_empty(&state->go_stack))
    JR_POP_GO();

  if(state->literal_active) {
    /* l_null_n/l_true_t/l_false_f push `go` (already container-level) onto go_stack purely
     * for symmetry with l_null_ll/l_true_e/l_false_e's matching pop on a *successful*
     * literal - erroring out mid-literal skips that pop, so it must happen here instead, or
     * this stale entry corrupts the level a future real container-close pops into. */
    JR_POP_GO();
    state->literal_active = 0;
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

l_num_s:
  jr_accum_reset(state);
  jr_accum_putc(state, cstr[-1]);
  state->accumulating = 1;
  state->in_number = 1;
  JR_PUSH_GO(go_num);
  JR_DISPATCH_NEXT();

l_num_exp:
  JR_DISPATCH_NEXT_GO(go_num_exp_sign);

l_num_e:
  state->accumulating = 0;
  state->in_number = 0;
  {
    jr_str_t data = {state->accum, (int32_t)(state->accum_len - 1)};
    cb(jr_type_number, &data, user_data);
  }
  JR_POP_GO_SEP();
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
  /* A key string's pop always restores go_col (colon expected next), never go_arr/go_obj
   * directly, so JR_POP_GO_SEP()'s swap is a no-op there - safe to use unconditionally for
   * both a key and a value string. */
  JR_POP_GO_SEP();
  JR_DISPATCH_NEXT();

l_esc:
  JR_DISPATCH_NEXT_GO(go_esc);

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

l_esc_u:
  state->unicode_val = 0;
  state->unicode_count = 0;
  JR_DISPATCH_NEXT_GO(go_esc_u);

l_esc_u_digit: {
  uint8_t c = (uint8_t)cstr[-1];
  int v = (c <= '9') ? c - '0' : (c | 0x20) - 'a' + 10; /* c|0x20 lowercases A-F */

  state->unicode_val = (state->unicode_val << 4) | (uint32_t)v;

  if(++state->unicode_count < 4) {
    JR_DISPATCH_NEXT_GO(go_esc_u);
  }
}
  /* 4th digit just read: accum holds [..., '\\', 'u', h1, h2, h3, h4] (6 raw bytes) - drop
   * them and append the decoded code point's UTF-8 bytes instead, exactly mirroring
   * src/json.c's json_scan_string() JSON_STR_UNICODE handling (same surrogate-pair rule). */
  state->accum_len -= 6;
  {
    uint32_t cp = state->unicode_val;

    if(is_utf16_high_surrogate(cp)) {
      state->surrogate_hi = cp; /* wait for the low half - the paired \uXXXX immediately follows */
    } else {
      uint8_t buf[UTF8_CHAR_LEN_MAX];
      int n, i;

      if(is_utf16_low_surrogate(cp) && state->surrogate_hi)
        cp = 0x10000 + ((state->surrogate_hi - 0xd800) << 10) + (cp - 0xdc00);

      state->surrogate_hi = 0;
      n = unicode_to_utf8(buf, cp);

      for(i = 0; i < n; i++)
        jr_accum_putc(state, (char)buf[i]);
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
  JR_DISPATCH_THIS();

l_null_n:
  JR_PUSH();
  state->literal_active = 1;
  JR_DISPATCH_NEXT_GO(go_null_n);

l_null_u:
  JR_DISPATCH_NEXT_GO(go_null_u);

l_null_l:
  JR_DISPATCH_NEXT_GO(go_null_l);

l_null_ll:
  cb(jr_type_null, 0, user_data);
  state->literal_active = 0;
  JR_POP_GO_SEP();
  JR_DISPATCH_NEXT();

l_true_t:
  JR_PUSH();
  state->literal_active = 1;
  JR_DISPATCH_NEXT_GO(go_true_t);

l_true_r:
  JR_DISPATCH_NEXT_GO(go_true_r);

l_true_u:
  JR_DISPATCH_NEXT_GO(go_true_u);

l_true_e:
  cb(jr_type_true, 0, user_data);
  state->literal_active = 0;
  JR_POP_GO_SEP();
  JR_DISPATCH_NEXT();

l_false_f:
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
  cb(jr_type_false, 0, user_data);
  state->literal_active = 0;
  JR_POP_GO_SEP();
  JR_DISPATCH_NEXT();

l_arr_s:
  cb(jr_type_array_start, 0, user_data);
  JR_PUSH_GO(go_arr);
  JR_DISPATCH_NEXT();

l_arr_e:
  cb(jr_type_array_end, 0, user_data);
  /* Closing this array is itself "a value ending" from the parent's point of view - same
   * comma-required rule applies to whatever comes after it there. */
  JR_POP_GO_SEP();
  JR_DISPATCH_NEXT();

l_obj_s:
  cb(jr_type_object_start, 0, user_data);
  JR_PUSH_GO(go_obj);
  JR_DISPATCH_NEXT();

l_obj_e:
  cb(jr_type_object_end, 0, user_data);
  JR_POP_GO_SEP();
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

l_arr_sep_comma:
  go = go_arr; /* comma consumed - back to expecting a value for the next element */
  JR_DISPATCH_NEXT();

l_obj_sep_comma:
  go = go_obj; /* comma consumed - back to expecting a key for the next member */
  JR_DISPATCH_NEXT();

l_oom: {
  /* Unlike jr_accum_putc()'s truncation, a lost go_stack entry can't be papered over: every
   * later pop would restore the wrong table. So stop for good instead of resyncing. */
  jr_str_t data = {0, 0};
  cb(jr_type_error, &data, user_data);
  state->error = 1;
  state->just_erred = 1;
  state->accumulating = 0;
  state->done = 1;
}
}

void
jr_finish(jr_callback cb, void* user_data, jr_state_t* state) {
  /* `error` is sticky and no longer means "dead" (see jr_read()'s l_err) - a resynced
   * stream must still be able to reach `done` normally here if it's actually complete. */
  if(state->done)
    return;

  if(state->in_number) {
    jr_str_t data = {state->accum, (int32_t)state->accum_len};

    cb(jr_type_number, &data, user_data);
    state->in_number = 0;
    state->accumulating = 0;

    if(!vector_empty(&state->go_stack))
      state->go = jr_pop(state);
  }

  if(!vector_empty(&state->go_stack) || state->accumulating) {
    jr_str_t data = {0, 0};

    cb(jr_type_error, &data, user_data);
    state->error = 1;
    return;
  }

  state->done = 1;
}
