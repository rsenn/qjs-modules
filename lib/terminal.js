/* terminal.js: the low-level terminal layer: tty I/O, escape sequences, raw mode, size, widths.
 *
 * depends on: terminal/internal.js (the backend: misc, os, textcode on qjsm, process
 *             and node:fs on node, bun and deno).
 *
 * rule: every write is one ttyWrite(f, string) call. On qjsm it flushes f, and a Win32
 * console without VT processing gets the sequences as console calls.
 * f is a std FILE on qjsm, and process.stdout, process.stderr or an fd number elsewhere.
 *
 * exports:
 *   - Cursor movement: cursorHome, cursorUp/Down/Left/Right, cursorPosition, cursorOrigin,
 *     cursorSave/Restore, cursorQuery, cursorHide/Show
 *   - Erase: eraseInDisplay, eraseInLine
 *   - Scroll: scrollUp/Down, setScrollRegion, resetScrollRegion
 *   - Screen switching: setScreen, alternateScreen
 *   - Line wrap: linewrapEnable/Disable
 *   - Mouse: mouseEnable/Disable, mousetrackingEnable/Disable
 *   - SGR/color: sgr, reverseVideoOn/Off, rgbForeground/Background, hslToRgb
 *   - String measures: stripAnsi, stringWidth, charWidth
 *   - Text: truncate, truncateLines, wrap, pad, fitTable, layoutInput, cells
 *
 * the higher layers import from here and live in terminal/:
 *   keys (decodeKeys, readKey), capabilities (queryCapabilities, enableCapabilities),
 *   style (style, gradient, parseColor), screen (Screen, Region, BOX_*),
 *   widgets (rule, boxLines, table, panel, ...), scrollview, lineeditor
 */


import { CSI, ESC, RESET, backend } from 'terminal/internal';

export const EAGAIN = backend.EAGAIN;

export const ttyGetWinSize = backend.ttyGetWinSize;

export const ttyRead = backend.ttyRead;

export const ttySetRaw = backend.ttySetRaw;

export const ttyWrite = backend.ttyWrite;

/* CSI <n> <c>, the count left out when it is 1 or missing */
const csiCount = (f, n, c) => ttyWrite(f, CSI + (n > 1 ? n : '') + c);

/* cursor home/movement. All functions use CSI sequences.
 * n defaults to 1 when not specified.
 * Row/column numbers are 1-based. */
export const cursorHome = f => ttyWrite(f, '\r');

/* move cursor up n rows */
export const cursorUp = (f, n = 1) => csiCount(f, n, 'A');

/* move cursor down n rows */
export const cursorDown = (f, n = 1) => csiCount(f, n, 'B');

/* move cursor forward n columns */
export const cursorForward = (f, n = 1) => csiCount(f, n, 'C');

/* move cursor backward n columns */
export const cursorBackward = (f, n = 1) => csiCount(f, n, 'D');

/* move cursor down n lines, to first column of target line */
export const cursorNextLine = (f, n = 1) => csiCount(f, n, 'E');

/* move cursor up n lines, to first column of target line */
export const cursorPreviousLine = (f, n = 1) => csiCount(f, n, 'F');

/* move cursor to horizontal column n (1-based) */
export const cursorHorizontalAbsolute = (f, n) => csiCount(f, n, 'G');

/* move cursor to row+column position (1-based) */
export const cursorPosition = (f, row, column) => ttyWrite(f, `${CSI}${row};${column}H`);

/* set cursor to origin (top-left, 1-based) */
export const cursorOrigin = f => ttyWrite(f, CSI + 'H');

/* save cursor position (ANSI SAVE - ESC 7) */
export const cursorSave = f => ttyWrite(f, ESC + '7');

/* restore cursor position (ANSI RESTORE - ESC 8) */
export const cursorRestore = f => ttyWrite(f, ESC + '8');

