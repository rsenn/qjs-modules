import { fitTable } from "./lib/terminal.js";
console.log("fitTable imported successfully, type:", typeof fitTable);

// Test basic
const rows = [['A', 'B'], ['1', '2']];
const t = fitTable(100, rows, [], []);
console.log("Test result:", t, "type:", typeof t);
