#!/usr/bin/env qjsm

// unused-functions.js - interactive unused-function browser for source-scanner.js's
// output.
//
// Reads a stream of source-scanner.js records (its NDJSON default, or its -j
// JSON-array form) from a file argument or stdin, and *while reading* live-builds and
// redraws a scrollable table: one row per 'decl' (function definition) seen so far,
// with how many 'ref' records have matched its name project-wide so far. Piped
// straight from a still-running `source-scanner.js ... -j | tee scan.json |
// unused-functions.js`, an early function's usage count visibly keeps ticking up as
// later files in the stream turn out to reference it - the table isn't a snapshot
// taken after the fact, it's built up live as records arrive.
//
// Correlation is by name, scoped by linkage: a `static` decl (internal linkage - can
// only be called from its own file) is only matched against 'ref' records from that
// same file, so two same-named statics in different files no longer mask each other;
// a non-static decl still correlates by bare name project-wide. 'proto' records
// aren't counted as usage - only 'ref' is, matching "never referenced" in the literal
// sense.
//
// Once ingest finishes (EOF), the view becomes an interactive picker: up/down or j/k
// to move, space to toggle a selection mark, a/n to select/clear all in the current
// (filtered) view, 'u' to toggle showing only unused (0-usage) functions, enter to
// print the selected entries as NDJSON and exit, q/ctrl-c to quit without printing
// anything.
//
// Since stdin may be the data stream itself (piped from source-scanner.js), keyboard
// input falls back to opening /dev/tty directly when stdin isn't a real terminal -
// the same trick fzf-style pickers use to accept piped input while staying
// interactive.
//
// Colors (name column, only on non-cursor rows): 0 usages - light red. 1 usage -
// yellow. 2+ - light green.

import * as std from 'std';
import { puts, exit } from 'std';
import * as os from 'os';
import { getOpt, isMainModule } from 'util';
import {
  terminal,
  readKey,
  windowSize,
  enableRawMode,
  disableRawMode,
  setAlternateScreen,
  setNormalScreen,
  cursorHide,
  cursorShow,
  cursorPosition,
  eraseInLine,
  eraseInDisplay,
  reverseVideoOn,
  reverseVideoOff,
  rgbForeground,
  sgr,
} from 'terminal';

const COLOR_UNUSED = [255, 85, 85]; // light red
const COLOR_ONCE = [255, 255, 85]; // yellow
const COLOR_WELL_USED = [85, 255, 85]; // light green

// Names that are real entry points called from outside anything this scanner can
// see (a dlsym()'d module-loader hook, not a source-level call) - see usagesOf().
const ALWAYS_USED = new Set(['JS_INIT_MODULE', 'js_init_module']);

function colorForUsages(n) {
  if(n === 0) return COLOR_UNUSED;
  if(n === 1) return COLOR_ONCE;
  return COLOR_WELL_USED;
}

function pad(s, w) {
  if(s.length > w) return s.length > 1 ? s.slice(0, w - 1) + '…' : s.slice(0, w);
  return s + ' '.repeat(w - s.length);
}

// --- ingest --------------------------------------------------------------------

/**
 * Reads source-scanner.js records one at a time from `input` (a std FILE), yielding
 * each as it's parsed. Handles both output forms: plain NDJSON, and the -j JSON-array
 * form (`[`, one object-plus-comma per line, `]`) - detected from the first line.
 */
function* readRecords(input) {
  const firstLine = input.getline();
  if(firstLine == null) return;

  if(firstLine.trim() !== '[') {
    if(firstLine.trim() !== '') yield JSON.parse(firstLine);
  }

  let line;
  while((line = input.getline()) != null) {
    const trimmed = line.trim();
    if(trimmed === '') continue;
    if(trimmed === ']') break;
    yield JSON.parse(trimmed.endsWith(',') ? trimmed.slice(0, -1) : trimmed);
  }
}

class FunctionTable {
  items = []; // { key, name, file, range, static }
  #byKey = new Map(); // key -> index into items
  #refCounts = new Map(); // name -> count (non-static correlation, project-wide)
  #fileRefCounts = new Map(); // "file:name" -> count (static correlation, same-file only)

