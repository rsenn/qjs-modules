import { clearLine, clearScreen, ttySetRaw } from 'misc';
import { read as osRead, isatty, ttyGetWinSize } from 'os';
import process from 'process';
import * as std from 'std';

const ESC = '\x1b';

export let terminal = process.stdout;

function putEscape(f) {
  f.puts(ESC + '[');
}

function putNum(f, n) {
  f.puts(`${n}`);
}

function putChar(f, c) {
  f.puts(c);
}

function putString(f, s) {
  f.puts(s);
}

export function numberSequence(f, n, c) {
  if(n > 1) f.puts(n + '');
  f.puts(c);
}

export function numbersSequence(f, numbers, c) {
  let i;
  for(i = 0; i < numbers.length; i++) {
    if(i > 0) f.puts(';');
    f.puts(numbers[i] + '');
  }
  f.puts(c);
}

export function escapeNumberChar(f, n, c) {
  putEscape(f);
  numberSequence(f, n, c);
  f.flush();
}

export function escapeChar(f, c) {
  f.puts(ESC);
  f.puts(c);
}

export function escapeSequence(f, seq) {
  putEscape(f);
  f.puts(seq);
}

export function commandSequence(f, seq) {
  escapeSequence(f, seq);
  f.flush();
}

export function commandNumberChar(n, c) {
  escapeNumberChar(terminal, n, c);
  terminal.flush();
}

export function commandChar(c) {
  escapeChar(terminal, c);
  terminal.flush();
}

export function cursorHome(n) {
  putChar(terminal, '\r');
}

export function cursorUp(n) {
  commandNumberChar(n, 'A');
}

export function cursorDown(n) {
  commandNumberChar(n, 'B');
}

export function cursorForward(n) {
  commandNumberChar(n, 'C');
}

export function cursorBackward(n) {
  commandNumberChar(n, 'D');
}

export function cursorNextLine(n) {
  commandNumberChar(n, 'E');
}

export function cursorPreviousLine(n) {
  commandNumberChar(n, 'F');
}

export function cursorHorizontalAbsolute(n) {
  commandNumberChar(n, 'G');
}

export function cursorPosition(row, column) {
  let coord = [row, column];
  putEscape(terminal);
  numbersSequence(terminal, coord, 'H');
  terminal.flush();
}

export function cursorOrigin() {
  escapeSequence(terminal, 'H');
  terminal.flush();
}

export function eraseInDisplay(n) {
  clearScreen(terminal.fileno(), n);
  //commandNumberChar(n, 'J');
}

export function eraseInLine(n) {
  clearLine(terminal.fileno(), n);
  //commandNumberChar(n, 'K');
}

export function scrollUp(n) {
  commandNumberChar(n, 'S');
}

export function scrollDown(n) {
  commandNumberChar(n, 'T');
}

/** DECSTBM - restricts scrolling (and scrollUp/scrollDown) to rows [top, bottom], 1-based inclusive. */
export function setScrollRegion(top, bottom, f = terminal) {
  putEscape(f);
  numbersSequence(f, [top, bottom], 'r');
  f.flush();
}

/** Restores the scroll region to the full screen. */
export function resetScrollRegion(f = terminal) {
  escapeSequence(f, 'r');
  f.flush();
}

export function setAlternateScreen() {
  setScreen(true);
}

export function setNormalScreen() {
  setScreen(false);
}

export function setScreen(alternate = false) {
  putEscape(terminal);
  putChar(terminal, '?');
  putNum(terminal, 1049);
  putChar(terminal, alternate ? 'h' : 'l');
  terminal.flush();
}

/**
 * Standard HSL->RGB (h in degrees [0,360), s/l in [0,1]) -> [r,g,b] each
 * 0-255. The generically useful piece of "pick a color off a color
 * wheel": h is the wheel angle, s/l shape how vivid/light that point on
 * the wheel is - a caller wanting "a palette calculated from a color
 * wheel" just walks h (optionally by a fixed step, e.g. the golden angle
 * ~137.508deg, for a well-spread sequence of hues with no clustering).
 */
