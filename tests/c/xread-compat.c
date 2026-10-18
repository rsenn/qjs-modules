/* xread-compat.c: xr_feed in XR_COMPAT mode against xml_next, event for event.
 * the same text goes through both at several chunk sizes and flag sets, the event
 * lists are written as text and compared. built and run by ctest (c-xread-compat). */

#define _GNU_SOURCE 1

#include "xml/xevent.h"
#include "xml/xml.h"
#include "xml/xread.h"
#include "xml/xtags.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char* buf;
  size_t len, cap;
} Out;

static int failures;

static void
out_put(Out* o, const char* s, size_t n) {
  if(o->len + n + 1 > o->cap)
    o->buf = realloc(o->buf, o->cap = (o->len + n + 1) * 2);

  memcpy(o->buf + o->len, s, n);
  o->buf[o->len += n] = 0;
}

static void
out_event(Out* o, const XmlEvent* ev) {
  char head[96];
  int n = snprintf(head, sizeof(head), "%d", ev->type);

  out_put(o, head, n);

  /* xml_next reports the position of the tag after an instruction, xread that of the instruction */
  if(ev->has_at && ev->type != XE_INSTRUCTION) {
    n = snprintf(head, sizeof(head), "@%u:%u", (unsigned)ev->at.line, (unsigned)ev->at.column);
    out_put(o, head, n);
  }

  out_put(o, " [", 2);

  if(ev->type == XE_ATTRIBUTE) {
    out_put(o, ev->name, ev->nlen);
    out_put(o, ev->value ? "=" : "!", 1);

    if(ev->value)
      out_put(o, ev->value, ev->vlen);
  } else if(ev->type == XE_ELEMENT_START || ev->type == XE_ELEMENT_END) {
    out_put(o, ev->name ? ev->name : "", ev->name ? ev->nlen : 0);
  } else {
    out_put(o, ev->str ? ev->str : "", ev->str ? ev->len : 0);
  }

  out_put(o, "]\n", 2);
}

static ssize_t
collect(void* user, const XmlEvent* ev) {
  out_event(user, ev);
  return 0;
}

static void
run_pull(const char* text, size_t n, unsigned flags, Out* o) {
  Reader rd = reader_from_bytes(text, n);
  XMLParser xp;
  XmlEvent ev;

  xml_init(&xp, &rd);
  if(flags & XR_TOLERANT)
    xml_set_tolerant(&xp, 1);

  if(flags & XR_NO_IMPLIED_END)
    xml_set_implied_end(&xp, 0);

  if(flags & XR_FLAT)
    xml_set_flat(&xp, 1);

  while(xml_next(&xp, &ev) == XML_PULL_EVENT)
    out_event(o, &ev);

  xml_free(&xp);
}

static void
run_push(const char* text, size_t n, unsigned flags, size_t step, Out* o) {
  xr_state_t st;

  xr_state_init_flags(&st, XR_COMPAT | flags);

  for(size_t i = 0; i < n; i += step)
    if(xr_feed(&st, text + i, n - i < step ? n - i : step, collect, o) < 0)
      break;

  xr_end(&st, collect, o);
  xr_state_free(&st);
}

static const unsigned flag_sets[] = {0, XR_TOLERANT, XR_TOLERANT | XR_NO_IMPLIED_END, XR_FLAT, XR_TOLERANT | XR_FLAT};

static size_t
count(const char* text, size_t n, const char* pat) {
  size_t c = 0;

  for(const char* p = text; (p = memmem(p, text + n - p, pat, 2)); p++)
    c++;

  return c;
}

static int
compare(const char* text, size_t n, int verbose) {
  static const size_t steps[] = {1, 2, 3, 7, 4096};
  int ok = 1;

  /* xml.c leaves an instruction not ended by ?> open as an element, xread does not */
  if(count(text, n, "<?") != count(text, n, "?>"))
    return 1;

  for(size_t f = 0; f < sizeof(flag_sets) / sizeof(flag_sets[0]); f++) {
    Out want = {0};

    /* flat mode: xml_next swallows the element end after an instruction, xread does not */
    if((flag_sets[f] & XR_FLAT) && memmem(text, n, "<?", 2))
      continue;

    run_pull(text, n, flag_sets[f], &want);

    for(size_t s = 0; s < sizeof(steps) / sizeof(steps[0]); s++) {
      Out got = {0};

      run_push(text, n, flag_sets[f], steps[s], &got);

      if(!want.buf || !got.buf ? want.len != got.len : strcmp(want.buf, got.buf)) {
        if(verbose)
          printf("--- flags %u step %zu text:\n%.*s\n--- want:\n%s--- got:\n%s", flag_sets[f], steps[s], (int)n, text, want.buf ? want.buf : "", got.buf ? got.buf : "");

        ok = 0;
      }

      free(got.buf);
    }

    free(want.buf);
  }

  return ok;
}

static const char* const corpus[] = {
    "<a id=\"7\" b><b/>t &amp; u<c x='1'></c></a>",
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE html>\n<!-- c --><r><![CDATA[x < y]]></r>",
    "<html><head><title>t</title><script>if(a<b && c>d) x = '</p>';</script><style>a>b{}</style></head>"
    "<body><p>one<p>two<br><img src=a.png><ul><li>a<li>b</ul></body></html>",
    "<a>\n  <b>\n    text\n  </b>\n</a>\n",
    "<a x=1 y='2' z=\"3\" w>t</a>",
    "<a><b></a>",
    "<a></b></a>",
    "<a b=\"unterminated",
    "<a><!-- x ",
    "<?php echo 1 ?><a/>",
    "text only &lt;&gt;",
    "<a>\n\n<b/>\n\n</a>",
    "<!---->x<!--->y-->",
    "<a/ ><b / >",
};

static unsigned long seed = 12345;

static unsigned
rnd(void) {
  return (unsigned)((seed = seed * 6364136223846793005UL + 1442695040888963407UL) >> 33);
}

int
main(void) {
  static const char* const atoms[] = {"<", ">", "/", "a", "b", "p", "li", "br", " ", "\n", "=", "\"", "'", "!", "-", "?", "&amp;", "x", "<!--", "-->", "<![CDATA[", "]]>", "<script>", "</script>", "<a>", "</a>", "<p>", "<br>", "id=1 "};
  int bad = 0;

  for(size_t i = 0; i < sizeof(corpus) / sizeof(corpus[0]); i++)
    if(!compare(corpus[i], strlen(corpus[i]), 1)) {
      printf("FAIL corpus %zu\n", i);
      bad++;
    }

  for(int i = 0; i < 20000; i++) {
    char buf[200];
    size_t n = 0;
    int k = 1 + rnd() % 14;

    while(k--) {
      const char* a = atoms[rnd() % (sizeof(atoms) / sizeof(atoms[0]))];

      memcpy(buf + n, a, strlen(a));
      n += strlen(a);
    }

    if(!compare(buf, n, 0)) {
      if(bad < 6)
        compare(buf, n, 1);

      bad++;
    }
  }

  failures = bad;
  printf("xread-compat: %d mismatches\n", failures);
  return failures ? 1 : 0;
}