  ingest(rec) {
    if(rec.type === 'decl') {
      const key = `${rec.file}:${rec.range[0]}:${rec.name}`;
      if(!this.#byKey.has(key)) {
        this.#byKey.set(key, this.items.length);
        this.items.push({ key, name: rec.name, file: rec.file, range: rec.range, static: !!rec.static });
      }
    } else if(rec.type === 'ref') {
      this.#refCounts.set(rec.name, (this.#refCounts.get(rec.name) ?? 0) + 1);

      const fileKey = `${rec.file}:${rec.name}`;
      this.#fileRefCounts.set(fileKey, (this.#fileRefCounts.get(fileKey) ?? 0) + 1);
    }
  }

  // A `static` function has internal linkage - it can only be called from its own
  // file, so correlating it by bare name project-wide would let an unrelated
  // same-named static elsewhere mask it as "used". Scope those to same-file uses;
  // non-static (external-linkage) names keep the project-wide count.
  usagesOf(item) {
    const raw = item.static ? (this.#fileRefCounts.get(`${item.file}:${item.name}`) ?? 0) : (this.#refCounts.get(item.name) ?? 0);

    // JS_INIT_MODULE is the per-module entry point macro (expands to
    // js_init_module_<name> - the same macro-aliasing case nm-symbols.js's own
    // skippedMacroAliases handling exists for): the real caller is the module loader
    // dlsym()-ing the expanded name, never a source-level call this scanner (or any
    // non-preprocessing scan) can see under either spelling. Never flag it dead.
    if(ALWAYS_USED.has(item.name)) return Math.max(raw, 1);

    return raw;
  }
}

// --- rendering -------------------------------------------------------------------

function render(table, view, selected, cursor, scroll, visibleRows, cols, unusedOnly, status) {
  cursorPosition(1, 1);

  terminal.puts(
    `unused-functions - ${table.items.length} read, ${view.length} shown, ${selected.size} selected` +
      `  (space:toggle a/n:all/none u:unused-only[${unusedOnly ? 'on' : 'off'}] enter:print q:quit)`,
  );
  eraseInLine(0);
  terminal.puts('\r\n');

  const nameW = 32,
    usagesW = 7;
  const fileW = Math.max(10, cols - nameW - usagesW - 6);

  terminal.puts(`   ${pad('NAME', nameW)} ${'USAGES'.padStart(usagesW)} ${pad('FILE:RANGE', fileW)}`);
  eraseInLine(0);
  terminal.puts('\r\n');

  terminal.puts('-'.repeat(Math.min(cols, 100)));
  eraseInLine(0);
  terminal.puts('\r\n');

  const end = Math.min(view.length, scroll + visibleRows);

  for(let i = scroll; i < end; i++) {
    const it = view[i];
    const n = table.usagesOf(it);
    const mark = selected.has(it.key) ? '[x]' : '[ ]';
    const fileCell = pad(`${it.file}:${it.range[0]}-${it.range[1]}`, fileW);
    const usagesCell = String(n).padStart(usagesW);
    const active = i === cursor;

    if(active) reverseVideoOn();

    terminal.puts(`${mark} `);

    const color = !active && colorForUsages(n);
    if(color) rgbForeground(terminal, ...color);
    terminal.puts(pad(it.name, nameW));
    if(color) sgr(terminal, 0);

    terminal.puts(` ${usagesCell} ${fileCell}`);

    if(active) reverseVideoOff();
    eraseInLine(0);
    terminal.puts('\r\n');
  }

  terminal.puts(status || '');
  eraseInDisplay(0);
  terminal.flush();
}

// --- ingest phase (passive, no key reads - see file header) ----------------------

function ingestAndShow(input, table, cols, visibleRows) {
  let count = 0;

  for(const rec of readRecords(input)) {
    table.ingest(rec);
    count++;

    if(count % 20 === 0) {
      const scroll = Math.max(0, table.items.length - visibleRows);
      render(table, table.items, new Set(), -1, scroll, visibleRows, cols, false, 'reading...');
    }
  }

  const scroll = Math.max(0, table.items.length - visibleRows);
  render(table, table.items, new Set(), -1, scroll, visibleRows, cols, false, '');
}

// --- interactive picker ------------------------------------------------------------

function pick(table, fd, cols0) {
  const selected = new Set();
  let cursor = 0,
    scroll = 0,
    unusedOnly = false,
    status = '';

  for(;;) {
    const [cols, rows] = windowSize(terminal.fileno());
    const visibleRows = Math.max(1, rows - 4);

    const view = unusedOnly ? table.items.filter(it => table.usagesOf(it) === 0) : table.items;
    if(cursor >= view.length) cursor = Math.max(0, view.length - 1);
    if(cursor < scroll) scroll = cursor;
    if(cursor >= scroll + visibleRows) scroll = cursor - visibleRows + 1;

    render(table, view, selected, cursor, scroll, visibleRows, cols, unusedOnly, status);
    status = '';

    const key = readKey(fd);

    switch(key.type) {
      case 'up':
        cursor = Math.max(0, cursor - 1);
        break;
      case 'down':
        cursor = Math.min(view.length - 1, cursor + 1);
        break;
      case 'pageup':
        cursor = Math.max(0, cursor - visibleRows);
        break;
      case 'pagedown':
        cursor = Math.min(view.length - 1, cursor + visibleRows);
        break;
      case 'space':
        if(view[cursor]) {
          const k = view[cursor].key;
          if(selected.has(k)) selected.delete(k);
          else selected.add(k);
        }
        break;
      case 'char':
        if(key.char === 'j') cursor = Math.min(view.length - 1, cursor + 1);
        else if(key.char === 'k') cursor = Math.max(0, cursor - 1);
        else if(key.char === 'a') view.forEach(it => selected.add(it.key));
        else if(key.char === 'n') selected.clear();
        else if(key.char === 'u') {
          unusedOnly = !unusedOnly;
          cursor = 0;
        } else if(key.char === 'q') return null;
        break;
      case 'enter':
        return table.items.filter(it => selected.has(it.key));
      case 'ctrlc':
      case 'eof':
        return null;
    }
  }
}

// --- main ------------------------------------------------------------------------

const OPTIONS = {
  help: [false, printHelp, 'h'],
  '@': 'paths',
};

function printHelp() {
  puts(
    `Usage: ${scriptArgs[0]} [OPTIONS] [scan.ndjson]\n\n` +
      'Interactively browses source-scanner.js output for unused (never-referenced)\n' +
      'functions. Reads the given file, or stdin if none given. See the file header\n' +
      'comment for the full field/keybinding/caveat rundown.\n\n' +
      'Options:\n' +
      '  -h, --help  show this help\n',
  );
  exit(0);
}

function main(...args) {
  const params = getOpt(OPTIONS, args);
  const inputPath = params['@'][0];

  const input = inputPath ? std.open(inputPath, 'r') : std.in;
  if(!input) {
    puts(`cannot open ${inputPath}\n`);
    exit(1);
  }

  let ttyFile = null;
  let fd = std.in.fileno();

  if(!os.isatty(fd)) {
    ttyFile = std.open('/dev/tty', 'r+');
    if(!ttyFile) {
      puts('unused-functions.js needs an interactive terminal: stdin is piped and /dev/tty is unavailable\n');
      exit(1);
    }
    fd = ttyFile.fileno();
  }

  if(!os.isatty(terminal.fileno())) {
    puts('unused-functions.js needs an interactive terminal for its own output\n');
    exit(1);
  }

  const table = new FunctionTable();

  setAlternateScreen();
  cursorHide();

  let picked = null;

  try {
    const [cols0, rows0] = windowSize(terminal.fileno());
    ingestAndShow(input, table, cols0, Math.max(1, rows0 - 4));

    if(inputPath) input.close();

    enableRawMode(fd);
    try {
      picked = pick(table, fd, cols0);
    } finally {
      disableRawMode(fd);
    }
  } finally {
    cursorShow();
    setNormalScreen();
    if(ttyFile) ttyFile.close();
  }

  if(picked == null) {
    puts('Aborted.\n');
    return;
  }

  for(const it of picked) puts(JSON.stringify(it) + '\n');
}

if(isMainModule(import.meta.url)) main(...scriptArgs.slice(1));
