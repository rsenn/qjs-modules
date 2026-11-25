import { fitTable, stringWidth, stripAnsi } from "./lib/terminal.js";
console.log("fitTable:", typeof fitTable);
console.log("stringWidth:", typeof stringWidth);
console.log("stripAnsi:", typeof stripAnsi);

// Test basic
const rows = [['A', 'B'], ['1', '2']];
const t = fitTable(100, rows, [], []);
console.log("Basic test - result:", t, "type:", typeof t);
