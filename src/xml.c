#include "xml.h"
#include <stdlib.h>
#include <string.h>

/**
 * \addtogroup xml
 * @{
 */

/* Same bit flags/table as quickjs-xml.c's character_classes_init() - including its
 * EXCLAM==HYPHEN aliasing (both 0x400: '!' and '-' share a classification bit).
 * Preserved verbatim rather than "fixed" here, since this file is a port and the
 * quirk is part of js_xml_parse()'s observable behavior (e.g. it's why <?...?>
 * processing instructions don't come out self-closing). */
#define WS 0x01
#define START 0x02
#define END 0x04
#define QUOTE 0x08
#define CLOSE 0x10
#define EQUAL 0x20
#define SPECIAL 0x40
#define SLASH 0x80
#define BACKSLASH 0x100
#define QUESTION 0x200
#define EXCLAM 0x400
#define HYPHEN 0x400

static int xml_chars[256] = {0};

static const char* const xml_default_self_closing_tags[] = {
    "area",
    "base",
    "br",
    "col",
    "embed",
    "hr",
    "img",
    "input",
    "link",
    "meta",
    "param",
    "source",
    "track",
    "wbr",
    0,
};

static void
xml_chars_init(void) {
  static int done;

  if(done)
    return;

  xml_chars[' '] = WS;
  xml_chars['\t'] = WS;
  xml_chars['\r'] = WS;
  xml_chars['\n'] = WS;
  xml_chars['!'] = SPECIAL | EXCLAM;
  xml_chars['"'] = QUOTE;
  xml_chars['\''] = QUOTE;
  xml_chars['/'] = END | SLASH;
  xml_chars['<'] = START;
  xml_chars['='] = EQUAL;
  xml_chars['>'] = END | CLOSE;
  xml_chars['?'] = SPECIAL | QUESTION;
  xml_chars['\\'] = BACKSLASH;
  xml_chars['-'] = HYPHEN;

  done = 1;
}

static int
xml_is_self_closing_tag(const char* name, size_t namelen, const char* const* tags) {
  const char* const* v;

  for(v = tags; v && *v; v++) {
    size_t n = strlen(*v);

    if(n == namelen && !strncasecmp(name, *v, namelen))
      return 1;
  }

  return 0;
}

static char*
xml_dup(const void* data, size_t len) {
  char* s = malloc(len + 1);

  if(s) {
    memcpy(s, data, len);
    s[len] = '\0';
  }

  return s;
}

/* ---- low-level byte source: `reader`, plus a small lookahead FIFO (`peek`) that
 * stands in for the ptr[1]/ptr[2]/... indexing js_xml_parse gets for free from its
 * flat in-memory buffer. Location tracking lives here too, applied exactly once per
 * byte actually consumed (via xml_nextbyte()) - never for a byte that's only been
 * peeked, and never twice for one that's been retried after XML_FILL_AGAIN. ---- */

enum { XML_FILL_OK = 0, XML_FILL_EOF = 1, XML_FILL_AGAIN = 2 };

static int
xml_fill(XMLParser* p, size_t n) {
  while(p->peek.size < n) {
    int c = reader_getc(p->reader);

    if(c == STREAM_ERROR)
      return XML_FILL_AGAIN;

    if(c < 0)
      return XML_FILL_EOF;

    dbuf_putc(&p->peek, (uint8_t)c);
  }

  return XML_FILL_OK;
}

/* peek[idx] is the byte `idx+1` positions beyond the current (not-yet-consumed)
 * p->c - i.e. xml_peekbyte(p, 0) is what js_xml_parse would read as ptr[1]. -1 if
 * fewer than idx+1 bytes were available (xml_fill() having hit EOF first). */
static int
xml_peekbyte(XMLParser* p, size_t idx) {
  return idx < p->peek.size ? p->peek.buf[idx] : -1;
}

static int
xml_nextbyte(XMLParser* p) {
  int c;

  if(p->peek.size > 0) {
    c = p->peek.buf[0];
    memmove(p->peek.buf, p->peek.buf + 1, --p->peek.size);
  } else {
    c = reader_getc(p->reader);
  }

  if(c >= 0) {
    if(c == '\n') {
      p->loc.line++;
      p->loc.column = 1;
    } else {
      p->loc.column++;
    }

    p->loc.byte_offset++;
    p->loc.char_offset++;
  }

  return c;
}

/* Pure functions of p->name's current (already fully scanned) content - called
 * fresh every time they're needed rather than cached in a local, since p->name
 * stays put for the whole tag-open sequence but a local wouldn't survive an
 * xml_parser_run() suspend-and-resume (XML_PARSE_AGAIN, or having just returned an
 * event) in between. */
