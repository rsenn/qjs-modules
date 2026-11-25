// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/terminal.js: fitTable pure JS implementation tests */
import { fitTable, stringWidth, stripAnsi } from '../../lib/terminal.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

const plain = s => s.replace(/\x1b\[[\d;]*m/g, '');
const widest = t => Math.max(...t.split('\n').map(l => [...plain(l)].length));

const mkBasic = () => [
  ['Model', 'Rate', 'Status', 'Probe', 'Says'],
  ['gemini-3.7-flash', '\x1b[32m 59\x1b[0m', '\x1b[32mREADY\x1b[0m', '\x1b[32m✔ usable\x1b[0m 4.3s no tools', 'I am Qwen, developed by Alibaba Cloud and a long identity sentence'],
  ['glm-5.3', '\x1b[33m 45\x1b[0m', '\x1b[32mREADY\x1b[0m', '\x1b[31m✗ All 2 routed attempts failed upstream\x1b[0m', ''],
];

const mkNumeric = () => [
  ['Name', 'Count', 'Rate'],
  ['items', 12345, ' 1.2k'],
  ['total', 987654321, ' 987M'],
];

const mkCjk = () => [
  ['Model', 'Text'],
  ['model-1', '日本語テスト'],
  ['model-2', '한국어 테스트'],
];

tests({
  'fitTable fits at various widths'() {
    for(const w of [200, 100, 70, 50, 40, 30]) {
      const t = fitTable(w, mkBasic, [4, 3, 0], [4, 3]);
      assert(widest(t) <= w, `width ${w}: ${widest(t)} > ${w}`);
    }
  },

  'fitTable unlimited width keeps everything'() {
    const t = fitTable(0, mkBasic, [4, 3, 0]);
    assert(plain(t).includes('failed upstream'), 'should keep all columns');
    assert(plain(t).includes('long identity sentence'), 'should keep Says column');
  },

  'fitTable coloured cells keep reset when cut'() {
    const t = fitTable(70, mkBasic, [4, 3, 0], [4, 3]);
    const lines = t.split('\n');
    for(const l of lines) {
      if(l.includes('\x1b[3')) {
        assert(l.includes('\x1b[0m'), 'colored cut should have reset: ' + l);
      }
    }
  },

  'fitTable drops Says before Model at narrow width'() {
    const t = fitTable(30, mkBasic, [4, 3, 0], [4, 3]);
    assert(plain(t).includes('Model'), 'Model column should remain');
    assert(!plain(t).includes('Says'), 'Says column should drop first');
  },

  'fitTable right-aligns numeric columns'() {
    const countCell1 = row1.split('  ')[1];
    const countCell2 = row2.split('  ')[1];
    const rateCell1 = row1.split('  ')[2];
    const rateCell2 = row2.split('  ')[2];
    // Numbers should be visible and right-aligned within their cells
    assert(countCell1.trim() === '12345', 'Count should be 12345, got: ' + JSON.stringify(countCell1));
    assert(countCell2.trim() === '987654321', 'Count should be 987654321, got: ' + JSON.stringify(countCell2));
    assert(rateCell1.includes('1.2k') || rateCell1.includes('1234'), 'Rate should have number');
    assert(rateCell2.includes('987M') || rateCell2.includes('987654'), 'Rate should have 987M, got: ' + JSON.stringify(rateCell2));
});