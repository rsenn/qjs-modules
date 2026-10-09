#!/usr/bin/env qjsm

// Applies a dead-function report - either from `nm-symbols.js --dead-code` (see there
// for how the report is produced) or the identifier list `extract-c.js -i -L start,end`
// / `-L range` writes (dead = a function with no references) - by surgically deleting each listed function - and, for
// functions defined in a .c file, its matching header prototype - via the same
// paren/brace-balancing token scan lib/c-functions.js uses for detection.
//
// The report is meant to be reviewed/trimmed by hand first: this script does no
// judgment of its own about what's safe to remove, it just removes whatever the
// (possibly-edited) entries list tells it to. `removeDeadFunctions()` is also usable
// as a library function (e.g. by deadwood.js) instead of going through the CLI/JSON.

import { puts, loadFile, open, exit, err } from 'std';
import { getOpt, isMainModule } from 'util';
import { findFunctionDefinitions, findFunctionPrototypes } from 'c-functions';

const OPTIONS = {
  help: [false, printHelp, 'h'],
  apply: [false, null, 'a'],
  '@': 'files',
};

function printHelp() {
  puts(
    `Usage: ${scriptArgs[0]} [OPTIONS] <dead-code.json>\n\n` +
      "Removes the functions listed in a nm-symbols.js '--dead-code' report (or a\n" +
      'hand-trimmed copy of one - either the full report object, or a bare array of\n' +
      '{ file, name, startLine, endLine } entries), or the unreferenced functions of a\n' +
      "`extract-c.js -i -L start,end ... -o identifiers.json` (or `-L range`, or `-L loc`)\n" +
      'identifier list. For each entry: re-locates the\n' +
      "function in its file by name + start line (skipping it with a warning if the\n" +
      "file's changed since the report was generated), then deletes the full\n" +
      'declaration (return type through closing brace) as whole lines. For a\n' +
      'src/<name>.c or quickjs-<name>.c entry, also removes the matching prototype\n' +
      'from include/<name>.h or quickjs-<name>.h, if one exists there.\n\n' +
      'By default nothing is modified: the plan goes to stderr and a POSIX shell script\n' +
      "(one `sed -i 'N,Md' file` per function/prototype, bottom-up per file so line\n" +
      'numbers stay valid) goes to stdout - review it, then run it with sh.\n\n' +
      'Options:\n' +
      '  -a, --apply  write changes directly instead of emitting a script\n' +
      '  -h, --help   show this help\n',
  );
  exit(0);
}

// Names that are called from outside anything a source scan can see.
const ALWAYS_USED = new Set(['main']);

function parsePosition(pos) {
  const m = typeof pos == 'string' ? /^(.*):(\d+):(\d+)$/.exec(pos) : null;
  return m ? { file: m[1], line: +m[2] } : null;
}

/**
 * Turns the output of `extract-c.js -i` into removal entries: every function that has a
 * declaration but no references. The declaration's position must say which file it is in
 * (`-L start|end|loc|range|file` - a plain `-L line` has no file) and where: a
 * "<file>:<line>:<column>" `start`, a `loc`, or a `range` (matched by character offset).
 */
export function entriesFromIdentifiers(records) {
  const entries = [];

  for(const { name, declaration: d, references } of records) {
    if(d?.kind != 'function' || references?.length || ALWAYS_USED.has(name)) continue;

    const at = parsePosition(d.start) ?? (d.loc && { file: d.loc.file, line: d.loc.line }) ?? null;

    if(at) entries.push({ file: at.file, name, startLine: at.line });
    else if(d.range?.file != null) entries.push({ file: d.range.file, name, startOffset: d.range.start });
    else throw new Error(`${name}: declaration has no file/position - run extract-c.js with -L start,end, loc or range`);
  }

  return entries;
}

function headerFor(file) {
  if(file.startsWith('src/')) return `include/${file.slice(4).replace(/\.c$/, '.h')}`;

  const m = /^quickjs-(.+)\.c$/.exec(file);
  return m ? `quickjs-${m[1]}.h` : null;
}

function lineStart(source, offset) {
  return source.lastIndexOf('\n', offset - 1) + 1;
}

function lineEndInclusive(source, offset) {
  const nl = source.indexOf('\n', offset);
  return nl == -1 ? source.length : nl + 1;
}

// spans: [{declStartOffset, endOffset}, ...], possibly-overlapping-free, any order
function lineNumber(source, offset) {
  let n = 1;
  for(let i = source.indexOf('\n'); i != -1 && i < offset; i = source.indexOf('\n', i + 1)) n++;
  return n;
}

function removeSpans(source, spans) {
  let result = source;

  for(const { declStartOffset, endOffset } of [...spans].sort((a, b) => b.declStartOffset - a.declStartOffset)) {
    const start = lineStart(result, declStartOffset);
    const end = lineEndInclusive(result, endOffset);
    result = result.slice(0, start) + result.slice(end);
  }

  return result;
}