static int
xml_name_is_bang(const XMLParser* p) {
  return p->name.size > 0 && (xml_chars[(uint8_t)((const char*)p->name.buf)[0]] & EXCLAM);
}

static int
xml_name_is_comment(const XMLParser* p) {
  return p->name.size == 3 && !strncmp((const char*)p->name.buf, "!--", 3);
}

static XMLFrame*
xml_find_frame(XMLParser* p, const char* name, size_t namelen) {
  XMLFrame* f;

  for(f = p->top; f; f = f->parent)
    if(f->namelen == namelen && !strncmp(f->name, name, namelen))
      return f;

  return 0;
}

/* True if p->c (not yet consumed) is '<' and is immediately followed - without
 * consuming any of it - by "/" + the current open element's name + ">". Unlike
 * js_xml_parse()'s fixed 2-byte EXCLAM/HYPHEN/HYPHEN peeks, this needs namelen+2
 * bytes of lookahead, since the element name length is only known at runtime.
 * Returns XML_FILL_AGAIN if the Reader can't supply enough bytes to decide yet. */
static int
xml_check_close(XMLParser* p) {
  size_t namelen;
  int r;

  if(!p->top || p->c != '<')
    return 0;

  namelen = p->top->namelen;
  r = xml_fill(p, namelen + 2);

  if(r == XML_FILL_AGAIN)
    return XML_FILL_AGAIN;

  return p->peek.size >= namelen + 2 && p->peek.buf[0] == '/' && !strncmp((const char*)&p->peek.buf[1], p->top->name, namelen) &&
         p->peek.buf[1 + namelen] == '>';
}

/* True if p->c is '!' and is followed by "--" (comment start). */
static int
xml_check_excl_hyphen(XMLParser* p) {
  int r;

  if(!(xml_chars[(uint8_t)p->c] & EXCLAM))
    return 0;

  r = xml_fill(p, 2);

  if(r == XML_FILL_AGAIN)
    return XML_FILL_AGAIN;

  return xml_peekbyte(p, 0) == '-' && xml_peekbyte(p, 1) == '-';
}

/* True if p->c is '-' and is followed by "->" (i.e. p->c plus the next two bytes
 * spell "-->", terminating a comment). */
static int
xml_check_comment_end(XMLParser* p) {
  int r;

  if(!(xml_chars[(uint8_t)p->c] & HYPHEN))
    return 0;

  r = xml_fill(p, 2);

  if(r == XML_FILL_AGAIN)
    return XML_FILL_AGAIN;

  return xml_peekbyte(p, 0) == '-' && xml_peekbyte(p, 1) == '>';
}

/* ---- js_xml_parse()'s macros, reimplemented on top of the primitives above.
 * `p` is expected to be in scope at every call site; everything that used to be a
 * plain local in js_xml_parse() (c, done, ...) is a field of *p instead, so that
 * suspending and resuming via computed goto - which re-enters xml_parser_run() in a
 * *fresh* stack frame - doesn't lose it.
 *
 * Every macro/helper that can hit "the Reader has nothing right now", and
 * XML_YIELD (one event ready), plants a XML_RESUME_LABEL (unique per call site, via
 * __LINE__ token-pasting) right at that point and returns; xml_parser_run() resumes
 * by jumping directly back to that label, past whatever one-time setup preceded it
 * at that call site. */

#define XML_PASTE_(a, b) a##b
#define XML_PASTE(a, b) XML_PASTE_(a, b)
#define XML_RESUME_LABEL XML_PASTE(l_resume_, __LINE__)

#define parse_getc() \
  do { \
    if(p->accum) \
      dbuf_putc(p->accum, (uint8_t)p->c); \
    XML_RESUME_LABEL: \
    p->c = xml_nextbyte(p); \
    if(p->c == STREAM_ERROR) { \
      p->resume = &&XML_RESUME_LABEL; \
      return XML_PARSE_AGAIN; \
    } \
    if(p->c < 0) \
      p->done = 1; \
  } while(0)

/* no-op: js_xml_parse()'s parse_loc() advances lineno/column by hand for the byte
 * about to be consumed; here that happens unconditionally inside xml_nextbyte(),
 * once per byte actually consumed, regardless of which macro/call site did it. */
#define parse_loc() ((void)0)

#define parse_skip(cond) \
  do { \
    if(!(cond)) \
      break; \
    parse_getc(); \
  } while(!p->done)

