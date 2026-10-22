#!/usr/bin/env qjsm
/* git-watch.js: shows the diff hunks a coding agent just wrote.
 * depends on: inotify (lib/inotify.js), child_process, fs, git.
 * rule: a hunk is printed once, keyed by file and a checksum of its body.
 * quickjs and linux only, inotify is not portable. */
import * as std from 'std';
import * as os from 'os';
import { execSync } from 'child_process';
import { existsSync, readdirSync, statSync, unlinkSync } from 'fs';
import inotify, { IN_CLOSE_WRITE, IN_CREATE, IN_ISDIR, IN_MOVED_TO } from 'inotify';

const USAGE = `usage: git-watch.js [options] [directory]

  --viewer EDITOR   run EDITOR file:line:column at each new change
  --filter NAME     only react while a process called NAME (or a child
                    of one) is running, e.g. --filter claude
  --full            print whole hunks instead of abbreviating long ones
  --context N       lines kept at each end of an abbreviated hunk (6)
  -h, --help        this text
`;

const HEAD = '\x1b[1m', DIM = '\x1b[2m', OFF = '\x1b[0m';
const GREEN_FG = '\x1b[32m', RED_FG = '\x1b[31m';
const GREEN_BG = '\x1b[48;5;22m', RED_BG = '\x1b[48;5;52m';
const DEBOUNCE_MS = 120;
const MASK = IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE;

const opts = { viewer: '', filter: '', full: false, context: 6, dir: '.' };
const tmp = `/tmp/git-watch-${process.pid}`;
const seen = new Map(); /* path -> Set of hunk keys currently in its diff */
const timers = new Map(); /* path -> pending debounce timer */

/* quotes one word for sh: abc -> 'abc', it's -> 'it'\''s' */
const quote = s => `'${String(s).replaceAll("'", "'\\''")}'`;

/* runs a shell command, returns its stdout ('' on failure).
 * execSync does not capture output, so it goes through a temp file. */
function run(cmd, cwd = opts.dir) {
  try {
    execSync(`cd ${quote(cwd)} && { ${cmd}; } > ${tmp} 2>/dev/null`);
  } catch(e) {}

  const f = std.open(tmp, 'r');
  const out = f ? f.readAsString() : '';

  f?.close();
  return out;
}

/* 53-bit string hash, enough to tell two hunk bodies apart */
function checksum(s) {
  let h1 = 0xdeadbeef, h2 = 0x41c6ce57;

  for(let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);

    h1 = Math.imul(h1 ^ c, 2654435761);
    h2 = Math.imul(h2 ^ c, 1597334677);
  }

  h1 = Math.imul(h1 ^ (h1 >>> 16), 2246822507) ^ Math.imul(h2 ^ (h2 >>> 13), 3266489909);
  h2 = Math.imul(h2 ^ (h2 >>> 16), 2246822507) ^ Math.imul(h1 ^ (h1 >>> 13), 3266489909);
  return (4294967296 * (2097151 & h2) + (h1 >>> 0)).toString(16);
}

/* ---- process filter ------------------------------------------------ */

function readProc(pid, name) {
  const f = std.open(`/proc/${pid}/${name}`, 'r');
  const s = f ? f.readAsString() : '';

  f?.close();
  return s;
}

/* true while a process named opts.filter, or a child of one, exists.
 * inotify reports no pid, so this is the closest to "accessed by". */
function filterActive() {
  const parent = new Map(), comm = new Map();

  for(const d of readdirSync('/proc')) {
    if(!/^\d+$/.test(d)) continue;

    const stat = readProc(d, 'stat');
    const m = /^\d+ \((.*)\) \S (\d+)/.exec(stat);

    if(!m) continue;
    parent.set(+d, +m[2]);
    comm.set(+d, m[1]);
  }

  const wanted = new Set();

  for(const [pid, name] of comm) {
    const exe = readProc(pid, 'cmdline').split('\0')[0].split('/').pop();

    if(pid != process.pid && (name == opts.filter || exe == opts.filter)) wanted.add(pid);
  }

  /* any process whose ancestry hits a wanted pid counts as a child */
  for(const pid of parent.keys()) {
    for(let p = parent.get(pid), n = 0; p > 1 && n < 64; p = parent.get(p), n++)
      if(wanted.has(p)) return true;
  }

  return wanted.size > 0;
}

/* ---- diff ---------------------------------------------------------- */

/* splits unified diff text into hunks:
 * @@ -1,2 +1,3 @@ -> { header, newStart, lines: [' a', '+b'], key } */
function parseHunks(text) {
  const hunks = [];
  let cur = null;

  for(const line of text.split('\n')) {
    const m = /^@@ -\d+(?:,\d+)? \+(\d+)/.exec(line);

    if(m) {
      cur = { header: line, newStart: +m[1], lines: [] };
      hunks.push(cur);
    } else if(cur && /^[-+ \\]/.test(line)) cur.lines.push(line);
  }

  for(const h of hunks) h.key = checksum(h.lines.join('\n'));
  return hunks;
}

/* where the first change of a hunk is in the new file: { line, column }.
 * for a replaced line the column is the first differing character. */
function firstChange(hunk) {
  let line = hunk.newStart;

  for(let i = 0; i < hunk.lines.length; i++) {
    const l = hunk.lines[i];

    if(l[0] == ' ') line++;
    else if(l[0] == '+') {
      const prev = hunk.lines[i - 1];
      let column = 1;

      if(prev && prev[0] == '-') while(column < l.length && l[column] == prev[column]) column++;
      return { line, column };
    } else if(l[0] == '-') {
      const next = hunk.lines.slice(i).find(x => x[0] != '-');
      let column = 1;

      if(next && next[0] == '+') while(column < next.length && next[column] == l[column]) column++;
      return { line, column };
    }
  }

  return { line, column: 1 };
}