/* query cursor position: terminal responds with ESC [ row;column R */
export const cursorQuery = f => ttyWrite(f, CSI + '6n');

/* hide cursor (DECTCEM - ?25l) */
export const cursorHide = f => ttyWrite(f, CSI + '?25l');

/* show cursor (DECTCEM - ?25h) */
export const cursorShow = f => ttyWrite(f, CSI + '?25h');

/* erase n=0: from cursor to end of display, 1: from cursor to start, 2: whole display. */
export const eraseInDisplay = (f, n = 0) => ttyWrite(f, `${CSI}${n}J`);

/* erase n=0: from cursor to end of line, 1: from start to cursor, 2: whole line. */
export const eraseInLine = (f, n = 0) => ttyWrite(f, `${CSI}${n}K`);

/* scroll n rows up (default 1) */
export const scrollUp = (f, n = 1) => csiCount(f, n, 'S');

/* scroll n rows down (default 1) */
export const scrollDown = (f, n = 1) => csiCount(f, n, 'T');

/* DECSTBM: scrolling stays within rows top to bottom, 1-based, inclusive */
export const setScrollRegion = (f, top, bottom) => ttyWrite(f, `${CSI}${top};${bottom}r`);

export const resetScrollRegion = f => ttyWrite(f, CSI + 'r');

/* set alternate screen buffer (DECSCA/DECSCL).
 * When alternate is true (default), switches to the alternate screen buffer,
 * which is separate from the main screen and restores on exit.
 * When alternate is false, switches back to the normal screen. */
export const setScreen = (f, alternate = false) => ttyWrite(f, CSI + '?1049' + (alternate ? 'h' : 'l'));

/* switch to the alternate screen buffer */
export const setAlternateScreen = f => setScreen(f, true);

/* switch back to the normal screen */
export const setNormalScreen = f => setScreen(f, false);

/* alternateScreen(f, fn): fn runs on the alternate screen, the normal one comes back after
 *
 *   alternateScreen(std.out, () => pager(lines))
 *
 * the normal screen and its cursor are kept untouched meanwhile. when fn
 * returns a promise, the switch back waits for it. errors pass through. */
export function alternateScreen(f, fn) {
  let promise;

  setScreen(f, true);

  try {
    promise = fn();
  } finally {
    if(!(promise instanceof Promise)) setScreen(f, false);
  }

  return promise instanceof Promise ? promise.finally(() => setScreen(f, false)) : promise;
}

/* enable automatic line wrapping (DECAWM - ?7h) */
export const linewrapEnable = f => ttyWrite(f, CSI + '?7h');

/* disable automatic line wrapping (DECAWM - ?7l) */
export const linewrapDisable = f => ttyWrite(f, CSI + '?7l');

/* enable mouse tracking (deprecated legacy tracking modes 1000/1006/1015) */
export const mousetrackingEnable = f => ttyWrite(f, CSI + '?1000;1006;1015h');

/* disable mouse tracking */
export const mousetrackingDisable = f => ttyWrite(f, CSI + '?1000;1006;1015l');

const MOUSE_MODES = { click: 1000, drag: 1002, move: 1003 };

/* mouseEnable(f, mode): mouse reports as SGR sequences, decodeKeys() reads them
 *
 *   click  presses, releases and the wheel
 *   drag   also motion while a button is held
 *   move   also motion without a button */
export const mouseEnable = (f, mode = 'click') => ttyWrite(f, `${CSI}?${MOUSE_MODES[mode] ?? 1000}h${CSI}?1006h`);

export const mouseDisable = f => ttyWrite(f, `${CSI}?1003l${CSI}?1002l${CSI}?1000l${CSI}?1006l${CSI}?1015l`);

/* n blank lines at the cursor row, the rows below move down inside the scroll region */
export const insertLines = (f, n) => csiCount(f, n, 'L');

/* n lines removed at the cursor row, the rows below move up inside the scroll region */
export const deleteLines = (f, n) => csiCount(f, n, 'M');

