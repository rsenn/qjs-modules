/* xtags.c: the HTML tag rules (void elements, raw text elements, optional end tags).
 * depends on: xtags.h only.
 * rule: names compare case-insensitively, the lists are the single copy for every XML parser. */

#define _GNU_SOURCE 1

#include "xml/xtags.h"
#include <string.h>
#include <strings.h>

/**
 * \addtogroup xtags
 * @{
 */

const char* const xtag_html_self_closing[] = {
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
static const char* const xtag_raw_text_tags[] = {
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
int
xtag_is_implied_end(const char* name, size_t namelen, const char* open_name, size_t open_namelen) {
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

int
xtag_in_list(const char* name, size_t namelen, const char* const* tags) {
  const char* const* v;

  for(v = tags; v && *v; v++) {
    size_t n = strlen(*v);

    if(n == namelen && !strncasecmp(name, *v, namelen))
      return 1;
  }

  return 0;
}

int
xtag_is_raw_text(const char* name, size_t namelen) {
  return xtag_in_list(name, namelen, xtag_raw_text_tags);
}

/**
 * @}
 */
