/* terminal.js: escape sequences, raw mode, size, keys, widths and a buffered Screen.
 * depends on: misc (ttyWrite, ttyRead, ttySetRaw, ttyGetWinSize), os (sleep), textcode.
 * rule: every write is one misc.ttyWrite(f, string) call. It flushes f, and a Win32
 * console without VT processing gets the sequences as console calls.
 */
import { EAGAIN, ttyGetWinSize, ttyRead, ttySetRaw, ttyWrite } from 'misc';
import { sleep } from 'os';
import { TextDecoder } from 'textcode';

export { EAGAIN, ttyGetWinSize, ttyRead, ttySetRaw, ttyWrite };

const ESC = '\x1b';
const CSI = ESC + '[';

/* CSI <n> <c>, the count left out when it is 1 or missing */
const csiCount = (f, n, c) => ttyWrite(f, CSI + (n > 1 ? n : '') + c);

export const cursorHome = f => ttyWrite(f, '\r');
export const cursorUp = (f, n) => csiCount(f, n, 'A');
export const cursorDown = (f, n) => csiCount(f, n, 'B');
export const cursorForward = (f, n) => csiCount(f, n, 'C');
export const cursorBackward = (f, n) => csiCount(f, n, 'D');
export const cursorNextLine = (f, n) => csiCount(f, n, 'E');
export const cursorPreviousLine = (f, n) => csiCount(f, n, 'F');
export const cursorHorizontalAbsolute = (f, n) => csiCount(f, n, 'G');
export const cursorPosition = (f, row, column) => ttyWrite(f, `${CSI}${row};${column}H`);
export const cursorOrigin = f => ttyWrite(f, CSI + 'H');
export const cursorSave = f => ttyWrite(f, ESC + '7');
export const cursorRestore = f => ttyWrite(f, ESC + '8');
export const cursorQuery = f => ttyWrite(f, CSI + '6n');
export const cursorHide = f => ttyWrite(f, CSI + '?25l');
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

export const linewrapEnable = f => ttyWrite(f, CSI + '?7h');
export const linewrapDisable = f => ttyWrite(f, CSI + '?7l');

export const mousetrackingEnable = f => ttyWrite(f, CSI + '?1000;1006;1015h');
export const mousetrackingDisable = f => ttyWrite(f, CSI + '?1000;1006;1015l');

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

  const [r1, g1, b1] =
    h < 60 ? [c, x, 0] : h < 120 ? [x, c, 0] : h < 180 ? [0, c, x] : h < 240 ? [0, x, c] : h < 300 ? [x, 0, c] : [c, 0, x];

  return [Math.round((r1 + m) * 255), Math.round((g1 + m) * 255), Math.round((b1 + m) * 255)];
}

/* raw mode of input fd: no echo, no line buffering, UTF-8 on Windows.
 * returns true when the output does VT itself. */
export const enableRawMode = fd => ttySetRaw(fd);
export const disableRawMode = fd => ttySetRaw(fd, true);

/* [columns, rows] of the terminal behind a FILE or fd, [80, 24] when it is none */
export function windowSize(f) {
  return ttyGetWinSize(typeof f == 'number' ? f : f.fileno()) ?? [80, 24];
}

/* ---- widths: how many terminal cells a string takes ---- */

const WIDE = [
  [0x1100, 0x115f], [0x2e80, 0x303e], [0x3041, 0x33ff], [0x3400, 0x4dbf], [0x4e00, 0x9fff], [0xa000, 0xa4cf], [0xac00, 0xd7a3], [0xf900, 0xfaff], [0xfe30, 0xfe6f], [0xff00, 0xff60], [0xffe0, 0xffe6],
  [0x1f300, 0x1f64f], [0x1f680, 0x1f6ff], [0x1f900, 0x1f9ff], [0x20000, 0x3fffd],
];
const ZERO = [[0x300, 0x36f], [0x200b, 0x200f], [0x20d0, 0x20ff], [0xfe00, 0xfe0f], [0x1ab0, 0x1aff], [0x1dc0, 0x1dff]];
const within = (ranges, cp) => ranges.some(([a, b]) => cp >= a && cp <= b);

/* cells of one character: 0 for controls and combining marks, 2 for wide ones */
export function charWidth(ch) {
  const cp = ch.codePointAt(0);

  if(cp < 32 || (cp >= 0x7f && cp < 0xa0) || within(ZERO, cp)) return 0;

  return within(WIDE, cp) ? 2 : 1;
}