export function hslToRgb(h, s, l) {
  h = ((h % 360) + 360) % 360;

  const c = (1 - Math.abs(2 * l - 1)) * s;
  const x = c * (1 - Math.abs(((h / 60) % 2) - 1));
  const m = l - c / 2;

  const [r1, g1, b1] =
    h < 60 ? [c, x, 0] : h < 120 ? [x, c, 0] : h < 180 ? [0, c, x] : h < 240 ? [0, x, c] : h < 300 ? [x, 0, c] : [c, 0, x];

  return [Math.round((r1 + m) * 255), Math.round((g1 + m) * 255), Math.round((b1 + m) * 255)];
}

export function rgbForeground(f, r, g, b) {
  putEscape(f);
  numbersSequence(f, [38, 2, r, g, b], 'm');
}

export function rgbBackground(f, r, g, b) {
  putEscape(f);
  numbersSequence(f, [48, 2, r, g, b], 'm');
}

export const mousetrackingEnable = (f = terminal) => {
  putEscape(f);
  putChar(f, '?');
  numbersSequence(f, [1000, 1006, 1015], 'h');
};

export const mousetrackingDisable = (f = terminal) => {
  putEscape(f);
  putChar(f, '?');
  numbersSequence(f, [1000, 1006, 1015], 'l');
};

export const devicecodeQuery = () => escapeSequence('c');
export const devicestatusQuery = () => commandNumberChar(5, 'n');
export const cursorQuery = () => commandNumberChar(6, 'n');
export const deviceReset = () => commandChar('c');
export const tabSet = () => commandChar('H');
export const tabClear = () => commandChar('g');
export const tabsClearall = () => commandNumberChar(3, 'g');
export const cursorSave = () => commandChar('s');
export const cursorRestore = () => commandChar('u');
export const linewrapEnable = () => commandNumberChar(7, 'h');
export const linewrapDisable = () => commandNumberChar(7, 'l');

/** Sets SGR text attribute(s) (e.g. sgr(f, 7) for reverse video, sgr(f, 0) to reset). */
export function sgr(f, ...codes) {
  putEscape(f);
  numbersSequence(f, codes, 'm');
}

export const reverseVideoOn = (f = terminal) => sgr(f, 7);
export const reverseVideoOff = (f = terminal) => sgr(f, 0);

export function cursorHide(f = terminal) {
  putEscape(f);
  putChar(f, '?');
  putNum(f, 25);
  putChar(f, 'l');
  f.flush();
}

export function cursorShow(f = terminal) {
  putEscape(f);
  putChar(f, '?');
  putNum(f, 25);
  putChar(f, 'h');
  f.flush();
}

/** Puts fd's tty into raw mode (no echo, no line buffering, single-byte reads). */
export function enableRawMode(fd) {
  ttySetRaw(fd);
}

/** Restores fd's tty to its normal ("cooked") mode. */
export function disableRawMode(fd) {
  ttySetRaw(fd, true);
}

/** [columns, rows] of the tty on fd (defaults to stdout), or a sane fallback if not a tty. */
export function windowSize(fd = terminal.fileno()) {
  return (isatty(fd) && ttyGetWinSize(fd)) || [80, 24];
}

const KEY_BUF = new Uint8Array(4);

/**
 * Reads and decodes one keypress from fd (which must already be in raw mode - see
 * enableRawMode()), blocking until a byte is available. Recognizes arrow keys, page
 * up/down, space, enter, backspace, and ctrl-c as named `type`s; anything else comes
 * back as `{ type: 'char', char }`. A bare Escape (not the start of a recognized CSI
 * sequence) comes back as `{ type: 'escape' }`.
 */
export function readKey(fd) {
  if(osRead(fd, KEY_BUF.buffer, 0, 1) <= 0) return { type: 'eof' };

  const b = KEY_BUF[0];

  if(b == 0x1b) {
    if(osRead(fd, KEY_BUF.buffer, 1, 1) <= 0 || KEY_BUF[1] != 0x5b) return { type: 'escape' };
    if(osRead(fd, KEY_BUF.buffer, 2, 1) <= 0) return { type: 'escape' };

    const c = KEY_BUF[2];
    if(c == 0x41) return { type: 'up' };
    if(c == 0x42) return { type: 'down' };
    if(c == 0x43) return { type: 'right' };
    if(c == 0x44) return { type: 'left' };
    if(c == 0x35 || c == 0x36) {
      osRead(fd, KEY_BUF.buffer, 3, 1); // trailing '~'
      return { type: c == 0x35 ? 'pageup' : 'pagedown' };
    }
    return { type: 'escape' };
  }

  if(b == 0x20) return { type: 'space' };
  if(b == 0x0d || b == 0x0a) return { type: 'enter' };
  if(b == 0x03) return { type: 'ctrlc' };
  if(b == 0x7f || b == 0x08) return { type: 'backspace' };

  return { type: 'char', char: String.fromCharCode(b) };
}