#define parse_until(cond) parse_skip(!(cond))
#define parse_skipspace() parse_skip(xml_chars[(uint8_t)p->c] & WS)
#define parse_is(ch, classes) (xml_chars[(uint8_t)(ch)] & (classes))
#define parse_inside(tag) (p->top && strlen(tag) == p->top->namelen && !strncmp(p->top->name, (tag), p->top->namelen))

/* Runs a xml_check_*(p) tristate call, suspending (like parse_getc()) if it returns
 * XML_FILL_AGAIN, otherwise leaving its 0/1 result in `var`. A plain statement (not
 * a GNU statement-expression) - the top-level `goto *target` in xml_parser_run()
 * that resumes a suspended parse cannot legally target a label declared inside a
 * ({ ... }) expression. */
#define XML_TRY(var, call) \
  do { \
    XML_RESUME_LABEL: \
    (var) = (call); \
    if((var) == XML_FILL_AGAIN) { \
      p->resume = &&XML_RESUME_LABEL; \
      return XML_PARSE_AGAIN; \
    } \
  } while(0)

/* The pull interface itself: stashes the event's name/value, then returns it -
 * resuming (the next xml_parser_run() call) re-enters right after this macro's own
 * call site, same mechanism as parse_getc()'s XML_PARSE_AGAIN. `hasval_`/`valdata_`/
 * `vallen_` may be 0/0/0 for an event with no value. */
#define XML_YIELD(event_, namedata_, namelen_, hasval_, valdata_, vallen_) \
  do { \
    p->event_name.data = (const char*)(namedata_); \
    p->event_name.len = (size_t)(namelen_); \
    p->event_has_value = (hasval_); \
    p->event_value.data = (const char*)(valdata_); \
    p->event_value.len = (size_t)(vallen_); \
    p->resume = &&XML_RESUME_LABEL; \
    return (event_); \
    XML_RESUME_LABEL:; \
  } while(0)

#define yield_push() \
  do { \
    XMLFrame* f = malloc(sizeof(XMLFrame)); \
    f->parent = p->top; \
    f->name = xml_dup(p->name.buf, p->name.size); \
    f->namelen = p->name.size; \
    p->top = f; \
  } while(0)

/* unused by xml_parser_run() itself (elements are closed via yield_return(), same as
 * js_xml_parse() closes via yield_return()/find_tag() rather than yield_pop()) -
 * kept, like js_xml_parse()'s own yield_pop(), for API/macro-name parity. */
#define yield_pop() \
  do { \
    if(p->top) { \
      XMLFrame* f = p->top; \
      p->top = f->parent; \
      free(f->name); \
      free(f); \
    } \
  } while(0)

#define yield_add(ptr_, len_) XML_YIELD(XML_TEXT, 0, 0, 1, (ptr_), (len_))

/* js_xml_parse()'s yield_next() creates the (still tagName-less) placeholder
 * element object; the tagName - and, for a comment/doctype, the rest of its content
 * too - isn't known until later. There's no tree to build here, so the actual
 * XML_ELEMENT_START is yielded at the point equivalent to js_xml_parse()'s
 * xml_set_attr_bytes(element,"tagName",...) call, inline in xml_parser_run(). This
 * macro is kept, doing nothing, purely for call-site/macro-name parity. */
#define yield_next() ((void)0)

/* Pops frames from the top of the open-element stack down through (and including)
 * `match`, yielding XML_ELEMENT_END for each - unlike js_xml_parse()'s
 * yield_return(index), which can silently vector_shrink() past several unclosed
 * elements at once because the JSValue tree's own nesting already reflects that; an
 * event-based consumer needs an explicit end for each one that tolerant recovery is
 * implicitly closing. Resumable across several xml_parser_run() calls (one
 * XML_ELEMENT_END per call): p->closing_target records which frame to stop at, so a
 * later call knows it's mid-yield_return() rather than looking up `match` again. */
#define yield_return(match) \
  do { \
    p->closing_target = (match); \
    while(p->closing_target && p->top) { \
      XML_YIELD(XML_ELEMENT_END, p->top->name, p->top->namelen, 0, 0, 0); \
      { \
        XMLFrame* f = p->top; \
        p->top = f->parent; \
        if(f == p->closing_target) \
          p->closing_target = 0; \
        free(f->name); \
        free(f); \
      } \
    } \
  } while(0)

