import { fitTable } from "./lib/terminal.js";

const rows = [
  ['A', 'B'],
  ['1', '2'],
  ['3', '4'],
];

const t = fitTable(100, rows, [], []);
console.log("Result:", t);
console.log("typeof t:", typeof t);
if (typeof t === 'string') {
  console.log("String length:", t.length);
}
