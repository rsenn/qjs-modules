#include "xml/xml.h"
#include "xml/xml_entities.h"
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

/* HTML5 "raw text"/"RCDATA" elements: everything up to the matching close tag is
 * literal content, never tokenized as markup - a bare '<' from e.g. `if(a<b)` inside
 * <script> or a selector like `a>b` inside <style> must not start a bogus tag. Was
 * hardcoded to "script" only; generalized to the other three real-world raw-text
 * tags (found via quickjs-xml.c's own <style> markup misparsing while auditing HTML
 * support - <script> already worked, <style> didn't). Doesn't distinguish "raw text"
 * (script/style - no entity decoding) from "RCDATA" (textarea/title - entities *are*
 * decoded) per the HTML5 spec; this parser doesn't decode entities inside either
 * case, which is the pre-existing (not newly introduced) simplification. */
static const char* const xml_raw_text_tags[] = {
    "script",
    "style",
    "textarea",
    "title",
    0,
};

/* HTML5 "optional end tag" elements: opening one of these implicitly closes whichever
 * of the listed tags is currently open, even with no explicit closing tag in the source.
 * Without it, tolerant input that relies on it (e.g. the Netscape bookmarks format, which
 * never closes <DT>) nests every sibling one level deeper than the last: an O(n)-deep tree
 * and O(n^2) for every consumer that walks the ancestors of a node. */
typedef struct {
  const char* tag;
  const char* const closes[4];
} xml_implied_end_t;

static const xml_implied_end_t xml_implied_end_tags[] = {
    {"li", {"li", 0}},
    {"dt", {"dt", "dd", 0}},
    {"dd", {"dt", "dd", 0}},
    {"p", {"p", 0}},
    {"option", {"option", 0}},
    {"optgroup", {"option", "optgroup", 0}},
    {"tr", {"tr", 0}},
    {"td", {"td", "th", 0}},
    {"th", {"td", "th", 0}},
    {"tbody", {"thead", "tbody", "tfoot", 0}},
    {"tfoot", {"thead", "tbody", 0}},
    {0, {0}},
};

/* An open <p>, <li>, <dt> or <dd> is also closed by any block-level element, not only by
 * a matching sibling: "<DT><H3>Folder</H3><DL><p>..</DL>" (no </DT>) puts the <DL> next to
 * the <DT>, not inside it. */
static const char* const xml_block_closing_tags[] = {
    "address", "article", "aside", "blockquote", "details", "div", "dl", "fieldset", "figcaption", "figure",  "footer", "form", "h1", "h2", "h3", "h4", "h5",
    "h6",      "header",  "hr",    "main",       "menu",    "nav", "ol", "p",        "pre",        "section", "table",  "ul",   "dt", "dd", "li", 0,
};

static const char* const xml_block_closable_open_tags[] = {"p", "li", "dt", "dd", 0};

/* whether a start tag `name` closes the open element `open_name` first. */
static int
xml_is_implied_end(const char* name, size_t namelen, const char* open_name, size_t open_namelen) {
  const xml_implied_end_t* e;
  const char* const* o;

  for(o = xml_block_closable_open_tags; *o; o++) {
    if(strlen(*o) == open_namelen && !strncasecmp(open_name, *o, open_namelen)) {
      const char* const* v;

      for(v = xml_block_closing_tags; *v; v++)
        if(strlen(*v) == namelen && !strncasecmp(name, *v, namelen))
          return 1;

      break;
    }
  }

  for(e = xml_implied_end_tags; e->tag; e++) {
    if(strlen(e->tag) == namelen && !strncasecmp(name, e->tag, namelen)) {
      const char* const* closes;

      for(closes = e->closes; *closes; closes++)
        if(strlen(*closes) == open_namelen && !strncasecmp(open_name, *closes, open_namelen))
          return 1;

      return 0;
    }
  }

  return 0;
}

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

static int
xml_is_raw_text_tag(const char* name, size_t namelen) {
  return xml_is_self_closing_tag(name, namelen, xml_raw_text_tags);
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
    uint8_t b = (uint8_t)c;

    location_count(&p->loc, &b, 1); /* \n, \r\n and a lone \r are one line break; columns count characters */
  }

  return c;
}

