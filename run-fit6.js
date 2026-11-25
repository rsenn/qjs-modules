import { fitTable } from "./lib/terminal.js";

const rows = [
  ['A', 'B', 'C'],
  ['1', '2', '3'],
];

const t = fitTable(100, rows, [], []);
console.log("Result:", t);