/** Box-drawing character sets for Screen.box() - single- and double-line, matching the usual curses/dialog default and its "important panel" variant. */
export const BOX_SINGLE = { tl: '┌', tr: '┐', bl: '└', br: '┘', h: '─', v: '│' };
export const BOX_DOUBLE = { tl: '╔', tr: '╗', bl: '╚', br: '╝', h: '═', v: '║' };

/**
 * The curses `newwin(height, width, (rows-height)/2, (cols-width)/2)`
 * idiom - a centered panel's origin/size, clamped to fit the screen.
 * Returns `{row, col, width, height}` (1-based, for Screen.moveTo()/box()).
 */
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

/**
 * A plain string backbuffer: every draw call appends escape sequences and
 * text to an in-memory buffer instead of writing straight to the tty, so a
 * full-screen redraw (e.g. a scrollable list repainted every keypress)
 * goes out as one write() instead of dozens - avoids the visible
 * flicker/tearing of drawing cell-by-cell straight to the terminal.
 */
export class Screen {
  #f;
  #buf = '';

  constructor(f = terminal) {
    this.#f = f;
  }

  /** Appends raw text (no escaping) to the backbuffer. */
  write(s) {
    this.#buf += s;
    return this;
  }

  /** Moves the (virtual) cursor before the next write(), buffered like everything else here. */
  moveTo(row, column) {
    this.#buf += `${ESC}[${row};${column}H`;
    return this;
  }

  /** n: 0 = cursor-to-end (default), 1 = start-to-cursor, 2 = whole screen. */
  clear(n = 2) {
    this.#buf += `${ESC}[${n}J`;
    return this;
  }

  clearLine(n = 2) {
    this.#buf += `${ESC}[${n}K`;
    return this;
  }

  fg(r, g, b) {
    this.#buf += `${ESC}[38;2;${r};${g};${b}m`;
    return this;
  }

  bg(r, g, b) {
    this.#buf += `${ESC}[48;2;${r};${g};${b}m`;
    return this;
  }

  sgr(...codes) {
    this.#buf += `${ESC}[${codes.join(';')}m`;
    return this;
  }

  /** Resets SGR attributes (colors, reverse video, etc.) to terminal defaults. */
  resetAttrs() {
    this.#buf += `${ESC}[0m`;
    return this;
  }

  /**
   * Fills a `width`x`height` rectangle with `ch` - the building block for
   * an ncurses-style "window": since this is just later writes in the
   * same buffered frame, it overwrites (occludes) whatever was already
   * drawn there earlier in the same flush(), which is all "draw a panel
   * on top of other content" needs to mean for a plain terminal (there is
   * no real double-buffer/z-order to manage - draw order IS z-order).
   */
  fillRect(row, col, width, height, ch = ' ') {
    const line = ch.repeat(Math.max(0, width));
    for(let r = 0; r < height; r++) this.moveTo(row + r, col).write(line);
    return this;
  }

  /**
   * Draws a bordered box (ncurses/dialog-style panel), filling its
   * interior first so it fully occludes anything drawn earlier in the
   * same frame. `title`, if given, is centered into the top border - a
   * common curses/whiptail/dialog convention for a panel's name. Caller
   * writes the panel's actual content afterward, inside
   * `[row+1, row+height-2] x [col+1, col+width-2]` (the interior), the
   * same way as any other Screen content.
   */
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

  /** Writes the accumulated buffer to the underlying file in one call and empties it. */
  flush() {
    this.#f.puts(this.#buf);
    this.#f.flush();
    this.#buf = '';
    return this;
  }
}