void
xml_parser_init(XMLParser* p, Reader* reader) {
  xml_chars_init();

  memset(p, 0, sizeof(*p));

  p->reader = reader;
  p->loc.line = 1;
  p->loc.column = 1;
  p->self_closing_tags = xml_default_self_closing_tags;

  dbuf_init(&p->peek);
  dbuf_init(&p->name);
  dbuf_init(&p->attr);
  dbuf_init(&p->text);
}

void
xml_parser_free(XMLParser* p) {
  while(p->top) {
    XMLFrame* f = p->top;

    p->top = f->parent;
    free(f->name);
    free(f);
  }

  dbuf_free(&p->peek);
  dbuf_free(&p->name);
  dbuf_free(&p->attr);
  dbuf_free(&p->text);
}

void
xml_parser_set_tolerant(XMLParser* p, int tolerant) {
  p->tolerant = tolerant;
}

void
xml_parser_set_self_closing_tags(XMLParser* p, const char* const* tags) {
  p->self_closing_tags = tags ? tags : xml_default_self_closing_tags;
}

xml_event_t
xml_parser_run(XMLParser* p) {
  if(p->resume) {
    void* target = p->resume;

    p->resume = 0;
    goto* target;
  }

  if(!p->primed) {
    parse_getc();
    p->primed = 1;

    if(p->done)
      return XML_PARSE_OK; /* empty input: clean EOF */
  }

  while(!p->done) {
    /* inside_script (== p->top is a <script> element) is intentionally never
     * cached in a local across this function: it's needed again below, after code
     * that may suspend and later resume in a fresh stack frame, where an ordinary
     * local's prior value wouldn't survive. p->top doesn't change across this span
     * (nothing here pushes/pops it), so recomputing parse_inside("script") each
     * time is cheap and always gives the same, correct answer. */

    dbuf_free(&p->text);
    dbuf_init(&p->text);
    p->text_pos = 0;
    p->accum = &p->text;

    if(parse_inside("script")) {
      for(;;) {
        if(p->c == '<') {
          int is_close;

          XML_TRY(is_close, xml_check_close(p));

          if(is_close)
            break;
        }

        parse_getc();

        if(p->done)
          break;
      }
    } else {
      parse_until(parse_is(p->c, START));
    }

    p->accum = 0;

    /* Yields p->text[0, size) as one or more XML_TEXT events - splitting on '\n'
     * and keeping leading indentation when inside a <script> (js_xml_parse() does
     * this so multi-line <script> bodies come out as one text node per line),
     * otherwise trimming leading/trailing whitespace and yielding the whole run as
     * a single event. Mirrors js_xml_parse()'s own start/ptr-based loop, just over
     * a DynBuf, with p->text_pos tracking progress so a suspend mid-split (one
     * xml_parser_run() call per line/segment) resumes at the next one rather than
     * restarting from the top. */
    while(p->text_pos < p->text.size) {
      const char* buf = (const char*)p->text.buf;
      size_t leading_ws = scan_whitenskip(buf, p->text.size);
      const char* pos = buf + p->text_pos;
      size_t remain = p->text.size - p->text_pos;
      size_t skip = scan_whitenskip(pos, leading_ws < remain ? leading_ws : remain);
      size_t n, real_len;

      pos += skip;
      remain -= skip;
      p->text_pos += skip;
      real_len = n = remain;

      if(parse_inside("script")) {
        size_t nl = byte_chr(pos, remain, '\n');

        if(nl < remain)
          nl++;

        real_len = n = nl;
      } else {
        while(n > 0 && is_whitespace_char(pos[n - 1]))
          n--;
      }

      p->text_pos += real_len;

      if(n > 0)
        yield_add(pos, n);
    }

    if(p->done)
      break;

    /* p->c == '<', not yet consumed */
    {
      parse_getc(); /* consume '<' */

      p->closing = 0;
      p->self_closing = 0;

      if(parse_is(p->c, SLASH)) {
        p->closing = 1;
        parse_getc();
      }

      dbuf_free(&p->name);
      dbuf_init(&p->name);
      p->accum = &p->name;

      {
        int is_comment_start = 0;

        if(parse_is(p->c, EXCLAM))
          XML_TRY(is_comment_start, xml_check_excl_hyphen(p));

        if(is_comment_start) {
          parse_getc();
          parse_getc();
          parse_getc();
        } else {
          parse_until(parse_is(p->c, WS | END));
        }
      }

      p->accum = 0;

      if(p->done)
        break;

      if(p->closing) {
        parse_skipspace();

        if(parse_is(p->c, CLOSE))
          parse_getc();

        if(p->done)
          break;

        /* Resuming mid yield_return() (a previous call yielded one of possibly
         * several XML_ELEMENT_END events and returned) jumps directly into its
         * while loop via the label inside XML_YIELD - never back through this
         * find_frame()/tolerant check - so it's only ever reached fresh, with
         * p->closing_target already 0. */
        {
          XMLFrame* match = xml_find_frame(p, (const char*)p->name.buf, p->name.size);

          if(!match) {
            if(!p->tolerant) {
              p->event_name.data = (const char*)p->name.buf;
              p->event_name.len = p->name.size;
              p->event_has_value = 0;
              p->done = 1;
              return XML_PARSE_ERROR;
            }
            /* tolerant: silently ignore the stray closing tag */
          } else {
            yield_return(match);
          }
        }
      } else {
        yield_next();

        if(xml_name_is_bang(p))
          p->self_closing = 1;

        if(xml_is_self_closing_tag((const char*)p->name.buf, p->name.size, p->self_closing_tags))
          p->self_closing = 1;

        if(xml_name_is_comment(p)) {
          /* scan (accumulating into p->name, right after the already-captured "!--")
           * through the terminating "--", excluding the final '>' - matching
           * js_xml_parse()'s tagName, which ends up "!-- ... --" */
          p->accum = &p->name;

          for(;;) {
            parse_getc();

            if(p->done)
              break;

            if(parse_is(p->c, HYPHEN)) {
              int is_comment_end;

              XML_TRY(is_comment_end, xml_check_comment_end(p));

              if(is_comment_end) {
                dbuf_putc(&p->name, (uint8_t)p->c); /* append 1st '-' (not yet accumulated) */
                p->c = xml_nextbyte(p);             /* consume 2nd '-' */
                dbuf_putc(&p->name, (uint8_t)p->c); /* append 2nd '-' */
                p->c = xml_nextbyte(p);             /* consume '>'; p->c becomes '>' (excluded) */
                break;
              }
            }
          }

          p->accum = 0;
        } else if(xml_name_is_bang(p)) {
          p->accum = &p->name;
          parse_until(parse_is(p->c, CLOSE));
          p->accum = 0;
        }

        if(p->done)
          break;

        XML_YIELD(XML_ELEMENT_START, p->name.buf, p->name.size, 0, 0, 0);

        if(xml_name_is_bang(p)) {
          XML_YIELD(XML_ELEMENT_END, p->name.buf, p->name.size, 0, 0, 0);
          parse_getc();
          continue;
        }

        /* attributes */
        while(!p->done) {
          parse_skipspace();

          if(parse_is(p->c, END))
            break;

          dbuf_free(&p->attr);
          dbuf_init(&p->attr);
          p->accum = &p->attr;
          parse_until(parse_is(p->c, EQUAL | WS | SPECIAL | CLOSE));
          p->accum = 0;

          if(p->attr.size == 0)
            break;

          if(parse_is(p->c, WS | CLOSE | SLASH)) {
            XML_YIELD(XML_ATTRIBUTE, p->attr.buf, p->attr.size, 0, 0, 0);
            continue;
          }

          if(parse_is(p->c, EQUAL)) {
            p->quote = 0;

            parse_getc();

            if(parse_is(p->c, QUOTE)) {
              p->quote = p->c;
              parse_getc();
            }

            dbuf_free(&p->text);
            dbuf_init(&p->text);
            p->accum = &p->text;

            if(p->quote)
              parse_until(p->c == p->quote);
            else
              parse_until(parse_is(p->c, (WS | CLOSE)));

            p->accum = 0;

            if(p->quote && parse_is(p->c, QUOTE))
              parse_getc();

            XML_YIELD(XML_ATTRIBUTE, p->attr.buf, p->attr.size, 1, p->text.buf, p->text.size);
          }
        }

        if(p->done)
          break;

        if(parse_is(p->c, SLASH)) {
          p->self_closing = 1;
          parse_getc();
        }

        if(p->name.size && parse_is(((const char*)p->name.buf)[0], QUESTION | EXCLAM))
          if(xml_chars[(uint8_t)p->c] == xml_chars[(uint8_t)((const char*)p->name.buf)[0]])
            parse_getc();

        if(p->done)
          break;

        if(!p->self_closing)
          yield_push();
        else
          XML_YIELD(XML_ELEMENT_END, p->name.buf, p->name.size, 0, 0, 0);
      }
    }

    parse_skipspace();

    if(parse_is(p->c, CLOSE))
      parse_getc();
  }

  return XML_PARSE_OK;
}

/**
 * @}
 */
