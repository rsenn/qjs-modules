// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
// Copy of test-terminal-fit.js simplified

import { fitTable, stringWidth, stripAnsi } from "./lib/terminal.js";
import { tests } from "./lib/tinytest.js";

const plain = s => s.replace(/\x1b\[[\d;]*m/g, '');
const mkBasic = () => [
  ['Model', 'Rate', 'Status', 'Probe', 'Says'],
  ['gemini-3.7-flash', '\x1b[32m 59\x1b[0m', '\x1b[32mREADY\x1b[0m', '\x1b[32m✔ usable\x1b[0m 4.3s no tools', 'I am Qwen, developed by Alibaba Cloud and a long identity sentence'],
  ['glm-5.3', '\x1b[33m 45\x1b[0m', '\x1b[32mREADY\x1b[0m', '\x1b[31m✗ All 2 routed attempts failed upstream\x1b[0m', ''],
];

const rows = mkBasic();

tests({
  'fitTable basic works'() {
    const t = fitTable(200, rows, [], []);
    console.log("fitTable result type:", typeof t);
    console.log("fitTable result:", t ? t.substring(0, 50) : "null");
    assert(typeof t === 'string', 'fitTable should return a string');
    assert(t.length > 0, 'fitTable should return non-empty result');
  },
});