/* Pure functions of p->name's current (already fully scanned) content - called
 * fresh every time they're needed rather than cached in a local, since p->name
 * stays put for the whole tag-open sequence but a local wouldn't survive an
 * xml_parse() suspend-and-resume (XML_PARSE_AGAIN, or having just returned an
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
 * suspending and resuming via computed goto - which re-enters xml_parse() in a
 * *fresh* stack frame - doesn't lose it.
 *
 * Every macro/helper that can hit "the Reader has nothing right now", and
 * XML_YIELD (one event ready), plants a XML_RESUME_LABEL (unique per call site, via
 * __LINE__ token-pasting) right at that point and returns; xml_parse() resumes
 * by jumping directly back to that label, past whatever one-time setup preceded it
 * at that call site. */

#define XML_PASTE_(a, b) a##b
#define XML_PASTE(a, b) XML_PASTE_(a, b)
#define XML_RESUME_LABEL XML_PASTE(l_resume_, __LINE__)

#define parse_getc() \
  do { \
    if(p->accum && p->c >= 0) \
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
#define parse_inside_raw_text() (p->top && xml_is_raw_text_tag(p->top->name, p->top->namelen))

/* Runs a xml_check_*(p) tristate call, suspending (like parse_getc()) if it returns
 * XML_FILL_AGAIN, otherwise leaving its 0/1 result in `var`. A plain statement (not
 * a GNU statement-expression) - the top-level `goto *target` in xml_parse()
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
 * resuming (the next xml_parse() call) re-enters right after this macro's own
 * call site, same mechanism as parse_getc()'s XML_PARSE_AGAIN. `hasval_`/`valdata_`/
 * `vallen_` may be 0/0/0 for an event with no value.
 *
 * Copies name/value data into dedicated ev_name/ev_value buffers so the pointers
 * remain stable until the next event is produced (unlike the old design where they
 * pointed into name/attr/text scan buffers that could be freed/reinit'd).
 */
#define XML_YIELD(event_, namedata_, namelen_, hasval_, valdata_, vallen_) \
  do { \
    /* Copy name into ev_name buffer */ \
    if((namelen_) > 0) { \
      p->ev_name.size = 0; \
      dbuf_put(&p->ev_name, (const uint8_t*)(namedata_), (namelen_)); \
      p->event_name.data = (const char*)p->ev_name.buf; \
      p->event_name.len = p->ev_name.size; \
    } else { \
      p->event_name.data = NULL; \
      p->event_name.len = 0; \
    } \
    p->event_has_value = (hasval_); \
    /* Copy value into ev_value buffer */ \
    if((hasval_) && (vallen_) > 0) { \
      p->ev_value.size = 0; \
      dbuf_put(&p->ev_value, (const uint8_t*)(valdata_), (vallen_)); \
      p->event_value.data = (const char*)p->ev_value.buf; \
      p->event_value.len = p->ev_value.size; \
    } else { \
      p->event_value.data = NULL; \
      p->event_value.len = 0; \
    } \
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

/* unused by xml_parse() itself (elements are closed via yield_return(), same as
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

#define yield_add(ptr_, len_) XML_YIELD(XE_TEXT, 0, 0, 1, (ptr_), (len_))

/* js_xml_parse()'s yield_next() creates the (still tagName-less) placeholder
 * element object; the tagName - and, for a comment/doctype, the rest of its content
 * too - isn't known until later. There's no tree to build here, so the actual
 * XE_ELEMENT_START is yielded at the point equivalent to js_xml_parse()'s
 * xml_set_attr_bytes(element,"tagName",...) call, inline in xml_parse(). This
 * macro is kept, doing nothing, purely for call-site/macro-name parity. */
#define yield_next() ((void)0)

/* Pops frames from the top of the open-element stack down through (and including)
 * `match`, yielding XE_ELEMENT_END for each - unlike js_xml_parse()'s
 * yield_return(index), which can silently vector_shrink() past several unclosed
 * elements at once because the JSValue tree's own nesting already reflects that; an
 * event-based consumer needs an explicit end for each one that tolerant recovery is
 * implicitly closing. Resumable across several xml_parse() calls (one
 * XE_ELEMENT_END per call): p->closing_target records which frame to stop at, so a
 * later call knows it's mid-yield_return() rather than looking up `match` again. */
#define yield_return(match) \
  do { \
    p->closing_target = (match); \
    while(p->closing_target && p->top) { \
      XML_YIELD(XE_ELEMENT_END, p->top->name, p->top->namelen, 0, 0, 0); \
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
xml_init(XMLParser* p, Reader* reader) {
  xml_chars_init();

  memset(p, 0, sizeof(*p));

  p->reader = reader;
  /* p->loc.line/column start at 0 (memset above) and count up from there, same
   * convention as location_zero()/location_nextchar() (src/location.c): 0-based
   * internally, +1 applied only at display time by location_print(). */
  p->self_closing_tags = xml_default_self_closing_tags;

  dbuf_init(&p->peek);
  dbuf_init(&p->name);
  dbuf_init(&p->attr);
  dbuf_init(&p->text);
  dbuf_init(&p->ev_name);
  dbuf_init(&p->ev_value);
}

void
xml_free(XMLParser* p) {
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
  dbuf_free(&p->ev_name);
  dbuf_free(&p->ev_value);
}

void
xml_set_tolerant(XMLParser* p, int tolerant) {
  p->tolerant = tolerant;
}

void
xml_set_flat(XMLParser* p, int flat) {
  p->flat = !!flat;
}

void
xml_set_implied_end(XMLParser* p, int enable) {
  p->no_implied_end = !enable;
}

int
xml_parse(XMLParser* p) {
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
    /* parse_inside_raw_text() (== p->top is a raw-text element: script/style/
     * textarea/title) is intentionally never cached in a local across this
     * function: it's needed again below, after code that may suspend and later
     * resume in a fresh stack frame, where an ordinary local's prior value
     * wouldn't survive. p->top doesn't change across this span (nothing here
     * pushes/pops it), so recomputing it each time is cheap and always gives
     * the same, correct answer. */

    dbuf_free(&p->text);
    dbuf_init(&p->text);
    p->text_pos = 0;
    p->accum = &p->text;

    if(parse_inside_raw_text()) {
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

    /* Yields p->text[0, size) as one or more XE_TEXT events - splitting on '\n'
     * and keeping leading indentation when inside a raw-text element (js_xml_parse()
     * does this so multi-line <script>/<style>/<textarea>/<title> bodies come out as
     * one text node per line), otherwise trimming leading/trailing whitespace and
     * yielding the whole run as a single event. Mirrors js_xml_parse()'s own
     * start/ptr-based loop, just over a DynBuf, with p->text_pos tracking progress
     * so a suspend mid-split (one xml_parse() call per line/segment) resumes at
     * the next one rather than restarting from the top. */
    while(p->text_pos < p->text.size) {
      const char* buf = (const char*)p->text.buf;
      const char* pos = buf + p->text_pos;
      size_t remain = p->text.size - p->text_pos;
      size_t n, real_len;

      if(parse_inside_raw_text()) {
        size_t leading_ws = scan_whitenskip(buf, p->text.size);
        size_t skip = scan_whitenskip(pos, leading_ws < remain ? leading_ws : remain);

        pos += skip;
        remain -= skip;
        p->text_pos += skip;

        size_t nl = byte_chr(pos, remain, '\n');

        if(nl < remain)
          nl++;

        real_len = n = nl;
      } else {
        /* A run that is only whitespace (indentation between sibling tags) is dropped.
         * Otherwise only the structural newlines around real content go: the writer puts
         * a newline (plus indent) first and a newline last, so the run is stripped
         * through its first newline when it starts with one, and from its last newline
         * to the end; whitespace strictly between, like the space in "Hello ", stays. */
        size_t total = remain, lead_strip = 0, trail_strip = 0, last_nl;

        if(scan_whitenskip(pos, total) == total) {
          p->text_pos += total;
          continue;
        }

        if(total > 0 && pos[0] == '\n')
          lead_strip = scan_whitenskip(pos, total);

        last_nl = byte_rchr(pos, total, '\n');
        trail_strip = (last_nl < total && last_nl >= lead_strip) ? total - last_nl : 0;

        pos += lead_strip;
        p->text_pos += lead_strip;
        real_len = n = total - lead_strip - trail_strip;
      }

      p->text_pos += real_len;

      if(n > 0)
        n = xml_decode_entities((char*)pos, n);

      if(n > 0)
        yield_add(pos, n);
    }

    if(p->done)
      break;

    /* p->c == '<', not yet consumed */
    {
      p->tag_line = p->loc.line + 1; /* p->loc already counts p->c */
      p->tag_column = p->loc.column;

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

        /* Checked *before* consuming the closing '>' below, not after: input
         * truncated before we ever saw it (e.g. "<a></a" with no final '>') is
         * the "real EOF, don't synthesize events for what's still open" case
         * this bails out for. But once parse_is() below has confirmed p->c
         * *is* '>', we've already seen a complete, valid closing tag - the
         * parse_getc() that consumes it may itself set p->done (if '>' was the
         * very last byte of input), and that must NOT stop this closing tag's
         * XE_ELEMENT_END from being yielded. Checking p->done here, before
         * that parse_getc(), used to instead bail out right after consuming a
         * legitimately-terminated final closing tag - silently dropping its
         * event (and only its event; the frame stays open forever, since
         * yield_return() never got a chance to run) whenever that tag's '>'
         * happened to be the last byte of the whole document. */
        if(p->done)
          break;

        if(parse_is(p->c, CLOSE))
          parse_getc();

        /* Resuming mid yield_return() (a previous call yielded one of possibly
         * several XE_ELEMENT_END events and returned) jumps directly into its
         * while loop via the label inside XML_YIELD - never back through this
         * find_frame()/tolerant check - so it's only ever reached fresh, with
         * p->closing_target already 0. */
        if(!p->flat && p->name.size == 0) {
          /* `</>` names the document root, which is never open: nothing to close */
        } else if(p->flat) /* no element stack: every closing tag is reported as it is */
          XML_YIELD(XE_ELEMENT_END, p->name.buf, p->name.size, 0, 0, 0);
        else {
          XMLFrame* match = xml_find_frame(p, (const char*)p->name.buf, p->name.size);

          if(!match) {
            if(!p->tolerant) {
              p->event_name.data = (const char*)p->name.buf;
              p->event_name.len = p->name.size;
              p->event_has_value = 0;
              p->done = 1;
              p->err_code = "mismatched-end-tag";
              p->err_len = p->name.size + 3; /* "</" name ">" */
              return XE_ERROR;
            }
            /* tolerant: ignore the stray closing tag; a '>' right after it stays text */
            continue;
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

        if(p->done && !xml_name_is_bang(p))
          break; /* input ends inside a tag; an unterminated comment or declaration still ends here */

        if(p->tolerant && !p->no_implied_end && !p->flat && p->top && xml_is_implied_end((const char*)p->name.buf, p->name.size, p->top->name, p->top->namelen)) {
          XML_YIELD(XE_ELEMENT_END, p->top->name, p->top->namelen, 0, 0, 0);
          yield_pop();
        }

        XML_YIELD(XE_ELEMENT_START, p->name.buf, p->name.size, 0, 0, 0);

        if(xml_name_is_bang(p)) {
          XML_YIELD(XE_ELEMENT_END, p->name.buf, p->name.size, 0, 0, 0);

          if(p->done)
            break;

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
          /* SLASH must terminate the name scan too, not just EQUAL/WS/SPECIAL/CLOSE -
           * otherwise a boolean attribute immediately followed by a self-closing '/'
           * (e.g. "<input disabled/>") swallows the '/' into the attribute name
           * itself ("disabled/") instead of stopping at it. The code right below
           * already handles p->c landing on SLASH correctly (the WS|CLOSE|SLASH
           * check yields the boolean attribute, and the outer loop's END check
           * breaks out so the self-closing detection after the loop still sees
           * it) - this scan just needs to stop there instead of consuming past it. */
          parse_until(parse_is(p->c, EQUAL | WS | SPECIAL | CLOSE | SLASH));
          p->accum = 0;

          if(p->attr.size == 0)
            break;

          if(parse_is(p->c, WS | CLOSE | SLASH)) {
            XML_YIELD(XE_ATTRIBUTE, p->attr.buf, p->attr.size, 0, 0, 0);
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

            p->text.size = xml_decode_entities((char*)p->text.buf, p->text.size);

            XML_YIELD(XE_ATTRIBUTE, p->attr.buf, p->attr.size, 1, p->text.buf, p->text.size);
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

        if(!p->self_closing) {
          if(!p->flat)
            yield_push();
        }
        else
          XML_YIELD(XE_ELEMENT_END, p->name.buf, p->name.size, 0, 0, 0);
      }
    }

    /* No parse_skipspace() here: this ran unconditionally after every tag, but only
     * has any effect after a *closing* tag - an opening tag's '>' is still unconsumed
     * at this point (the CLOSE-class check right below consumes it), so p->c can't be
     * whitespace yet, while a closing tag's own branch already consumed its '>'
     * earlier, leaving p->c sitting on whatever text follows. That asymmetry meant
     * leading whitespace of text right after a closing tag got silently eaten here,
     * before the text-accumulation loop above ever saw it - e.g.
     * "<p>before <b>x</b> after</p>" lost the space before "after". Whitespace-only
     * runs between sibling tags are already dropped by that loop (see its comment
     * above), so this was redundant for its likely intent and actively harmful for
     * genuine boundary whitespace in mixed content. */
    if(parse_is(p->c, CLOSE))
      parse_getc();
  }

  return XML_PARSE_OK;
}

/**
 * @}
 */
