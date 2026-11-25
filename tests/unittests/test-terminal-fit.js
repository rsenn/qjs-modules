// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/terminal.js: fitTable, the table fitter (native in textcode, pure JS elsewhere) */
import { fitTable, stringWidth } from '../../lib/terminal.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

const plain = s => s.replace(/\x1b\[[0-9;]*[A-Za-z]/g, '');
const widest = t => Math.max(...t.split('\n').map(l => stringWidth(l)));

const mk = () => [
  ['Model', 'Rate', 'Status', 'Probe', 'Says'],
  ['gemini-3.7-flash', '\x1b[32m 59\x1b[0m', '\x1b[32mREADY\x1b[0m', '\x1b[32m✔ usable\x1b[0m 4.3s no tools', 'I am Qwen, developed by Alibaba Cloud and a long identity sentence'],
  ['glm-5.3', '\x1b[33m 45\x1b[0m', '\x1b[32mREADY\x1b[0m', '\x1b[31m✗ All 2 routed attempts failed upstream\x1b[0m', ''],
];

tests({
  'fits 200, 100, 70, 50, 40 and 30 cells'() {
    for(const w of [200, 100, 70, 50, 40, 30]) {
      const t = fitTable(w, mk, [4, 3, 0], [4, 3]);

      assert(widest(t) <= w, `width ${w}: ${widest(t)}`);
    }
  },

  'width 0 keeps everything'() {
    const t = plain(fitTable(0, mk, [4, 3, 0]));

    assert(t.includes('failed upstream'), 'Probe column cut');
    assert(t.includes('long identity sentence'), 'Says column cut');
  },

  'a coloured cell keeps its reset when cut'() {
    const t = fitTable(70, mk, [4, 3, 0], [4, 3]);

    assert(t.includes('…'), 'nothing was cut');
    assert(t.split('\n').every(l => !l.includes('\x1b[3') || l.includes('\x1b[0m')), 'reset lost');
  },

  'at 30 the Says column goes before Model'() {
    const t = fitTable(30, mk, [4, 3, 0], [4, 3]);

    assert(t.includes('Model'), 'Model dropped');
    assert(!t.includes('Says'), 'Says kept');
  },

  'make is called with the bar widths 6, 3, 0 until the table fits'() {
    const bars = [];

    fitTable(20, bar => (bars.push(bar), [['a', 'meter'], ['x', '#'.repeat(bar) + 'xxxxxxxxxxxxxxxxx']]));
    assert(bars.join() == '6,3,0', bars.join());
  },

  'make runs once for each bar, also when columns are dropped'() {
    const bars = [];
    const make = bar => (bars.push(bar), [['a', 'b', 'c'], ['x'.repeat(30), 'y'.repeat(30), 'z'.repeat(30)]]);
    const text = fitTable(20, make, [0], [2, 1]);

    eq('6,3,0', bars.join());
    assert(stringWidth(text.split('\n')[1]) <= 20, text);
    eq('a', text.split('\n')[0]);
  },

  'a table made from the same make result is not changed by the fitting'() {
    const table = [['a', 'b'], ['x'.repeat(30), 'y'], ['k', 'v']];
    const before = JSON.stringify(table);

    fitTable(20, () => table, [0], [1]);
    eq(before, JSON.stringify(table));
  },

  'flex may be one number'() {
    const rows = () => [['id', 'text'], ['1', 'a very long line of text indeed']];

    assert(fitTable(20, rows, 1) == fitTable(20, rows, [1]), 'number and array differ');
    assert(widest(fitTable(20, rows, 1)) <= 20, 'too wide');
  },

  'a CJK cell counts 2 cells'() {
    const rows = () => [['id', 'text'], ['1', '日本語テスト日本語テスト']];
    const t = fitTable(12, rows, [1]);

    assert(stringWidth('日本') == 4, 'stringWidth');
    assert(widest(t) <= 12, `width ${widest(t)}: ${t}`);
    assert(t.includes('…'), 'not cut');
  },

  'a numeric column lines up on the right'() {
    const t = fitTable(0, () => [['Name', 'Count', 'Text'], ['items', '12345', 'a'], ['total', '7', 'b']], [], []);

    assert(t == 'Name   Count  Text\nitems  12345  a\ntotal      7  b', JSON.stringify(t));
  },
});
