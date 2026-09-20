#!/usr/bin/env qjsm

// source-scanner.js - non-preprocessing C source scanner.
//
// Tokenizes .c/.h files with lexer/c.js (no macro expansion: #define/#pragma/etc are
// opaque). Follows local `#include "..."` lines into the same scan (system
// `#include <...>` headers are skipped - not ours to report on). Streams one NDJSON
// record per identifier token reached this way:
//
//   { name, type, file, range, conditions, static?, context? }
//
// `type` is 'proto' (prototype, ends in ';'), 'decl' (definition, has a '{...}' body -
// both detected the same way lib/c-functions.js's scanTopLevel() does: a top-level
// `name '(' ... ')'` shape, and both carry `static` - whether the declaration itself
// has internal linkage, relevant because a name-only usage correlation has to scope a
// static function's uses to the same file, or two same-named statics in different
// files mask each other) or 'ref' (any other identifier that matches a name already
// seen as a 'proto' or 'decl' - a known function - earlier in this same scan; type
// names/locals/struct fields never match one, so they're never reported as 'ref').
// Single-pass and streaming (no full pre-scan): a use reached before its matching
// proto/decl - e.g. a .c file processed before the header declaring a function it
// calls, depending on #include order across the given entry files - is silently not
// reported, an accepted tradeoff for staying streamable. `range` is `[startLine,
// endLine]` (equal for a single-token ref). Comments/whitespace are never tokenized
// as identifiers, so they're never reported.
//
// `conditions` is the stack of enclosing `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`
// directive texts active at this token, outermost first - tracked, not evaluated (no
// macro expansion means there's no way to know which branch is "real"; both sides of
// an `#ifdef`/`#else` get scanned and reported, tagged with which branch they're in).
// A consequence of not evaluating: if the two branches of one `#if` open/close a
// different number of braces than each other (rare but real, e.g. an `#ifdef`
// wrapping only an opening or closing brace of an otherwise-shared block), the brace
// depth this scanner computes past that point can drift - same class of imprecision
// already accepted for macro-generated code.
//
// `context` (only on 'ref' records) says where the identifier was found: `{ in:
// 'function'|'data', static, enclosing }` - `in: 'function'` means inside a function
// body (`enclosing` = that function's name, `static` = whether it's a static
// function); `in: 'data'` means a top-level declarator/initializer, e.g. a
// `JSCFunctionListEntry` table (`enclosing` = the declared name if found, `static` =
// whether the declaration itself is static - relevant because a `static` table's
// references are only reachable from within this same file, unlike an exported one).
//
// This is a pure source-level alternative to nm-symbols.js's compiled-binary
// approach: no build required, so it's immune to inlining/-O level/linker
// --gc-sections differences entirely (the actual blocker nm-symbols.js has - see
// TODO.md), and in particular it sees a function-pointer-table entry (`.u.func.cfunc.
// generic = js_foo`) as a 'ref' in `data` context, something no compiled-binary
// symbol/relocation walk short of a full call graph can tell apart from "the table
// itself is dead". Its own known blind spot: it correlates by bare name, so two
// same-named `static` functions in different files can mask each other (same caveat
// remove-dead-functions.js's docstring already gives for name-only matching) - the
// `context.static` flag on 'ref' records is there so a consumer can restrict
// correlation to same-file uses for a `static` definition instead of matching
// project-wide.
//
// No bracket ('[' ']') balancing is needed: function bodies/signatures never require
// array-subscript nesting awareness to find their boundaries (paren/brace balancing
// alone suffices), and a bare identifier used as an array index still classifies
// correctly as a 'ref' without it.

import { puts, loadFile, open, exit } from 'std';
import { readdir } from 'os';
import { getOpt, isMainModule } from 'util';
import { dirname, join, normalize, exists } from 'path';
import CLexer from 'lexer/c.js';

const SKIP_TYPES = new Set(['whitespace', 'singleLineComment', 'multiLineComment']);
const INCLUDE_RE = /^#\s*include\s*(["<])([^">]+)[">]/;
const COND_RE = /^#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$/;

function tokenize(source, filename) {
  const lexer = new CLexer(source, undefined, filename);
  const toks = [];

  let tok;
  while((tok = lexer.nextToken())) if(!SKIP_TYPES.has(tok.type)) toks.push(tok);

  return toks;
}

/**
 * Skips a balanced '(' ... ')' group starting at toks[i] (an lparen). Returns the
 * index just past the matching rparen, or -1 if unbalanced.
 */
function skipParens(toks, i) {
  let depth = 0;

  do {
    if(toks[i].type === 'lparen') depth++;
    else if(toks[i].type === 'rparen') depth--;
    i++;
  } while(i < toks.length && depth > 0);

  return depth === 0 ? i : -1;
}

/**
 * Skips trailing attribute-like annotations between a function's ')' and its
 * body/';', e.g. `__attribute__((noreturn))` - same as lib/c-functions.js.
 */
