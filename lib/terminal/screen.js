/* terminal/screen.js: Screen (a string backbuffer), Region, BOX_* sets and centeredRect().
 * depends on: terminal.js (ttyWrite, sgr, syncOutput, stringWidth), terminal/internal.js (CSI).
 * rule: a frame is built as one string and sent with one ttyWrite. */

import { sgr, stringWidth, syncOutput, ttyWrite } from 'terminal';
import { CSI } from 'terminal/internal';

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
