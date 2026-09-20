#!/usr/bin/env qjsm

// deadwood - interactive dead C function finder/remover.
//
// Generic harness around nm-symbols.js's `findDeadFunctions()` (detection) and
// remove-dead-functions.js's `removeDeadFunctions()` (surgical removal): point it at
// a project's built binaries (.so/.a/.o/executables) and its C source dirs, review the
// candidates in a terminal list (space to toggle one, '/' to toggle a regex-matched
// group), then confirm by retyping a generated phrase before anything is deleted.
// Not qjs-modules-specific - works for any C project built with
// -ffunction-sections/-fdata-sections -fno-inline + --gc-sections.

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
} from 'terminal';
import { findDeadFunctions } from './nm-symbols.js';
import { removeDeadFunctions } from './remove-dead-functions.js';

const OPTIONS = {
  help: [false, printHelp, 'h'],
  source: [true, (v, prev) => (prev ?? []).concat(v), 's'],
  '@': 'paths',
};

function printHelp() {
  puts(
    `Usage: ${scriptArgs[0]} [OPTIONS] <bin-dir-or-files...>\n\n` +
      'Interactively finds and removes dead C functions from a project: runs the same\n' +
      "detection as `nm-symbols.js --dead-code` (see there for the method/caveats),\n" +
      'shows the candidates in a terminal list for you to redact, then removes exactly\n' +
      'what you leave selected via `remove-dead-functions.js`.\n\n' +
      '<bin-dir-or-files...>  the built .so/.a/.o/executables (or dirs to search) to\n' +
      '                       check symbol presence against\n\n' +
      'Options:\n' +
      '  -s, --source <dir>  source dir to scan (repeatable, default: include, src)\n' +
      '  -h, --help          show this help\n\n' +
      'In the list: up/down or j/k to move, space to toggle one, a/n to select\n' +
      'all/none, / to toggle a regex-matched group (matched against "file:name"),\n' +
      'enter to proceed, q or ctrl-c to abort without changing anything.\n',
  );
  exit(0);
}

// --- interactive picker ------------------------------------------------------------

/**
 * Terminal list widget: lets the user redact `items` down to the subset that should
 * actually be removed. Returns the selected items, or null if the user aborted.
 *
 * Runs on the alternate screen buffer (like an ncurses app) and redraws in place each
 * frame - move cursor home, overwrite each line, erase-to-end-of-line/-screen to wipe
 * only leftover text from the previous frame - rather than clearing the whole screen
 * first, so there's no blank-then-repaint flicker.
 *
 * @param {Array<{file: string, name: string, startLine: number, endLine: number, caution?: ?string}>} items
 */
export function pickFunctions(items) {
  const fd = std.in.fileno();

  if(!os.isatty(fd) || !os.isatty(terminal.fileno())) throw new Error('deadwood needs an interactive terminal (stdin/stdout) to redact the candidate list');

  const selected = new Set(items.map((it, i) => (it.caution ? -1 : i)).filter(i => i >= 0));
  let cursor = 0,
    scroll = 0,
    status = '';

  setAlternateScreen();
  cursorHide();
  enableRawMode(fd);

  try {
    for(;;) {
      const [cols, rows] = windowSize(terminal.fileno());
      const visibleRows = Math.max(1, rows - 4);

      if(cursor < scroll) scroll = cursor;
      if(cursor >= scroll + visibleRows) scroll = cursor - visibleRows + 1;

      render(items, selected, cursor, scroll, visibleRows, cols, status);
      status = '';

      const key = readKey(fd);

      switch(key.type) {
        case 'up':
          cursor = Math.max(0, cursor - 1);
          break;
        case 'down':
          cursor = Math.min(items.length - 1, cursor + 1);
          break;
        case 'pageup':
          cursor = Math.max(0, cursor - visibleRows);
          break;
        case 'pagedown':
          cursor = Math.min(items.length - 1, cursor + visibleRows);
          break;
        case 'space':
          if(selected.has(cursor)) selected.delete(cursor);
          else selected.add(cursor);
          break;
        case 'char':
          if(key.char == 'j') cursor = Math.min(items.length - 1, cursor + 1);
          else if(key.char == 'k') cursor = Math.max(0, cursor - 1);
          else if(key.char == 'a') items.forEach((_, i) => selected.add(i));
          else if(key.char == 'n') selected.clear();
          else if(key.char == 'q') return null;
          else if(key.char == '/') status = regexToggle(fd, items, selected, cols);
          break;
        case 'enter':
          return items.filter((_, i) => selected.has(i));
        case 'ctrlc':
        case 'eof':
          return null;
      }
    }
  } finally {
    disableRawMode(fd);
    cursorShow();
    setNormalScreen();
  }
}

