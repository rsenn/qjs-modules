import { clearLine, clearScreen } from 'misc';
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
