#!/usr/bin/env qjsm

/* extract-comments.js: prints the comments of source files, or as JSON.
 * depends on: lexers (file -> lexer), util (getOpt, isMainModule).
 * rule: every position comes from the source text, not a lexer column.
 *
 * ```sh
 * extract-comments.js src/a.c                # 12: // a comment
 * extract-comments.js src/*.c                # src/a.c:12: // a comment
 * extract-comments.js -t line -m src         # C++ comments, 2+ lines
 * extract-comments.js -j -L range src/a.c    # [{"comment":..,"range":..}]
 * ```
 *
 * a language is a key of lexers.js: c (also C++), js (also TypeScript),
 * sh, cmake, make, ini, xml, bnf (also EBNF, yacc), csv (no comments). */
import * as std from 'std';
import { readFileSync } from 'fs';
import { readdir } from 'os';
import { getOpt, isMainModule } from 'util';
import { Lexers, languageFor, lexerFor } from 'lexers';

const LOC_MODES = ['line', 'offset', 'loc', 'range', 'file', 'start', 'end'];
const TYPE_ALIASES = { line: 'line', 'c++': 'line', cpp: 'line', cxx: 'line', block: 'block', c: 'block' };

/* regexp after these keywords and `)` of these statements starts a literal */
const STATEMENT_KEYWORDS = new Set(['if', 'while', 'for', 'with']);

/* a `/` after one of these tokens divides, after any other a regexp starts */
const DIVIDES_AFTER = new Set(['identifier', 'privateIdentifier', 'numericLiteral', 'stringLiteral', 'booleanLiteral', 'nullLiteral', 'templateLiteral', 'templateLiteralTail']);

/* positions: a char index -> { line, column }, both 1-based */
function lineTable(source) {
  const starts = [0];

  for(let i = source.indexOf('\n'); i != -1; i = source.indexOf('\n', i + 1)) starts.push(i + 1);

  return idx => {
    let lo = 0,
      hi = starts.length - 1;

    while(lo < hi) {
      const mid = (lo + hi + 1) >> 1;
      if(starts[mid] <= idx) lo = mid;
      else hi = mid - 1;
    }

    return { line: lo + 1, column: idx - starts[lo] + 1, lineStart: starts[lo] };
  };
}

/* lexer offsets count code points; JS string indices count UTF-16 units.
 *
 *   returns function  code point offset -> string index
 */
function indexMap(source) {
  if(!/[\ud800-\udbff][\udc00-\udfff]/.test(source)) return cp => cp;

  const map = [];
  let idx = 0;

  for(const ch of source) {
    map.push(idx);
    idx += ch.length;
  }

  map.push(idx);
  return cp => map[cp];
}

// the comments inside a C preprocessor line, which lexer/c.js keeps whole:
// `#define X 1 /* a */` is one token.
//
//   string  text  the `#...` token, continuation lines included
//
//   returns array  [{ start, end }] within `text`; `end` is -1 for a block
//                  comment still open at the end of the token
function preprocessorComments(text) {
  const found = [],
    directive = /^#\s*(\w+)/.exec(text)?.[1];
  let angle = /^(include|include_next|import)$/.test(directive ?? '');

  for(let i = 0; i < text.length; ) {
    const c = text[i];

    if(c == '"' || c == "'") {
      for(i++; i < text.length && text[i] != c; i++) if(text[i] == '\\') i++;
      i++;
    } else if(c == '<' && angle) {
      const close = text.indexOf('>', i);
      angle = false;
      i = close == -1 ? text.length : close + 1;
    } else if(c == '/' && text[i + 1] == '/') {
      found.push({ start: i, end: text.length });
      break;
    } else if(c == '/' && text[i + 1] == '*') {
      const close = text.indexOf('*/', i + 2);
      found.push({ start: i, end: close == -1 ? -1 : close + 2 });
      i = close == -1 ? text.length : close + 2;
    } else i++;
  }

  return found;
}

/* the comments of `source` as { start, end } string indices, plus the lexer
 * error that stopped the scan early, if any.
 *
 *   string  source    text to scan
 *   string  language  a key of Lexers
 *   string  file      name given to the lexer, for its error messages
 *
 *   returns object  { items, error }; never throws
 */