/* ---- terminal features: switches, see queryCapabilities() for which ones the terminal has ---- */

/* synchronized output: what lies between begin and end is drawn as one frame, no tearing.
 * syncOutput(f, frame) sends frame wrapped in both with one ttyWrite. a terminal without it ignores them. */
export const SYNC_BEGIN = CSI + '?2026h';

export const SYNC_END = CSI + '?2026l';

export const syncOutput = (f, frame) => ttyWrite(f, SYNC_BEGIN + frame + SYNC_END);

/* bracketed paste: a paste arrives as ESC [200~ text ESC [201~,
 * decodeKeys() makes it one 'paste' key with key.paste containing the text. */
export const bracketedPasteEnable = f => ttyWrite(f, CSI + '?2004h');

export const bracketedPasteDisable = f => ttyWrite(f, CSI + '?2004l');

/* focus events: ESC [I when the window gets the focus, ESC [O when it loses it ('focusin', 'focusout') */
export const focusEventsEnable = f => ttyWrite(f, CSI + '?1004h');

export const focusEventsDisable = f => ttyWrite(f, CSI + '?1004l');

/* grapheme clusters: the terminal takes an emoji sequence or a base with its marks as one cell group */
export const graphemeClustersEnable = f => ttyWrite(f, CSI + '?2027h');

export const graphemeClustersDisable = f => ttyWrite(f, CSI + '?2027l');

/* kitty keyboard protocol: pushes flags on the terminal's stack, disable pops them again.
 * flags 1 reports Esc, Alt and Ctrl chords as ESC [ code ; modifiers u, decodeKeys() reads them,
 * Shift-Enter and Ctrl-Enter are told apart then. 2 adds key release and repeat, 8 reports every key. */
export const kittyKeyboardEnable = (f, flags = 1) => ttyWrite(f, `${CSI}>${flags}u`);

export const kittyKeyboardDisable = f => ttyWrite(f, CSI + '<u');

/* device code query (DA - primary device attributes) */
export const devicecodeQuery = f => ttyWrite(f, CSI + 'c');

/* device status report query (DSR - 5n) */
export const devicestatusQuery = f => ttyWrite(f, CSI + '5n');

/* full terminal reset (RIS - ESC c) */
export const deviceReset = f => ttyWrite(f, ESC + 'c');

/* set tab stop at current column (HTS - ESC H) */
export const tabSet = f => ttyWrite(f, ESC + 'H');

/* clear tab stop at current column (TBC - CSI g) */
export const tabClear = f => ttyWrite(f, CSI + 'g');

/* clear all tab stops (TBC - CSI 3g) */
export const tabsClearall = f => ttyWrite(f, CSI + '3g');

/* SGR text attributes: sgr(f, 7) reverse video, sgr(f, 0) reset */
export const sgr = (f, ...codes) => ttyWrite(f, CSI + codes.join(';') + 'm');

export const reverseVideoOn = f => sgr(f, 7);

export const reverseVideoOff = f => sgr(f, 0);

export const rgbForeground = (f, r, g, b) => sgr(f, 38, 2, r, g, b);

export const rgbBackground = (f, r, g, b) => sgr(f, 48, 2, r, g, b);

/* hslToRgb(h, s, l): [r, g, b] each 0-255, for h in degrees, s and l in 0-1
 *
 *   hslToRgb(120, 1, 0.5)   // [0, 255, 0]
 *
 * walking h by the golden angle (137.508) gives well spread hues. */
export function hslToRgb(h, s, l) {
  h = ((h % 360) + 360) % 360;

  const c = (1 - Math.abs(2 * l - 1)) * s;
  const x = c * (1 - Math.abs(((h / 60) % 2) - 1));
  const m = l - c / 2;

  const [r1, g1, b1] = h < 60 ? [c, x, 0] : h < 120 ? [x, c, 0] : h < 180 ? [0, c, x] : h < 240 ? [0, x, c] : h < 300 ? [x, 0, c] : [c, 0, x];

  return [Math.round((r1 + m) * 255), Math.round((g1 + m) * 255), Math.round((b1 + m) * 255)];
}