function regexToggle(fd, items, selected, cols) {
  const prompt = () => {
    cursorPosition(1, 1);
    terminal.puts('/' + pattern);
    eraseInLine(0);
    terminal.flush();
  };

  let pattern = '';
  prompt();

  for(;;) {
    const key = readKey(fd);

    if(key.type == 'enter') break;
    if(key.type == 'ctrlc' || key.type == 'escape') return 'regex cancelled';

    if(key.type == 'backspace') pattern = pattern.slice(0, -1);
    else if(key.type == 'char') pattern += key.char;
    else continue;

    prompt();
  }

  if(pattern == '') return '';

  let re;
  try {
    re = new RegExp(pattern);
  } catch(e) {
    return `invalid regex: ${e.message}`;
  }

  let n = 0;
  items.forEach((it, i) => {
    if(re.test(`${it.file}:${it.name}`)) {
      if(selected.has(i)) selected.delete(i);
      else selected.add(i);
      n++;
    }
  });

  return `toggled ${n} matching /${pattern}/`;
}

function render(items, selected, cursor, scroll, visibleRows, cols, status) {
  cursorPosition(1, 1);

  terminal.puts(`deadwood - ${selected.size}/${items.length} selected  (space:toggle /:regex a:all n:none enter:confirm q:quit)`);
  eraseInLine(0);
  terminal.puts('\r\n');

  terminal.puts('-'.repeat(Math.min(cols, 100)));
  eraseInLine(0);
  terminal.puts('\r\n');

  const end = Math.min(items.length, scroll + visibleRows);

  for(let i = scroll; i < end; i++) {
    const it = items[i];
    const mark = selected.has(i) ? '[x]' : '[ ]';
    const guard = it.caution ? `  (${it.caution})` : '';
    let line = `${mark} ${it.file}:${it.startLine} ${it.name}${guard}`;

    if(line.length > cols - 2) line = line.slice(0, cols - 5) + '...';

    if(i == cursor) reverseVideoOn();
    terminal.puts(line);
    if(i == cursor) reverseVideoOff();
    eraseInLine(0);
    terminal.puts('\r\n');
  }

  terminal.puts(status || '');
  eraseInDisplay(0);
  terminal.flush();
}

// --- confirmation --------------------------------------------------------------

function confirm(count, fileCount) {
  const phrase = `remove ${count}`;

  puts(`\nAbout to remove ${count} function(s) across ${fileCount} file(s).\n`);
  puts(`Type "${phrase}" to confirm, or anything else to abort:\n> `);
  std.out.flush();

  const line = std.in.getline();

  return line != null && line.trim() == phrase;
}

// --- main ------------------------------------------------------------------------

function main(...args) {
  const params = getOpt(OPTIONS, args);
  const sourceDirs = params.source ?? ['include', 'src'];
  const binPaths = params['@'];

  if(binPaths.length == 0) printHelp();

  puts(`Scanning ${binPaths.join(', ')} against ${sourceDirs.join(', ')}...\n`);
  std.out.flush();

  const report = findDeadFunctions(sourceDirs, binPaths);

  puts(
    `${report.totalFunctions} functions scanned, ${report.deadFunctionCount} dead-stripped, ` +
      `${report.platformGuarded.length} platform-guarded, ${report.referencedButUnlinked.length} referenced-but-unlinked.\n`,
  );
  std.out.flush();

  const items = [
    ...report.deadFunctions.map(e => ({ ...e, caution: null })),
    ...report.platformGuarded.map(e => ({ ...e, caution: 'platform-guarded' })),
    ...report.referencedButUnlinked.map(e => ({ ...e, caution: 'referenced elsewhere' })),
  ];

  if(items.length == 0) {
    puts('Nothing to remove.\n');
    return;
  }

  const picked = pickFunctions(items);

  if(picked == null) {
    puts('Aborted, nothing changed.\n');
    return;
  }

  if(picked.length == 0) {
    puts('Nothing selected, nothing changed.\n');
    return;
  }

  const fileCount = new Set(picked.map(e => e.file)).size;

  if(!confirm(picked.length, fileCount)) {
    puts('Confirmation text did not match, aborted, nothing changed.\n');
    return;
  }

  const { removedFns, removedProtos, skipped } = removeDeadFunctions(picked, { apply: true, log: puts });

  puts(`\nRemoved ${removedFns} function(s), ${removedProtos} header prototype(s); ${skipped} entries skipped.\n`);
}

if(isMainModule(import.meta.url)) main(...scriptArgs.slice(1));