/* str without CSI sequences: stripAnsi('\x1b[1mb\x1b[0m') is 'b' */
export const stripAnsi = str => str.replace(/\x1b\[[0-9;?]*[ -\/]*[@-~]/g, '');

/* cells of str without its CSI sequences: stringWidth('漢x') is 3 */
export const stringWidth = str => [...stripAnsi(str)].reduce((w, ch) => w + charWidth(ch), 0);

/* ---- keys: input bytes to { sequence, name, ctrl, meta, shift } ---- */

const CSI_LETTER = { A: 'up', B: 'down', C: 'right', D: 'left', E: 'clear', F: 'end', H: 'home', P: 'f1', Q: 'f2', R: 'f3', S: 'f4', Z: 'tab' };
const CSI_TILDE = { 1: 'home', 2: 'insert', 3: 'delete', 4: 'end', 5: 'pageup', 6: 'pagedown', 7: 'home', 8: 'end', 11: 'f1', 12: 'f2', 13: 'f3', 14: 'f4', 15: 'f5', 17: 'f6', 18: 'f7', 19: 'f8', 20: 'f9', 21: 'f10', 23: 'f11', 24: 'f12' };

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

/* decodeKeys(str, final): the keys in str, and rest, an unfinished ESC sequence for the next chunk
 *
 *   decodeKeys('a\x1b[1;5D').keys.map(k => k.key.name)   // ['a', 'left'], the second with ctrl
 *
 * final: an ESC at the end is the escape key, not the start of a sequence. */
export function decodeKeys(str, final = false) {
  const keys = [];
  let i = 0;

  while(i < str.length) {
    const ch = String.fromCodePoint(str.codePointAt(i));

    if(ch != '\x1b') {
      keys.push({ s: ch, key: charKey(ch) });
      i += ch.length;
      continue;
    }

    if(i + 1 >= str.length) {
      if(!final) return { keys, rest: str.slice(i) };

      keys.push({ s: ch, key: charKey(ch) });
      break;
    }

    const next = str[i + 1];

    if(next == '[' || next == 'O') {
      let j = i + 2;

      while(j < str.length && /[0-9;?]/.test(str[j])) j++;

      if(j >= str.length) {
        if(!final) return { keys, rest: str.slice(i) };

        break;
      }

      const params = str.slice(i + 2, j).split(';').filter(p => p && p != '?').map(Number);
      const mod = (params[1] ?? 1) - 1;
      const sequence = str.slice(i, j + 1);
      const name = str[j] == '~' ? CSI_TILDE[params[0]] : CSI_LETTER[str[j]];

      keys.push({ s: sequence, key: { sequence, name, ctrl: !!(mod & 4), meta: !!(mod & 10), shift: !!(mod & 1) || str[j] == 'Z' } });
      i = j + 1;
      continue;
    }

    const inner = String.fromCodePoint(str.codePointAt(i + 1));
    const key = charKey(inner);

    key.meta = true;
    key.sequence = '\x1b' + inner;
    keys.push({ s: key.sequence, key });
    i += 1 + inner.length;
  }

  return { keys, rest: '' };
}

/* per fd: keys read but not returned yet, and the decoder of a split character */
const keyQueues = new Map();

const KEY_TYPES = { up: 'up', down: 'down', left: 'left', right: 'right', pageup: 'pageup', pagedown: 'pagedown', space: 'space', return: 'enter', enter: 'enter', backspace: 'backspace', escape: 'escape' };

/* readKey(fd): one keypress from fd in raw mode, waits until there is one
 *
 *   readKey(0)   // { type: 'up' }, { type: 'char', char: 'ä' }, { type: 'ctrlc' }
 *
 * type is up down left right pageup pagedown space enter backspace ctrlc
 * escape (also any other sequence), char, or eof. */
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

  if(s.length > 1 && s[0] == '\x1b') return { type: KEY_TYPES[key.name] ?? 'escape' };

  return KEY_TYPES[key.name] ? { type: KEY_TYPES[key.name] } : { type: 'char', char: s };
}

/* box drawing sets for Screen.box(): single and double line */
export const BOX_SINGLE = { tl: '┌', tr: '┐', bl: '└', br: '┘', h: '─', v: '│' };
export const BOX_DOUBLE = { tl: '╔', tr: '╗', bl: '╚', br: '╝', h: '═', v: '║' };

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

  constructor(f) {
    this.#f = f;
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
    for(let r = 1; r < height - 1; r++) this.moveTo(row + r, col).write(v).moveTo(row + r, col + width - 1).write(v);
    this.moveTo(row + height - 1, col).write(bl + h.repeat(width - 2) + br);

    return this;
  }

  /* sends the buffer to the file in one ttyWrite and empties it */
  flush() {
    ttyWrite(this.#f, this.#buf);
    this.#buf = '';
    return this;
  }
}