function findComments(source, language, file) {
  if(language == 'csv') return { items: [], error: null };

  const items = [],
    toIndex = indexMap(source),
    parens = [];
  let lexer = lexerFor(file, language)(source, file),
    skipUntil = 0,
    prev = null,
    error = null;

  try {
    for(let tok; (tok = lexer.nextToken()); ) {
      const start = toIndex(tok.loc.charOffset),
        end = toIndex(tok.loc.charOffset + tok.charLength);

      if(tok.type == 'whitespace') continue;
      if(start < skipUntil) continue;

      if(/comment/i.test(tok.type)) {
        if(language == 'sh' && start == 0 && tok.lexeme.startsWith('#!')) continue;
        items.push({ start, end });
        continue;
      }

      if(/(^|_)preprocessor$/.test(tok.type)) {
        for(const c of preprocessorComments(tok.lexeme)) {
          let stop = c.end == -1 ? source.indexOf('*/', start + c.start + 2) : start + c.end;

          if(c.end == -1) stop = stop == -1 ? source.length : stop + 2;
          items.push({ start: start + c.start, end: stop });
          skipUntil = Math.max(skipUntil, stop);
        }
        continue;
      }

      if(language == 'js') {
        const top = lexer.topState();

        if(top == 'INITIAL' || top == 'NOREGEX') {
          let divides = DIVIDES_AFTER.has(tok.type) || (tok.type == 'keyword' && (/^(this|super)$/.test(tok.lexeme) || /^\??\.$/.test(prev?.lexeme ?? '')));

          if(tok.type == 'punctuator') {
            if(tok.lexeme == '(') parens.push(prev?.type == 'keyword' && STATEMENT_KEYWORDS.has(prev.lexeme));
            else if(tok.lexeme == ')') divides = !parens.pop();
            else divides = /^(\]|\+\+|--)$/.test(tok.lexeme);
          }

          lexer.state = divides ? 'NOREGEX' : 'INITIAL';
        }

        prev = tok;
      }
    }
  } catch(e) {
    error = e;
  }

  lexer = null;
  return { items, error };
}

