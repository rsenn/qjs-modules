import { fitTable } from "./lib/terminal.js";

const mkBasic = () => [
  ['Model', 'Rate', 'Status', 'Probe', 'Says'],
  ['gemini-3.7-flash', '\x1b[32m 59\x1b[0m', '\x1b[32mREADY\x1b[0m', '\x1b[32m✔ usable\x1b[0m 4.3s no tools', 'I am Qwen, developed by Alibaba Cloud and a long identity sentence'],
  ['glm-5.3', '\x1b[33m 45\x1b[0m', '\x1b[32mREADY\x1b[0m', '\x1b[31m✗ All 2 routed attempts failed upstream\x1b[0m', ''],
];

const rows = mkBasic();
console.log("rows[0]:", rows[0]);
console.log("rows[0].length:", rows[0].length);

const t = fitTable(200, rows, [4, 3, 0], [4, 3]);
console.log("Result type:", typeof t);
if (typeof t === 'string') {
  console.log("Result length:", t.length);
  const lines = t.split('\n');
  console.log("Number of lines:", lines.length);
  lines.forEach((l, i) => {
    console.log("Line", i, ":", l ? l.substring(0, 60) : "null");
  });
}
