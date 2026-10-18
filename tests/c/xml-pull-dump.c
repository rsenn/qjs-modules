/* xml-pull-dump.c: the events of the pull parser xml_next() with their positions, for a table of
 * inputs, flag sets and read sizes, as text.
 *
 *   xml-pull-dump                 writes the text
 *   xml-pull-dump FIXTURE         compares it with FIXTURE (ctest: c-xml-pull-dump) */

#define _GNU_SOURCE 1

#include "xml/xevent.h"
#include "xml/xml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char* data;
  size_t len, pos, step;
} Feed;

static ssize_t
feed_read(intptr_t fd, void* buf, size_t n, Reader* rd) {
  Feed* f = rd->opaque;
  size_t k = f->len - f->pos;

  (void)fd;

  if(k > f->step)
    k = f->step;
  if(k > n)
    k = n;

  memcpy(buf, f->data + f->pos, k);
  f->pos += k;
  return (ssize_t)k;
}

static void
put_escaped(FILE* out, const char* s, size_t n) {
  for(size_t i = 0; i < n; i++)
    fprintf(out, s[i] == '\n' ? "\\n" : "%c", s[i]);
}

static void
dump(FILE* out, unsigned flags, const char* s, size_t step) {
  Feed feed = {s, strlen(s), 0, step};
  Reader rd = {&feed_read, &feed, NULL, NULL};
  XMLParser xp;
  XmlEvent ev;
  XmlPull st;

  xml_init(&xp, &rd);
  xml_set_tolerant(&xp, !!(flags & 1));
  xml_set_implied_end(&xp, !(flags & 2));
  xml_set_flat(&xp, !!(flags & 4));
  fprintf(out, "flags=%u step=%zu input=", flags, step);
  put_escaped(out, s, strlen(s));
  fputc('\n', out);

  while((st = xml_next(&xp, &ev)) != XML_PULL_END && st != XML_PULL_FAIL) {
    if(st == XML_PULL_AGAIN) {
      if(feed.pos >= feed.len)
        break;

      continue;
    }

    fprintf(out, "  %d", ev.type);

    if(ev.has_at)
      fprintf(out, "@%u:%u", (unsigned)ev.at.line, (unsigned)ev.at.column);

    if(ev.self_closing)
      fputc('/', out);

    fputs(" [", out);

    if(ev.type == XE_ATTRIBUTE) {
      put_escaped(out, ev.name, ev.nlen);
      fputc(ev.value ? '=' : '!', out);

      if(ev.value)
        put_escaped(out, ev.value, ev.vlen);
    } else if(ev.type == XE_ELEMENT_START || ev.type == XE_ELEMENT_END) {
      put_escaped(out, ev.name ? ev.name : "", ev.name ? ev.nlen : 0);
    } else {
      put_escaped(out, ev.str ? ev.str : "", ev.str ? ev.len : 0);
    }

    fputs("]\n", out);
  }

  xml_free(&xp);
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

static unsigned long seed = 4711;

static unsigned
rnd(void) {
  return (unsigned)((seed = seed * 6364136223846793005UL + 1442695040888963407UL) >> 33);
}

int
main(int argc, char** argv) {
  static const char* const atoms[] = {"<", ">", "/", "a", "b", "p", "li", "br", " ", "\n", "=", "\"", "'", "!", "-", "?", "&amp;", "x", "<!--", "-->", "<![CDATA[", "]]>", "<script>", "</script>", "<a>", "</a>", "<p>", "<br>", "id=1 ", "<?xml ?>", "<!DOCTYPE html>"};
  static const unsigned flag_sets[] = {0, 1, 3, 4, 5};
  static const size_t steps[] = {1, 3, 16384};
  FILE* out = argc > 1 ? tmpfile() : stdout;
  int bad = 0;

  for(size_t i = 0; i < sizeof(corpus) / sizeof(corpus[0]); i++)
    for(size_t f = 0; f < 5; f++)
      for(size_t k = 0; k < 3; k++)
        dump(out, flag_sets[f], corpus[i], steps[k]);

  for(int i = 0; i < 400; i++) {
    char buf[200];
    size_t n = 0;
    int k = 1 + rnd() % 14;

    while(k--) {
      const char* a = atoms[rnd() % (sizeof(atoms) / sizeof(atoms[0]))];

      memcpy(buf + n, a, strlen(a));
      n += strlen(a);
    }

    buf[n] = 0;

    for(size_t f = 0; f < 5; f++)
      dump(out, flag_sets[f], buf, i % 2 ? 3 : 16384);
  }

  if(argc > 1) {
    FILE* want = fopen(argv[1], "r");
    int a, b;
    long line = 1;

    if(!want) {
      printf("cannot open %s\n", argv[1]);
      return 1;
    }

    rewind(out);

    while((a = fgetc(out)) == (b = fgetc(want)) && a != EOF)
      if(a == '\n')
        line++;

    if(a != b) {
      printf("FAIL: line %ld differs from %s\n", line, argv[1]);
      bad = 1;
    } else {
      printf("ok\n");
    }
  }

  return bad;
}
