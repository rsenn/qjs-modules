// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/terminal.js prevCluster() and nextCluster(): the character boundaries a cursor steps over.
 * On qjsm they are the native ones of textcode, elsewhere the JS twin: one table of expectations for both.
 * Node, bun and deno also compare with Intl.Segmenter. LineEditor uses them for Backspace, Delete and the arrows. */
import { nextCluster, prevCluster } from '../../lib/terminal.js';
import { LineEditor } from '../../lib/terminal/lineeditor.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* str cut into clusters by walking nextCluster from 0, and by walking prevCluster back from the end */
const forward = str => {
  const out = [];

  for(let i = 0, j; i < str.length; i = j) {
    j = nextCluster(str, i);
    assert(j > i, `no progress at ${i} in ${JSON.stringify(str)}`);
    out.push(str.slice(i, j));
  }

  return out;
};
const backward = str => {
  const out = [];

  for(let i = str.length, j; i > 0; i = j) {
    j = prevCluster(str, i);
    assert(j < i, `no progress at ${i} in ${JSON.stringify(str)}`);
    out.unshift(str.slice(j, i));
  }

  return out;
};

const FAMILY = '👨‍👩‍👧';
const FLAGS = '🇨🇭🇩🇪🇫';
const CASES = [
  ['', []],
  ['abc', ['a', 'b', 'c']],
  ['a😀b', ['a', '😀', 'b']],
  ['éx', ['é', 'x']],
  ['é̂̃', ['é̂̃']],
  ['a\r\nb', ['a', '\r\n', 'b']],
  ['a\n\nb', ['a', '\n', '\n', 'b']],
  [`a${FAMILY}b`, ['a', FAMILY, 'b']],
  ['👍🏽x', ['👍🏽', 'x']],
  ['❤️!', ['❤️', '!']],
  [FLAGS, ['🇨🇭', '🇩🇪', '🇫']],
  ['한', ['한']],
  ['한글', ['한', '글']],
  ['漢字', ['漢', '字']],
  ['a‌b', ['a‌', 'b']],
  ['กิ่', ['กิ่']],
];

tests({
  'nextCluster walks every case into its clusters'() {
    for(const [str, want] of CASES) eq(JSON.stringify(want), JSON.stringify(forward(str)));
  },

  'prevCluster walks every case back into the same clusters'() {
    for(const [str, want] of CASES) eq(JSON.stringify(want), JSON.stringify(backward(str)));
  },

  'the ends: nextCluster at the end and prevCluster at 0 stay'() {
    eq(0, prevCluster('abc', 0));
    eq(3, nextCluster('abc', 3));
    eq(0, prevCluster('', 0));
    eq(0, nextCluster('', 0));
  },

  'a position out of range is clamped'() {
    eq(3, nextCluster('abc', 99));
    eq(0, prevCluster('abc', -5));
    eq(2, prevCluster('abc', 99));
    eq(1, nextCluster('abc', -5));
  },

  'a position inside a cluster gives its start or its end'() {
    const s = 'a😀b';

    eq(1, prevCluster(s, 2));
    eq(3, nextCluster(s, 2));
    eq(1, prevCluster(`a${FAMILY}`, 4));
    eq(1 + FAMILY.length, nextCluster(`a${FAMILY}`, 4));
  },

  'the third regional indicator starts a new flag'() {
    eq(4, nextCluster(FLAGS, 0));
    eq(8, nextCluster(FLAGS, 4));
    eq(10, nextCluster(FLAGS, 8));
    eq(8, prevCluster(FLAGS, 10));
  },

  'an emoji after a ZWJ joins only when it follows an emoji'() {
    eq(['a‍', '😀'].join('|'), forward('a‍😀').join('|'));
    eq(1, forward('😀‍😀').length);
  },

  'a lone surrogate is one step'() {
    eq(1, nextCluster('\ud83dx', 0));
    eq(2, nextCluster('\ud83dx', 1));
    eq(1, prevCluster('x\ude00', 2));
  },

  'a non-string is converted'() {
    eq(1, nextCluster(12, 0));
    eq(2, nextCluster(12, 1));
  },

  'LineEditor: Backspace takes a character with its marks'() {
    const ed = new LineEditor();

    ed.set('aé');
    ed.key({ name: 'backspace' });
    eq('a', ed.cmd);
    eq(1, ed.cursor);
  },

  'LineEditor: Backspace takes a whole ZWJ family and a flag'() {
    const ed = new LineEditor();

    ed.set(`x${FAMILY}`);
    ed.key({ name: 'backspace' });
    eq('x', ed.cmd);
    ed.set('y🇨🇭');
    ed.key({ name: 'backspace' });
    eq('y', ed.cmd);
  },

  'LineEditor: Delete and the arrows step over a cluster'() {
    const ed = new LineEditor();

    ed.set(`${FAMILY}b`);
    ed.cursor = 0;
    ed.key({ name: 'right' });
    eq(FAMILY.length, ed.cursor);
    ed.key({ name: 'left' });
    eq(0, ed.cursor);
    ed.key({ name: 'delete' });
    eq('b', ed.cmd);
  },

  'LineEditor: an accent typed after a letter stays with it'() {
    const ed = new LineEditor();

    ed.set('');
    ed.key({ name: 'e' });
    ed.insertText('́');
    ed.key({ name: 'left' });
    eq(0, ed.cursor);
  },

  'Intl.Segmenter agrees on the cases (node, bun, deno)'() {
    if(typeof Intl?.Segmenter != 'function') return;

    const seg = new Intl.Segmenter('en', { granularity: 'grapheme' });

    for(const [str] of CASES) eq(JSON.stringify([...seg.segment(str)].map(x => x.segment)), JSON.stringify(forward(str)));
  },
});
