/* terminal/widgets.js: widgets that return strings or lines: rule, boxLines, panel, table, ...
 * depends on: terminal.js (text), terminal/style.js, terminal/screen.js (BOX_*), terminal/internal.js.
 * rule: nothing here writes to a tty, draw the result with Screen.lines() or print it. */

import { cells, pad, stringWidth, stripAnsi, truncate, wrap } from 'terminal/core';
import { CSI, RESET, getenv } from 'terminal/internal';
import { BOX_ROUNDED, BOX_SINGLE } from 'terminal/screen';
import { colorCode, gradient, style } from 'terminal/style';

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
export function boxLines(content, { width, padding = 1, title, titleAlign = 'left', footer, align = 'left', chars = BOX_SINGLE, border  } = {}) {
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

/* panel(title, text, cols, opts): a titled panel, as one string
 *
 *   panel('note', 'a\nb\nc', 40, { max: 2 })
 *   // 3 lines boxed: a, b, then a dim '… 1 more lines' row
 *   panel('note', 'a\nb', 40, { kind: 'plain', background: '#1e293b' })
 *   // a filled rectangle, no border: blank row, bold title, a, b, blank row
 *
 * the panel is as wide as the text, at most cols - 1. opts: kind (box
 * with a rounded border, or plain with none, box), border (color of the
 * border, box only), max (lines shown, rest replaced by one dim row),
 * more (fn(hidden) giving that row's text), background (color of every
 * cell, kept after the resets inside the line, plain defaults to 236). */
export function panel(title, text, cols, { kind = 'box', border, max = Infinity, background, more } = {}) {
  let lines = String(text).replace(/\n+$/, '').split('\n');

  if(lines.length > max)
    lines = [...lines.slice(0, max), style(more?.(lines.length - max) ?? `… ${lines.length - max} more lines`, { dim: true })];

  let rows;

  if(kind == 'plain') {
    const width = Math.max(10, Math.min(cols - 1, Math.max(...lines.map(stringWidth), stringWidth(title)) + 2));
    const inner = width - 2;
    const row = l => ' ' + pad(l, inner) + ' ';

    rows = ['', style(truncate(title, inner), { bold: true }), ...lines.flatMap(l => wrap(l, inner)), ''].map(row);
    background ??= 236;
  } else {
    const width = Math.max(10, Math.min(cols - 1, Math.max(...lines.map(stringWidth), stringWidth(title) + 4) + 4));

    rows = boxLines(lines, { title: style(title, { bold: true }), width, chars: BOX_ROUNDED, border: border ? { fg: border } : undefined });
  }

  if(background === undefined) return rows.join('\n');

  const on = `${CSI}${colorCode(background, true)}m`;

  return rows.map(l => on + l.replaceAll(RESET, RESET + on) + RESET).join('\n');
}

/* sideBySide(left, right, gap): two lists of lines next to each other
 *
 *   sideBySide(['ab', 'c'], ['1', '2', '3'], 2)   // ['ab  1', 'c   2', '    3']
 *
 * right starts gap cells after the widest left line, trailing spaces are cut. */
export function sideBySide(left, right, gap = 3) {
  const w = Math.max(0, ...left.map(stringWidth));

  return Array.from({ length: Math.max(left.length, right.length) }, (_, k) => pad(left[k] ?? '', w) + ' '.repeat(gap) + (right[k] ?? '')).map(l => l.replace(/ +$/, ''));
}

/* meter(fraction, width, opts): a progressBar colored by how full it is
 *
 *   meter(0.9, 10, { ok: '#a3e635', warn: '#fbbf24', bad: '#f87171' })
 *
 * opts: ok, warn, bad (colors, green yellow red), warnAt (0.6), badAt
 * (0.85), color (one fixed color, the thresholds are off). */
export function meter(fraction, width, { ok = 'green', warn = 'yellow', bad = 'red', warnAt = 0.6, badAt = 0.85, color } = {}) {
  const f = Math.max(0, Math.min(1, fraction || 0));
  const fg = color ?? (f >= badAt ? bad : f >= warnAt ? warn : ok);

  return progressBar(f, width, { style: { fg }, empty: '·', emptyStyle: { dim: true } });
}

/* gradientRule(cols, label, stops): a rule colored along a gradient
 *
 *   gradientRule(40, 'log', ['#6ec3f4', '#c084fc'])
 *
 * rule() with the label, then gradient() over it. */
export const gradientRule = (cols, label = '', stops) => gradient(rule(cols, { label }), stops);

/* shortPath(path, max): path with HOME as ~, cut at the left to max cells
 *
 *   shortPath('/home/u/src/a/b', 6)   // '…c/a/b' with HOME=/home/u
 *
 * the cut counts characters, not cells. */
export function shortPath(path, max = Infinity) {
  const home = getenv('HOME');
  const p = home && (path === home || path.startsWith(home + '/')) ? '~' + path.slice(home.length) : path;

  return p.length > max ? '…' + p.slice(p.length - max + 1) : p;
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
