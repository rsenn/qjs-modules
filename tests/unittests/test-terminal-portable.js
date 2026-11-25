// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/terminal.js: the pure functions and the exports, the same on every runtime */
import { EAGAIN, decodeKeys, stringWidth, stripAnsi, truncate, ttyGetWinSize, ttyRead, ttySetRaw, ttyWrite, windowSize, wrap } from '../../lib/terminal.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'tty primitives are exported'() {
    for(const f of [ttyGetWinSize, ttyRead, ttySetRaw, ttyWrite]) eq(typeof f, 'function');
    eq(EAGAIN, 11);
  },

  'stripAnsi / stringWidth'() {
    eq(stripAnsi('\x1b[1mbold\x1b[0m'), 'bold');
    eq(stringWidth('abc'), 3);
    eq(stringWidth('\x1b[31m日本\x1b[0m'), 4);
  },

  'truncate / wrap'() {
    eq(truncate('hello world', 6), 'hello…');
    assert(wrap('aaa bbb ccc', 7).every(l => l.length <= 7));
  },

  'stringWidth counts cells, not code points'() {
    eq(stringWidth(''), 0);
    eq(stringWidth('e\u0301'), 1);
    eq(stringWidth('😀'), 2);
    eq(stringWidth('a\x1b]0;title\x07b'), 2);
    eq(stringWidth('a\x1b]8;;http://x\x1b\\b'), 2);
    eq(stringWidth('\x1b7a'), 1);
  },

  'stripAnsi cuts CSI, OSC and two byte sequences'() {
    eq(stripAnsi('plain'), 'plain');
    eq(stripAnsi('\x1b[1;4mx\x1b[m\x1b7y'), 'xy');
    eq(stripAnsi('a\x1b]0;t\x07b\x1b]8;;u\x1b\\c'), 'abc');
    eq(stripAnsi(12), '12');
  },

  'truncate keeps escapes, closes a style, honors the ellipsis'() {
    eq(truncate('hello', 5), 'hello');
    eq(truncate('hello world', 6, '..'), 'hell..');
    eq(truncate('日本語', 4), '日…');
    eq(truncate('\x1b[31mhello world\x1b[0m', 6), '\x1b[31mhello\x1b[0m…');
    eq(truncate('hello', 0), '');
    eq(truncate('hello', 1, '....'), '');
  },

  'wrap breaks after spaces and carries a style over the break'() {
    eq(JSON.stringify(wrap('one two three', 7)), '["one two","three"]');
    eq(JSON.stringify(wrap('a\n\nb', 5)), '["a","","b"]');
    eq(JSON.stringify(wrap('\x1b[31mred green\x1b[0m', 5)), JSON.stringify(['\x1b[31mred\x1b[0m', '\x1b[31mgreen\x1b[0m']));
    eq(JSON.stringify(wrap('abcdefgh', 3)), '["abc","def","gh"]');
  },

  'wrap puts a cell wider than the width on a line of its own'() {
    eq(JSON.stringify(wrap('日本x', 1)), '["日","本","x"]');
  },

  'decodeKeys'() {
    const { keys } = decodeKeys('a\x1b[A', true);
    eq(keys.length, 2);
    eq(keys[0].s, 'a');
    eq(keys[1].key.name, 'up');
  },

  'windowSize falls back to 80x24 or the real size'() {
    const [c, r] = windowSize(1);
    assert(c > 0 && r > 0);
  },
});