function skipQualifiers(toks, i) {
  for(;;) {
    if(toks[i]?.type === 'const') {
      i++;
      continue;
    }

    if(toks[i]?.type === 'identifier' && toks[i + 1]?.type === 'lparen') {
      const j = skipParens(toks, i + 1);
      if(j === -1) break;
      i = j;
      continue;
    }

    break;
  }

  return i;
}

/** Finds the line of the '}' matching the '{' at toks[openIndex], without consuming. */
function matchingBraceLine(toks, openIndex) {
  let depth = 1,
    j = openIndex + 1;

  while(j < toks.length && depth > 0) {
    if(toks[j].type === 'lbrace') depth++;
    else if(toks[j].type === 'rbrace') depth--;
    j++;
  }

  return toks[j - 1]?.loc.line;
}

/**
 * Resolves a quoted `#include "path"` the way a compiler would for quote-includes:
 * relative to the including file's own directory first, then each of `includeDirs`.
 * Angle-bracket includes are the caller's job to have already filtered out.
 *
 * @returns {string|null} the resolved path, or null if not found anywhere searched
 */
function resolveInclude(literal, fromFile, includeDirs) {
  const candidates = [join(dirname(fromFile), literal), ...includeDirs.map(dir => join(dir, literal))];

  for(const candidate of candidates) {
    const p = normalize(candidate);
    if(exists(p)) return p;
  }

  return null;
}

function directiveString(frame) {
  return frame.kind === 'else' ? `#else /* of ${frame.origText} */` : frame.text;
}

/**
 * Scans one file's already-tokenized stream, yielding { name, type, file, range,
 * conditions, context? } records. Calls `onInclude(resolvedPath)` for each followed
 * local #include. `knownNames` is shared across every file in a scanFiles() run:
 * a 'ref' is only emitted for a name already seen as a 'proto' or 'decl' (in this
 * file or an earlier one) - cuts out type names/locals/struct fields, which never
 * have either. Single-pass, so a use reached before its matching proto/decl (e.g. a
 * .c file processed before the header declaring a function it calls, depending on
 * #include order across the given entry files) is silently not reported - accepted
 * tradeoff for staying streamable rather than needing a full first pass.
 */
function* scanTokens(toks, file, onInclude, knownNames) {
  const conditions = [];
  const frames = []; // stack of { kind: 'function'|'data', name, static }

  let topLevelStatic = false;
  let topLevelName = null;
  let pendingFrame = null; // set right before a '{' that's known to start a function body

  const context = () => {
    const top = frames[frames.length - 1];
    return top ? { in: top.kind, static: top.static, enclosing: top.name } : { in: 'data', static: topLevelStatic, enclosing: topLevelName };
  };

  for(let i = 0; i < toks.length; i++) {
    const tok = toks[i];

    if(tok.type === 'preprocessor') {
      const inc = INCLUDE_RE.exec(tok.lexeme);
      if(inc) {
        if(inc[1] === '"') onInclude(inc[2]);
        continue;
      }

      const m = COND_RE.exec(tok.lexeme.replace(/\\\r?\n/g, ' '));
      if(m) {
        const [, kw, rest] = m;
        const text = `#${kw}${rest}`.trim().replace(/\s+/g, ' ');

        if(kw === 'if' || kw === 'ifdef' || kw === 'ifndef') conditions.push({ kind: 'if', text, origText: text });
        else if(kw === 'elif') {
          if(conditions.length) conditions[conditions.length - 1] = { kind: 'elif', text, origText: conditions[conditions.length - 1].origText };
        } else if(kw === 'else') {
          if(conditions.length) conditions[conditions.length - 1] = { kind: 'else', text: '#else', origText: conditions[conditions.length - 1].origText };
        } else if(kw === 'endif') conditions.pop();
      }
      continue;
    }

    if(tok.type === 'lbrace') {
      frames.push(pendingFrame ?? { kind: 'data', name: topLevelName, static: topLevelStatic });
      pendingFrame = null;
      continue;
    }

    if(tok.type === 'rbrace') {
      frames.pop();
      if(frames.length === 0) {
        topLevelStatic = false;
        topLevelName = null;
      }
      continue;
    }

    if(frames.length === 0) {
      if(tok.type === 'static') topLevelStatic = true;

      if(tok.type === 'semi') {
        topLevelStatic = false;
        topLevelName = null;
        continue;
      }

      if(tok.type === 'identifier' && toks[i + 1]?.type === 'lparen') {
        const afterParams = skipParens(toks, i + 1);

        if(afterParams !== -1) {
          const afterQual = skipQualifiers(toks, afterParams);
          const conds = conditions.map(directiveString);

          if(toks[afterQual]?.type === 'lbrace') {
            const endLine = matchingBraceLine(toks, afterQual);
            knownNames.add(tok.lexeme);
            yield { name: tok.lexeme, type: 'decl', file, range: [tok.loc.line, endLine], conditions: conds, static: topLevelStatic };
            pendingFrame = { kind: 'function', name: tok.lexeme, static: topLevelStatic };
            i = afterQual - 1;
            continue;
          }

          if(toks[afterQual]?.type === 'semi') {
            knownNames.add(tok.lexeme);
            yield { name: tok.lexeme, type: 'proto', file, range: [tok.loc.line, toks[afterQual].loc.line], conditions: conds, static: topLevelStatic };
            i = afterQual;
            continue;
          }
        }
      }
    }

    if(tok.type !== 'identifier') continue;

    if(frames.length === 0) topLevelName = tok.lexeme;

    if(knownNames.has(tok.lexeme)) {
      yield {
        name: tok.lexeme,
        type: 'ref',
        file,
        range: [tok.loc.line, tok.loc.line],
        conditions: conditions.map(directiveString),
        context: context(),
      };
    }
  }
}

