/* terminal.js: escape sequences, raw mode, size, keys, widths and a buffered Screen.
 *
 * depends on: qjsm: misc (ttyWrite, ttyRead, ttySetRaw, ttyGetWinSize), os (sleep), textcode.
 *             node, bun, deno: process and node:fs.
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
 *   - Key decoding: decodeKeys, readKey, charKey
 *   - String measures: stripAnsi, stringWidth, charWidth
 *   - Capabilities: queryCapabilities
 *   - Screen class: Screen (write, moveTo, clear, clearLine, fg/bg/sgr, resetAttrs, fillRect, box)
 */

/* the four tty primitives, from misc on qjsm and from process and node:fs elsewhere */
const backend = await (async () => {
  /* scriptArgs is QuickJS only: importing misc elsewhere makes bun fetch a package of that name */
  if(typeof scriptArgs != 'undefined') {
    const { EAGAIN, ttyGetWinSize, ttyRead, ttySetRaw, ttyWrite } = await import('misc');
    const { sleep } = await import('os');
    /* the text functions are missing from a textcode older than this file: the JS ones below stay in use */
    const { TextDecoder, stringWidth, stripAnsi, truncate, truncateLines, wrap, fitTable, prevCluster, nextCluster, layoutInput } = await import('textcode');

    return { EAGAIN, ttyGetWinSize, ttyRead, ttySetRaw, ttyWrite, sleep, TextDecoder, stringWidth, stripAnsi, truncate, truncateLines, wrap, fitTable, prevCluster, nextCluster, layoutInput };
  }

  const fs = await import('node:fs');
  const wait = new Int32Array(new SharedArrayBuffer(4));
  const stream = fd => (fd == 2 ? process.stderr : process.stdout);

  return {
    EAGAIN: 11,
    TextDecoder: globalThis.TextDecoder,
    sleep: ms => void Atomics.wait(wait, 0, 0, ms),
    ttyWrite(f, str) {
      if(typeof f == 'number') fs.writeSync(f, str);
      else f.write(str);
    },
    ttyRead(fd, buf, offset, length) {
      try {
        return fs.readSync(fd, new Uint8Array(buf, offset, length), 0, length, null);
      } catch(e) {
        if(e.code == 'EAGAIN') return -11;
        if(e.code == 'EOF') return 0;
        throw e;
      }
    },
    ttySetRaw(fd, restore) {
      if(!process.stdin.isTTY) return false;
      process.stdin.setRawMode(!restore);
      return !!process.stdout.isTTY;
    },
    ttyGetWinSize(fd) {
      const { columns, rows } = stream(fd);

      return columns && rows ? [columns, rows] : undefined;
    },
  };
})();

const { sleep, TextDecoder } = backend;

export const EAGAIN = backend.EAGAIN;
export const ttyGetWinSize = backend.ttyGetWinSize;
export const ttyRead = backend.ttyRead;
export const ttySetRaw = backend.ttySetRaw;
export const ttyWrite = backend.ttyWrite;

const ESC = '\x1b';
const CSI = ESC + '[';

/* CSI <n> <c>, the count left out when it is 1 or missing */
const csiCount = (f, n, c) => ttyWrite(f, CSI + (n > 1 ? n : '') + c);

/* cursor home/movement */
export const cursorHome = f => ttyWrite(f, '\r');
/* move cursor up n rows (default 1) */
export const cursorUp = (f, n = 1) => csiCount(f, n, 'A');
/* move cursor down n rows (default 1) */
export const cursorDown = (f, n = 1) => csiCount(f, n, 'B');
/* move cursor forward n columns (default 1) */
export const cursorForward = (f, n = 1) => csiCount(f, n, 'C');
/* move cursor backward n columns (default 1) */
export const cursorBackward = (f, n = 1) => csiCount(f, n, 'D');
/* move cursor down n lines, cursor to first column of target line */
export const cursorNextLine = (f, n = 1) => csiCount(f, n, 'E');
/* move cursor up n lines, cursor to first column of target line */
export const cursorPreviousLine = (f, n = 1) => csiCount(f, n, 'F');
/* move cursor to horizontal column n (1-based) */
export const cursorHorizontalAbsolute = (f, n) => csiCount(f, n, 'G');
/* move cursor to row+column position (1-based) */
export const cursorPosition = (f, row, column) => ttyWrite(f, `${CSI}${row};${column}H`);
/* set cursor to origin (top-left, 1-based) */
export const cursorOrigin = f => ttyWrite(f, CSI + 'H');
/* save cursor position (ANSI SAVE) */
export const cursorSave = f => ttyWrite(f, ESC + '7');
/* restore cursor position (ANSI RESTORE) */
export const cursorRestore = f => ttyWrite(f, ESC + '8');
/* query cursor position: terminal responds with ESC [ row;column R */
export const cursorQuery = f => ttyWrite(f, CSI + '6n');
/* hide cursor (DECTCEM) */
export const cursorHide = f => ttyWrite(f, CSI + '?25l');
/* show cursor (DECTCEM) */
export const cursorShow = f => ttyWrite(f, CSI + '?25h');

/* n: 0 cursor to end, 1 start to cursor, 2 all */
export const eraseInDisplay = (f, n = 0) => ttyWrite(f, `${CSI}${n}J`);
export const eraseInLine = (f, n = 0) => ttyWrite(f, `${CSI}${n}K`);

export const scrollUp = (f, n) => csiCount(f, n, 'S');
export const scrollDown = (f, n) => csiCount(f, n, 'T');

/* DECSTBM: scrolling stays within rows top to bottom, 1-based, inclusive */
export const setScrollRegion = (f, top, bottom) => ttyWrite(f, `${CSI}${top};${bottom}r`);
export const resetScrollRegion = f => ttyWrite(f, CSI + 'r');

export const setScreen = (f, alternate = false) => ttyWrite(f, CSI + '?1049' + (alternate ? 'h' : 'l'));
export const setAlternateScreen = f => setScreen(f, true);
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

export const linewrapEnable = f => ttyWrite(f, CSI + '?7h');
export const linewrapDisable = f => ttyWrite(f, CSI + '?7l');

export const mousetrackingEnable = f => ttyWrite(f, CSI + '?1000;1006;1015h');
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

/* bracketed paste: a paste arrives as ESC [200~ text ESC [201~, decodeKeys() makes it one 'paste' key */
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

export const devicecodeQuery = f => ttyWrite(f, CSI + 'c');
export const devicestatusQuery = f => ttyWrite(f, CSI + '5n');
export const deviceReset = f => ttyWrite(f, ESC + 'c');
export const tabSet = f => ttyWrite(f, ESC + 'H');
export const tabClear = f => ttyWrite(f, CSI + 'g');
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

