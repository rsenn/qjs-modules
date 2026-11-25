import { LineEditor } from "../../lib/terminal.js";

// Test up/down navigation - simpler test
const ed = new LineEditor({ history: ['one', 'two', 'three'] });
print("Initial history: " + ed.history);

// Start search
ed.key({ name: "ctrlR", ctrl: false, meta: false, shift: false });
print("After ctrlR: searchIndex=" + ed.searchIndex + " searchMode=" + ed.searchMode);

// Type 'o' - should find 'one' first
ed.key({ name: "o", ctrl: false, meta: false, shift: false });
print("After 'o': searchIndex=" + ed.searchIndex + " cmd=" + ed.cmd);

// Press up - should wrap to last entry
const r1 = ed.key({ name: "up", ctrl: false, meta: false, shift: false });
print("After up: result=" + JSON.stringify(r1) + " searchIndex=" + ed.searchIndex + " cmd=" + ed.cmd);

// Press down - should go to 'two'
const r2 = ed.key({ name: "down", ctrl: false, meta: false, shift: false });
print("After down: result=" + JSON.stringify(r2) + " searchIndex=" + ed.searchIndex + " cmd=" + ed.cmd);