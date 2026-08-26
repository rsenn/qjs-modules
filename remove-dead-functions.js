#!/usr/bin/env qjsm

// Applies a dead-function report from `nm-symbols.js --dead-code` (see there for how
// the report is produced) by surgically deleting each listed function - and, for
// functions defined in a .c file, its matching header prototype - via the same
// paren/brace-balancing token scan lib/c-functions.js uses for detection.
//
// The report is meant to be reviewed/trimmed by hand first: this script does no
// judgment of its own about what's safe to remove, it just removes whatever the
// (possibly-edited) entries list tells it to. `removeDeadFunctions()` is also usable
// as a library function (e.g. by deadwood.js) instead of going through the CLI/JSON.

import { puts, loadFile, open, exit } from 'std';
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
      '{ file, name, startLine, endLine } entries). For each entry: re-locates the\n' +
      "function in its file by name + start line (skipping it with a warning if the\n" +
      "file's changed since the report was generated), then deletes the full\n" +
      'declaration (return type through closing brace) as whole lines. For a\n' +
      'src/<name>.c or quickjs-<name>.c entry, also removes the matching prototype\n' +
      'from include/<name>.h or quickjs-<name>.h, if one exists there.\n\n' +
      'Options:\n' +
      '  -a, --apply  write changes (default: dry-run, just prints the plan)\n' +
      '  -h, --help   show this help\n',
  );
  exit(0);
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
 *   of progress/summary output (default: std.puts).
 * @returns {{removedFns: number, removedProtos: number, skipped: number}}
 */
export function removeDeadFunctions(entries, { apply = false, log = puts } = {}) {
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
      const def = defs.find(d => d.name == w.name && d.startLine == w.startLine);

      if(!def) {
        log(`SKIP ${file}:${w.startLine} ${w.name}: no longer matches current source (edited since the report was generated?)\n`);
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
  const entries = Array.isArray(raw) ? raw : raw.deadFunctions;

  if(!Array.isArray(entries)) throw new Error(`${jsonPath}: expected an array, or a report object with a 'deadFunctions' array`);

  const { removedFns, removedProtos, skipped } = removeDeadFunctions(entries, { apply, log: puts });

  puts(`\n${apply ? 'Removed' : 'Would remove'} ${removedFns} function(s), ${removedProtos} header prototype(s); ${skipped} entries skipped.\n`);
  if(!apply) puts('Re-run with --apply to write changes.\n');
}

if(isMainModule(import.meta.url)) main(...scriptArgs.slice(1));