/* raw mode of input fd: no echo, no line buffering, UTF-8 on Windows.
 * returns true when the output does VT itself. */
export const enableRawMode = fd => ttySetRaw(fd);

export const disableRawMode = fd => ttySetRaw(fd, true);

/* [columns, rows] of the terminal behind a FILE, stream or fd, [80, 24] when it is none */
export function windowSize(f) {
  return ttyGetWinSize(typeof f == 'number' ? f : (f.fileno?.() ?? f.fd ?? 1)) ?? [80, 24];
}

/* ---- widths: how many terminal cells a string takes ---- */

const WIDE = [
  [0x1100, 0x115f],
  [0x2e80, 0x303e],
  [0x3041, 0x33ff],
  [0x3400, 0x4dbf],
  [0x4e00, 0x9fff],
  [0xa000, 0xa4cf],
  [0xac00, 0xd7a3],
  [0xf900, 0xfaff],
  [0xfe30, 0xfe6f],
  [0xff00, 0xff60],
  [0xffe0, 0xffe6],
  [0x1f300, 0x1f64f],
  [0x1f680, 0x1f6ff],
  [0x1f900, 0x1f9ff],
  [0x20000, 0x3fffd],
];

const ZERO = [
  [0x300, 0x36f],
  [0x200b, 0x200f],
  [0x20d0, 0x20ff],
  [0xfe00, 0xfe0f],
  [0x1ab0, 0x1aff],
  [0x1dc0, 0x1dff],
];

/* true when cp lies in one of the sorted [lo, hi] ranges, binary search */
function within(ranges, cp) {
  let lo = 0,
    hi = ranges.length - 1;

  while(lo <= hi) {
    const mid = (lo + hi) >> 1,
      r = ranges[mid];

    if(cp < r[0]) hi = mid - 1;
    else if(cp > r[1]) lo = mid + 1;
    else return true;
  }

  return false;
}

/* cells of one character: 0 for controls and combining marks, 2 for wide ones */
export const charWidth = ch => cpWidth(ch.codePointAt(0));

/* CSI, OSC to BEL or ST, DCS/SOS/PM/APC to ST, two-byte ESC sequences */
const ANSI_RE = /\x1b(?:\[[0-?]*[ -\/]*[@-~]|\][^\x07\x1b]*(?:\x07|\x1b\\)?|[PX^_][^\x1b]*(?:\x1b\\)?|[ -\/]*[0-~])/g;

const PLAIN_RE = /^[\x20-\x7e]*$/;

/* ---- clusters: the characters a user sees, as a cursor steps over them ---- */

const EXTEND = [
  [0x300, 0x36f],
  [0x483, 0x489],
  [0x591, 0x5bd],
  [0x5bf, 0x5bf],
  [0x5c1, 0x5c2],
  [0x610, 0x61a],
  [0x64b, 0x65f],
  [0x670, 0x670],
  [0x6d6, 0x6dc],
  [0xe31, 0xe31],
  [0xe34, 0xe3a],
  [0xe47, 0xe4e],
  [0x1ab0, 0x1aff],
  [0x1dc0, 0x1dff],
  [0x200c, 0x200d],
  [0x20d0, 0x20ff],
  [0x3099, 0x309a],
  [0xfe00, 0xfe0f],
  [0xfe20, 0xfe2f],
  [0x1f3fb, 0x1f3ff],
  [0xe0020, 0xe007f],
  [0xe0100, 0xe01ef],
];