/* cells of code point cp: 0 for controls and combining marks, 2 for wide ones */
function cpWidth(cp) {
  if(cp >= 0x20 && cp < 0x7f) return 1;
  if(cp < 0x20 || (cp >= 0x7f && cp < 0xa0)) return 0;
  if(cp < 0x300) return 1;

  return within(ZERO, cp) ? 0 : within(WIDE, cp) ? 2 : 1;
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

const CSI_LETTER = { A: 'up', B: 'down', C: 'right', D: 'left', E: 'clear', F: 'end', H: 'home', P: 'f1', Q: 'f2', R: 'f3', S: 'f4', Z: 'tab' };
const CSI_TILDE = {
  1: 'home',
  2: 'insert',
  3: 'delete',
  4: 'end',
  5: 'pageup',
  6: 'pagedown',
  7: 'home',
  8: 'end',
  11: 'f1',
  12: 'f2',
  13: 'f3',
  14: 'f4',
  15: 'f5',
  17: 'f6',
  18: 'f7',
  19: 'f8',
  20: 'f9',
  21: 'f10',
  23: 'f11',
  24: 'f12',
};

function charKey(ch) {
  const key = { sequence: ch, name: undefined, ctrl: false, meta: false, shift: false };
  const code = ch.codePointAt(0);

  if(ch == '\r') key.name = 'return';
  else if(ch == '\n') key.name = 'enter';
  else if(ch == '\t') key.name = 'tab';
  else if(ch == '\b' || ch == '\x7f') key.name = 'backspace';
  else if(ch == '\x1b') key.name = 'escape';
  else if(ch == ' ') key.name = 'space';
  else if(code <= 0x1a) Object.assign(key, { name: String.fromCharCode(code + 96), ctrl: true });
  else if(ch >= 'A' && ch <= 'Z') Object.assign(key, { name: ch.toLowerCase(), shift: true });
  else key.name = ch;

  return key;
}

/* kitty keyboard protocol: key codes that are no letter, and the event types */
const KITTY_NAMES = { 9: 'tab', 13: 'return', 27: 'escape', 32: 'space', 127: 'backspace' };
const KITTY_EVENTS = ['press', 'repeat', 'release'];

const MOUSE_BUTTONS = ['left', 'middle', 'right', 'none'];
const WHEEL = ['wheelup', 'wheeldown', 'wheelleft', 'wheelright'];

/* key of one mouse report, b the button code of the X protocol */
function mouseKey(sequence, b, x, y, release) {
  const wheel = b & 64,
    motion = b & 32;
  const button = wheel ? WHEEL[b & 3] : MOUSE_BUTTONS[b & 3];
  const action = wheel ? 'wheel' : release || button == 'none' ? (motion ? 'move' : 'release') : motion ? 'drag' : 'press';

  return { s: sequence, key: { sequence, name: 'mouse', ctrl: !!(b & 16), meta: !!(b & 8), shift: !!(b & 4), mouse: { button, action, x, y } } };
}

/* the SGR (ESC [ < b ; x ; y M/m) or X10 (ESC [ M bxy) report at i, null when cut off */
function mouseAt(str, i) {
  if(str[i + 2] == 'M') {
    if(i + 6 > str.length) return null;

    const [b, x, y] = [3, 4, 5].map(k => str.charCodeAt(i + k) - 32);
    return mouseKey(str.slice(i, i + 6), b, x, y, false);
  }

  const m = /^\x1b\[<(\d+);(\d+);(\d+)([Mm])/.exec(str.slice(i, i + 32));

  if(!m) return /^\x1b\[<[\d;]*$/.test(str.slice(i)) ? null : { s: str.slice(i, i + 3), key: { sequence: str.slice(i, i + 3), name: undefined, ctrl: false, meta: false, shift: false } };

  return mouseKey(m[0], +m[1], +m[2], +m[3], m[4] == 'm');
}

/* decodeKeys(str, final): the keys in str, and rest, an unfinished ESC sequence for the next chunk
 *
 *   decodeKeys('a\x1b[1;5D').keys.map(k => k.key.name)   // ['a', 'left'], the second with ctrl
 *
 * final: an ESC at the end is the escape key, not the start of a sequence.
 *
 * besides the legacy sequences: kitty keyboard keys (ESC [ code ; mods u, with an
 * event of press, repeat or release when the terminal reports it), 'focusin' and
 * 'focusout' (focusEventsEnable), and 'paste' with the text in key.paste
 * (bracketedPasteEnable).
 *
 * legacy and kitty sequences yield the exact same {name, ctrl, meta, shift} flags.
 * the matrix: Esc: \x1b <=> \x1b[27u, Shift-Tab: \x1b[Z <=> \x1b[9;2u,
 *   Alt-Tab: \x1b\t <=> \x1b[9;3u, Alt-1..4: \x1b1..\x1b4 <=> \x1b[49;3u..\x1b[52;3u,
 *   Alt-0: \x1b0 <=> \x1b[48;3u, Alt-: \x1b\ <=> \x1b[92;3u, Alt-letter: \x1ba <=> \x1b[97;3u,
 *   Ctrl-letter: \x01 <=> \x1b[97;5u, Enter: \r <=> \x1b[13u, Backspace: \x7f <=> \x1b[127u,
 *   F2: \x1bOQ and \x1b[12~ <=> \x1b[50;1u, PageUp/Down: \x1b[5~ \x1b[6~,
 *   Esc Esc: \x1b\x1b => name "escape", meta true. */
export function decodeKeys(str, final = false) {
  const keys = [];
  let i = 0;

  while(i < str.length) {
    const chCode = str.codePointAt(i);

    if(chCode != 0x1b) {
      /* not ESC: single character or surrogate pair */
      if(chCode >= 0xd800 && chCode <= 0xdfff) {
        /* surrogate pair: need 2 UTF-16 units */
        if(i + 1 >= str.length) {
          if(!final) return { keys, rest: str.slice(i) };
          chCode = 0xfffd;
        } else {
          const ch2Code = str.codePointAt(i + 1);
          if(ch2Code >= 0xdc00 && ch2Code <= 0xdfff) {
            i += 2;
            chCode = 0x10000 + ((chCode - 0xd800) << 10) + (ch2Code - 0xdc00);
          } else {
            /* orphan surrogate, treat as replacement char */
            i++;
            chCode = 0xfffd;
          }
        }
      } else {
        i += chCode >= 0x10000 ? 2 : 1;
      }

      keys.push({ s: String.fromCodePoint(chCode), key: charKey(String.fromCodePoint(chCode)) });
      continue;
    }

    /* chCode == 0x1b (ESC) */
    if(i + 1 >= str.length) {
      if(!final) return { keys, rest: str.slice(i) };
      keys.push({ s: '\x1b', key: charKey('\x1b') });
      break;
    }

    const nextCode = str.codePointAt(i + 1);

    if(nextCode == 0x5b && str.startsWith('\x1b[200~', i)) {
      /* bracketed paste: ESC [ 200~ text ESC [ 201~ */
      const end = str.indexOf('\x1b[201~', i + 6);

      if(end < 0) {
        if(!final) return { keys, rest: str.slice(i) };
        break;
      }

      const sequence = str.slice(i, end + 6);
      keys.push({ s: sequence, key: { sequence, name: 'paste', paste: str.slice(i + 6, end), ctrl: false, meta: false, shift: false } });
      i = end + 6;
      continue;
    }

    if(nextCode == 0x5b && (str.codePointAt(i + 2) < 0x3c || str.codePointAt(i + 2) > 0x4d)) {
      /* mouse: ESC [ < ... M or ESC [ M 
         check for ESC [ M (X10 mouse, 3 bytes) or ESC [ < ... M (SGR mouse) */
      if(nextCode == 0x5b && str.codePointAt(i + 2) == 0x4d) {
        /* SGR mouse: ESC [ <b;x;yM/m 
           parse parameter bytes: 0x30-0x3f (0-?), 0x20-0x2f (space-/) */
        let j = i + 3;
        let p1 = -1,
          p2 = -1,
          p3 = -1,
          finalByte = 0;

        /* parse first parameter (button code) - bytes 0-? */
        while(j < str.length) {
          const c = str.codePointAt(j);
          if(c >= 0x30 && c <= 0x3f) {
            if(p1 < 0) p1 = c - 0x30;
            j++;
          } else if(c >= 0x20 && c <= 0x2f) {
            j++;
          } else break;
        }

        /* parse second parameter - bytes space to / */
        while(j < str.length) {
          const c = str.codePointAt(j);
          if(c >= 0x20 && c <= 0x2f) {
            j++;
          } else break;
        }

        /* parse third parameter */
        if(j < str.length) {
          const c = str.codePointAt(j);
          if(c >= 0x30 && c <= 0x3f) {
            p2 = c - 0x30;
            j++;
          }
        }

        /* final byte M or m */
        if(j < str.length) {
          finalByte = str.codePointAt(j);
          if(finalByte === 0x4d || finalByte === 0x6d) {
            /* valid SGR mouse */
            if(p1 >= 0 && p2 >= 0) {
              const x = p2;
              const y = j + 1 < str.length ? str.codePointAt(j + 1) - 0x30 : 0;
              keys.push(mouseKey(str.slice(i, j + 2), p1, x, y, false));
              i = j + 2;
              continue;
            }
          }
        }
        /* fall through to error handling */
      }

      /* ESC [ < ... or other mouse formats - try to parse */
      let j = i + 3;
      let p1 = -1,
        p2 = -1,
        p3 = -1;

      /* parse parameter bytes using charCodeAt range checks */
      while(j < str.length) {
        const c = str.codePointAt(j);
        if(c >= 0x30 && c <= 0x3f) {
          /* parameter byte: 0-? */
          if(p1 < 0) p1 = c - 0x30;
          else if(p2 < 0) p2 = c - 0x30;
          else if(p3 < 0) p3 = c - 0x30;
          j++;
        } else if(c >= 0x20 && c <= 0x2f) {
          /* intermediate byte: space-/ */
          j++;
        } else break;
      }

      /* check if we have the SGR mouse pattern <b;x;yM or <b;x;ym */
      if(p1 >= 0 && p2 >= 0 && p3 >= 0 && j < str.length) {
        const final = str.codePointAt(j);
        if(final === 0x4d || final === 0x6d) {
          /* SGR mouse: button=p1-32, x=p2, y=p3 */
          keys.push(mouseKey(str.slice(i, j + 1), p1 - 32, p2, p3, false));
          i = j + 1;
          continue;
        }
      }

      /* If not a valid mouse sequence, treat as unknown */
      if(!final) return { keys, rest: str.slice(i) };
      break;
    }

    if(nextCode == 0x5b || nextCode == 0x4f) {
      /* CSI or OCSI sequence: ESC [ or ESC O */
      /* parameter bytes 0-?, then intermediate bytes space to /, then final byte */
      let j = i + 2;

      /* parse parameter bytes: 0x30..0x3f ('0'..'?') */
      while(j < str.length) {
        const c = str.codePointAt(j);
        if(c >= 0x30 && c <= 0x3f) {
          j++;
        } else break;
      }

      /* parse intermediate bytes: 0x20..0x2f (' '..'/') */
      while(j < str.length) {
        const c = str.codePointAt(j);
        if(c >= 0x20 && c <= 0x2f) {
          j++;
        } else break;
      }

      if(j >= str.length) {
        if(!final) return { keys, rest: str.slice(i) };
        break;
      }

      const finalCode = str.codePointAt(j);
      const body = str.slice(i + 2, j);
      const kitty = nextCode == 0x5b && finalCode == 0x75 && /^\d+[\d:;]*$/.test(body);

      if(kitty) {
        /* kitty keyboard: ESC [ code ; mods u */
        /* parse the body: code;mods */
        let codeStr = body,
          modsStr = '';
        const semicolon = body.indexOf(';');
        if(semicolon >= 0) {
          codeStr = body.slice(0, semicolon);
          modsStr = body.slice(semicolon + 1);
        }

        /* parse code: could have colons for modifier groups */
        let cpStr = codeStr;
        const colon = codeStr.indexOf(':');
        if(colon >= 0) {
          cpStr = codeStr.slice(0, colon);
        }
        const cp = cpStr ? parseInt(cpStr, 10) : 0;

        /* parse mods: : separates groups, each digit is a modifier */
        let mods = 1; /* default mod = 1 */
        if(modsStr) {
          /* mods can be like "3:5" or "2" or "10" */
          const modParts = modsStr.split(':');
          mods = 1;
          for(const mp of modParts) {
            const m = mp ? parseInt(mp, 10) : 0;
            mods |= m; /* OR the modifiers together */
          }
        }

        const mod = mods - 1;
        const name = KITTY_NAMES[cp] ?? String.fromCodePoint(cp);

        keys.push({
          s: str.slice(i, j + 1),
          key: {
            sequence: str.slice(i, j + 1),
            name,
            ctrl: !!(mod & 4),
            meta: !!(mod & 8) /* note: was 10, but mod bits: 1=alt, 2=shift, 4=ctrl, so meta=8 for alt */,
            shift: !!(mod & 2),
            ...(modsStr ? { event: undefined } : {}) /* event is optional; if present decode later */,
          },
        });
        i = j + 1;
        continue;
      }

      /* check for focusin/focusout: ESC [ I or ESC [ O */
      if(nextCode == 0x5b && !body && (finalCode == 0x49 || finalCode == 0x4f)) {
        const sequence = str.slice(i, j + 1);

        keys.push({ s: sequence, key: { sequence, name: finalCode == 0x49 ? 'focusin' : 'focusout', ctrl: false, meta: false, shift: false } });
        i = j + 1;
        continue;
      }

      /* parse SGR/legacy CSI: final byte is ~, letter, or M */
      let name;
      if(finalCode == 0x7e) {
        /* tilde-terminated: CSI ... ~ */
        /* parse params: split on ;, filter out '?' */
        const paramsStr = body;
        const params = paramsStr
          .split(';')
          .filter(p => p && p != '?')
          .map(Number);
        const mod = (params[1] ?? 1) - 1;
        name = CSI_TILDE[params[0]];
      } else {
        /* letter-terminated: CSI [ ... letter */
        name = CSI_LETTER[str[j]] || CSI_LETTER[str.slice(i + 2, j).charAt(0)];
      }

      /* SGR mouse: ESC [ M b x y (urxvt 1015) or ESC [ < b;x;y M/m */
      if(finalCode == 0x4d || finalCode == 0x6d) {
        /* Check if this is SGR mouse <b;x;yM/m */
        /* params already parsed above, check if we have 3 params */
        const params = body
          .split(';')
          .filter(p => p && p != '?')
          .map(Number);
        if(params.length >= 3) {
          keys.push(mouseKey(str.slice(i, j + 1), params[0] - 32, params[1], params[2], finalCode == 0x6d));
          i = j + 1;
          continue;
        }
      }

      const mod = (params[1] ?? 1) - 1;
      const sequence = str.slice(i, j + 1);

      keys.push({ s: sequence, key: { sequence, name, ctrl: !!(mod & 4), meta: !!(mod & 8), shift: !!(mod & 2) || str[j] == 'Z'.charCodeAt(0) } });
      i = j + 1;
      continue;
    }

    /* ESC followed by a non-CSI/non-kitty sequence: ESC + one character */
    /* This handles: ESC c, ESC 7, etc. - treated as escape with meta */
    const innerCode = str.codePointAt(i + 1);
    const innerCh = String.fromCodePoint(innerCode);
    const key = charKey(innerCh);

    key.meta = true;
    key.sequence = '\x1b' + innerCh;
    keys.push({ s: key.sequence, key });
    i += 1 + (innerCode >= 0x10000 ? 2 : 1);
  }

  return { keys, rest: '' };
}

/* per fd: keys read but not returned yet, and the decoder of a split character */
const keyQueues = new Map();

const KEY_TYPES = {
  up: 'up',
  down: 'down',
  left: 'left',
  right: 'right',
  pageup: 'pageup',
  pagedown: 'pagedown',
  space: 'space',
  return: 'enter',
  enter: 'enter',
  backspace: 'backspace',
  escape: 'escape',
};

/* readKey(fd): one keypress from fd in raw mode, waits until there is one
 *
 *   readKey(0)   // { type: 'up' }, { type: 'char', char: 'ä' }, { type: 'ctrlc' }
 *
 * type is up down left right pageup pagedown space enter backspace ctrlc
 * escape (also any other sequence), char, mouse, or eof. a mouse key has
 * button, action, x, y (1-based) and ctrl, meta, shift, see mouseEnable(). */
export function readKey(fd) {
  let q = keyQueues.get(fd);

  if(!q) keyQueues.set(fd, (q = { keys: [], decoder: new TextDecoder(), buf: new ArrayBuffer(64) }));

  while(!q.keys.length) {
    const n = ttyRead(fd, q.buf, 0, q.buf.byteLength);

    if(n == -EAGAIN) {
      sleep(10);
      continue;
    }

    if(n <= 0) return { type: 'eof' };

    q.keys.push(...decodeKeys(q.decoder.decode(new Uint8Array(q.buf, 0, n), { stream: true }), true).keys);
  }

  const { s, key } = q.keys.shift();

  if(key.ctrl && key.name == 'c') return { type: 'ctrlc' };
  if(key.name == 'mouse') return { type: 'mouse', ...key.mouse, ctrl: key.ctrl, meta: key.meta, shift: key.shift };
  if(s.length > 1 && s[0] == '\x1b') return { type: KEY_TYPES[key.name] ?? 'escape' };
  return KEY_TYPES[key.name] ? { type: KEY_TYPES[key.name] } : { type: 'char', char: s };
}

/* ---- capabilities: ask the terminal what it can do ---- */

/* DECRQM for each mode asked about, then the kitty keyboard flags, the terminal's name and version, and
 * the primary device attributes last: they are answered in order, so that answer ends the wait */
const PROBE_MODES = [2026, 2027, 2004, 1004, 1006];
const PROBE = PROBE_MODES.map(m => `${CSI}?${m}$p`).join('') + CSI + '?u' + CSI + '>q' + CSI + 'c';

/* parseCapabilityReplies(str): the answers to queryCapabilities()' questions found in str
 *
 *   parseCapabilityReplies('\x1b[?2026;2$y\x1b[?0u\x1b[?62;4c')
 *     // { modes: { 2026: 2 }, kitty: 0, version: undefined, da1: true, rest: '' }
 *
 * modes: mode number -> 0 unknown, 1 set, 2 reset, 3 always set, 4 always reset (DECRQM).
 * kitty: the keyboard flags now in force, undefined when the protocol did not answer.
 * version: name and version text of the terminal (XTVERSION). da1: the final answer arrived.
 * rest: str without the answers, what the user typed meanwhile. */
export function parseCapabilityReplies(str) {
  const modes = {};
  let kitty,
    version,
    da1 = false;

  const rest = String(str).replace(/\x1b\[\?(\d+);(\d)\$y|\x1b\[\?(\d+)u|\x1bP>\|([^\x1b]*)\x1b\\|\x1b\[\?[\d;]*c/g, (m, mode, state, flags, name) => {
    if(mode !== undefined) modes[mode] = +state;
    else if(flags !== undefined) kitty = +flags;
    else if(name !== undefined) version = name;
    else da1 = true;

    return '';
  });

  return { modes, kitty, version, da1, rest };
}

/* queryCapabilities(f, options): which of the features above the terminal behind f has
 *
 *   const caps = queryCapabilities(std.out)
 *   // { syncOutput: true, bracketedPaste: true, kittyKeyboard: true, truecolor: true, terminal: 'kitty(0.35)', ... }
 *   if(caps.syncOutput) screen = new Screen(std.out, { sync: true })
 *
 * it sends the questions, then reads the answers from the input until the last one came or timeout ms
 * passed. a terminal that answers nothing, or no terminal, gives false everywhere, so every feature stays
 * off. call it once at start-up, before anything else reads the input: what the user typed meanwhile is
 * returned as typeahead, and the answers are not seen by the line editor.
 *
 * options: input (fd of the input, 0), timeout (ms, 300), raw (put the input in raw mode and back, true:
 * pass false when it is in raw mode already), env (where COLORTERM is looked up, process.env).
 *
 * truecolor is read from COLORTERM, no terminal answers that one. */
export function queryCapabilities(f, { input = 0, timeout = 300, raw = true, env = globalThis.process?.env ?? {} } = {}) {
  const fd = typeof f == 'number' ? f : (f.fileno?.() ?? f.fd ?? 1);
  const caps = {
    syncOutput: false,
    graphemeClusters: false,
    bracketedPaste: false,
    focusEvents: false,
    sgrMouse: false,
    kittyKeyboard: false,
    kittyFlags: 0,
    truecolor: /truecolor|24bit/i.test(env.COLORTERM ?? '') || /-direct$/.test(env.TERM ?? ''),
    terminal: undefined,
    answered: false,
    typeahead: '',
  };

  if(!ttyGetWinSize(fd) || !ttyGetWinSize(input)) return caps;

  const decoder = new TextDecoder();
  const buf = new ArrayBuffer(256);
  const end = Date.now() + timeout;
  let text = '',
    got;

  if(raw) ttySetRaw(input);

  try {
    ttyWrite(f, PROBE);

    do {
      const n = ttyRead(input, buf, 0, buf.byteLength);

      if(n == -EAGAIN) sleep(5);
      else if(n <= 0) break;
      else text += decoder.decode(new Uint8Array(buf, 0, n), { stream: true });

      got = parseCapabilityReplies(text);
    } while(!got?.da1 && Date.now() < end);
  } finally {
    if(raw) ttySetRaw(input, true);
  }

  got = parseCapabilityReplies(text);

  const on = m => (got.modes[m] ?? 0) != 0;

  return Object.assign(caps, {
    syncOutput: on(2026),
    graphemeClusters: on(2027),
    bracketedPaste: on(2004),
    focusEvents: on(1004),
    sgrMouse: on(1006),
    kittyKeyboard: got.kitty !== undefined,
    kittyFlags: got.kitty ?? 0,
    terminal: got.version,
    answered: got.da1,
    typeahead: got.rest,
  });
}

/* enableCapabilities(f, caps, options): switches on what caps says the terminal has, returns a function
 * that switches it off again (call it at exit)
 *
 *   const restore = enableCapabilities(std.out, queryCapabilities(std.out), { kitty: true })
 *
 * options, each true to ask for it: paste (bracketed paste, on by default), graphemes (grapheme
 * clusters, on by default), focus (focus events), kitty (kitty keyboard, flags 1). the last two are off
 * by default, since the code reading the keys has to understand what they change.
 * synchronized output is not switched on here, it is per frame: new Screen(f, { sync: caps.syncOutput }) */
export function enableCapabilities(f, caps, { paste = true, graphemes = true, focus = false, kitty = false } = {}) {
  const undo = [];

  if(paste && caps.bracketedPaste) {
    bracketedPasteEnable(f);
    undo.push(() => bracketedPasteDisable(f));
  }

  if(graphemes && caps.graphemeClusters) {
    graphemeClustersEnable(f);
    undo.push(() => graphemeClustersDisable(f));
  }

  if(focus && caps.focusEvents) {
    focusEventsEnable(f);
    undo.push(() => focusEventsDisable(f));
  }

  if(kitty && caps.kittyKeyboard) {
    kittyKeyboardEnable(f);
    undo.push(() => kittyKeyboardDisable(f));
  }

  return () => {
    for(const f of undo.reverse()) f();
    undo.length = 0;
  };
}

/* box drawing sets for Screen.box(), boxLines() and table()
 *
 *   tl tr bl br  corners          t b  tees down and up
 *   h v          lines            l r  tees right and left, x the cross */
export const BOX_SINGLE = { tl: '┌', tr: '┐', bl: '└', br: '┘', h: '─', v: '│', t: '┬', b: '┴', l: '├', r: '┤', x: '┼' };
export const BOX_DOUBLE = { tl: '╔', tr: '╗', bl: '╚', br: '╝', h: '═', v: '║', t: '╦', b: '╩', l: '╠', r: '╣', x: '╬' };
export const BOX_ROUNDED = { ...BOX_SINGLE, tl: '╭', tr: '╮', bl: '╰', br: '╯' };
export const BOX_HEAVY = { tl: '┏', tr: '┓', bl: '┗', br: '┛', h: '━', v: '┃', t: '┳', b: '┻', l: '┣', r: '┫', x: '╋' };

/* centeredRect(termCols, termRows, width, height): a centered panel, clamped to the screen
 *
 *   centeredRect(80, 24, 40, 10)   // { row: 8, col: 21, width: 40, height: 10 }
 *
 * 1-based, for Screen.moveTo() and box(), like curses newwin((rows-h)/2, (cols-w)/2). */
export function centeredRect(termCols, termRows, width, height) {
  width = Math.max(1, Math.min(width, termCols));
  height = Math.max(1, Math.min(height, termRows));

  return {
    row: Math.max(1, Math.floor((termRows - height) / 2) + 1),
    col: Math.max(1, Math.floor((termCols - width) / 2) + 1),
    width,
    height,
  };
}

/* Screen(f): a string backbuffer, flush() sends a whole frame in one ttyWrite
 *
 *   new Screen(std.out).clear().moveTo(2, 3).write('x').flush()
 *
 * one write per frame avoids the flicker of drawing cell by cell. later
 * draws in a frame cover earlier ones, draw order is z-order. */
export class Screen {
  #f;
  #buf = '';
  #sync;

  /* sync: flush() wraps each frame in synchronized output, see queryCapabilities() */
  constructor(f, { sync = false } = {}) {
    this.#f = f;
    this.#sync = sync;
  }

  /* raw text, nothing escaped */
  write(s) {
    this.#buf += s;
    return this;
  }

  moveTo(row, column) {
    this.#buf += `${CSI}${row};${column}H`;
    return this;
  }

  /* n: 0 cursor to end, 1 start to cursor, 2 whole screen */
  clear(n = 2) {
    this.#buf += `${CSI}${n}J`;
    return this;
  }

  clearLine(n = 2) {
    this.#buf += `${CSI}${n}K`;
    return this;
  }

  fg(r, g, b) {
    this.#buf += `${CSI}38;2;${r};${g};${b}m`;
    return this;
  }

  bg(r, g, b) {
    this.#buf += `${CSI}48;2;${r};${g};${b}m`;
    return this;
  }

  sgr(...codes) {
    this.#buf += `${CSI}${codes.join(';')}m`;
    return this;
  }

  /* colors, reverse video and the rest back to the terminal defaults */
  resetAttrs() {
    this.#buf += `${CSI}0m`;
    return this;
  }

  /* fills a width x height rectangle with ch, covering what was drawn there */
  fillRect(row, col, width, height, ch = ' ') {
    const line = ch.repeat(Math.max(0, width));
    for(let r = 0; r < height; r++) this.moveTo(row + r, col).write(line);
    return this;
  }

  /* box(row, col, width, height, { title, chars }): a bordered, filled panel
   *
   * title is centered into the top border. content goes inside, rows
   * row+1 to row+height-2, columns col+1 to col+width-2. */
  box(row, col, width, height, { title = null, chars = BOX_SINGLE } = {}) {
    if(width < 2 || height < 2) return this;

    const { tl, tr, bl, br, h, v } = chars;

    this.fillRect(row + 1, col + 1, width - 2, height - 2);

    let top = h.repeat(width - 2);
    if(title) {
      const label = ` ${title} `.slice(0, Math.max(0, width - 2));
      const start = Math.max(0, Math.floor((width - 2 - label.length) / 2));
      top = h.repeat(start) + label + h.repeat(Math.max(0, width - 2 - start - label.length));
    }

    this.moveTo(row, col).write(tl + top + tr);
    for(let r = 1; r < height - 1; r++)
      this.moveTo(row + r, col)
        .write(v)
        .moveTo(row + r, col + width - 1)
        .write(v);
    this.moveTo(row + height - 1, col).write(bl + h.repeat(width - 2) + br);

    return this;
  }

  /* switches to the alternate screen (on) or back to the normal one */
  alternate(on = true) {
    this.#buf += CSI + '?1049' + (on ? 'h' : 'l');
    return this;
  }

  /* lines (strings, e.g. from boxLines() or table()) one below the other from row, col */
  lines(row, col, lines) {
    lines.forEach((l, k) => this.moveTo(row + k, col).write(l));
    return this;
  }

  /* sends the buffer to the file in one ttyWrite and empties it */
  flush() {
    if(this.#sync) syncOutput(this.#f, this.#buf);
    else ttyWrite(this.#f, this.#buf);

    this.#buf = '';
    return this;
  }
}

/* ---- drawing without flicker: only what changed is written ---- */

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

/* Region(top, height, width): rows of the screen that remember what they show
 *
 *   const status = new Region(23, 2, 80);
 *   ttyWrite(f, status.update(['model x', 'tokens 12']));   // both rows
 *   ttyWrite(f, status.update(['model x', 'tokens 13']));   // the second row only
 *   ttyWrite(f, status.update(['model x', 'tokens 13']));   // '' - nothing changed
 *
 * update() returns the escape sequences that rewrite the rows whose text
 * changed, each erased to its end instead of clearing the screen below,
 * so other parts of the screen are never touched and nothing flickers.
 * lines must fit the width. invalidate() after the screen was cleared or
 * the region moved by other means. */
export class Region {
  #shown = [];

  constructor(top, height, width = Infinity) {
    this.top = top;
    this.height = height;
    this.width = width;
  }

  /* a new place or size, everything is drawn again on the next update() */
  moveTo(top, height = this.height, width = this.width) {
    if(top !== this.top || height !== this.height || width !== this.width) {
      Object.assign(this, { top, height, width });
      this.invalidate();
    }

    return this;
  }

  invalidate() {
    this.#shown = [];
    return this;
  }

  update(lines) {
    let s = '';

    for(let r = 0; r < this.height; r++) {
      const l = lines[r] ?? '';
      if(this.#shown[r] === l) continue;

      /* a full row leaves the cursor past its end, an erase there would take the last cell */
      s += `${CSI}${this.top + r};1H${CSI}0m${l}${CSI}0m${stringWidth(l) < this.width ? CSI + 'K' : ''}`;
      this.#shown[r] = l;
    }

    return s;
  }
}

/* ---- widgets: string builders, each returns a string or an array of lines ---- */

/* one escape sequence at a position, for walking strings cell by cell */
const ANSI_STICKY = new RegExp(ANSI_RE.source, 'y');
const RESET = CSI + '0m';
const SGR_RE = /\x1b\[[\d;]*m/g;

/* [text, width] for each escape sequence (width 0) and each character of str */
function* cells(str) {
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

const NAMED = { black: 30, red: 31, green: 32, yellow: 33, blue: 34, magenta: 35, cyan: 36, white: 37, gray: 90, grey: 90 };

/* [r, g, b] of '#rrggbb', '#rgb' or [r, g, b], null for anything else */
export function parseColor(c) {
  if(Array.isArray(c)) return c;

  const m = typeof c == 'string' && /^#([\da-f]{3}|[\da-f]{6})$/i.exec(c);
  if(!m) return null;

  const h = m[1].length == 3 ? [...m[1]].map(d => d + d).join('') : m[1];
  return [0, 2, 4].map(k => parseInt(h.slice(k, k + 2), 16));
}

/* SGR parameters of a color: '#ff8800' or [255, 136, 0] truecolor, 0-255 palette, a name */
function colorCode(c, bg) {
  const rgb = parseColor(c);

  if(rgb) return `${bg ? 48 : 38};2;${rgb.join(';')}`;
  if(typeof c == 'number') return `${bg ? 48 : 38};5;${c}`;
  if(c in NAMED) return String(NAMED[c] + (bg ? 10 : 0));

  throw new TypeError(`unknown color '${c}'`);
}

/* style(text, attrs): text with SGR attributes, reset after it
 *
 *   style('ok', { fg: '#00ff00', bold: true })   // '\x1b[1;38;2;0;255;0mok\x1b[0m'
 *
 * attrs: fg, bg (see colorCode), bold, dim, italic, underline, inverse,
 * strike. '' stays '', null attrs leave text as it is. */
export function style(text, attrs) {
  const { fg, bg, bold, dim, italic, underline, inverse, strike } = attrs ?? {};
  const codes = [
    bold && 1,
    dim && 2,
    italic && 3,
    underline && 4,
    inverse && 7,
    strike && 9,
    typeof fg == 'string' ? colorCode(fg, false) : undefined,
    typeof bg == 'string' ? colorCode(bg, true) : false,
  ].filter(Boolean);

  if(text === '' || !codes.length) return String(text);
  return `${CSI}${codes.join(';')}m${text}${RESET}`;
}

/* the color at t (0-1) on a gradient through stops */
function colorAt(stops, t) {
  if(stops.length == 1) return stops[0];

  const x = Math.max(0, Math.min(1, t)) * (stops.length - 1);
  const k = Math.min(Math.floor(x), stops.length - 2);
  const f = x - k;

  return stops[k].map((v, n) => Math.round(v + (stops[k + 1][n] - v) * f));
}

/* gradient(text, stops, bg): each column colored along stops, left to right
 *
 *   gradient(bigText('qjs'), ['#6ec3f4', '#c084fc', '#f9a8d4'])
 *
 * text is a string or an array of lines. lines share the columns, so a
 * banner gets one gradient across. spaces stay uncolored. */
export function gradient(text, stops, bg = false) {
  const lines = Array.isArray(text) ? text : String(text).split('\n');
  const width = Math.max(1, ...lines.map(stringWidth));
  const rgb = stops.map(parseColor);
  const out = lines.map(line => {
    let s = '',
      col = 0;

    for(const [t, w] of cells(line)) {
      if(w < 0 || t == ' ') s += t;
      else s += `${CSI}${bg ? 48 : 38};2;${colorAt(rgb, width > 1 ? col / (width - 1) : 0).join(';')}m${t}`;

      col += Math.max(0, w);
    }

    return s + RESET;
  });

  return Array.isArray(text) ? out : out.join('\n');
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

/* rule(width, opts): a horizontal line, with a label after the first two cells
 *
 *   rule(20, { label: 'Thinking' })   // '── Thinking ────────'
 *
 * opts: char ('─'), label, style (attrs for style()). */
export function rule(width, { char = '─', label = '', style: attrs } = {}) {
  const head = label ? `${char.repeat(2)} ${label} ` : '';
  const line = truncate(head, width, '') + char.repeat(Math.max(0, width - stringWidth(head)));

  return attrs ? style(line, attrs) : line;
}

/* boxLines(content, opts): content inside a border, as lines
 *
 *   boxLines('hi', { title: 'note', chars: BOX_ROUNDED })
 *   // ['╭─ note ─╮', '│ hi     │', '╰────────╯']
 *
 * content is a string or lines, longer ones are wrapped. opts: width
 * (outer, else from content), padding (cells left and right, 1), title,
 * titleAlign (left, center, right), footer, align, chars, border
 * (attrs for the border). */
export function boxLines(content, { width, padding = 1, title, titleAlign = 'left', footer, align = 'left', chars = BOX_SINGLE, border } = {}) {
  const raw = Array.isArray(content) ? content : String(content).split('\n');
  const label = s => (s ? ` ${s} ` : '');

  width ??= Math.max(...raw.map(stringWidth), stringWidth(label(title)) + 2, stringWidth(label(footer)) + 2) + 2 * padding + 2;

  const inner = Math.max(0, width - 2);
  const text = Math.max(1, inner - 2 * padding);
  const paint = s => (border ? style(s, border) : s);
  const edge = (l, r, s) => {
    s = truncate(label(s), Math.max(0, inner - 2), '');
    const gap = inner - stringWidth(s);
    const left = !s ? gap : titleAlign == 'right' ? gap - 1 : titleAlign == 'center' ? Math.floor(gap / 2) : 1;

    return paint(l + chars.h.repeat(left)) + s + paint(chars.h.repeat(gap - left) + r);
  };
  const body = raw.flatMap(l => wrap(l, text)).map(l => paint(chars.v) + ' '.repeat(padding) + pad(l, text, align) + ' '.repeat(inner - text - padding) + paint(chars.v));

  return [edge(chars.tl, chars.tr, title), ...body, edge(chars.bl, chars.br, footer)];
}

/* inputBox(value, opts): a one-line input field between two rules
 *
 *   inputBox('', { width: 40, placeholder: 'Type your message' })
 *
 * opts: width, prompt ('> '), placeholder, cursor (shows a block cursor
 * after the text), rule (attrs of the rules), promptStyle,
 * placeholderStyle ({ dim: true }). the text keeps its end visible. */
export function inputBox(value, { width = 80, prompt = '> ', placeholder = '', cursor = false, rule: ruleAttrs, promptStyle, placeholderStyle = { dim: true } } = {}) {
  const room = Math.max(1, width - stringWidth(prompt) - 2);
  let text = String(value);

  while(stringWidth(text) > room - (cursor ? 1 : 0)) text = text.slice(1);

  const shown = text ? text : placeholder ? style(truncate(placeholder, room - (cursor ? 1 : 0)), placeholderStyle) : '';
  const caret = cursor ? style(' ', { inverse: true }) : '';
  const line = ' ' + style(prompt, promptStyle) + (text ? shown + caret : caret + shown);

  return [rule(width, { style: ruleAttrs }), line, rule(width, { style: ruleAttrs })];
}

/* statusBar(parts, width): left, center and right text in one line
 *
 *   statusBar(['~/src', 'no sandbox', 'model (100%)'], 50)
 *
 * the center stays centered while there is room, else it moves between
 * the others. left, then center, are truncated when all do not fit. */
export function statusBar([left = '', center = '', right = ''], width) {
  right = truncate(right, width);

  const rw = stringWidth(right);
  left = truncate(left, Math.max(0, width - rw - (rw ? 1 : 0)));

  const lw = stringWidth(left);
  const free = width - lw - rw;
  center = truncate(center, Math.max(0, free - 2));

  const cw = stringWidth(center);
  if(!cw) return left + ' '.repeat(Math.max(0, free)) + right;

  const start = Math.min(Math.max(lw + 1, Math.floor((width - cw) / 2)), width - rw - cw - 1);
  return left + ' '.repeat(start - lw) + center + ' '.repeat(Math.max(0, width - rw - start - cw)) + right;
}

/* keyValue(pairs, opts): aligned key / value lines
 *
 *   keyValue({ model: 'qwen', tokens: '1.2k' })   // ['model   qwen', 'tokens  1.2k']
 *
 * pairs is an object or [key, value] entries, null values are left out.
 * opts: width (wraps values), keyStyle, gap (2). multi-line values are
 * indented under the first line. */
export function keyValue(pairs, { width, keyStyle, gap = 2 } = {}) {
  const rows = (Array.isArray(pairs) ? pairs : Object.entries(pairs)).filter(([, v]) => v != null && v !== '');
  const kw = Math.max(0, ...rows.map(([k]) => stringWidth(k)));
  const indent = ' '.repeat(kw + gap);

  return rows.flatMap(([k, v]) => {
    const lines = String(v)
      .split('\n')
      .flatMap(l => (width ? wrap(l, Math.max(1, width - kw - gap)) : [l]));

    return lines.map((l, n) => (n ? indent : style(pad(k, kw), keyStyle) + ' '.repeat(gap)) + l);
  });
}

const NUMERIC = /^[-+]?[\d.,]+\s*[%kKMGx]?$|^[-–]$/;

/* table(rows, opts): a bordered table, as lines
 *
 *   table([['a', 1], { section: 'more' }, ['b', 22]], { header: ['name', 'n'] })
 *
 * a row is an array of cells or { section: title }, a full-width
 * centered row. opts:
 *   header        header cells, separated by a rule
 *   align         per column: left, right, center (numbers go right)
 *   columnStyle   per column attrs for style(), applied to the whole
 *                 padded cell, so a bg fills it (highlighted columns)
 *   headerStyle, sectionStyle, border   attrs
 *   chars         box set with tees (BOX_SINGLE)
 *   maxWidth      total width, the widest columns are truncated */
export function table(rows, { header, align = [], columnStyle = [], headerStyle = { bold: true }, sectionStyle = { bold: true }, border, chars = BOX_SINGLE, maxWidth } = {}) {
  const data = rows.filter(Array.isArray);
  const all = header ? [header, ...data] : data;
  const n = Math.max(0, ...all.map(r => r.length));
  const widths = Array.from({ length: n }, (_, c) => Math.max(1, ...all.map(r => stringWidth(String(r[c] ?? '')))));
  const paint = s => (border ? style(s, border) : s);

  if(maxWidth)
    while(widths.reduce((a, b) => a + b, 0) + 3 * n + 1 > maxWidth) {
      const k = widths.indexOf(Math.max(...widths));
      if(widths[k] <= 3) break;
      widths[k]--;
    }

  const total = widths.reduce((a, b) => a + b, 0) + 3 * n - 1;
  const line = (l, m, r) => paint(l + widths.map(w => chars.h.repeat(w + 2)).join(m) + r);
  const full = (l, r) => paint(l + chars.h.repeat(total) + r);
  const row = (cells, attrsOf) =>
    paint(chars.v) +
    widths
      .map((w, c) => {
        const v = String(cells[c] ?? '');
        const a = align[c] ?? (NUMERIC.test(stripAnsi(v).trim()) ? 'right' : 'left');
        const attrs = attrsOf(c);

        return attrs ? style(' ' + pad(v, w, a) + ' ', attrs) : ' ' + pad(v, w, a) + ' ';
      })
      .join(paint(chars.v)) +
    paint(chars.v);

  /* a rule between two rows needs tees where the columns above and below are split */
  const between = (above, below) => (above && below ? line(chars.l, chars.x, chars.r) : above ? line(chars.l, chars.b, chars.r) : below ? line(chars.l, chars.t, chars.r) : full(chars.l, chars.r));

  const out = [];
  let prev = null;

  for(const r of header ? [{ header }, ...rows] : rows) {
    const split = !r.section;

    if(prev === null) out.push(split ? line(chars.tl, chars.t, chars.tr) : full(chars.tl, chars.tr));
    else if(r.section || prev.section || prev.header) out.push(between(!prev.section, split));

    if(r.header) out.push(row(r.header, () => headerStyle));
    else if(r.section) out.push(paint(chars.v) + style(' ' + pad(r.section, total - 2, 'center') + ' ', sectionStyle) + paint(chars.v));
    else out.push(row(r, c => columnStyle[c]));

    prev = r;
  }

  if(prev) out.push(prev.section ? full(chars.bl, chars.br) : line(chars.bl, chars.b, chars.br));
  return out;
}

const EIGHTHS = ' ▏▎▍▌▋▊▉█';

/* progressBar(fraction, width, opts): a bar with eighth-cell steps
 *
 *   progressBar(0.5, 10)   // '█████     '
 *
 * opts: style (attrs of the filled part), empty (the char of the rest,
 * ' '), emptyStyle. */
export function progressBar(fraction, width, { style: attrs, empty = ' ', emptyStyle } = {}) {
  const eighths = Math.round(Math.max(0, Math.min(1, fraction || 0)) * width * 8);
  const full = Math.floor(eighths / 8);
  const part = eighths % 8 ? EIGHTHS[eighths % 8] : '';
  const rest = Math.max(0, width - full - (part ? 1 : 0));

  return style('█'.repeat(full) + part, attrs) + style(empty.repeat(rest), emptyStyle);
}

const SPARKS = '▁▂▃▄▅▆▇█';

/* sparkline(values, opts): one cell per value, the last width ones
 *
 *   sparkline([1, 5, 3, 8])   // '▁▅▃█'
 *
 * opts: width, min, max (from values). null values are spaces. */
export function sparkline(values, { width, min, max } = {}) {
  if(width) values = values.slice(-width);

  const known = values.filter(v => v != null);
  min ??= Math.min(...known);
  max ??= Math.max(...known);

  return values.map(v => (v == null ? ' ' : SPARKS[max > min ? Math.round(((Math.max(min, Math.min(max, v)) - min) / (max - min)) * 7) : 3])).join('');
}

/* spinner frame sets: SPINNERS.dots[i % SPINNERS.dots.length] */
export const SPINNERS = {
  dots: [...'⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏'],
  line: ['-', '\\', '|', '/'],
  arc: [...'◜◠◝◞◡◟'],
  circle: [...'◐◓◑◒'],
  quadrant: [...'▖▘▝▗'],
  bounce: [...'⠁⠂⠄⠂'],
  pulse: [...'█▓▒░▒▓'],
};

/* block letters, 5 rows each, # is a set cell */
const FONT = {
  A: [' ### ', '#   #', '#####', '#   #', '#   #'],
  B: ['#### ', '#   #', '#### ', '#   #', '#### '],
  C: [' ####', '#    ', '#    ', '#    ', ' ####'],
  D: ['#### ', '#   #', '#   #', '#   #', '#### '],
  E: ['#####', '#    ', '#### ', '#    ', '#####'],
  F: ['#####', '#    ', '#### ', '#    ', '#    '],
  G: [' ####', '#    ', '#  ##', '#   #', ' ####'],
  H: ['#   #', '#   #', '#####', '#   #', '#   #'],
  I: ['###', ' # ', ' # ', ' # ', '###'],
  J: ['  ###', '   # ', '   # ', '#  # ', ' ##  '],
  K: ['#   #', '#  # ', '###  ', '#  # ', '#   #'],
  L: ['#    ', '#    ', '#    ', '#    ', '#####'],
  M: ['#   #', '## ##', '# # #', '#   #', '#   #'],
  N: ['#   #', '##  #', '# # #', '#  ##', '#   #'],
  O: [' ### ', '#   #', '#   #', '#   #', ' ### '],
  P: ['#### ', '#   #', '#### ', '#    ', '#    '],
  Q: [' ### ', '#   #', '# # #', '#  # ', ' ## #'],
  R: ['#### ', '#   #', '#### ', '#  # ', '#   #'],
  S: [' ####', '#    ', ' ### ', '    #', '#### '],
  T: ['#####', '  #  ', '  #  ', '  #  ', '  #  '],
  U: ['#   #', '#   #', '#   #', '#   #', ' ### '],
  V: ['#   #', '#   #', '#   #', ' # # ', '  #  '],
  W: ['#   #', '#   #', '# # #', '## ##', '#   #'],
  X: ['#   #', ' # # ', '  #  ', ' # # ', '#   #'],
  Y: ['#   #', ' # # ', '  #  ', '  #  ', '  #  '],
  Z: ['#####', '   # ', '  #  ', ' #   ', '#####'],
  0: [' ### ', '#  ##', '# # #', '##  #', ' ### '],
  1: [' # ', '## ', ' # ', ' # ', '###'],
  2: [' ### ', '#   #', '  ## ', ' #   ', '#####'],
  3: ['#### ', '    #', ' ### ', '    #', '#### '],
  4: ['#   #', '#   #', '#####', '    #', '    #'],
  5: ['#####', '#    ', '#### ', '    #', '#### '],
  6: [' ### ', '#    ', '#### ', '#   #', ' ### '],
  7: ['#####', '   # ', '  #  ', ' #   ', '#    '],
  8: [' ### ', '#   #', ' ### ', '#   #', ' ### '],
  9: [' ### ', '#   #', ' ####', '    #', ' ### '],
  ' ': ['   ', '   ', '   ', '   ', '   '],
  '-': ['    ', '    ', '####', '    ', '    '],
  _: ['    ', '    ', '    ', '    ', '####'],
  '.': [' ', ' ', ' ', ' ', '#'],
  ':': [' ', '#', ' ', '#', ' '],
  '!': ['#', '#', '#', ' ', '#'],
  '?': ['### ', '   #', ' ## ', '    ', ' #  '],
  '>': ['#  ', ' # ', '  #', ' # ', '#  '],
  '<': ['  #', ' # ', '#  ', ' # ', '  #'],
  '/': ['    #', '   # ', '  #  ', ' #   ', '#    '],
  '+': ['     ', '  #  ', '#####', '  #  ', '     '],
};

/* bigText(text, opts): text in block letters, as lines
 *
 *   bigText('hi', { half: true })   // 3 lines of ▀ ▄ █
 *
 * opts: half (two rows per line with half blocks, 3 lines instead of 5),
 * fill ('█'), shadow (a ░ shadow down and right, not with half). letters
 * are upper-cased, unknown ones become '?'. */
export function bigText(text, { half = false, fill = '█', shadow = false } = {}) {
  const glyphs = [...String(text).toUpperCase()].map(ch => FONT[ch] ?? FONT['?']);
  let rows = [0, 1, 2, 3, 4].map(r => glyphs.map(g => g[r]).join(' '));

  if(half) {
    rows.push(' '.repeat(rows[0].length));
    return [0, 2, 4].map(r =>
      [...rows[r]]
        .map((c, k) => (c == '#' ? (rows[r + 1][k] == '#' ? '█' : '▀') : rows[r + 1][k] == '#' ? '▄' : ' '))
        .join('')
        .trimEnd(),
    );
  }

  if(shadow) {
    const grid = [...rows, ' '.repeat(rows[0].length)].map(r => [...(r + ' ')]);
    for(let r = grid.length - 1; r > 0; r--) for (let k = grid[r].length - 1; k > 0; k--) if(grid[r][k] == ' ' && grid[r - 1][k - 1] == '#') grid[r][k] = '░';
    rows = grid.map(r => r.join(''));
  }

  return rows.map(r => r.replace(/#/g, fill).trimEnd());
}

/* scrollbar(height, total, offset): one cell per row, ┃ for the visible part
 *
 *   scrollbar(4, 8, 4)   // ['│', '│', '┃', '┃']
 *
 * all blank when everything fits. */
export function scrollbar(height, total, offset) {
  if(total <= height) return Array(height).fill(' ');

  const size = Math.max(1, Math.round((height * height) / total));
  const start = Math.min(height - size, Math.round((offset / (total - height)) * (height - size)));

  return Array.from({ length: height }, (_, r) => (r >= start && r < start + size ? '┃' : '│'));
}

/* ScrollView(height): a window of height lines onto a longer list
 *
 *   const view = new ScrollView(10)
 *   view.push(...wrap(text, 60))
 *   view.handleKey(key)          // pageup, wheel, ... from decodeKeys()
 *   screen.lines(2, 1, view.render(60))
 *
 * while follow is on (at the bottom) new lines keep the end in view, like
 * a log. scrolling up turns it off, reaching the bottom turns it on. */
export class ScrollView {
  lines = [];
  offset = 0;
  follow = true;

  constructor(height, lines = []) {
    this.height = Math.max(1, height);
    this.push(...lines);
  }

  get maxOffset() {
    return Math.max(0, this.lines.length - this.height);
  }

  get atBottom() {
    return this.offset >= this.maxOffset;
  }

  push(...lines) {
    this.lines.push(...lines);
    if(this.follow) this.offset = this.maxOffset;
    return this;
  }

  setLines(lines) {
    this.lines = [...lines];
    this.offset = this.follow ? this.maxOffset : Math.min(this.offset, this.maxOffset);
    return this;
  }

  /* new window height, e.g. after a resize */
  resize(height) {
    this.height = Math.max(1, height);
    this.offset = this.follow ? this.maxOffset : Math.min(this.offset, this.maxOffset);
    return this;
  }

  scrollTo(offset) {
    this.offset = Math.max(0, Math.min(this.maxOffset, Math.round(offset)));
    this.follow = this.atBottom;
    return this;
  }

  scrollBy(n) {
    return this.scrollTo(this.offset + n);
  }

  pageUp() {
    return this.scrollBy(-(this.height - 1 || 1));
  }

  pageDown() {
    return this.scrollBy(this.height - 1 || 1);
  }

  home() {
    return this.scrollTo(0);
  }

  end() {
    return this.scrollTo(this.maxOffset);
  }

  /* Private: source entries {text, wrappedLines} cached per width */
  _sourceEntries = [];
  /* Private: current wrap width (undefined = old-style, no wrapping) */
  _width = undefined;
  /* Private: number of source entries that have been wrapped */
  _rewraps = 0;
  /* Public: existing flat concatenation of all display lines (backward compatible) */
  lines = [];
  /* Public: offset into lines for scrolling */
  offset = 0;
  /* Public: when true, keep end of view in sight when new lines added */
  follow = true;
  /* Public: wrap width, undefined when not in source mode */
  get width() {
    return this._width;
  }
  /* Public: number of source entries wrapped so far (test hook) */
  get rewraps() {
    return this._rewraps;
  }

  /* the lines in the window now */
  visible() {
    return this.lines.slice(this.offset, this.offset + this.height);
  }

  /* exactly height lines of width cells, a scrollbar in the last column when bar is on */
  render(width, { bar = true } = {}) {
    const sb = bar ? scrollbar(this.height, this.lines.length, this.offset) : null;
    const w = bar ? width - 1 : width;
    const vis = this.visible();

    return Array.from({ length: this.height }, (_, r) => pad(vis[r] ?? '', w) + (sb ? sb[r] : ''));
  }

  /* scrolls for up, down, pageup, pagedown, home, end and the wheel (3 lines)
   *
   * key is a decodeKeys() key or a readKey() result. returns whether it
   * scrolled the view. */
  handleKey(key) {
    const name = key.name ?? key.type;
    const wheel = name == 'mouse' ? (key.mouse ?? key).button : null;
    const before = this.offset;

    if(wheel == 'wheelup' || name == 'up') this.scrollBy(wheel ? -3 : -1);
    else if(wheel == 'wheeldown' || name == 'down') this.scrollBy(wheel ? 3 : 1);
    else if(name == 'pageup') this.pageUp();
    else if(name == 'pagedown') this.pageDown();
    else if(name == 'home') this.home();
    else if(name == 'end') this.end();
    else return false;

    return this.offset != before || name == 'end';
  }

  /* ---------- Source mode API ---------- */

  /**
   * Set wrap width. First call turns source mode on.
   * If width equals current width, do nothing.
   * Otherwise re-wrap all source entries, rebuild display lines,
   * and anchor the view (top of window stays on the same source entry/sub-line).
   * If follow is on, keep end in view.
   */
  setWidth(width) {
    if(this._width === width) return; /* no change */

    const wasSource = this._width !== undefined;
    this._width = width;
    this._rewraps += this._sourceEntries.length; /* each entry gets wrapped again */

    if(!wasSource) {
      /* First call to setWidth turns source mode on.
         Convert existing this.lines into source entries. */
      const existingLines = this.lines;
      this._sourceEntries = existingLines.map(text => ({ text, wrapped: [] }));
      /* Now re-wrap with the new width */
      this._rewrapAll();
    } else {
      /* Re-wrap all existing source entries with new width */
      this._rewrapAll();
    }

    /* Rebuild this.lines from _sourceEntries wrapped lines */
    this._rebuildLines();

    /* Anchor the view: keep the same source entry index if possible
       The offset should stay pointing at the same logical position.
       Simple approach: if follow was on and we were at bottom, stay at bottom.
       Otherwise, try to keep offset proportional. */
    if(this.follow && this.atBottom) {
      this.offset = this.maxOffset;
    } else if(!this.follow && this.offset > this.maxOffset) {
      this.offset = this.maxOffset;
    }

    return this;
  }

  /**
   * Appends source entries (strings with newlines/colors).
   * Wraps each with wrap(text, width) and appends to display lines.
   * Throws Error if setWidth hasn't been called yet.
   */
  pushText(...texts) {
    if(this._width === undefined) {
      throw new Error('setWidth must be called before pushText');
    }
    for(const text of texts) {
      const wrapped = wrap(text, this._width);
      this._sourceEntries.push({ text, wrapped });
      this._rewraps += 1; // each entry counts as one wrapped entry
    }
    this._rebuildLines();
    if(this.follow) this.offset = this.maxOffset;
    return this;
  }

  /**
   * Replaces all source entries and rebuilds display lines.
   */
  setText(texts) {
    this._sourceEntries = texts.map(text => {
      const wrapped = wrap(text, this._width);
      return { text, wrapped };
    });
    this._rewraps = this._sourceEntries.length;
    this._rebuildLines();
    if(this.follow) this.offset = this.maxOffset;
    return this;
  }

  /** Returns number of source entries wrapped so far (test hook). */
  get rewraps() {
    return this._rewraps;
  }

  /** Returns wrap width or undefined. */
  get width() {
    return this._width;
  }

  /* ---------- Internal helpers ---------- */

  /**
   * Wrap all source entries and accumulate wrapped lines into each entry.
   * Does NOT touch this.lines here; caller must call _rebuildLines().
   */
  _rewrapAll() {
    for(const entry of this._sourceEntries) {
      entry.wrapped = wrap(entry.text, this._width);
    }
  }

  /**
   * Rebuild this.lines from _sourceEntries wrapped arrays.
   * Appending must not re-flatten the whole array, so we just concat.
   */
  _rebuildLines() {
    /* Build new flat lines array from all wrapped entries */
    const newLines = this._sourceEntries.flatMap(entry => entry.wrapped);
    /* Replace this.lines entirely; existing methods (render, handleKey, etc.) work untouched */
    this.lines = newLines;
    /* Adjust offset so we don't lose view position unexpectedly */
    if(this.offset > this.lines.length) this.offset = Math.max(0, this.lines.length - this.height);
    if(this.follow) this.offset = this.maxOffset;
  }

  /**
   * Append lines to the display (old-style, no source mode).
   * Pushes each line into this.lines; if follow is on, scrolls to bottom.
   */
  push(...lines) {
    /* In source mode, we need to convert lines to source entries.
       But old-style ScrollView (without setWidth) should just push lines directly. */
    if(this._width === undefined) {
      /* Old-style: just append to this.lines */
      this.lines.push(...lines);
      if(this.follow) this.offset = this.maxOffset;
    } else {
      /* Source mode: convert each line to a source entry */
      for(const text of lines) {
        const wrapped = wrap(text, this._width);
        this._sourceEntries.push({ text, wrapped });
        this._rewraps += 1;
      }
      this._rebuildLines();
      if(this.follow) this.offset = this.maxOffset;
    }
    return this;
  }

  /**
   * Replace all display lines (old-style, no source mode).
   * Replaces this.lines entirely and resets offset.
   */
  setLines(lines) {
    if(this._width === undefined) {
      /* Old-style: just replace lines flat */
      this.lines = [...lines];
      this.offset = this.follow ? this.maxOffset : Math.min(this.offset, this.maxOffset);
    } else {
      /* Source mode: convert lines to source entries */
      this._sourceEntries = lines.map(text => {
        const wrapped = wrap(text, this._width);
        return { text, wrapped };
      });
      this._rewraps = this._sourceEntries.length;
      this._rebuildLines();
      this.offset = this.follow ? this.maxOffset : Math.min(this.offset, this.maxOffset);
    }

    return this;
  }
}

/**
 * A stateful, single- and multi-line terminal text editor with Readline-style
 * keybindings, command history, reverse incremental search, kill ring, and tab
 * completion.
 *
 * Processes normalized key objects produced by {@link decodeKeys} via
 * {@link LineEditor#key} and computes wrapped cursor coordinates and
 * ghost text via {@link LineEditor#layout}.
 *
 * @example
 * const editor = new LineEditor({ history: ['git status', 'npm test'], maxHistory: 100 });
 * const action = editor.key(decodedKey);
 * if(action === 'submit') {
 *   const command = editor.accept();
 * }
 */
export class LineEditor {
  constructor({ history, maxHistory } = {}) {
    this.cmd = '';
    this.cursor = 0;
    this.killRing = '';
    this.complete = null; /* (text, pos) => { suggestion: string, range: [start, end] } | null */
    this.ghost = ''; /* current ghost text to display */
    this.searchMode = false;
    this.searchBuffer = '';
    this.searchIndex = -1;
    this.history = history !== undefined ? [...history] : [];
    this.historyPos = -1; /* -1 = a new entry, else the index of the entry shown (history.length = a new empty one) */
    this.maxHistory = maxHistory;
    this.multiline = false; /* when true, Enter inserts a newline */
    this.isIncomplete = null; /* (text) => boolean */
    this.onRemember = null; /* (entry) => void */
    this.historyFilter = null; /* (entry) => boolean */
  }

  /* one key from decodeKeys(): 'change', 'submit', 'cancel', 'eof' or null (not handled, nothing changed).
   *
   *   Ctrl-C, Escape        clear the line, 'cancel'      Ctrl-D   'eof' on an empty line, else delete right
   *   Return, Enter         'submit', a newline when multiline, or with Alt or Shift
   *   Ctrl-K, Ctrl-U        kill to the end / start of the line
   *   Ctrl-W, Ctrl-Backspace, Alt-Backspace   kill the word left     Ctrl-Delete, Alt-D   the word right
   *   the names ctrlc, ctrlD and ctrlJ of older callers are still understood
   */
  key(key) {
    const { name = '', ctrl = false, meta = false, shift = false } = key;
    const before = this.cmd + '\0' + this.cursor;
    const done = () => (this.cmd + '\0' + this.cursor != before ? 'change' : null);

    if(name == 'escape' || name == 'ctrlc' || (ctrl && name == 'c')) {
      if(this.searchMode) {
        /* exit search mode, restore previous cmd/cursor */
        this.searchMode = false;
        this.searchBuffer = '';
        this.searchIndex = -1;

        return 'change';
      }

      this.cmd = '';
      this.cursor = 0;
      this.historyPos = -1;

      return 'cancel';
    }

    /* ctrlR: enter reverse search mode */
    if(name == 'ctrlR' && !this.searchMode) {
      this.searchMode = true;
      this.searchBuffer = '';
      this.searchIndex = this.history.length - 1;

      return 'change';
    }

    if(name == 'tab') {
      const result = this.complete?.(this.cmd, this.cursor);
      if(result && result.suggestion !== null) {
        /* Insert the suggestion at the range */
        const [start, end] = result.range;
        /* If start is beyond or at the end of the text, just append the suggestion */
        if(start >= this.cmd.length) {
          this.cmd += result.suggestion;
          this.cursor = this.cmd.length;
        } else {
          /* effectiveEnd is the actual end position, clamped to text length */
          const effectiveEnd = end > this.cmd.length ? this.cmd.length : end;
          /* Insert suggestion, replacing text from start to effectiveEnd */
          this.cmd = this.cmd.substring(0, start) + result.suggestion + this.cmd.substring(effectiveEnd);
          this.cursor = start + result.suggestion.length;
        }
        /* Update ghost: if the suggestion is longer than the original range,
           the ghost contains the remaining suggestion text beyond what was in the range */
        const originalRangeLength = end - start;
        if(result.suggestion.length > originalRangeLength) {
          this.ghost = result.suggestion.substring(originalRangeLength);
        } else {
          this.ghost = '';
        }
      }
      return 'change';
    }

    if(this.searchMode) {
      if(name == 'escape' || name == 'ctrlC') {
        /* exit search mode, restore previous cmd/cursor */
        this.searchMode = false;
        this.searchBuffer = '';
        this.searchIndex = -1;

        return 'change';
      }

      if(name == 'enter') {
        /* accept current match, exit search mode */
        if(this.searchIndex >= 0 && this.searchIndex < this.history.length) {
          this.cmd = this.history[this.searchIndex];
          this.cursor = this.cmd.length;
        }

        this.searchMode = false;
        this.searchBuffer = '';
        this.searchIndex = -1;

        return 'submit';
      }

      if(name == 'backspace') {
        /* remove last char from searchBuffer, re-search */
        this.searchBuffer = this.searchBuffer.slice(0, -1);
        this._searchHistory(0); // <--- FIXED: 0 means "re-search current and older"
        return 'change';
      }

      if(name == 'up' || name == 'down') {
        /* navigate through matching history entries */
        this._searchHistory(name == 'up' ? -1 : 1);

        return 'change';
      }

      /* printable character: append to searchBuffer, re-search */
      if(name.length == 1 && /^[\x20-\x7e]$/.test(name)) {
        this.searchBuffer += name;
        this._searchHistory();

        return 'change';
      }
    }

    if(name == 'ctrlD' || (ctrl && name == 'd')) {
      if(this.cmd === '') {
        this.historyPos = -1;

        return 'eof';
      }

      this.deleteRight();

      return done();
    }

    if(name == 'paste') {
      this.insertText(String(key.paste ?? '').replace(/\r\n?/g, '\n'));

      return done();
    }

    if(name == 'return' || name == 'enter' || name == 'ctrlJ') {
      if(name == 'ctrlJ' || shift) {
        this.insertPrintable('\n');
        return 'change';
      }

      if(this.multiline || (this.isIncomplete && this.isIncomplete(this.cmd)) || meta) {
        this.insertText('\n');
        return 'change';
      }

      this.historyPos = -1;

      return 'submit';
    }

    if(ctrl) {
      switch (name) {
        case 'k':
          this.killToEnd();
          break;
        case 'u':
          this.killToStart();
          break;
        case 'w':
        case 'backspace':
          this.killWordLeft();
          break;
        case 'delete':
          this.killWordRight();
          break;
        case 'y':
          this.insertText(this.killRing);
          break;
        case 'a':
          this.cursor = this.lineStart();
          break;
        case 'e':
          this.cursor = this.lineEnd();
          break;
        case 'b':
          this.cursor = this.prevIndex(this.cursor);
          break;
        case 'f':
          this.cursor = this.nextIndex(this.cursor);
          break;
        case 'h':
          this.deleteLeft();
          break;
        case 'left':
          this.cursor = this.prevWordPos(this.cursor);
          break;
        case 'right':
          this.cursor = this.nextWordPos(this.cursor);
          break;
        case 'p':
          return this.arrow('up');
        case 'n':
          return this.arrow('down');
      }

      return done();
    }

    if(meta) {
      switch (name) {
        case 'b':
        case 'left':
          this.cursor = this.prevWordPos(this.cursor);
          break;
        case 'f':
        case 'right':
          this.cursor = this.nextWordPos(this.cursor);
          break;
        case 'd':
        case 'delete':
          this.killWordRight();
          break;
        case 'backspace':
          this.killWordLeft();
          break;
      }

      return done();
    }

    switch (name) {
      case 'space':
        this.insertText(' ');
        break;
      case 'backspace':
        this.deleteLeft();
        break;
      case 'delete':
        this.deleteRight();
        break;
      case 'left':
        this.cursor = this.prevIndex(this.cursor);
        break;
      case 'right':
        this.cursor = this.nextIndex(this.cursor);
        break;
      case 'home':
        this.cursor = this.lineStart();
        break;
      case 'end':
        this.cursor = this.lineEnd();
        break;
      case 'up':
      case 'down':
        return this.arrow(name);
      default:
        /* one code point is a character, longer names (f1, pageup, tab) are keys */
        if([...name].length == 1) this.insertText(name);
    }

    return done();
  }

  /* Up and Down: the line above or below in a text with newlines, else the history */
  arrow(name) {
    const start = this.lineStart();
    const col = this.cursor - start;

    if(name == 'up' ? start > 0 : this.lineEnd() < this.cmd.length) {
      const target = name == 'up' ? this.lineStart(start - 1) : this.lineEnd() + 1;

      this.cursor = Math.min(target + col, this.lineEnd(target));

      return 'change';
    }

    return this.navigateHistory(name == 'up' ? -1 : 1) ? 'change' : null;
  }

  /* start and end of the line of the text that holds pos: 'ab\ncd', 4 -> 3, 5 */
  lineStart(pos = this.cursor) {
    return pos > 0 ? this.cmd.lastIndexOf('\n', pos - 1) + 1 : 0;
  }

  lineEnd(pos = this.cursor) {
    const end = this.cmd.indexOf('\n', pos);

    return end < 0 ? this.cmd.length : end;
  }

  /* the index of the character before / after pos: a surrogate pair, a combining mark, a ZWJ emoji or a flag is one */
  prevIndex(pos) {
    return pos <= 0 ? 0 : prevCluster(this.cmd, pos);
  }

  nextIndex(pos) {
    return pos >= this.cmd.length ? this.cmd.length : nextCluster(this.cmd, pos);
  }

  insertText(text) {
    if(!text) return;

    this.cmd = this.cmd.substring(0, this.cursor) + text + this.cmd.substring(this.cursor);
    this.cursor += text.length;
  }

  insertPrintable(ch) {
    this.insertText(ch);
  }

  deleteLeft() {
    const at = this.prevIndex(this.cursor);

    this.cmd = this.cmd.substring(0, at) + this.cmd.substring(this.cursor);
    this.cursor = at;
  }

  deleteRight() {
    this.cmd = this.cmd.substring(0, this.cursor) + this.cmd.substring(this.nextIndex(this.cursor));
  }

  /* Ctrl-K: to the end of the line, at its end the newline */
  killToEnd() {
    const end = this.lineEnd();

    this.kill(this.cursor, end == this.cursor && end < this.cmd.length ? end + 1 : end);
  }

  /* Ctrl-U: to the start of the line */
  killToStart() {
    this.kill(this.lineStart(), this.cursor);
  }

  killWordLeft() {
    this.kill(this.prevWordPos(this.cursor), this.cursor);
  }

  killWordRight() {
    this.kill(this.cursor, this.nextWordPos(this.cursor));
  }

  /* takes cmd[from, to) out into killRing, the cursor goes to from */
  kill(from, to) {
    if(to <= from) return;

    this.killRing = this.cmd.substring(from, to);
    this.cmd = this.cmd.substring(0, from) + this.cmd.substring(to);
    this.cursor = from;
  }

  /* the start of the word before pos: spaces are skipped, then the word */
  prevWordPos(pos) {
    while(pos > 0 && /\s/.test(this.cmd[pos - 1])) pos--;
    while(pos > 0 && !/\s/.test(this.cmd[pos - 1])) pos--;

    return pos;
  }

  /* the end of the word after pos: spaces are skipped, then the word */
  nextWordPos(pos) {
    while(pos < this.cmd.length && /\s/.test(this.cmd[pos])) pos++;
    while(pos < this.cmd.length && !/\s/.test(this.cmd[pos])) pos++;

    return pos;
  }

  /* adds text to the history unless it is empty or the newest entry; maxHistory drops the oldest */
  remember(text) {
    if(!text || text === this.history[this.history.length - 1]) return;
    if(this.historyFilter && !this.historyFilter(text)) return;

    this.history.push(text);

    if(this.maxHistory !== undefined && this.history.length > this.maxHistory) this.history.shift();

    if(this.onRemember) this.onRemember(text);
  }

  loadHistory(entries) {
    this.history = [...entries];
    this.historyPos = -1;
  }

  saveHistory() {
    return [...this.history];
  }

  /* delta -1 older, +1 newer. Leaving a new entry with text keeps it as the newest history entry,
   * past the newest the line is a new empty one. true if the line changed; Down on a new entry does nothing.
   */
  navigateHistory(delta) {
    /* a new entry has nothing newer to go to */
    if(this.historyPos == -1 && delta > 0) return false;

    if(this.historyPos == -1) {
      if(this.cmd !== '') {
        this.remember(this.cmd);
        this.historyPos = this.history.length - 1;
      } else {
        this.historyPos = this.history.length;
      }
    }

    const pos = Math.max(0, Math.min(this.history.length, this.historyPos + delta));
    const cmd = pos == this.history.length ? '' : this.history[pos];
    const changed = pos != this.historyPos || cmd != this.cmd;

    this.historyPos = pos;
    this.cmd = cmd;
    this.cursor = cmd.length;

    return changed;
  }

  /* _searchHistory(direction): find history entries containing searchBuffer
   * direction: 0 = re-search (current then older), -1 = older, 1 = newer
   */
  _searchHistory(direction = 0) {
    const query = this.searchBuffer;
    let idx = this.searchIndex;

    if(direction === 0) {
      /* Re-search: check if current index still matches, else go older */
      if(idx >= 0 && idx < this.history.length && this.history[idx].includes(query)) {
        /* current still matches */
      } else {
        idx = idx >= 0 ? idx - 1 : this.history.length - 1;
        while(idx >= 0) {
          if(this.history[idx].includes(query)) break;
          idx--;
        }
      }
    } else if(direction === -1) {
      /* Older */
      idx = idx >= 0 ? idx - 1 : this.history.length - 1;
      while(idx >= 0) {
        if(this.history[idx].includes(query)) break;
        idx--;
      }
    } else if(direction === 1) {
      /* Newer */
      idx = idx + 1;
      while(idx < this.history.length) {
        if(this.history[idx].includes(query)) break;
        idx++;
      }
    }

    if(idx >= 0 && idx < this.history.length) {
      this.searchIndex = idx;
      this.cmd = this.history[idx];
      /* place cursor at the end of the matched substring, or end of line */
      const matchIdx = this.cmd.indexOf(query);
      this.cursor = matchIdx >= 0 ? matchIdx + query.length : this.cmd.length;
    } else {
      /* no match found */
      this.searchIndex = -1;
    }
  }

  set(cmd) {
    this.cmd = cmd !== undefined ? cmd : '';
    this.cursor = this.cmd.length;
    this.historyPos = -1;
    this.killRing = '';
  }

  accept() {
    const text = this.cmd;

    this.cmd = '';
    this.cursor = 0;
    this.historyPos = -1;
    this.killRing = '';
    this.remember(text);

    return text;
  }

  layout(prefixes, columns) {
    const { lines, row, col } = layoutInput(prefixes, this.cmd, this.cursor, columns);

    /* Calculate ghost lines based on this.ghost */
    const ghostLines = [];
    if(this.ghost) {
      const ghostLinesRaw = String(this.ghost).split('\n');

      for(const gLine of ghostLinesRaw) {
        /* Ensure ghost text starts at the correct cell position using stringWidth */
        const prefixWidth = stringWidth(lines[row] || '');
        ghostLines.push(gLine);
      }
    }

    return { lines, row, col, ghostLines };
  }
}