/**
 * Removes the functions named in `entries` (each `{ file, name, startLine }`, as
 * produced by nm-symbols.js's `findDeadFunctions()`) from the source tree, plus their
 * matching header prototypes where applicable.
 *
 * @param {Array<{file: string, name: string, startLine: number}>} entries
 * @param {{apply?: boolean, log?: (line: string) => void}} [options] - apply: actually
 *   write files (default: dry-run, just report the plan). log: called once per line
 *   of progress/summary output (default: std.puts). script: if given (and not
 *   applying), called with each line of an equivalent shell script (`sed -i` line
 *   ranges, bottom-up per file).
 * @returns {{removedFns: number, removedProtos: number, skipped: number}}
 */
export function removeDeadFunctions(entries, { apply = false, log = puts, script } = {}) {
  const byFile = new Map();
  for(const e of entries) (byFile.get(e.file) ?? byFile.set(e.file, []).get(e.file)).push(e);

  // fileName -> Map(declStartOffset -> def), for the .c/.h files named directly in the
  // report, plus any paired headers accumulated as their .c files are processed.
  const plan = new Map();
  let skipped = 0;

  const planFor = file => plan.get(file) ?? plan.set(file, new Map()).get(file);

  for(const [file, wanted] of byFile) {
    const source = loadFile(file);

    if(source == null) {
      log(`SKIP ${file}: file not found\n`);
      skipped += wanted.length;
      continue;
    }

    const defs = findFunctionDefinitions(source, file);
    const matched = [];

    for(const w of wanted) {
      const def = defs.find(d => d.name == w.name && (w.startOffset != null ? d.startOffset == w.startOffset : d.startLine == w.startLine));

      if(!def) {
        log(`SKIP ${file}:${w.startLine ?? "@" + w.startOffset} ${w.name}: no longer matches current source (edited since the report was generated?)\n`);
        skipped++;
        continue;
      }

      matched.push(def);
    }

    if(matched.length == 0) continue;

    for(const def of matched) planFor(file).set(def.declStartOffset, def);

    const header = headerFor(file);
    if(!header) continue;

    const headerSource = loadFile(header);
    if(headerSource == null) continue;

    const protos = findFunctionPrototypes(headerSource, header);

    for(const def of matched) {
      const proto = protos.find(p => p.name == def.name);
      if(proto) planFor(header).set(proto.declStartOffset, proto);
    }
  }

  let removedFns = 0,
    removedProtos = 0;

  for(const [file, spansByOffset] of plan) {
    const source = loadFile(file);
    const spans = [...spansByOffset.values()];
    const isHeader = /\.h$/i.test(file) && !byFile.has(file);

    if(isHeader) removedProtos += spans.length;
    else removedFns += spans.length;

    log(`${apply ? 'REMOVE' : 'WOULD REMOVE'} ${spans.length} ${isHeader ? 'prototype(s)' : 'function(s)'} from ${file}: ${spans.map(s => s.name).join(', ')}\n`);

    if(script && !apply) {
      const quoted = `'${file.replace(/'/g, "'\\''")}'`;
      for(const { declStartOffset, endOffset } of [...spans].sort((a, b) => b.declStartOffset - a.declStartOffset)) {
        const start = lineNumber(source, lineStart(source, declStartOffset));
        const end = lineNumber(source, lineEndInclusive(source, endOffset) - 1);
        script(`sed -i '${start},${end}d' ${quoted}  # ${spans.find(x => x.declStartOffset == declStartOffset).name}\n`);
      }
    }

    if(apply) {
      const result = removeSpans(source, spans);
      const f = open(file, 'w+');
      f.puts(result);
      f.close();
    }
  }

  return { removedFns, removedProtos, skipped };
}

function main(...args) {
  const params = getOpt(OPTIONS, args);
  const apply = !!params.apply;
  const [jsonPath] = params['@'];

  if(!jsonPath) printHelp();

  const raw = JSON.parse(loadFile(jsonPath));
  let entries = Array.isArray(raw) ? raw : raw.deadFunctions;

  // `extract-c.js -i` identifier records carry a `declaration` instead of file/name/startLine
  if(Array.isArray(entries) && entries.some(e => 'declaration' in e)) entries = entriesFromIdentifiers(entries);

  if(!Array.isArray(entries)) throw new Error(`${jsonPath}: expected an array, a report object with a 'deadFunctions' array, or a extract-c.js -i identifier list`);

  const say = s => err.puts(s);

  if(!apply) puts('#!/bin/sh\n# generated by remove-dead-functions.js - review before running\nset -e\n');

  const { removedFns, removedProtos, skipped } = removeDeadFunctions(entries, { apply, log: apply ? puts : say, script: apply ? undefined : puts });

  (apply ? puts : say)(`\n${apply ? 'Removed' : 'Would remove'} ${removedFns} function(s), ${removedProtos} header prototype(s); ${skipped} entries skipped.\n`);
  if(!apply) say('Dry run: shell script written to stdout. Re-run with --apply to write changes directly.\n');
}

if(isMainModule(import.meta.url)) main(...scriptArgs.slice(1));