/**
 * Scans `entryFiles`, following local #include "..." lines, yielding every
 * proto/decl/ref record reached. `visited` (a Set of resolved paths) lets repeated
 * calls share one file across a whole session, same as real header guards.
 *
 * @param {string[]} entryFiles
 * @param {string[]} includeDirs
 * @param {Set<string>} [visited]
 */
export function* scanFiles(entryFiles, includeDirs = ['include'], visited = new Set()) {
  const queue = [...entryFiles];
  const knownNames = new Set(); // shared across every file - see scanTokens()'s doc comment

  while(queue.length) {
    const file = queue.shift();
    const resolved = normalize(file);
    if(visited.has(resolved)) continue;
    visited.add(resolved);

    const source = loadFile(resolved);
    if(source == null) continue;

    const toks = tokenize(source, resolved);

    yield* scanTokens(
      toks,
      resolved,
      literal => {
        const inc = resolveInclude(literal, resolved, includeDirs);
        if(inc) queue.push(inc);
      },
      knownNames,
    );
  }
}

function* walkFiles(dir, re) {
  const [entries, err] = readdir(dir);
  if(err || !entries) return;

  for(const entry of entries) {
    if(entry === '.' || entry === '..') continue;

    const p = `${dir}/${entry}`;
    const [subEntries, subErr] = readdir(p);

    if(!subErr && subEntries) yield* walkFiles(p, re);
    else if(re.test(entry)) yield p;
  }
}

function expandPaths(paths) {
  const files = [];

  for(const p of paths) {
    const [entries, err] = readdir(p);

    if(!err && entries) files.push(...walkFiles(p, /\.[ch]$/i));
    else files.push(p);
  }

  return files;
}

const OPTIONS = {
  help: [false, printHelp, 'h'],
  'include-dir': [true, (v, prev) => (prev ?? []).concat(v), 'I'],
  output: [true, null, 'o'],
  'json-array': [false, null, 'j'],
  '@': 'paths',
};

function printHelp() {
  puts(
    `Usage: ${scriptArgs[0]} [OPTIONS] <files-or-dirs...>\n\n` +
      'Tokenizes the given .c/.h files (and any directories, searched recursively for\n' +
      '.c/.h) with lexer/c.js, follows local #include "..." lines into the same scan,\n' +
      'and streams one NDJSON record per identifier reached: { name, type, file, range,\n' +
      'conditions, context? }. See the file header comment for the full field/caveat\n' +
      'rundown.\n\n' +
      'Options:\n' +
      '  -I, --include-dir <dir>  #include "..." search path (repeatable, default: include)\n' +
      '  -o, --output <file>      write to file instead of stdout\n' +
      '  -j, --json-array         wrap output as a streamed JSON array (`[\\n`, one\n' +
      '                           object per line separated by `,\\n`, `]\\n` at the end)\n' +
      '                           instead of bare NDJSON - still written incrementally,\n' +
      '                           so it stays pipeable through e.g. tee\n' +
      '  -h, --help                show this help\n',
  );
  exit(0);
}

function main(...args) {
  const params = getOpt(OPTIONS, args);
  const includeDirs = params['include-dir'] ?? ['include'];
  const files = expandPaths(params['@']);

  const out = params.output ? open(params.output, 'w+') : null;
  const emit = out ? line => out.puts(line) : line => puts(line);

  if(params['json-array']) {
    // One-record lookahead so the comma goes *between* objects (valid JSON) rather
    // than after every one (which would leave a trailing comma before the final ']').
    emit('[\n');
    let prev = null;
    for(const record of scanFiles(files, includeDirs)) {
      if(prev !== null) emit(JSON.stringify(prev) + ',\n');
      prev = record;
    }
    if(prev !== null) emit(JSON.stringify(prev) + '\n');
    emit(']\n');
  } else {
    for(const record of scanFiles(files, includeDirs)) emit(JSON.stringify(record) + '\n');
  }

  if(out) out.close();
}

if(isMainModule(import.meta.url)) main(...scriptArgs.slice(1));