const PICT = [
  [0xa9, 0xa9],
  [0xae, 0xae],
  [0x203c, 0x203c],
  [0x2049, 0x2049],
  [0x2122, 0x2122],
  [0x2139, 0x2139],
  [0x2194, 0x21aa],
  [0x231a, 0x23ff],
  [0x24c2, 0x24c2],
  [0x25aa, 0x25fe],
  [0x2600, 0x27bf],
  [0x2934, 0x2935],
  [0x2b05, 0x2b55],
  [0x3030, 0x3030],
  [0x303d, 0x303d],
  [0x3297, 0x3297],
  [0x3299, 0x3299],
  [0x1f000, 0x1faff],
];

/* Hangul class: 1 L, 2 V, 3 T, 4 LV, 5 LVT, 0 none */
const hangul = cp => (cp >= 0x1100 && cp <= 0x115f ? 1 : cp >= 0x1160 && cp <= 0x11a7 ? 2 : cp >= 0x11a8 && cp <= 0x11ff ? 3 : cp >= 0xac00 && cp <= 0xd7a3 ? ((cp - 0xac00) % 28 ? 5 : 4) : 0);

const isControl = cp => cp < 0x20 || (cp >= 0x7f && cp < 0xa0) || cp == 0x2028 || cp == 0x2029;

const isRI = cp => cp >= 0x1f1e6 && cp <= 0x1f1ff;

/* true when a cluster boundary lies before cps[i], 0 < i < cps.length */
function clusterBreak(cps, i) {
  const a = cps[i - 1],
    b = cps[i];

  if(a == 13 && b == 10) return false;
  if(isControl(a) || isControl(b)) return true;
  if(within(EXTEND, b)) return false;

  if(a == 0x200d && within(PICT, b)) {
    let k = i - 1;

    while(k > 0 && within(EXTEND, cps[k - 1]) && cps[k - 1] != 0x200d) k--;

    if(k > 0 && within(PICT, cps[k - 1])) return false;
  }

  if(isRI(a) && isRI(b)) {
    let k = i,
      run = 0;

    while(k > 0 && isRI(cps[k - 1])) (run++, k--);

    return run % 2 == 0;
  }

  const ha = hangul(a),
    hb = hangul(b);

  if(ha && hb && ((ha == 1 && hb != 3) || ((ha == 4 || ha == 2) && (hb == 2 || hb == 3)) || ((ha == 5 || ha == 3) && hb == 3))) return false;

  return true;
}

/* the code points of str and the UTF-16 index each starts at, plus the end */
function clusterScan(str) {
  const cps = [],
    off = [0];

  for(const ch of String(str)) {
    const cp = ch.codePointAt(0);

    cps.push(cp);
    off.push(off[off.length - 1] + (cp >= 0x10000 ? 2 : 1));
  }

  return [cps, off];
}

/* the index of the character boundary before pos: prevCluster('e\u0301x', 3) is 2, of 'e\u0301' alone 0 */
function prevClusterJs(str, pos = 0) {
  const [cps, off] = clusterScan(str);
  let i = 0;

  while(i + 1 < cps.length && off[i + 1] < pos) i++;
  while(i > 0 && !clusterBreak(cps, i)) i--;

  return pos <= 0 ? 0 : off[i];
}

/* the index of the character boundary after pos: nextCluster('e\u0301x', 0) is 2 */
function nextClusterJs(str, pos = 0) {
  const [cps, off] = clusterScan(str);
  let i = 0;

  while(i < cps.length && off[i + 1] <= pos) i++;

  if(i < cps.length) {
    i++;

    while(i < cps.length && !clusterBreak(cps, i)) i++;
  }

  return off[i];
}

export const prevCluster = backend.prevCluster ?? prevClusterJs;

export const nextCluster = backend.nextCluster ?? nextClusterJs;

/* str without escape sequences: stripAnsi('\x1b[1mb\x1b[0m') is 'b' */
const stripAnsiJs = str => String(str).replace(ANSI_RE, '');

export const stripAnsi = backend.stripAnsi ?? stripAnsiJs;