/* one line of a hunk as a gutter plus text, changed lines on a colored
 * background:  3 +import { Popover } from './Popover.js'; */
function fmtLine(l, num) {
  const text = `${String(num).padStart(5)} ${l[0]}${l.slice(1)}`;

  return l[0] == '+' ? GREEN_BG + text + OFF : l[0] == '-' ? RED_BG + text + OFF : text;
}

/* prints one hunk, cutting the middle of a long one:
 *   ........ 1337 more lines ..... */
function printHunk(h) {
  let o = +/^@@ -(\d+)/.exec(h.header)[1], n = h.newStart;
  const rows = h.lines.map(l => fmtLine(l, l[0] == '-' ? o++ : (o++, n++)));
  const keep = opts.context;

  if(opts.full || rows.length <= 2 * keep + 1) return void rows.forEach(r => console.log(r));

  rows.slice(0, keep).forEach(r => console.log(r));
  console.log(`${DIM}........ ${rows.length - 2 * keep} more lines .....${OFF}`);
  rows.slice(-keep).forEach(r => console.log(r));
}

/* heading in the style  ───── / path  +9 -4 / ───── */
function printHeader(rel, hunks, untracked) {
  const add = hunks.reduce((a, h) => a + h.lines.filter(l => l[0] == '+').length, 0);
  const del = hunks.reduce((a, h) => a + h.lines.filter(l => l[0] == '-').length, 0);
  const rule = DIM + '─'.repeat(Math.min(process.stdout.columns || 78, 78)) + OFF;

  console.log(rule);
  console.log(`${HEAD}${rel}${OFF} ${untracked ? DIM + '(untracked)' + OFF : `${GREEN_FG}+${add}${OFF} ${RED_FG}-${del}${OFF}`}`);
  console.log(rule);
}

function isTracked(rel) {
  return run(`git ls-files --error-unmatch -- ${quote(rel)}`) != '';
}

/* diff text of one file against HEAD, an untracked file as all-added */
function diffOf(rel, tracked = isTracked(rel)) {
  return run(tracked ? `git diff --no-color -U3 HEAD -- ${quote(rel)}` : `git diff --no-color -U3 --no-index -- /dev/null ${quote(rel)}`);
}

function check(rel) {
  const untracked = !isTracked(rel);
  const hunks = parseHunks(diffOf(rel, !untracked));
  const known = seen.get(rel) ?? new Set();
  const fresh = hunks.filter(h => !known.has(h.key));

  /* a reverted hunk leaves the set, so writing it again shows again */
  seen.set(rel, new Set(hunks.map(h => h.key)));

  if(!fresh.length) return;
  if(opts.filter && !filterActive()) return;

  printHeader(rel, hunks, untracked);
  fresh.forEach(printHunk);

  if(opts.viewer) {
    const { line, column } = firstChange(fresh[fresh.length - 1]);

    try {
      execSync(`${opts.viewer} ${quote(`${opts.dir}/${rel}:${line}:${column}`)} > /dev/null 2>&1`);
    } catch(e) {}
  }
}

/* ---- watching ------------------------------------------------------ */

const ino = new inotify();

function addTree(dir) {
  const rel = dir.slice(opts.dir.length + 1);

  if(rel && run(`git check-ignore -q -- ${quote(rel)} && echo y`) != '') return;

  ino.add(dir, MASK);

  for(const name of readdirSync(dir)) {
    if(name == '.git') continue;

    const path = `${dir}/${name}`;
    const st = os.lstat(path)[0];

    if(st && (st.mode & os.S_IFMT) == os.S_IFDIR) addTree(path);
  }
}

function schedule(rel) {
  clearTimeout(timers.get(rel));
  timers.set(rel, setTimeout(() => {
    timers.delete(rel);
    if(existsSync(`${opts.dir}/${rel}`)) check(rel);
  }, DEBOUNCE_MS));
}

ino.onread = ev => {
  const watch = ino.watch(ev.wd);

  if(!watch || !ev.name || ev.name.endsWith('.swp') || ev.name.endsWith('~')) return;

  const path = `${watch.pathname}/${ev.name}`;

  if(ev.mask & IN_ISDIR) {
    if(ev.mask & IN_CREATE) addTree(path);
  } else schedule(path.slice(opts.dir.length + 1));
};

/* ---- main ---------------------------------------------------------- */

const args = process.argv.slice(2);

for(let i = 0; i < args.length; i++) {
  const a = args[i];

  if(a == '-h' || a == '--help') {
    process.stdout.write(USAGE);
    process.exit(0);
  } else if(a == '--viewer') opts.viewer = args[++i];
  else if(a == '--filter') opts.filter = args[++i];
  else if(a == '--context') opts.context = +args[++i];
  else if(a == '--full') opts.full = true;
  else if(a[0] == '-') {
    process.stderr.write(`git-watch: unknown option ${a}\n${USAGE}`);
    process.exit(2);
  } else opts.dir = a;
}

opts.dir = run('git rev-parse --show-toplevel', opts.dir).trim();

if(!opts.dir) {
  process.stderr.write('git-watch: not inside a git work tree\n');
  process.exit(1);
}

addTree(opts.dir);

/* the current diff is the baseline, only later changes are shown */
for(const f of run('git diff --name-only HEAD').split('\n').filter(Boolean)) seen.set(f, new Set(parseHunks(diffOf(f)).map(h => h.key)));

os.signal(os.SIGINT, () => {
  try {
    unlinkSync(tmp);
  } catch(e) {}
  std.exit(0);
});

process.stderr.write(`git-watch: watching ${opts.dir}${opts.filter ? ` for ${opts.filter}` : ''}\n`);
