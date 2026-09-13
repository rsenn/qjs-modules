/**
 * Static-site generator for the qjs-modules GitHub Pages site.
 *
 *   qjsm tools/site/build.js [outdir]      (default outdir: _site)
 *
 * Renders the repo's own markdown into a self-contained HTML site: a
 * hand-written landing page plus every doc/{js,native}/*.md page behind a
 * shared shell with sidebar navigation and a per-page table of contents.
 * Inter-doc *.md links are rewritten to their generated pages; links pointing
 * at anything else in the repo (quickjs-*.c, lib/, tests/) become github.com
 * blob/tree URLs.
 *
 * Same shape as qjs-lws/qjs-opencv's tools/site, plus two things their
 * smaller doc sets didn't need: markdown.js also anchors every documented
 * function/class/constant (see its header), and build.js emits a search
 * index (assets/search-index.js + assets/search.js) covering every page,
 * heading and member anchor across all ~80 doc pages.
 *
 * Everything is relative-path linked so the site works both at
 * https://rsenn.github.io/qjs-modules/ and from a local file:// checkout.
 */

import * as fs from 'fs';
import { render } from './markdown.js';
import { highlight } from './highlight.js';

const REPO = 'rsenn/qjs-modules';
const GITHUB = 'https://github.com/' + REPO;
const TAGLINE = 'The standard library QuickJS deserves';

/* --------------------------------------------------------------- site map */

