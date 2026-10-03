#!/usr/bin/env qjsm
import { readFileSync } from 'fs';
import { basename, extname } from 'path';
import { getOpt } from 'util';
import * as std from 'std';
import BNFLexer from 'lexer/bnf.js';
import CLexer from 'lexer/c.js';
import CMakeLexer from 'lexer/cmake.js';
import CSVLexer from 'lexer/csv.js';
import ECMAScriptLexer from 'lexer/ecmascript.js';
import IniLexer from 'lexer/ini.js';
import { GNUMakeLexer } from 'lexer/make.js';
import ShellLexer from 'lexer/shell.js';
import XMLLexer from 'lexer/xml.js';

const Lexers = {
  js: (str, file) => new ECMAScriptLexer(str, file),
  c: (str, file) => new CLexer(str, CLexer.LONGEST, file),
  bnf: (str, file) => new BNFLexer(str, file),
  csv: (str, file) => new CSVLexer(str, file),
  xml: (str, file) => new XMLLexer(str, file),
  sh: (str, file) => new ShellLexer(str, ShellLexer.LONGEST, file),
  cmake: (str, file) => new CMakeLexer(str, CMakeLexer.LONGEST, file),
  make: (str, file) => new GNUMakeLexer(str, GNUMakeLexer.LONGEST, file),
  ini: (str, file) => new IniLexer(str, IniLexer.LONGEST, file),
};

Lexers.h = Lexers.hpp = Lexers.cc = Lexers.cpp = Lexers.c;
Lexers.mjs = Lexers.cjs = Lexers.json = Lexers.ts = Lexers.js;
Lexers.g4 = Lexers.ebnf = Lexers.l = Lexers.y = Lexers.bnf;
Lexers.html = Lexers.htm = Lexers.svg = Lexers.xml;
Lexers.bash = Lexers.sh;
Lexers.mk = Lexers.mak = Lexers.make;

const MakeBasenames = /^(GNUmakefile|makefile|Makefile)$/;

function lexerFor(file) {
  const base = basename(file);

  if(/\.(ini|mc[wp])$/i.test(base)) return Lexers.ini;
  if(MakeBasenames.test(base)) return Lexers.make;
  if(base == 'CMakeLists.txt') return Lexers.cmake;

  return Lexers[extname(file).substring(1).toLowerCase()];
}

/* Each lexer names its tokens differently, so rule names are mapped onto a small
 * fixed set of categories that every output format styles the same way. */
function classify({ type, lexeme }) {
  const t = (type ?? '').toLowerCase();

  if(/comment|preprocessor|directive|shebang/.test(t)) return 'comment';
  if(/regex|template/.test(t) || lexeme[0] == '`') return 'regex';
  if(t == 'keyword' || t == lexeme.toLowerCase()) return 'keyword';
  if(/numeric|number|integer|float/.test(t) || /^[+-]?\d/.test(lexeme)) return 'number';
  if((/string|literal|quoted/.test(t) && !/boolean|null/.test(t)) || lexeme[0] == '"' || lexeme[0] == "'") return 'string';
  if(/identifier|name/.test(t)) return 'identifier';
  if(t == 'punctuator' || /^[^\w\s]+$/.test(lexeme)) return 'punct';
  return 'other';
}

/* Some lexers (xml, bnf) skip whitespace instead of emitting it as a token, so the gaps
 * between tokens are filled back in from the source text to keep the output lossless. */
function* tokens(str, lexer) {
  let pos = 0;

  for(const tok of lexer) {
    const start = tok.loc.charOffset;

    if(start > pos) yield ['other', str.slice(pos, start)];

    yield [classify(tok), tok.lexeme];
    pos = start + tok.charLength;
  }

  if(pos < str.length) yield ['other', str.slice(pos)];
}

const AnsiColors = {
  keyword: '\x1b[1;31m',
  identifier: '\x1b[1;33m',
  comment: '\x1b[1;32m',
  string: '\x1b[1;36m',
  number: '\x1b[1;34m',
  punct: '\x1b[0;36m',
  regex: '\x1b[1;35m',
  other: '\x1b[0;37m',
};

/* One palette feeds every non-legacy format; the 8-colour `ansi` format keeps AnsiColors
 * because the basic terminal palette can't be derived from RGB. */
const Palette = {
  keyword: '#d73a49',
  identifier: '#b08800',
  comment: '#6a737d',
  string: '#032f62',
  number: '#005cc5',
  punct: '#586069',
  regex: '#6f42c1',
  other: '#24292e',
};

const Categories = Object.keys(Palette);
const rgb = cat => [1, 3, 5].map(i => parseInt(Palette[cat].substr(i, 2), 16));
const cube = c => Math.round((c / 255) * 5);

const escapeHtml = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

/* RTF is 7-bit: non-ASCII becomes \uN? with N a signed UTF-16 code unit. */
const escapeRtf = s =>
  s.replace(/[\\{}\n\t]|[^\x00-\x7f]/g, ch => {
    if(ch == '\n') return '\\par\n';
    if(ch == '\t') return '\\tab ';
    if(ch < '\x80') return '\\' + ch;

    const unit = ch.charCodeAt(0);
    return `\\u${unit > 32767 ? unit - 65536 : unit}?`;
  });

const ansiFg = (code, text) => `\x1b[${code}m${text}\x1b[0m`;

/* A format is a set of string-returning hooks: head/foot wrap the whole output,
 * begin/end wrap each file, token renders one classified lexeme. opts holds the
 * command-line flags. */
