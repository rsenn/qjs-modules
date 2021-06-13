import filesystem from 'fs';

const ESC = '\x1b';

Object.defineProperty(globalThis, 'terminal', {
  get() {
    const g = globalThis;
    delete g.terminal;
    g.terminal = filesystem.stdout;
    return g.terminal;
  },
  configurable: true,
  enumerable: false
});

function putEscape(buf) {
  filesystem.puts(buf, ESC + '[');
}

function putNum(buf, n) {
  filesystem.puts(buf, `${n}`);
}

function putChar(buf, c) {
  filesystem.puts(buf, c);
}

function putString(buf, s) {
  filesystem.puts(buf, s);
}

export function numberSequence(buf, n, c) {
  putEscape(buf);
  if(n > 1) filesystem.puts(buf, n + '');
  filesystem.puts(buf, c);
}

export function numbersSequence(buf, numbers, c) {
  let i;
  for(i = 0; i < numbers.length; i++) {
    if(i > 0) filesystem.puts(buf, ';');
    filesystem.puts(buf, numbers[i] + '');
  }
  filesystem.puts(buf, c);
}

export function escapeNumberChar(buf, n, c) {
  numberSequence(buf, n, c);
  filesystem.flush(buf);
}

export function escapeChar(buf, c) {
  filesystem.puts(buf, ESC);
  filesystem.puts(buf, c);
}

export function escapeSequence(buf, seq) {
  putEscape(buf);
  filesystem.puts(buf, seq);
}

export function commandSequence(buf, seq) {
  escapeSequence(buf, seq);
  filesystem.flush(buf);
}
export function commandNumberChar(n, c) {
  escapeNumberChar(terminal, n, c);
  filesystem.flush(terminal);
}
export function commandChar(c) {
  escapeChar(terminal, c);
  filesystem.flush(terminal);
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
  filesystem.flush(terminal);
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
  filesystem.flush(terminal);
}

export function setNormalScreen() {
  putEscape(terminal);
  putChar(terminal, '?');
  putNum(terminal, 1049);
  putChar(terminal, 'l');
  filesystem.flush(terminal);
}

export function rgbForeground(buf, r, g, b) {
  putEscape(buf);
  numbersSequence(buf, [38, 2, r, g, b], 'm');
}

export function rgbBackground(buf, r, g, b) {
  putEscape(buf);
  numbersSequence(buf, [48, 2, r, g, b], 'm');
}

export const mousetrackingEnable = (buf = terminal) => {
  putEscape(buf);
  putChar(buf, '?');
  numbersSequence(buf, [1000, 1006, 1015], 'h');
};
export const mousetrackingDisable = (buf = terminal) => {
  putEscape(buf);
  putChar(buf, '?');
  numbersSequence(buf, [1000, 1006, 1015], 'l');
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