const NAV = [
  {
    group: 'Start here',
    pages: [
      ['README.md', 'getting-started.html', 'Getting started'],
      ['TODO.md', 'roadmap.html', 'Roadmap'],
    ],
  },
  {
    group: 'Reference',
    pages: [
      ['doc/README.md', 'docs/index.html', 'API reference'],
      ['doc/api-compatibility.md', 'docs/api-compatibility.html', 'Standards compatibility'],
      ['doc/api-compatibility-plan.md', 'docs/api-compatibility-plan.html', 'Compatibility roadmap'],
      ['doc/grammar.md', 'docs/grammar.html', 'Grammar'],
      ['doc/buffer.md', 'docs/buffer.html', 'Buffer handling'],
      ['doc/readline.md', 'docs/readline.html', 'Readline'],
    ],
  },
  {
    group: 'Native — Data & Text',
    pages: [
      ['doc/native/README.md', 'docs/native/index.html', 'Native modules overview'],
      ['doc/native/archive.md', 'docs/native/archive.html', 'archive'],
      ['doc/native/arraybuffer-sink.md', 'docs/native/arraybuffer-sink.html', 'arraybuffer-sink'],
      ['doc/native/bcrypt.md', 'docs/native/bcrypt.html', 'bcrypt'],
      ['doc/native/bjson.md', 'docs/native/bjson.html', 'bjson'],
      ['doc/native/blob.md', 'docs/native/blob.html', 'blob'],
      ['doc/native/json.md', 'docs/native/json.html', 'json'],
      ['doc/native/lexer.md', 'docs/native/lexer.html', 'lexer'],
      ['doc/native/list.md', 'docs/native/list.html', 'list'],
      ['doc/native/textcode.md', 'docs/native/textcode.html', 'textcode'],
      ['doc/native/xml.md', 'docs/native/xml.html', 'xml'],
      ['doc/native/yaml.md', 'docs/native/yaml.html', 'yaml'],
    ],
  },
  {
    group: 'Native — System & I/O',
    pages: [
      ['doc/native/child-process.md', 'docs/native/child-process.html', 'child-process'],
      ['doc/native/directory.md', 'docs/native/directory.html', 'directory'],
      ['doc/native/gpio.md', 'docs/native/gpio.html', 'gpio'],
      ['doc/native/location.md', 'docs/native/location.html', 'location'],
      ['doc/native/magic.md', 'docs/native/magic.html', 'magic'],
      ['doc/native/misc.md', 'docs/native/misc.html', 'misc'],
      ['doc/native/mmap.md', 'docs/native/mmap.html', 'mmap'],
      ['doc/native/path.md', 'docs/native/path.html', 'path'],
      ['doc/native/serial.md', 'docs/native/serial.html', 'serial'],
      ['doc/native/sockets.md', 'docs/native/sockets.html', 'sockets'],
      ['doc/native/syscallerror.md', 'docs/native/syscallerror.html', 'syscallerror'],
    ],
  },
  {
    group: 'Native — Objects & Streams',
    pages: [
      ['doc/native/deep.md', 'docs/native/deep.html', 'deep'],
      ['doc/native/inspect.md', 'docs/native/inspect.html', 'inspect'],
      ['doc/native/pointer.md', 'docs/native/pointer.html', 'pointer'],
      ['doc/native/predicate.md', 'docs/native/predicate.html', 'predicate'],
      ['doc/native/tree-walker.md', 'docs/native/tree-walker.html', 'tree-walker'],
      ['doc/native/queue.md', 'docs/native/queue.html', 'queue'],
      ['doc/native/repeater.md', 'docs/native/repeater.html', 'repeater'],
      ['doc/native/stream.md', 'docs/native/stream.html', 'stream'],
      ['doc/native/virtual.md', 'docs/native/virtual.html', 'virtual'],
    ],
  },
  {
    group: 'Native — Databases',
    pages: [
      ['doc/native/mysql.md', 'docs/native/mysql.html', 'mysql'],
      ['doc/native/pgsql.md', 'docs/native/pgsql.html', 'pgsql'],
    ],
  },
  {
    group: 'JS — Core & Extensions',
    pages: [
      ['doc/js/README.md', 'docs/js/index.html', 'JS modules overview'],
      ['doc/js/util.md', 'docs/js/util.html', 'util'],
      ['doc/js/reflect.md', 'docs/js/reflect.html', 'reflect'],
      ['doc/js/iterator.md', 'docs/js/iterator.html', 'iterator'],
      ['doc/js/asyncIterator.md', 'docs/js/asyncIterator.html', 'asyncIterator'],
      ['doc/js/arrayLike.md', 'docs/js/arrayLike.html', 'arrayLike'],
      ['doc/js/extendArray.md', 'docs/js/extendArray.html', 'extendArray'],
      ['doc/js/extendArrayBuffer.md', 'docs/js/extendArrayBuffer.html', 'extendArrayBuffer'],
      ['doc/js/extendObject.md', 'docs/js/extendObject.html', 'extendObject'],
      ['doc/js/extendMap.md', 'docs/js/extendMap.html', 'extendMap'],
      ['doc/js/extendSet.md', 'docs/js/extendSet.html', 'extendSet'],
      ['doc/js/extendMath.md', 'docs/js/extendMath.html', 'extendMath'],
      ['doc/js/extendFunction.md', 'docs/js/extendFunction.html', 'extendFunction'],
      ['doc/js/extendAsyncFunction.md', 'docs/js/extendAsyncFunction.html', 'extendAsyncFunction'],
      ['doc/js/extendGenerator.md', 'docs/js/extendGenerator.html', 'extendGenerator'],
      ['doc/js/extendAsyncGenerator.md', 'docs/js/extendAsyncGenerator.html', 'extendAsyncGenerator'],
    ],
  },
  {
    group: 'JS — Runtime',
    pages: [
      ['doc/js/assert.md', 'docs/js/assert.html', 'assert'],
      ['doc/js/console.md', 'docs/js/console.html', 'console'],
      ['doc/js/process.md', 'docs/js/process.html', 'process'],
      ['doc/js/events.md', 'docs/js/events.html', 'events'],
      ['doc/js/abort.md', 'docs/js/abort.html', 'abort'],
      ['doc/js/timers.md', 'docs/js/timers.html', 'timers'],
      ['doc/js/perf_hooks.md', 'docs/js/perf_hooks.html', 'perf_hooks'],
      ['doc/js/module.md', 'docs/js/module.html', 'module'],
      ['doc/js/require.md', 'docs/js/require.html', 'require'],
      ['doc/js/stack.md', 'docs/js/stack.html', 'stack'],
    ],
  },
  {
    group: 'JS — I/O & Filesystem',
    pages: [
      ['doc/js/fs.md', 'docs/js/fs.html', 'fs'],
      ['doc/js/fsPromises.md', 'docs/js/fsPromises.html', 'fsPromises'],
      ['doc/js/io.md', 'docs/js/io.html', 'io'],
      ['doc/js/streams.md', 'docs/js/streams.html', 'streams'],
      ['doc/js/vfs.md', 'docs/js/vfs.html', 'vfs'],
      ['doc/js/inotify.md', 'docs/js/inotify.html', 'inotify'],
      ['doc/js/tty.md', 'docs/js/tty.html', 'tty'],
      ['doc/js/terminal.md', 'docs/js/terminal.html', 'terminal'],
      ['doc/js/socklen_t.md', 'docs/js/socklen_t.html', 'socklen_t'],
    ],
  },
  {
    group: 'JS — Parsing & DOM',
    pages: [
      ['doc/js/dom.md', 'docs/js/dom.html', 'dom'],
      ['doc/js/xpath.md', 'docs/js/xpath.html', 'xpath'],
      ['doc/js/parser.md', 'docs/js/parser.html', 'parser'],
      ['doc/js/parsel.md', 'docs/js/parsel.html', 'parsel'],
      ['doc/js/css-selectors.md', 'docs/js/css-selectors.html', 'css-selectors'],
      ['doc/js/css3-selectors.md', 'docs/js/css3-selectors.html', 'css3-selectors'],
      ['doc/js/url.md', 'docs/js/url.html', 'url'],
    ],
  },
  {
    group: 'JS — Databases & Tooling',
    pages: [
      ['doc/js/db.md', 'docs/js/db.html', 'db'],
      ['doc/js/repl.md', 'docs/js/repl.html', 'repl'],
      ['doc/js/testharness.md', 'docs/js/testharness.html', 'testharness'],
      ['doc/js/testharnessreport.md', 'docs/js/testharnessreport.html', 'testharnessreport'],
    ],
  },
];

