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
