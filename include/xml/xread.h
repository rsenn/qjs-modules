/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/xread/blob/master/LICENSE
 */

#ifndef XREAD_H
#define XREAD_H

#include <stdint.h>
#include <stddef.h>
#include "srcpos.h"
#include "xml/xevent.h"

/**
 * \defgroup xread xread: XML push parser
 * @{
 */

/* xr_error_t: why an XE_ERROR was reported; xr_error_name() is `err.code`. */
typedef enum xr_error {
  XR_E_NONE,
  XR_E_UNEXPECTED_CHARACTER,
  XR_E_UNEXPECTED_END,
} xr_error_t;

const char* xr_error_name(xr_error_t code);

/* xr_flags: how a parser behaves, the argument of xr_state_init_flags().
 *
 *   XR_COMPAT           the parser of xml.c: markup events (comment, declaration, instruction, cdata), an
 *                       open-element stack, void elements, raw text elements (script, style, textarea, title),
 *                       lenient names and attributes, no error at the end of the input. Without it the
 *                       strict lexer of xread: elements, attributes and text only, errors on stray bytes
 *   XR_TOLERANT         (XR_COMPAT) a stray closing tag is ignored, optional end tags close open elements;
 *                       else a closing tag without an open element is a mismatched-end-tag error
 *   XR_FLAT             (XR_COMPAT) no stack: a start tag is never closed by itself, every closing tag is an
 *                       element end, raw text elements are not special
 *   XR_NO_IMPLIED_END   (XR_COMPAT) tolerant mode does not close <li>, <p>, ... when a sibling starts */
enum {
  XR_COMPAT = 1,
  XR_TOLERANT = 2,
  XR_FLAT = 4,
  XR_NO_IMPLIED_END = 8,
};

/* A single growable buffer used to accumulate one token's bytes as they're read,
 * since a token isn't necessarily contiguous in any single chunk the caller passes
 * to xr_feed(). */
typedef struct xr_accum {
  char* buf;
  size_t len;
  size_t cap;
} xr_accum_t;

/*
 * Everything xr_feed() needs to resume parsing exactly where a previous call left
 * off, including in the middle of a tag/attribute name, an attribute value, or a
 * run of text content: which lexer state to re-enter (go/resume/name_handle), and
 * four independent accumulators - tag_accum, name_accum, val_accum, text_accum -
 * since a self-closing tag's element_end event reuses the *tag* name captured for
 * its element_start event (needing its own buffer that outlives attribute
 * scanning), an attribute callback delivers its name and value together (needing
 * two more, live at once), and text content between tags needs a buffer of its
 * own since it can't reuse tag_accum/name_accum/val_accum without clobbering
 * whichever of those the surrounding tag is still using.
 *
 * Zero-initialize (or use xr_state_init()) before the first xr_feed() call.
 */
typedef struct xr_state {
  void** go;
  void* resume;
  void* name_handle;
  xr_accum_t tag_accum, name_accum, val_accum, text_accum;
  xr_accum_t* cur_accum;
  int accumulating;
  int at_root; /* go == go_root: nothing open, a clean place for xr_end() to land */
  int done;
  int error;
  /* position: `pos` counts every byte fed; err_* describe the error when reported:
   * err_pos is the offending byte (end of input for xr_finish), err_len its bytes
   * (0 at end of input). Chunk-size independent. */
  SourcePos pos;
  const char* mark; /* chunk byte that `pos` has been advanced up to (xr_feed() only) */
  xr_error_t err_code;
  SourcePos err_pos;
  size_t err_len;

  /* XR_COMPAT only */
  unsigned flags;                       /* XR_* */
  const char* const* self_closing_tags; /* void elements, null terminated: set after xr_state_init_flags() to change */
  unsigned cs;                          /* lexer state */
  unsigned closing : 1, self_closing : 1, is_pi : 1, raw : 1;
  int quote;                   /* closing quote of the attribute value being read */
  size_t raw_m;                /* raw text element: bytes of </name> matched so far */
  SourcePos tag_pos, cand_pos; /* where the current tag starts, and the '<' that may start </name> */
  xr_accum_t pi_accum;         /* the text of a processing instruction, name and attributes */
  char* fnames;                /* names of the open elements, one after the other */
  size_t fnames_len, fnames_cap;
  size_t* foffs; /* start of each name in fnames */
  size_t nframes, foffs_cap;
} xr_state_t;

void xr_state_init(xr_state_t* state);
void xr_state_init_flags(xr_state_t* state, unsigned flags);
void xr_state_free(xr_state_t* state);

/**
 * xr_feed: feeds one chunk (chunk[0..len)) to the parser, calling the handler per event that completes.
 *
 *   xr_state_t*         state    parser state, set up with xr_state_init()
 *   const char*         chunk    next bytes of the stream, chunk[0..len)
 *   size_t              len      number of bytes in chunk
 *   XmlEventHandler*    handler  receives each event; the first negative result stops delivery
 *   void*               user     passed to handler unchanged
 *
 *   returns ssize_t  0, or the first negative handler result
 *
 * a chunk boundary may fall anywhere, also mid-tag-name or mid-attribute-value, and an event is
 * sent only when it completes. No-op once state->error or state->done is set.
 *
 * text arrives in str / len; an XE_ERROR event has the message in str and the offending byte in at.
 * To feed a sink:
 *
 *   xr_feed(&st, chunk, n, xevent_handler, &sink);
 *
 * never throws. */
ssize_t xr_feed(xr_state_t* state, const char* chunk, size_t len, XmlEventHandler* handler, void* user);

/* xr_end: signals the end of the input, the twin of xr_feed().
 *
 * every XML token here has a terminator, so nothing is flushed: it checks state->at_root and
 * either sends an XE_ERROR event (truncated input) or sets state->done. */
ssize_t xr_end(xr_state_t* state, XmlEventHandler* handler, void* user);

/**
 * @}
 */

#endif // #ifndef XREAD_H