const PAGES = [];
for (const { group, pages } of NAV)
  for (const [src, out, title] of pages) PAGES.push({ src, out, title, group });

const bySrc = new Map(PAGES.map(p => [p.src, p]));

/* ------------------------------------------------------------ path helpers */

const dirname = p => (p.indexOf('/') >= 0 ? p.slice(0, p.lastIndexOf('/')) : '');

function normalize(p) {
  const out = [];
  for (const part of p.split('/')) {
    if (!part || part === '.') continue;
    if (part === '..') out.pop();
    else out.push(part);
  }
  return out.join('/');
}

/** Path from a directory to a file, both site-relative. */
function relative(fromDir, to) {
  const f = fromDir ? fromDir.split('/') : [];
  const t = to.split('/');
  let i = 0;
  while (i < f.length && i < t.length - 1 && f[i] === t[i]) i++;
  return '../'.repeat(f.length - i) + t.slice(i).join('/');
}

function mkdirp(path) {
  let cur = path.startsWith('/') ? '/' : '';
  for (const part of path.split('/')) {
    if (!part) continue;
    cur += (cur ? '/' : '') + part;
    if (!fs.existsSync(cur)) fs.mkdirSync(cur, 0o755);
  }
}

function read(path) {
  return fs.readFileSync(path, 'utf8');
}

function write(path, text) {
  mkdirp(dirname(path));
  fs.writeFileSync(path, text);
}

/* --------------------------------------------------------- link rewriting */

/** Rewrite one markdown href found in `page` into a link that works on the site. */
function linkFor(page, href) {
  if (!href || href.startsWith('#') || href.startsWith('//') || /^[a-z][a-z0-9+.-]*:/i.test(href))
    return href;

  const hash = href.indexOf('#');
  const path = hash < 0 ? href : href.slice(0, hash);
  const frag = hash < 0 ? '' : href.slice(hash);
  if (!path) return href;

  const target = normalize(dirname(page.src) + '/' + path);
  const hit = bySrc.get(target);
  if (hit) return relative(dirname(page.out), hit.out) + frag;

  const kind = path.endsWith('/') ? 'tree' : 'blob';
  return GITHUB + '/' + kind + '/main/' + target + frag;
}

/* ------------------------------------------------------------- html shell */

const escAttr = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');

function sidebar(page) {
  const here = dirname(page.out);
  let html = '';
  for (const { group, pages } of NAV) {
    html += '<div class="navgroup"><h3>' + group + '</h3><ul>';
    for (const [, out, title] of pages) {
      const cls = out === page.out ? ' class="here"' : '';
      html += '<li><a href="' + escAttr(relative(here, out)) + '"' + cls + '>' + title + '</a></li>';
    }
    html += '</ul></div>';
  }
  return html;
}

function toc(headings) {
  const items = headings.filter(h => h.level >= 2 && h.level <= 3);
  if (items.length < 2) return '';
  const links = items.map(h =>
    '<li class="lvl' + h.level + '"><a href="#' + escAttr(h.id) + '">' + escAttr(h.text) + '</a></li>').join('');
  return '<nav class="toc"><h3>On this page</h3><ul>' + links + '</ul></nav>';
}