/* cells of one character: 0 for controls and combining marks, 2 for wide ones.
 * cp is a Unicode code point. Returns 1 for typical printable, 0 for controls,
 * 2 for wide (CJK, etc), 0 for combining. */
/* cells of code point cp: 0 for controls and combining marks, 2 for wide ones */
function cpWidth(cp) {
  if(cp >= 0x20 && cp < 0x7f) return 1;
  if(cp < 0x20 || (cp >= 0x7f && cp < 0xa0)) return 0;
  if(cp < 0x300) return 1;

  return within(ZERO, cp) ? 0 : within(WIDE, cp) ? 2 : 1;
}

/* cells of str without its escape sequences: stringWidth('漢x') is 3 */
function stringWidthJs(str) {
  str = String(str);

  /* printable ASCII without ESC: one cell each */
  if(PLAIN_RE.test(str)) return str.length;

  str = str.replace(ANSI_RE, '');

  let w = 0;

  for(let i = 0, n = str.length; i < n; i++) {
    const cp = str.codePointAt(i);

    if(cp > 0xffff) i++;

    w += cpWidth(cp);
  }

  return w;
}

export const stringWidth = backend.stringWidth ?? stringWidthJs;

/* ---- keys: input bytes to { sequence, name, ctrl, meta, shift } ---- */

/* layoutInput(prefixes, text, cursor, columns): the rows an edited text takes, and the cursor in them
 *
 *   layoutInput(['> ', '. '], 'ab\ncd', 4, 80)   // { lines: ['> ab', '. cd'], row: 1, col: 3 }
 *
 * prefixes[0] starts the text, prefixes[1] each line after a newline in
 * it. rows are cut at columns cells, cursor is an index into text. a
 * cursor after a full row sits at the start of the next one. */
function layoutInputJs([first, cont = ''], text, cursor, columns) {
  const lines = [];
  let row = 0,
    col = 0,
    pos = 0;

  for(const [k, line] of String(text).split('\n').entries()) {
    let cur = k ? cont : first,
      w = stringWidth(cur);

    for(let i = 0; i < line.length;) {
      const ch = String.fromCodePoint(line.codePointAt(i));
      const cw = charWidth(ch);

      if(w + cw > columns) {
        lines.push(cur);
        cur = '';
        w = 0;
      }

      if(pos + i === cursor) [row, col] = [lines.length, w];

      cur += ch;
      w += cw;
      i += ch.length;
    }

    if(pos + line.length === cursor) {
      if(w >= columns) {
        lines.push(cur);
        cur = '';
        w = 0;
      }

      [row, col] = [lines.length, w];
    }

    lines.push(cur);
    pos += line.length + 1;
  }

  return { lines, row, col };
}

export const layoutInput = backend.layoutInput ?? layoutInputJs;

/* one escape sequence at a position, for walking strings cell by cell */
const ANSI_STICKY = new RegExp(ANSI_RE.source, 'y');

