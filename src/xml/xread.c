/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/xread/blob/master/LICENSE
 */

#include "char-utils.h"
#include "xml/xml_entities.h"
#include "xml/xread.h"
#include "xml/xtags.h"

#include <stdlib.h>

typedef struct xr_str {
  const char* cstr;
  int32_t len;
} xr_str_t;

typedef void (*xr_callback)(XmlEventType type, const xr_str_t* name, const xr_str_t* value, void* user_data);

/**
 * \addtogroup xread
 * @{
 */

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
 *    *state->resume` at the top of xr_feed() re-enters past that setup, without
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
  srcpos_init(&state->pos, 0);
  state->err_pos = state->pos;
}

const char*
xr_error_name(xr_error_t code) {
  static const char* const names[] = {"", "unexpected-character", "unexpected-end"};

  return (unsigned)code < sizeof(names) / sizeof(names[0]) ? names[code] : "";
}

void
xr_state_init_flags(xr_state_t* state, unsigned flags) {
  xr_state_init(state);
  state->flags = flags;
  state->self_closing_tags = xtag_html_self_closing;
  state->cs = 0;
}

void
xr_state_free(xr_state_t* state) {
  free(state->pi_accum.buf);
  free(state->fnames);
  free(state->foffs);
  free(state->tag_accum.buf);
  free(state->name_accum.buf);
  free(state->val_accum.buf);
  free(state->text_accum.buf);
  memset(state, 0, sizeof(*state));
}

static void
xr_read_(xr_callback cb, const char* chunk, size_t len, void* user_data, xr_state_t* state) {
  static void* go_root[] = {
      [0] = &&l_error,
      [1 ... 8] = &&l_error,
      ['\t'] = &&l_text_begin,
      ['\n'] = &&l_text_begin,
      [11 ... 12] = &&l_error,
      ['\r'] = &&l_text_begin,
      [14 ... 31] = &&l_error,
      [' '] = &&l_text_begin,
      [33 ... 59] = &&l_text_begin,
      ['<'] = &&l_tag,
      [61 ... 255] = &&l_text_begin,
  };

  /* Entered via go_root's l_text_begin, from the very first byte of a text run -
   * including leading whitespace, so a run like " after" that starts with whitespace
   * but has real content later keeps that whitespace instead of silently losing it
   * (go_root used to route whitespace bytes to l_next, skip them without
   * accumulating, and only start the run at the first non-whitespace byte - which
   * meant leading whitespace right after a closing tag was always dropped, even when
   * real text followed). Every byte except '<' continues accumulating here, so
   * interior whitespace is preserved too; l_text_end (below) is what decides whether
   * the whole run turns out to be nothing but whitespace, in which case it's dropped
   * there instead - matching how insignificant inter-tag whitespace should be
   * silently dropped, without also dropping whitespace that borders real content. */
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
  state->err_code = XR_E_UNEXPECTED_CHARACTER;
  state->err_pos = state->pos;
  srcpos_advance(&state->err_pos, state->mark, cstr - 1 - state->mark);
  state->err_len = 1;
  xr_str_t name = {"Error!", 6};
  xr_str_t val = {cstr - 1, 1};
  cb(XE_ERROR, &name, &val, user_data);
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
    BOOL all_whitespace = TRUE;

    for(int32_t i = 0; i < text.len; i++) {
      if(!is_whitespace_char(text.cstr[i])) {
        all_whitespace = FALSE;
        break;
      }
    }

    /* Whitespace-only runs (e.g. pretty-printed indentation between sibling tags,
     * or leading go_root whitespace that never turns into real content) carry no
     * meaning - drop them entirely rather than emitting an insignificant text
     * event, matching js_xml_parse()'s own "whitespace-only text between tags is
     * dropped" behavior. A run with any real content is emitted verbatim, boundary
     * whitespace included - see go_root's comment above for why leading
     * whitespace needs accumulating from the start rather than being skipped. */
    if(!all_whitespace) {
      text.len = (int32_t)xml_decode_entities(state->text_accum.buf, (size_t)text.len);
      cb(XE_TEXT, 0, &text, user_data);
    }
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
    cb(XE_ELEMENT_START, &tag, 0, user_data);
  }
  go = go_attrib;
  XR_DISPATCH_THIS();

l_etag_name:
  state->accumulating = 0;
  {
    xr_str_t tag = {state->tag_accum.buf, (int32_t)(state->tag_accum.len - 1)};
    cb(XE_ELEMENT_END, &tag, 0, user_data);
  }
  go = go_tag_close;
  XR_DISPATCH_THIS();

l_empty_element_tag: {
  xr_str_t tag = {state->tag_accum.buf, (int32_t)(state->tag_accum.len - 1)};
  cb(XE_ELEMENT_END, &tag, 0, user_data);
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
    cb(XE_ATTRIBUTE, &name, &val, user_data);
  }
  go = go_attrib;
  XR_DISPATCH_NEXT();

l_attrib_bool: {
  xr_str_t name = {state->name_accum.buf, (int32_t)(state->name_accum.len - 1)};
  cb(XE_ATTRIBUTE, &name, 0, user_data);
}
  go = go_attrib;
  XR_DISPATCH_THIS();
}

static void
xr_read(xr_callback cb, const char* chunk, size_t len, void* user_data, xr_state_t* state) {
  state->mark = chunk;
  xr_read_(cb, chunk, len, user_data, state);
  srcpos_advance(&state->pos, state->mark, chunk + len - state->mark);
  state->mark = 0;
}

static void
xr_finish(xr_callback cb, void* user_data, xr_state_t* state) {
  if(state->error || state->done)
    return;

  if(!state->at_root) {
    xr_str_t name = {"Error!", 6};
    xr_str_t val = {"", 0};

    state->err_code = XR_E_UNEXPECTED_END;
    state->err_pos = state->pos;
    state->err_len = 0;
    cb(XE_ERROR, &name, &val, user_data);
    state->error = 1;
    return;
  }

  state->done = 1;
}

typedef struct {
  XmlEventHandler* handler;
  void* user;
  const xr_state_t* state;
  ssize_t failure; /* first negative result of the handler, 0 if none */
} XmlFeed;

static void
xfeed_send(XmlFeed* f, const XmlEvent* ev) {
  ssize_t r;

  if(f->failure)
    return;

  if((r = f->handler(f->user, ev)) < 0)
    f->failure = r;
}

static void
xfeed_callback(XmlEventType type, const xr_str_t* name, const xr_str_t* value, void* user_data) {
  XmlFeed* f = user_data;
  XmlEvent ev = {0};

  switch(type) {
    case XE_ELEMENT_START:
      ev = (XmlEvent){
          .type = XE_ELEMENT_START,
          .name = name->cstr,
          .nlen = name->len,
      };
      break;
    case XE_ELEMENT_END:
      ev = (XmlEvent){
          .type = XE_ELEMENT_END,
          .name = name ? name->cstr : NULL,
          .nlen = name ? name->len : 0,
      };
      break;
    case XE_ATTRIBUTE:
      ev = (XmlEvent){
          .type = XE_ATTRIBUTE,
          .name = name->cstr,
          .nlen = name->len,
          .value = value ? value->cstr : NULL,
          .vlen = value ? value->len : 0,
      };
      break;
    case XE_TEXT:
      ev = (XmlEvent){
          .type = XE_TEXT,
          .str = value->cstr,
          .len = value->len,
      };
      break;
    case XE_ERROR:
      ev = (XmlEvent){
          .type = XE_ERROR,
          .str = value ? value->cstr : NULL,
          .len = value ? value->len : 0,
          .code = xr_error_name(f->state->err_code),
          .span = f->state->err_len,
          .at = f->state->err_pos,
          .has_at = 1,
      };
      break;
    default: return; /* xread produces no markup events */
  }

  xfeed_send(f, &ev);
}

/* ---- XR_COMPAT: the parser of xml.c as a push parser ------------------------------------------
 *
 * One byte at a time through a state machine, the state in xr_state_t so a chunk may end anywhere.
 * Text, names and values are scanned in runs. The rules are those of xml.c, see xml.h:
 *
 *   state          reads
 *   CS_TEXT        character data up to '<' (raw text elements: up to </name>)
 *   CS_LT          the byte after '<': '/' makes a closing tag
 *   CS_NAME0       first byte of a tag name, '!' or '-' may start a comment or declaration
 *   CS_BANG1,2     the "--" after it
 *   CS_NAME        the rest of the tag name, up to white space, '/' or '>'
 *   CS_COMMENT     <!-- ... -->, CS_BANG: <!...> and <![CDATA[...]]>, up to '>'
 *   CS_CLOSE_SPACE white space after a closing tag name, then the end of the tag
 *   CS_ATTR...     attributes: CS_ATTR before one, CS_ATTR_NAME, CS_ATTR_EQ, CS_VAL_Q, CS_VAL_U
 *   CS_AFTER       the end of the attributes: '/' and '?'
 *   CS_FINISH      the end of the start tag: the element end, the push or the instruction
 *   CS_TAIL        the '>' that closes the tag
 */

#include "xml/xtags.h"
#include <string.h>

enum {
  CS_TEXT,
  CS_LT,
  CS_NAME0,
  CS_BANG1,
  CS_BANG2,
  CS_PEEKED,
  CS_NAME,
  CS_COMMENT,
  CS_BANG,
  CS_CLOSE_SPACE,
  CS_ATTR,
  CS_ATTR_NAME,
  CS_ATTR_EQ,
  CS_VAL_Q,
  CS_VAL_U,
  CS_AFTER,
  CS_FINISH,
  CS_FINISH2,
  CS_TAIL,
};

/* the character classes of xml.c, same values */
enum {
  XC_WS = 0x01,
  XC_QUOTE = 0x08,
  XC_CLOSE = 0x10,
  XC_EQUAL = 0x20,
  XC_SPECIAL = 0x40,
  XC_SLASH = 0x80,
  XC_END = 0x04,
  XC_HYPHEN = 0x400,
  XC_EXCLAM = 0x400,
};

static const uint16_t xr_class[256] = {
    [' '] = XC_WS,
    ['\t'] = XC_WS,
    ['\r'] = XC_WS,
    ['\n'] = XC_WS,
    ['!'] = XC_SPECIAL | XC_EXCLAM,
    ['"'] = XC_QUOTE,
    ['\''] = XC_QUOTE,
    ['/'] = XC_END | XC_SLASH,
    ['='] = XC_EQUAL,
    ['>'] = XC_END | XC_CLOSE,
    ['?'] = XC_SPECIAL | 0x200,
    ['-'] = XC_HYPHEN,
};

#define XCLS(c) (xr_class[(uint8_t)(c)])

typedef struct {
  XmlEventHandler* handler;
  void* user;
  ssize_t fail; /* first negative result of the handler */
} XrOut;

static void
xo_send(XrOut* o, const XmlEvent* ev) {
  ssize_t r;

  if(!o->fail && (r = o->handler(o->user, ev)) < 0)
    o->fail = r;
}

static void
xr_accum_put(xr_accum_t* a, const char* data, size_t n) {
  if(a->len + n > a->cap) {
    size_t cap = a->cap ? a->cap : 64;
    char* p;

    while(cap < a->len + n)
      cap *= 2;

    if(!(p = realloc(a->buf, cap)))
      return;

    a->buf = p;
    a->cap = cap;
  }

  memcpy(a->buf + a->len, data, n);
  a->len += n;
}

static void
xo_start(XrOut* o, xr_state_t* s, const char* name, size_t len) {
  XmlEvent ev = {.type = XE_ELEMENT_START, .name = len ? name : NULL, .nlen = len, .at = s->tag_pos, .has_at = 1};

  xo_send(o, &ev);
}

static void
xo_end(XrOut* o, xr_state_t* s, const char* name, size_t len, int self_closing) {
  XmlEvent ev = {.type = XE_ELEMENT_END, .name = len ? name : NULL, .nlen = len, .at = s->tag_pos, .has_at = 1, .self_closing = !!self_closing};

  xo_send(o, &ev);
}

static void
xo_attr(XrOut* o, xr_state_t* s, const char* name, size_t nlen, const char* value, size_t vlen) {
  XmlEvent ev = {.type = XE_ATTRIBUTE, .name = name, .nlen = nlen, .value = value, .vlen = vlen, .at = s->tag_pos, .has_at = 1};

  xo_send(o, &ev);
}

static void
xo_text(XrOut* o, const char* str, size_t len) {
  XmlEvent ev = {.type = XE_TEXT, .str = str, .len = len};

  xo_send(o, &ev);
}

/* frames: the open elements */

static void
xr_push(xr_state_t* s, const char* name, size_t len) {
  if(s->nframes == s->foffs_cap) {
    size_t cap = s->foffs_cap ? s->foffs_cap * 2 : 16;
    size_t* p = realloc(s->foffs, cap * sizeof(size_t));

    if(!p)
      return;

    s->foffs = p;
    s->foffs_cap = cap;
  }

  if(s->fnames_len + len > s->fnames_cap) {
    size_t cap = s->fnames_cap ? s->fnames_cap : 256;
    char* p;

    while(cap < s->fnames_len + len)
      cap *= 2;

    if(!(p = realloc(s->fnames, cap)))
      return;

    s->fnames = p;
    s->fnames_cap = cap;
  }

  s->foffs[s->nframes++] = s->fnames_len;
  memcpy(s->fnames + s->fnames_len, name, len);
  s->fnames_len += len;
  s->raw = !(s->flags & XR_FLAT) && xtag_is_raw_text(name, len);
}

static const char*
xr_frame_name(const xr_state_t* s, size_t i, size_t* len) {
  size_t end = i + 1 < s->nframes ? s->foffs[i + 1] : s->fnames_len;

  *len = end - s->foffs[i];
  return s->fnames + s->foffs[i];
}

static void
xr_pop(xr_state_t* s) {
  size_t len;

  if(!s->nframes)
    return;

  xr_frame_name(s, s->nframes - 1, &len);
  s->fnames_len -= len;
  s->nframes--;

  if(s->nframes) {
    const char* n = xr_frame_name(s, s->nframes - 1, &len);

    s->raw = !(s->flags & XR_FLAT) && xtag_is_raw_text(n, len);
  } else {
    s->raw = 0;
  }
}

/* xr_find: the open element with this name, innermost first, or -1 */
static ssize_t
xr_find(const xr_state_t* s, const char* name, size_t len) {
  for(size_t i = s->nframes; i > 0; i--) {
    size_t n;
    const char* f = xr_frame_name(s, i - 1, &n);

    if(n == len && !memcmp(f, name, len))
      return (ssize_t)(i - 1);
  }

  return -1;
}

/* xr_flush_text: sends the text run as the XE_TEXT events xml.c makes of it.
 *
 *   an element with raw text      one event per line, leading white space of the run dropped
 *   anything else                 white space only is dropped, else the run without the newline
 *                                 (and the indentation after it) it starts with and the newline
 *                                 (and what follows) it ends with
 *
 * entities are decoded. */
static void
xr_flush_text(xr_state_t* s, XrOut* o) {
  char* buf = s->text_accum.buf;
  size_t size = s->text_accum.len, text_pos = 0;

  while(text_pos < size) {
    char* pos = buf + text_pos;
    size_t remain = size - text_pos, n, real_len;

    if(s->raw) {
      size_t leading_ws = scan_whitenskip(buf, size);
      size_t skip = scan_whitenskip(pos, leading_ws < remain ? leading_ws : remain);
      size_t nl;

      pos += skip;
      remain -= skip;
      text_pos += skip;
      nl = byte_chr(pos, remain, '\n');

      if(nl < remain)
        nl++;

      real_len = n = nl;
    } else {
      size_t total = remain, lead_strip = 0, trail_strip = 0, last_nl;

      if(scan_whitenskip(pos, total) == total)
        break;

      if(total > 0 && pos[0] == '\n')
        lead_strip = scan_whitenskip(pos, total);

      last_nl = byte_rchr(pos, total, '\n');
      trail_strip = (last_nl < total && last_nl >= lead_strip) ? total - last_nl : 0;
      pos += lead_strip;
      text_pos += lead_strip;
      real_len = n = total - lead_strip - trail_strip;
    }

    text_pos += real_len;

    if(n > 0)
      n = xml_decode_entities(pos, n);

    if(n > 0)
      xo_text(o, pos, n);
  }

  s->text_accum.len = 0;
}

static void
xo_markup(XrOut* o, xr_state_t* s, const char* name, size_t len) {
  XmlEvent ev = {.at = s->tag_pos, .has_at = 1};

  xevent_markup(&ev, name, len);
  xo_send(o, &ev);
}

/* xo_bang: the name starting with ! (or - : the class of xml.c is the same for both) is complete.
 * ! is markup, - an element that ends at once. */
static void
xo_bang(XrOut* o, xr_state_t* s) {
  if(s->tag_accum.buf[0] == '!') {
    xo_markup(o, s, s->tag_accum.buf, s->tag_accum.len);
  } else {
    xo_start(o, s, s->tag_accum.buf, s->tag_accum.len);
    xo_end(o, s, s->tag_accum.buf, s->tag_accum.len, 1);
  }
}

static void
xo_instruction(XrOut* o, xr_state_t* s) {
  size_t len = s->pi_accum.len;
  XmlEvent ev = {.type = XE_INSTRUCTION, .str = s->pi_accum.buf, .at = s->tag_pos, .has_at = 1};

  if(len && s->pi_accum.buf[len - 1] == '?')
    len--;

  ev.len = len;
  s->is_pi = 0;
  xo_send(o, &ev);
}

/* xr_attribute: an attribute is complete, name_accum and val_accum hold it (has_value: val_accum is its value) */
static void
xr_attribute(xr_state_t* s, XrOut* o, int has_value) {
  const char* name = s->name_accum.buf;
  size_t nlen = s->name_accum.len;

  if(has_value)
    s->val_accum.len = xml_decode_entities(s->val_accum.buf ? s->val_accum.buf : "", s->val_accum.len);

  if(s->is_pi) {
    xr_accum_put(&s->pi_accum, " ", 1);
    xr_accum_put(&s->pi_accum, name, nlen);

    if(has_value) {
      xr_accum_put(&s->pi_accum, "=\"", 2);
      xr_accum_put(&s->pi_accum, s->val_accum.buf, s->val_accum.len);
      xr_accum_put(&s->pi_accum, "\"", 1);
    }

    return;
  }

  xo_attr(o, s, name, nlen, has_value ? (s->val_accum.buf ? s->val_accum.buf : "") : NULL, has_value ? s->val_accum.len : 0);
}

/* xr_close_tag: a closing tag is complete. returns 1 when the '>' after it may still follow */
static int
xr_close_tag(xr_state_t* s, XrOut* o) {
  const char* name = s->tag_accum.buf;
  size_t len = s->tag_accum.len;
  ssize_t m;

  if(s->flags & XR_FLAT) {
    xo_end(o, s, name, len, 0);
    return 1;
  }

  if(len == 0)
    return 1; /* </> names the document root, which is never open */

  if((m = xr_find(s, name, len)) < 0) {
    if(!(s->flags & XR_TOLERANT)) {
      XmlEvent ev = {.type = XE_ERROR, .code = "mismatched-end-tag", .span = len + 3, .at = s->tag_pos, .has_at = 1};

      /* the message is mismatch </name>, kept NUL-terminated in pi_accum, free at this point */
      s->pi_accum.len = 0;
      xr_accum_put(&s->pi_accum, "mismatch </", 11);
      xr_accum_put(&s->pi_accum, name, len);
      xr_accum_put(&s->pi_accum, ">", 2);
      ev.str = s->pi_accum.buf;
      ev.len = s->pi_accum.len - 1;
      s->done = 1;
      xo_send(o, &ev);
      return 0;
    }

    return 0; /* tolerant: the stray closing tag is ignored, a '>' right after it stays text */
  }

  /* every element down to the match ends, innermost first */
  while(s->nframes > (size_t)m && !o->fail) {
    size_t n;
    const char* f = xr_frame_name(s, s->nframes - 1, &n);

    xo_end(o, s, f, n, 0);
    xr_pop(s);
  }

  return 1;
}

/* xr_open_tag: a start tag name is complete, c is the byte after it. returns the next state */
static unsigned
xr_open_tag(xr_state_t* s, XrOut* o) {
  const char* name = s->tag_accum.buf;
  size_t len = s->tag_accum.len;

  s->self_closing = xtag_in_list(name, len, s->self_closing_tags);

  if(len && name[0] == '?') {
    s->is_pi = 1;
    s->pi_accum.len = 0;
    xr_accum_put(&s->pi_accum, name + 1, len - 1);
    return CS_ATTR;
  }

  if((s->flags & (XR_TOLERANT | XR_NO_IMPLIED_END | XR_FLAT)) == XR_TOLERANT && s->nframes) {
    size_t n;
    const char* top = xr_frame_name(s, s->nframes - 1, &n);

    if(xtag_is_implied_end(name, len, top, n)) {
      xo_end(o, s, top, n, 0);
      xr_pop(s);
    }
  }

  xo_start(o, s, name, len);
  return CS_ATTR;
}

/* xr_open_finish: the start tag ends, the element is closed, pushed or (an instruction) sent */
static void
xr_open_finish(xr_state_t* s, XrOut* o) {
  if(s->is_pi)
    xo_instruction(o, s);
  else if(s->self_closing)
    xo_end(o, s, s->tag_accum.buf, s->tag_accum.len, 1);
  else if(!(s->flags & XR_FLAT))
    xr_push(s, s->tag_accum.buf, s->tag_accum.len);
}

/* xr_mark_pos: pos up to chunk byte `at` */
static void
xr_mark_pos(xr_state_t* s, const char* at) {
  srcpos_advance(&s->pos, s->mark, at - s->mark);
  s->mark = at;
}

static ssize_t
xr_feed_compat(xr_state_t* s, const char* chunk, size_t len, XmlEventHandler* handler, void* user) {
  XrOut o = {handler, user, 0};
  size_t i = 0;

  s->mark = chunk;

  while(i < len && !o.fail && !s->done) {
    const char* p = chunk + i;
    uint8_t c = (uint8_t)*p;
    size_t n;

    switch(s->cs) {
      case CS_TEXT:
        if(s->raw) {
          /* up to </name> of the open element, a '<' of anything else is text */
          size_t nl, total;
          const char* top = xr_frame_name(s, s->nframes - 1, &nl);
          static const char pre[2] = {'<', '/'};

          total = nl + 3;
          xr_accum_put(&s->text_accum, p, 1);

          if(s->raw_m == 0 || c == '<') {
            if(c == '<') {
              s->raw_m = 1;
              xr_mark_pos(s, p);
              s->cand_pos = s->pos;
            }
          } else {
            char want = s->raw_m < 2 ? pre[s->raw_m] : s->raw_m < 2 + nl ? top[s->raw_m - 2] : '>';

            s->raw_m = c == (uint8_t)want ? s->raw_m + 1 : 0;
          }

          i++;

          if(s->raw_m == total) {
            size_t tl;
            const char* f = xr_frame_name(s, s->nframes - 1, &tl);

            s->text_accum.len -= total;
            s->raw_m = 0;
            xr_flush_text(s, &o);
            s->tag_pos = s->cand_pos;
            xo_end(&o, s, f, tl, 0);
            xr_pop(s);
            s->cs = CS_TAIL;
          }

          break;
        } else {
          const char* lt = memchr(p, '<', len - i);

          n = lt ? (size_t)(lt - p) : len - i;
          xr_accum_put(&s->text_accum, p, n);
          i += n;

          if(lt) {
            xr_flush_text(s, &o);
            xr_mark_pos(s, lt);
            s->tag_pos = s->pos;
            s->closing = s->self_closing = s->is_pi = 0;
            i++;
            s->cs = CS_LT;
          }
        }

        break;

      case CS_LT:
        if(c == '/') {
          s->closing = 1;
          i++;
        }

        s->tag_accum.len = 0;
        s->cs = CS_NAME0;
        break;

      case CS_NAME0:
        if(c == '!') {
          xr_accum_put(&s->tag_accum, p, 1);
          i++;
          s->cs = CS_BANG1;
        } else {
          s->cs = CS_NAME;
        }

        break;

      case CS_BANG1:
      case CS_BANG2:
        if(XCLS(c) & XC_HYPHEN) {
          xr_accum_put(&s->tag_accum, p, 1);
          i++;
          /* the two peeked bytes pass as hyphens, only !-- itself is a comment */
          s->cs = s->cs == CS_BANG1 ? CS_BANG2 : CS_PEEKED;
        } else {
          s->cs = CS_NAME;
        }

        break;

      case CS_PEEKED:
        s->cs = !memcmp(s->tag_accum.buf, "!--", 3) ? CS_COMMENT : CS_BANG;
        break;

      case CS_NAME:
        for(n = 0; i + n < len && !(XCLS(p[n]) & (XC_WS | XC_END)); n++) {}

        xr_accum_put(&s->tag_accum, p, n);
        i += n;

        if(i < len) {
          if(s->closing)
            s->cs = CS_CLOSE_SPACE;
          else if(s->tag_accum.len && (XCLS(s->tag_accum.buf[0]) & XC_EXCLAM))
            s->cs = CS_BANG;
          else
            s->cs = xr_open_tag(s, &o);
        }

        break;

      case CS_COMMENT: {
        /* "-->" ends it, the first character of the body cannot start the end */
        const char* gt = memchr(p, '>', len - i);

        n = gt ? (size_t)(gt - p) : len - i;
        xr_accum_put(&s->tag_accum, p, n);
        i += n;

        if(gt) {
          const char* b = s->tag_accum.buf;
          size_t l = s->tag_accum.len;

          if(l >= 6 && b[l - 1] == '-' && (XCLS(b[l - 2]) & XC_HYPHEN)) {
            xo_markup(&o, s, b, l);
            i++;
            s->cs = CS_TEXT;
          } else {
            xr_accum_put(&s->tag_accum, gt, 1);
            i++;
          }
        }

        break;
      }

      case CS_BANG: {
        const char* gt = memchr(p, '>', len - i);

        n = gt ? (size_t)(gt - p) : len - i;
        xr_accum_put(&s->tag_accum, p, n);
        i += n;

        if(gt) {
          xo_bang(&o, s);
          i++;
          s->cs = CS_TEXT;
        }

        break;
      }

      case CS_CLOSE_SPACE:
        if(XCLS(c) & XC_WS) {
          i++;
          break;
        }

        if(c == '>')
          i++;

        s->cs = xr_close_tag(s, &o) ? CS_TAIL : CS_TEXT;
        break;

      case CS_ATTR:
        if(XCLS(c) & XC_WS) {
          i++;
        } else if(XCLS(c) & (XC_END | XC_EQUAL | XC_SPECIAL)) {
          s->cs = CS_AFTER;
        } else {
          s->name_accum.len = 0;
          s->cs = CS_ATTR_NAME;
        }

        break;

      case CS_ATTR_NAME:
        for(n = 0; i + n < len && !(XCLS(p[n]) & (XC_EQUAL | XC_WS | XC_SPECIAL | XC_CLOSE | XC_SLASH)); n++) {}

        xr_accum_put(&s->name_accum, p, n);
        i += n;

        if(i < len) {
          c = (uint8_t)chunk[i];

          if(XCLS(c) & (XC_WS | XC_CLOSE | XC_SLASH)) {
            xr_attribute(s, &o, 0);
            s->cs = CS_ATTR;
          } else if(c == '=') {
            i++;
            s->cs = CS_ATTR_EQ;
          } else {
            s->cs = CS_AFTER; /* '!' or '?' after a name: the attribute is dropped */
          }
        }

        break;

      case CS_ATTR_EQ:
        s->val_accum.len = 0;

        if(XCLS(c) & XC_QUOTE) {
          s->quote = c;
          i++;
          s->cs = CS_VAL_Q;
        } else {
          s->quote = 0;
          s->cs = CS_VAL_U;
        }

        break;

      case CS_VAL_Q: {
        const char* q = memchr(p, s->quote, len - i);

        n = q ? (size_t)(q - p) : len - i;
        xr_accum_put(&s->val_accum, p, n);
        i += n;

        if(q) {
          i++;
          xr_attribute(s, &o, 1);
          s->cs = CS_ATTR;
        }

        break;
      }

      case CS_VAL_U:
        for(n = 0; i + n < len && !(XCLS(p[n]) & (XC_WS | XC_CLOSE)); n++) {}

        xr_accum_put(&s->val_accum, p, n);
        i += n;

        if(i < len) {
          xr_attribute(s, &o, 1);
          s->cs = CS_ATTR;
        }

        break;

      case CS_AFTER:
        if(c == '/') {
          s->self_closing = 1;
          i++;
        }

        s->cs = CS_FINISH;
        break;

      case CS_FINISH:
        if(c == '?' && s->tag_accum.len && s->tag_accum.buf[0] == '?') {
          i++;
          s->cs = CS_FINISH2;
          break;
        }

        /* fall through */
      case CS_FINISH2:
        xr_open_finish(s, &o);
        s->cs = CS_TAIL;
        break;

      case CS_TAIL:
        if(c == '>')
          i++;

        s->cs = CS_TEXT;
        break;

      default: s->cs = CS_TEXT; break;
    }
  }

  xr_mark_pos(s, chunk + len);
  s->mark = NULL;
  return o.fail;
}

/* xr_end_compat: the input ends, what the state holds is sent as xml.c does, no error */
static ssize_t
xr_end_compat(xr_state_t* s, XmlEventHandler* handler, void* user) {
  XrOut o = {handler, user, 0};

  if(s->done)
    return 0;

  switch(s->cs) {
    case CS_TEXT: xr_flush_text(s, &o); break;
    case CS_COMMENT: xo_markup(&o, s, s->tag_accum.buf, s->tag_accum.len); break;
    case CS_BANG: xo_bang(&o, s); break;
    case CS_ATTR_EQ:
    case CS_VAL_Q:
    case CS_VAL_U:
      if(s->cs == CS_ATTR_EQ)
        s->val_accum.len = 0;

      xr_attribute(s, &o, 1);
      /* fall through */
    case CS_ATTR:
    case CS_ATTR_NAME:
    case CS_AFTER:
    case CS_FINISH:
    case CS_FINISH2:
      if(s->is_pi)
        xo_instruction(&o, s);

      break;
    default: break;
  }

  s->done = 1;
  return o.fail;
}

ssize_t
xr_feed(xr_state_t* state, const char* chunk, size_t len, XmlEventHandler* handler, void* user) {
  XmlFeed f = {handler, user, state, 0};

  if(state->flags & XR_COMPAT)
    return xr_feed_compat(state, chunk, len, handler, user);

  xr_read(xfeed_callback, chunk, len, &f, state);
  return f.failure;
}

ssize_t
xr_end(xr_state_t* state, XmlEventHandler* handler, void* user) {
  XmlFeed f = {handler, user, state, 0};

  if(state->flags & XR_COMPAT)
    return xr_end_compat(state, handler, user);

  xr_finish(xfeed_callback, &f, state);
  return f.failure;
}

/**
 * @}
 */