function shell({ title, root, body, cls }) {
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${escAttr(title)}</title>
<meta name="description" content="${escAttr(TAGLINE)} — WHATWG/W3C/Node-shaped native modules and JS helpers for QuickJS.">
<link rel="stylesheet" href="${root}assets/style.css">
<link rel="icon" href="${root}assets/favicon.svg" type="image/svg+xml">
<script>try{var t=localStorage.getItem('theme');if(t)document.documentElement.dataset.theme=t}catch(e){}</script>
</head>
<body class="${cls}" data-root="${root}">
<header class="topbar">
  <a class="brand" href="${root}index.html"><span class="mark">{ }</span> qjs-modules</a>
  <div class="search">
    <input id="site-search" type="search" placeholder="Search docs… ( / )" autocomplete="off" spellcheck="false">
  </div>
  <nav class="topnav">
    <a href="${root}getting-started.html">Get started</a>
    <a href="${root}docs/index.html">Docs</a>
    <a href="${root}roadmap.html">Roadmap</a>
    <a href="${GITHUB}" target="_blank" rel="noopener">GitHub</a>
  </nav>
  <button class="themetoggle" type="button" aria-label="Toggle colour scheme">◐</button>
</header>
${body}
<footer class="sitefoot">
  <p>qjs-modules — MIT licensed. Built from the repo's own markdown by
     <a href="${GITHUB}/blob/main/tools/site/build.js">tools/site/build.js</a>, running on qjsm.</p>
</footer>
<script src="${root}assets/search-index.js"></script>
<script src="${root}assets/search.js"></script>
<script>
document.querySelector('.themetoggle').addEventListener('click', function () {
  var d = document.documentElement;
  var dark = d.dataset.theme ? d.dataset.theme === 'dark'
    : matchMedia('(prefers-color-scheme: dark)').matches;
  d.dataset.theme = dark ? 'light' : 'dark';
  try { localStorage.setItem('theme', d.dataset.theme); } catch (e) {}
});
</script>
</body>
</html>
`;
}

/* ------------------------------------------------------------------ build */

const SEARCH = [];

function buildPage(page) {
  const md = read(page.src);
  const { html, headings, members } = render(md, {
    link: href => linkFor(page, href),
    highlight,
  });

  const depth = page.out.split('/').length - 1;
  const root = '../'.repeat(depth);
  const h1 = headings.find(h => h.level === 1);
  const pageTitle = h1 ? h1.text : page.title;

  SEARCH.push({ u: page.out, t: pageTitle, k: 'page' });
  for (const h of headings)
    if (h.level >= 2) SEARCH.push({ u: page.out + '#' + h.id, t: h.text, p: pageTitle, k: 'section' });
  for (const m of members)
    SEARCH.push({ u: page.out + '#' + m.id, t: m.text, p: m.section || pageTitle, k: 'member' });

  const body = `<div class="layout">
<aside class="sidebar">${sidebar(page)}</aside>
<main class="doc">
<article>${html}</article>
<p class="editlink"><a href="${GITHUB}/blob/main/${page.src}">Edit this page on GitHub →</a></p>
</main>
${toc(headings)}
</div>`;

  write(OUT + '/' + page.out, shell({
    title: pageTitle + ' — qjs-modules',
    root, body, cls: 'has-sidebar',
  }));
}

/** Strip the common leading indentation from a block of source text. */
function dedent(text) {
  const lines = text.replace(/^\n+|\s+$/g, '').split('\n');
  const pad = Math.min(...lines.filter(l => l.trim()).map(l => l.match(/^ */)[0].length));
  return lines.map(l => l.slice(pad)).join('\n');
}

/**
 * The landing page is hand-written HTML, so its code samples are marked up as
 * <x-code lang="js">…</x-code> and expanded here through the same highlighter
 * the markdown pages use.
 */
function expandCode(html) {
  return html.replace(/<x-code lang="([^"]*)">([\s\S]*?)<\/x-code>/g, (m, lang, body) => {
    const src = dedent(body).replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&');
    return '<div class="codeblock" data-lang="' + escAttr(lang) + '"><pre><code class="language-' +
      escAttr(lang) + '">' + highlight(src, lang) + '</code></pre></div>';
  });
}

function buildLanding() {
  // placeholders first: they also appear inside <x-code>, where expansion
  // would otherwise scatter them across highlight spans
  const body = expandCode(read(SELF + '/landing.html')
    .replace(/\{\{GITHUB\}\}/g, GITHUB)
    .replace(/\{\{REPO\}\}/g, REPO));
  write(OUT + '/index.html', shell({
    title: 'qjs-modules — ' + TAGLINE,
    root: '', body, cls: 'landing',
  }));
}

/** JSON-safe-ish emit: search entries are our own generated strings (doc
 * headings/table cells), never arbitrary input, so JSON.stringify is enough -
 * no need for an HTML-escaping pass like the page templates use. */
function buildSearchIndex() {
  write(OUT + '/assets/search-index.js', 'window.SEARCH_INDEX = ' + JSON.stringify(SEARCH) + ';\n');
}

const SELF = dirname(import.meta.url.replace(/^file:\/\//, '')) || '.';
const OUT = (typeof scriptArgs !== 'undefined' ? scriptArgs[1] : process.argv[2]) || '_site';

buildLanding();
for (const page of PAGES) buildPage(page);
buildSearchIndex();

write(OUT + '/assets/style.css', read(SELF + '/style.css'));
write(OUT + '/assets/search.js', read(SELF + '/search.js'));
write(OUT + '/assets/favicon.svg', read(SELF + '/favicon.svg'));
write(OUT + '/.nojekyll', '');

console.log('built ' + (PAGES.length + 1) + ' pages (' + SEARCH.length + ' search entries) into ' + OUT + '/');
