import { fitTable } from "./lib/terminal.js";

// Test 1: minimal rows
const rows1 = [['A', 'B'], ['1', '2']];
console.log("Test 1 - rows1:", rows1);
const t1 = fitTable(100, rows1, [], []);
console.log("Test 1 - result:", t1);

// Test 2: with header only
const rows2 = [['A', 'B', 'C']];
console.log("\nTest 2 - rows2:", rows2);
try {
  const t2 = fitTable(100, rows2, [], []);
  console.log("Test 2 - result:", t2);
} catch(e) {
  console.log("Test 2 - ERROR:", e.message);
}

// Test 3: empty-ish
const rows3 = [];
console.log("\nTest 3 - rows3:", rows3);
try {
  const t3 = fitTable(100, rows3, [], []);
  console.log("Test 3 - result:", t3);
} catch(e) {
  console.log("Test 3 - ERROR:", e.message);
}