// what a comment is, from its opening text alone:
//
//   `// a`         -> { kind: 'line',  marker: '//' }
//   `/// a`        -> { kind: 'line',  marker: '///', doc: 'doxygen' }  (c)
//   `/** a */`     -> { kind: 'block', doc: 'doxygen' | 'jsdoc' }  (c | js)
function classify(lexeme, language) {
  if(/^(\/\*|\(\*|<!--|#\[=*\[)/.test(lexeme)) {
    let doc;

    if(language == 'c' && /^\/\*(\*(?![*\/])|!)/.test(lexeme)) doc = 'doxygen';
    if(language == 'js' && /^\/\*\*(?![*\/])/.test(lexeme)) doc = 'jsdoc';

    return { kind: 'block', marker: lexeme.slice(0, 2), doc };
  }

  const marker = /^\/\/(?:\/(?!\/)|!)?/.exec(lexeme)?.[0] ?? lexeme[0];

  return { kind: 'line', marker, doc: language == 'c' && (marker == '///' || marker == '//!') ? 'doxygen' : undefined };
}

/* `// a` lines that follow each other are one comment.
 *
 *   number  gap  blank lines allowed between two lines (0: adjacent lines)
 */
function groupComments(items, source, gap) {
  const out = [];

  for(const it of items) {
    const prev = out[out.length - 1];

    if(prev && prev.kind == 'line' && it.kind == 'line' && prev.marker == it.marker && !prev.trailing) {
      const between = source.slice(prev.end, it.start),
        breaks = between.split('\n').length - 1;

      if(/^\s*$/.test(between) && breaks >= 1 && breaks <= gap + 1) {
        prev.end = it.end;
        continue;
      }
    }

    out.push({ ...it });
  }

  return out;
}

// the comment text without its delimiters:
//
//   `/** a */`          -> "a"
//   `/* a`, ` * b */`   -> "a", "b" (one string, joined by a newline)
//   `// a`, `// b`      -> "a", "b"
//
//   returns object  { text, lead }, `lead` being the number of leading blank
//                   lines dropped, so the line numbers stay right
function stripComment(rec) {
  let lines;

  if(rec.kind == 'line') {
    lines = rec.text.split('\n').map(l => l.replace(/^[ \t]*(?:\/\/(?:\/(?!\/)|!)?<?|[#;]) ?/, '').trimEnd());
    return { text: lines.join('\n'), lead: 0 };
  }

  const star = rec.text.startsWith('/*');

  lines = rec.text
    .replace(/(?:\*\/|\*\)|-->|\]=*\])$/, '')
    .replace(/^(?:\/\*[*!]?<?|\(\*|<!--|#\[=*\[)/, '')
    .split('\n');

  if(star) lines = lines.map((l, i) => (i ? l.replace(/^[ \t]*\*(?!\/) ?/, '') : l));

  lines = lines.map(l => l.trimEnd());
  lines[0] = lines[0].trimStart();

  let lead = 0;
  while(lines.length > 1 && lines[0] == '') {
    lines.shift();
    lead++;
  }
  while(lines.length > 1 && lines[lines.length - 1] == '') lines.pop();

  return { text: lines.join('\n'), lead };
}

/* does `rec` pass the filters in `opts`? */
function keep(rec, opts) {
  if(opts.types && !opts.types.includes(rec.kind)) return false;
  if(opts.doc === true && !rec.doc) return false;
  if(opts.doc === false && rec.doc) return false;
  if(opts.style && rec.doc != opts.style) return false;
  if(rec.lines < (opts.minLines ?? 1) || rec.lines > (opts.maxLines ?? Infinity)) return false;
  if(opts.pattern && opts.pattern.test(rec.body) == !!opts.invert) return false;

  return true;
}

/* the comments of one source text, grouped and filtered.
 *
 *   string  source    text to scan
 *   string  language  a key of Lexers (see lexers.js)
 *   object  opts      all optional:
 *     string  file      reported in each record (default "<stdin>")
 *     bool    group     join adjacent line comments (default true)
 *     number  gap       blank lines a group may span (default 0)
 *     bool    strip     report the text without delimiters
 *     array   types     only these kinds: "line", "block"
 *     bool    doc       true: only doc comments, false: none
 *     string  style     only "jsdoc" or "doxygen" comments
 *     number  minLines  fewest source lines, maxLines: most
 *     RegExp  pattern   keep comments whose text matches; invert: the rest
 *
 *   returns generator  records { file, kind, marker, doc?, comment, line,
 *                      column, endLine, endColumn, offset, end, lines,
 *                      textLine }; a lexer error is thrown after the records
 *                      found before it have been yielded
 */
export function* extractComments(source, language, opts = {}) {
  const file = opts.file ?? '<stdin>',
    { items, error } = findComments(source, language, file),
    where = lineTable(source);

  for(const it of items) {
    Object.assign(it, classify(source.slice(it.start, it.end), language));
    it.trailing = /\S/.test(source.slice(where(it.start).lineStart, it.start));
  }

  for(const it of opts.group === false ? items : groupComments(items, source, opts.gap ?? 0)) {
    const a = where(it.start),
      b = where(it.end),
      rec = { file, kind: it.kind, marker: it.marker, text: source.slice(it.start, it.end).replace(/\r\n?/g, '\n') };
    const { text, lead } = opts.strip ? stripComment(rec) : { text: rec.text, lead: 0 };

    Object.assign(rec, {
      body: text,
      line: a.line,
      column: a.column,
      endLine: b.line,
      endColumn: b.column,
      offset: it.start,
      end: it.end,
      lines: b.line - a.line + 1,
      textLine: a.line + lead,
    });
    if(it.doc) rec.doc = it.doc;

    if(keep(rec, opts)) yield rec;
  }

  if(error) throw error;
}

/* a record as the JSON object `-j`/`-n` print.
 *
 *   object  rec    a record from extractComments()
 *   object  shape  { modes: ['loc', ...] (see --loc), key: 'comment' }
 *
 *   returns object  { comment, kind, doc?, ...position properties }
 */
export function toObject(rec, { modes = ['loc'], key = 'comment' } = {}) {
  const out = { [key]: rec.body, kind: rec.kind };

  if(rec.doc) out.doc = rec.doc;
  if(modes.includes('line')) Object.assign(out, { line: rec.line, column: rec.column });
  if(modes.includes('offset')) out.offset = rec.offset;
  if(modes.includes('loc')) out.loc = { line: rec.line, column: rec.column, file: rec.file };
  if(modes.includes('range')) out.range = { start: rec.offset, end: rec.end, file: rec.file };
  if(modes.includes('file')) out.file = rec.file;
  if(modes.includes('start')) out.start = `${rec.file}:${rec.line}:${rec.column}`;
  if(modes.includes('end')) out.end = `${rec.file}:${rec.endLine}:${rec.endColumn}`;

  return out;
}

/* a record as text, one output line per comment line.
 *
 *   `// a` newline `// b` -> "12: // a" newline "13: // b"   (one file)
 *                         -> "x.c:12: // a" ...               (several)
 */
export function toLines(rec, withFile) {
  const prefix = n => (withFile ? `${rec.file}:${n}: ` : `${n}: `);

  return rec.body.split('\n').map((text, i) => (prefix(rec.textLine + i) + text).trimEnd() + '\n');
}

function* walkFiles(dir) {
  const [entries, err] = readdir(dir);
  if(err || !entries) return;

  for(const entry of entries.filter(e => !e.startsWith('.') && e != 'node_modules').sort()) {
    const p = `${dir.replace(/\/+$/, '')}/${entry}`,
      [sub, subErr] = readdir(p);

    if(!subErr && sub) yield* walkFiles(p);
    else if(languageFor(entry) && languageFor(entry) != 'csv') yield p;
  }
}

/* replaces each directory in `paths` by the files below it with a lexer */
function expandPaths(paths) {
  return paths.flatMap(p => {
    const [entries, err] = readdir(p);
    return !err && entries ? [...walkFiles(p)] : [p];
  });
}

const OPTIONS = {
  pattern: [true, null, 'p'],
  'ignore-case': [false, null, 'i'],
  'invert-match': [false, null, 'v'],
  type: [true, (v, prev) => (prev ? prev + ',' : '') + v, 't'],
  doc: [false, null, 'd'],
  'no-doc': [false, null, 'D'],
  style: [true, null],
  multiline: [false, null, 'm'],
  singleline: [false, null],
  'min-lines': [true, null],
  'max-lines': [true, null],
  'no-group': [false, null, 'G'],
  'group-gap': [true, null],
  strip: [false, null, 's'],
  lang: [true, null],
  loc: [true, null, 'L'],
  'comment-key': [true, null, 'k'],
  format: [true, null, 'f'],
  json: [false, null, 'j'],
  ndjson: [false, null, 'n'],
  output: [true, null, 'o'],
  help: [false, null, 'h'],
  '@': 'paths',
};

const HELP = `Usage: extract-comments.js [OPTIONS] FILE|DIR...

Prints the comments of the given files, each line as "line: text" ("file:line: text"
with more than one file). A DIR stands for the files below it that have a lexer.
A FILE of - reads standard input and needs --lang.

Which comments:
  -p, --pattern REGEXP   only comments whose text matches (flag m; -i adds i)
  -i, --ignore-case      match case-insensitively
  -v, --invert-match     only comments that do not match
  -t, --type TYPE[,TYPE] line (c++, cpp), block (c); repeatable
  -d, --doc              only doc comments (/** */, /*! */, ///, //! in C,
                         /** */ in JS)
  -D, --no-doc           leave doc comments out
      --style NAME       only jsdoc or doxygen comments
  -m, --multiline        only comments on 2 or more lines
      --singleline       only comments on one line
      --min-lines N      only comments on at least N lines
      --max-lines N      only comments on at most N lines
  -G, --no-group         do not join adjacent line comments
      --group-gap N      join line comments up to N blank lines apart (default 0)
      --lang NAME        language, instead of from the file name: ${Object.keys(Lexers).join(', ')}

Output:
  -s, --strip            text without the delimiters (/* */, //, #, ' * ')
  -f, --format NAME      text (default), json (an array) or ndjson (one per line)
  -j, --json             same as -f json
  -n, --ndjson           same as -f ndjson
  -k, --comment-key KEY  JSON property for the text (default: comment)
  -L, --loc MODE[,MODE]  JSON positions (default: loc), as c-extract.js:
                         line   .line and .column
                         offset .offset (character offset)
                         loc    .loc   { line, column, file }
                         range  .range { start, end, file } (character offsets)
                         file   .file
                         start  .start "<file>:<line>:<column>"
                         end    .end   "<file>:<line>:<column>" (past the last character)
  -o, --output FILE      write to FILE instead of stdout
  -h, --help             show this help
`;

function fail(message) {
  std.err.puts(`extract-comments.js: ${message}\n`);
  return 1;
}

export function main(...args) {
  const params = getOpt(OPTIONS, args);

  if(params.help) {
    std.puts(HELP);
    return 0;
  }

  const format = params.json ? 'json' : params.ndjson ? 'ndjson' : (params.format ?? 'text'),
    modes = (params.loc ?? 'loc').split(','),
    types = params.type?.split(',').map(t => TYPE_ALIASES[t]),
    number = (name, min = 0) => (params[name] === undefined ? undefined : /^\d+$/.test(params[name]) && +params[name] >= min ? +params[name] : NaN),
    minLines = params.multiline ? 2 : number('min-lines'),
    maxLines = params.singleline ? 1 : number('max-lines'),
    gap = number('group-gap');
  let pattern;

  if(!['text', 'json', 'ndjson'].includes(format)) return fail(`--format: expected text, json or ndjson`);
  if(modes.some(m => !LOC_MODES.includes(m))) return fail(`--loc: expected ${LOC_MODES.join(', ')}`);
  if(types?.includes(undefined)) return fail(`--type: expected line, c++, cpp, block or c`);
  if(params.style && !['jsdoc', 'doxygen'].includes(params.style)) return fail(`--style: expected jsdoc or doxygen`);
  if(params.lang && !Object.prototype.hasOwnProperty.call(Lexers, params.lang)) return fail(`--lang: expected ${Object.keys(Lexers).join(', ')}`);
  if([minLines, maxLines, gap].some(Number.isNaN)) return fail(`--min-lines, --max-lines and --group-gap take a number`);

  if(params.pattern !== undefined) {
    try {
      pattern = new RegExp(params.pattern, 'm' + (params['ignore-case'] ? 'i' : ''));
    } catch(e) {
      return fail(`--pattern: ${e.message}`);
    }
  }

  const paths = params['@'];

  if(!paths.length) {
    std.err.puts(HELP);
    return 1;
  }

  const files = expandPaths(paths),
    opts = {
      group: !params['no-group'],
      gap,
      strip: !!params.strip,
      types,
      doc: params.doc || params.style ? true : params['no-doc'] ? false : undefined,
      style: params.style,
      minLines,
      maxLines,
      pattern,
      invert: !!params['invert-match'],
    },
    shape = { modes, key: params['comment-key'] ?? 'comment' },
    out = params.output ? std.open(params.output, 'w+') : std.out;
  let status = 0,
    pending = null;

  const emit = rec => {
    if(format == 'text') return out.puts(toLines(rec, files.length > 1).join(''));
    if(format == 'ndjson') return out.puts(JSON.stringify(toObject(rec, shape)) + '\n');

    /* a record is held back so the comma goes between objects, not after */
    out.puts(pending ? JSON.stringify(pending) + ',\n' : '[\n');
    pending = toObject(rec, shape);
  };

  for(const file of files) {
    const name = file == '-' ? '<stdin>' : file,
      language = params.lang ?? languageFor(file);

    if(!language) {
      status = fail(`${file}: no lexer for this file type, see --lang`);
      continue;
    }

    try {
      const source = file == '-' ? std.in.readAsString() : readFileSync(file, 'utf-8');

      if(typeof source != 'string') throw new Error(`${file}: cannot read`);

      for(const rec of extractComments(source, language, { ...opts, file: name })) emit(rec);
    } catch(e) {
      status = fail(String(e.message).split('\n')[0]);
    }

    std.gc();
  }

  if(format == 'json') out.puts(pending ? JSON.stringify(pending) + '\n]\n' : '[]\n');

  out.flush();
  if(params.output) out.close();

  return status;
}

if(isMainModule(import.meta.url)) std.exit(main(...scriptArgs.slice(1)));
