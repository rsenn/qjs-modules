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
