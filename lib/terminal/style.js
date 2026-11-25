/* terminal/style.js: colors and SGR styling of strings: style(), gradient(), parseColor().
 * depends on: terminal.js (stringWidth, cells), terminal/internal.js (CSI, RESET).
 * rule: a styled string always ends with a reset, so it can be placed anywhere. */

import { cells, stringWidth } from 'terminal';
import { CSI, RESET } from 'terminal/internal';

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
export function colorCode(c, bg) {
  if(c === '') return '';

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
    typeof fg == 'string' && fg !== '' ? colorCode(fg, false) : false,
    typeof bg == 'string' && bg !== '' ? colorCode(bg, true) : false,
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