const SGR_RE = /\x1b\[[\d;]*m/g;

/* [text, width] for each escape sequence (width 0) and each character of str */
export function* cells(str) {
  for(let i = 0; i < str.length;) {
    ANSI_STICKY.lastIndex = i;
    const m = str[i] == ESC && ANSI_STICKY.exec(str);

    if(m) {
      yield [m[0], -1];
      i += m[0].length;
      continue;
    }

    const cp = str.codePointAt(i),
      len = cp > 0xffff ? 2 : 1;

    yield [str.substr(i, len), cpWidth(cp)];
    i += len;
  }
}

/* truncate(str, width, ellipsis): str cut to width cells, escapes kept
 *
 *   truncate('hello world', 8)   // 'hello w…'
 *
 * a str that fits comes back unchanged. */
function truncateJs(str, width, ellipsis = '…') {
  str = String(str);
  if(stringWidth(str) <= width) return str;

  const ew = stringWidth(ellipsis),
    room = Math.max(0, width - ew);
  /* plain ASCII: a slice, no walk */
  if(PLAIN_RE.test(str)) return str.slice(0, room) + (width >= ew ? ellipsis : '');

  let out = '',
    w = 0,
    styled = false;

  for(let i = 0, n = str.length; i < n;) {
    if(str.charCodeAt(i) == 27) {
      ANSI_STICKY.lastIndex = i;

      const m = ANSI_STICKY.exec(str);

      if(m) {
        out += m[0];
        styled = true;
        i += m[0].length;
        continue;
      }
    }

    const cp = str.codePointAt(i),
      cw = cpWidth(cp),
      len = cp > 0xffff ? 2 : 1;

    if(w + cw > room) break;

    out += str.substr(i, len);
    w += cw;
    i += len;
  }

  return out + (styled ? RESET : '') + (width >= ew ? ellipsis : '');
}

export const truncate = backend.truncate ?? truncateJs;

/* truncateLines(lines, width, ellipsis): truncate() of each element of lines
 *
 *   truncateLines(['hello world', 'hi'], 6)   // ['hello…', 'hi']
 *
 * one native call for the whole array where textcode has it. */
export const truncateLines = backend.truncateLines ?? ((lines, width, ellipsis) => Array.from(lines, line => truncate(line, width, ellipsis)));

/* tableText(rows): rows of cells, the first is the header, as aligned text
 *
 *   tableText([['a', 'n'], ['bb', 5]])   // 'a   n\nbb  5'
 *
 * columns are two spaces apart and as wide as their widest cell in
 * stringWidth cells. a column below the header whose cells are all made of
 * 0-9 . , k M G % - lines up on the right, the others on the left. */
function tableText(rows) {
  const widths = rows[0].map((_, i) => rows.reduce((m, r) => Math.max(m, stringWidth(r[i])), 0));
  const numeric = rows[0].map((_, i) => i > 0 && rows.slice(1).every(r => /^[\d.,kMG%-]*$/.test(String(r[i]))));

  return rows
    .map((r, k) =>
      rows[0]
        .map((_, i) => {
          const c = String(r[i]),
            fill = ' '.repeat(Math.max(0, widths[i] - stringWidth(c)));

          return numeric[i] && k ? fill + c : c + fill;
        })
        .join('  ')
        .replace(/ +$/, ''),
    )
    .join('\n');
}

/* fitOnce(width, make, cols): one try of fitTable, { text, fits }
 *
 * make(bar) is called with 6, 3 and 0 until the table fits. cols are the
 * columns that may shrink: they give up what is missing, the widest first,
 * each down to its header width (at least 6). the meters shrink first: a
 * try with a meter is skipped when the first column would lose below 24. */
function fitOnce(width, make, cols) {
  const widths = rows => rows[0].map((_, i) => rows.reduce((m, r) => Math.max(m, stringWidth(r[i])), 0));
  let rows;

  for(const bar of [6, 3, 0]) {
    rows = make(bar);

    const colw = widths(rows);

    cols = cols.filter(i => i >= 0 && i < colw.length);

    let over = width > 0 ? colw.reduce((a, w) => a + w, 2 * (colw.length - 1)) - width : 0;

    if(over <= 0) return { text: tableText(rows), fits: true };
    if(bar !== 0 && (!cols.length || colw[cols[0]] - over < 24)) continue;

    const target = new Map(cols.map(i => [i, colw[i]])),
      min = i => Math.max(6, stringWidth(rows[0][i]));

    while(over > 0) {
      let best;

      for(const [i, w] of target) if(w > min(i) && (best === undefined || w > target.get(best))) best = i;
      if(best === undefined) break;
      target.set(best, target.get(best) - 1);
      over--;
    }

    return { text: tableText(rows.map((r, k) => r.map((c, i) => (k && target.has(i) ? truncate(c, target.get(i)) : c)))), fits: over <= 0 };
  }
}

/* fitTableJs(width, make, flex, drop): a table that fits width cells
 *
 *   fitTable(80, bar => rows(bar), [5], [4, 3])
 *
 * make(bar) builds the rows, header first, with meters of bar cells, once
 * for each bar, also after a column was dropped; the
 * meters shrink (6, 3, none), then the flex columns are cut with an
 * ellipsis, then the drop columns go one by one, in the order given.
 * flex and drop are column indices of the rows make returns, flex may be
 * one number. no width, 0, keeps the first try. */
function fitTableJs(width, make, flex = 0, drop = []) {
  const gone = new Set(),
    made = new Map();
  const rowsOf = bar => (made.has(bar) || made.set(bar, make(bar)), made.get(bar));

  for(;;) {
    const cols = []
      .concat(flex)
      .filter(i => !gone.has(i))
      .map(i => i - [...gone].filter(g => g < i).length);
    const r = fitOnce(
      width,
      bar => rowsOf(bar).map(row => row.filter((_, i) => !gone.has(i))),
      cols.filter((c, k) => cols.indexOf(c) == k),
    );
    const next = drop.find(d => !gone.has(d));

    if(r.fits || next === undefined) return r.text;
    gone.add(next);
  }
}

export const fitTable = backend.fitTable ?? fitTableJs;

/* pad(str, width, align, fill): str filled to width cells, truncated when longer
 *
 *   pad('ab', 6, 'center')   // '  ab  '
 *
 * align is left, right or center. */
export function pad(str, width, align = 'left', fill = ' ') {
  str = truncate(str, width);

  const gap = Math.max(0, width - stringWidth(str));
  const left = align == 'right' ? gap : align == 'center' ? Math.floor(gap / 2) : 0;

  return fill.repeat(left) + str + fill.repeat(gap - left);
}

/* SGR sequences that are still in effect at the end of str */
function activeStyle(str) {
  if(!str.includes(ESC)) return '';

  let on = '';

  for(const [t] of str.matchAll(SGR_RE)) on = t == `${CSI}m` || t == `${CSI}0m` ? '' : on + t;

  return on;
}

/* str without trailing spaces, escape sequences at the end kept */
const trimEnd = str => str.replace(/ +((?:\x1b\[[\d;]*m)*)$/, '$1');

/* wrap(str, width): lines of at most width cells, broken at spaces
 *
 *   wrap('one two three', 7)   // ['one two', 'three']
 *
 * newlines in str are kept, words longer than width are split. a color
 * that runs over a break is closed at the line end and reopened. */
function wrapJs(str, width) {
  const lines = [];
  width = Math.max(1, width);

  for(const para of String(str).split('\n')) {
    let line = '',
      lw = 0;

    const flush = () => {
      const open = activeStyle(line);

      lines.push(trimEnd(open ? line + RESET : line));
      line = open;
      lw = 0;
    };

    for(let word of para.split(/(?<= )/)) {
      let ww = stringWidth(word);

      if(lw && lw + (word.trimEnd().length == word.length ? ww : stringWidth(word.trimEnd())) > width) flush();

      while(ww > width) {
        let head = '',
          hw = 0,
          k = 0;

        for(const [t, cw] of cells(word)) {
          if(cw >= 0 && hw + cw > width - lw) {
            /* a cell wider than an empty line still goes onto it */
            if(!hw && !lw) {
              head += t;
              hw += cw;
              k += t.length;
            }

            break;
          }

          head += t;
          hw += Math.max(0, cw);
          k += t.length;
        }

        if(!k) {
          flush();
          continue;
        }

        line += head;
        lw += hw;
        word = word.slice(k);
        ww = stringWidth(word);
        flush();
      }

      line += word;
      lw += ww;
    }

    lines.push(trimEnd(line));
  }

  return lines;
}

export const wrap = backend.wrap ?? wrapJs;
