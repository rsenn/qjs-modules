import { fitTable } from "./lib/terminal.js";

const mkBasic = () => [
  ['Model', 'Rate', 'Status', 'Probe', 'Says'],
  ['gemini-3.7-flash', '\x1b[32m 59\x1b[0m', '\x1b[32mREADY\x1b[0m', '\x1b[32m✔ usable\x1b[0m 4.3s no tools', 'I am Qwen, developed by Alibaba Cloud and a long identity sentence'],
  ['glm-5.3', '\x1b[33m 45\x1b[0m', '\x1b[32mREADY\x1b[0m', '\x1b[31m✗ All 2 routed attempts failed upstream\x1b[0m', ''],
];

const rows = mkBasic();

// Test various widths
for(const w of [200, 100, 70, 50, 40, 30, 0]) {
  try {
    const t = fitTable(w, rows, [4, 3, 0], [4, 3]);
    console.log("width", w, "-> typeof:", typeof t, "length:", t ? t.length : "null");
    if (typeof t === 'string' && t.includes('\n')) {
      const lines = t.split('\n');
      console.log("  lines:", lines.length);
    }
  } catch(e) {
    console.log("width", w, "-> ERROR:", e.message);
  }
}
