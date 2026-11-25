import { fitTable } from "./lib/terminal.js";

const rows = [
  ['A', 'B'],
  ['1', '2'],
];

const t = fitTable(100, rows, [], []);
console.log("Result:", t);
