//import fs from 'fs';

const ESC = '\x1b';

Object.defineProperty(globalThis, 'terminal', {
  get() {
    const g = globalThis;
    delete g.terminal;
    g.terminal = fs.stdout;
    return g.terminal;
  },
  configurable: true,
  enumerable: false
});

function putEscape(f) {
  fs.puts(f, ESC + '[');
}

function putNum(f, n) {
  fs.puts(f, `${n}`);
}

function putChar(f, c) {
  fs.puts(f, c);
}

function putString(f, s) {
  fs.puts(f, s);
}

export function numberSequence(f, n, c) {
  putEscape(f);
  if(n > 1) fs.puts(f, n + '');
  fs.puts(f, c);
}

export function numbersSequence(f, numbers, c) {
  let i;
  for(i = 0; i < numbers.length; i++) {
    if(i > 0) fs.puts(f, ';');
    fs.puts(f, numbers[i] + '');
  }
  fs.puts(f, c);
}

export function escapeNumberChar(f, n, c) {
  numberSequence(f, n, c);
  fs.flush(f);
}

export function escapeChar(f, c) {
  fs.puts(f, ESC);
  fs.puts(f, c);
}

export function escapeSequence(f, seq) {
  putEscape(f);
  fs.puts(f, seq);
}

export function commandSequence(f, seq) {
  escapeSequence(f, seq);
  fs.flush(f);
}
export function commandNumberChar(n, c) {
  escapeNumberChar(terminal, n, c);
  fs.flush(terminal);
}
export function commandChar(c) {
  escapeChar(terminal, c);
  fs.flush(terminal);
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
  fs.flush(terminal);
}

export function eraseInDisplay(n) {
  commandNumberChar(n, 'J');
}

export function eraseInLine(n) {
  commandNumberChar(n, 'K');
}

export function scrollUp(n) {
  commandNumberChar(n, 'S');
}

export function scrollDown(n) {
  commandNumberChar(n, 'T');
}

export function setAlternateScreen() {
  putEscape(terminal);
  putChar(terminal, '?');
  putNum(terminal, 1049);
  putChar(terminal, 'h');
  fs.flush(terminal);
}

export function setNormalScreen() {
  putEscape(terminal);
  putChar(terminal, '?');
  putNum(terminal, 1049);
  putChar(terminal, 'l');
  fs.flush(terminal);
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
