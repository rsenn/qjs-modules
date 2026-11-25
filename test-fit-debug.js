import { fitTable, stringWidth, stripAnsi } from "./lib/terminal.js";

const mkBasic = () => [
  ['Model', 'Rate', 'Status', 'Probe', 'Says'],
  ['gemini-3.7-flash', '\x1b[32m 59\x1b[0m', '\x1b[32mREADY\x1b[0m', '\x1b[32m✔ usable\x1b[0m 4.3s no tools', 'I am Qwen, developed by Alibaba Cloud and a long identity sentence'],
  ['glm-5.3', '\x1b[33m 45\x1b[0m', '\x1b[32mREADY\x1b[0m', '\x1b[31m✗ All 2 routed attempts failed upstream\x1b[0m', ''],
];

const plain = s => s.replace(/\x1b\[[\d;]*m/g, '');
const widest = t => Math.max(...t.split('\n').map(l => [...plain(l)].length));

// Test with width 200
const t200 = fitTable(200, mkBasic, [4, 3, 0], [4, 3]);
console.log("width 200:");
console.log("typeof:", typeof t200);
if (typeof t200 === 'string') {
  console.log("length:", t200.length);
  const lines = t200.split('\n');
  console.log("num lines:", lines.length);
  lines.forEach((l, i) => {
    console.log("line", i, "plain:", plain(l), "length:", [...plain(l)].length);
  });
  console.log("widest:", widest(t200));
}

// Test with width 100
const t100 = fitTable(100, mkBasic, [4, 3, 0], [4, 3]);
console.log("\nwidth 100:");
console.log("typeof:", typeof t100);
if (typeof t100 === 'string') {
  const lines = t100.split('\n');
  console.log("num lines:", lines.length);
  lines.forEach((l, i) => {
    console.log("line", i, "plain:", plain(l), "length:", [...plain(l)].length);
  });
  console.log("widest:", widest(t100));
}