const Formats = {
  ansi: {
    description: 'ANSI escape codes for the terminal (8 colors)',
    head: () => '',
    begin: () => '',
    token: (cat, text) => AnsiColors[cat] + text + '\x1b[0m',
    end: () => '\n',
    foot: () => '',
  },
  ansi256: {
    description: 'ANSI escape codes, 256-color palette',
    head: () => '',
    begin: () => '',
    token: (cat, text) => {
      const [r, g, b] = rgb(cat);
      return ansiFg(`38;5;${16 + 36 * cube(r) + 6 * cube(g) + cube(b)}`, text);
    },
    end: () => '\n',
    foot: () => '',
  },
  truecolor: {
    description: 'ANSI escape codes, 24-bit color',
    head: () => '',
    begin: () => '',
    token: (cat, text) => ansiFg('38;2;' + rgb(cat).join(';'), text),
    end: () => '\n',
    foot: () => '',
  },
  html: {
    description: 'standalone HTML page, colors as CSS classes (--fragment: bare <pre>, inline styles)',
    head: opts =>
      opts.fragment
        ? ''
        : '<!DOCTYPE html>\n<html><head><meta charset="utf-8"><title>highlight</title>\n<style>\n' +
          'pre.hl { padding: 1em; overflow: auto; background: #f6f8fa; color: #24292e; }\n' +
          Categories.map(cat => `.hl .${cat} { color: ${Palette[cat]}; }`).join('\n') +
          '\n.hl .comment { font-style: italic; }\n</style></head><body>\n',
    begin: (file, opts) => (opts.fragment ? '<pre class="hl">' : `<h3>${escapeHtml(file)}</h3>\n<pre class="hl">`),
    token: (cat, text, opts) => {
      if(cat == 'other') return escapeHtml(text);

      return opts.fragment ? `<span style="color:${Palette[cat]}">${escapeHtml(text)}</span>` : `<span class="${cat}">${escapeHtml(text)}</span>`;
    },
    end: () => '</pre>\n',
    foot: opts => (opts.fragment ? '' : '</body></html>\n'),
  },
  json: (() => {
    let files = 0,
      tokens = 0;

    return {
      description: 'JSON array, one {file, tokens: [{category, text}]} object per file',
      head: () => '[',
      begin: file => {
        tokens = 0;
        return `${files++ ? ',\n' : ''}{"file":${JSON.stringify(file)},"tokens":[`;
      },
      token: (cat, text) => (tokens++ ? ',' : '') + JSON.stringify({ category: cat, text }),
      end: () => ']}',
      foot: () => ']\n',
    };
  })(),
  rtf: {
    description: 'Rich Text Format with a color table, pastes into word processors',
    head: () =>
      '{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Courier New;}}{\\colortbl;' +
      Categories.map(cat => {
        const [r, g, b] = rgb(cat);
        return `\\red${r}\\green${g}\\blue${b};`;
      }).join('') +
      '}\\f0\\fs20\n',
    begin: file => `{\\b ${escapeRtf(file)}}\\par\n`,
    token: (cat, text) => `{\\cf${Categories.indexOf(cat) + 1} ${escapeRtf(text)}}`,
    end: () => '\\par\n',
    foot: () => '}\n',
  },
};

function usage(exitCode) {
  std.puts(
    `Usage: ${scriptArgs[0]} [OPTIONS] <files...>\n\n` +
      `Syntax-highlights files using the lexers in lib/lexer/, chosen by file name.\n\n` +
      `  -f, --format NAME  output format (default: ansi, see --list)\n` +
      `  -F, --fragment     html: emit only the <pre>, with inline styles\n` +
      `  -o, --output FILE  write to FILE instead of stdout\n` +
      `  -l, --list         list output formats\n` +
      `  -h, --help         show this help\n`,
  );
  std.exit(exitCode);
}

function listFormats() {
  for(const [name, { description }] of Object.entries(Formats)) std.puts(`${name.padEnd(10)} ${description}\n`);
  std.exit(0);
}

function main(...args) {
  const params = getOpt(
    {
      help: [false, () => usage(0), 'h'],
      list: [false, listFormats, 'l'],
      format: [true, null, 'f'],
      fragment: [false, null, 'F'],
      output: [true, null, 'o'],
      '@': 'files',
    },
    args,
  );

  const files = params['@'];
  const format = Formats[params.format ?? 'ansi'];

  if(!format) {
    std.err.puts(`unknown format '${params.format}', see --list\n`);
    std.exit(1);
  }

  if(!files.length) usage(1);

  const out = params.output ? std.open(params.output, 'w+') : std.out;
  const opts = { fragment: !!params.fragment };
  let failed = false;

  out.puts(format.head(opts));

  for(const file of files) {
    const make = lexerFor(file);

    if(!make) {
      std.err.puts(`${file}: no lexer for this file type\n`);
      failed = true;
      continue;
    }

    try {
      const str = readFileSync(file, 'utf-8');
      const lexed = [...tokens(str, make(str, file))];
      const parts = [format.begin(file, opts)];

      for(const [category, text] of lexed) parts.push(format.token(category, text, opts));

      parts.push(format.end(file, opts));
      out.puts(parts.join(''));
    } catch(error) {
      std.err.puts(`${file}: ${error.message}\n`);
      failed = true;
    }
  }

  out.puts(format.foot(opts));
  out.flush();

  if(failed) std.exit(1);
}

main(...scriptArgs.slice(1));
