import { LineEditor } from "../../lib/terminal/lineeditor.js";

// Test up/down navigation
const ed = new LineEditor({ history: ['one', 'two', 'three', 'one'] });
print("Initial: historyPos=" + ed.historyPos + " historyIndex=" + ed.historyIndex);

// Start search
ed.key({ name: "ctrlR", ctrl: false, meta: false, shift: false });
print("After ctrlR: searchIndex=" + ed.searchIndex + " searchMode=" + ed.searchMode);

// Type 'o'
ed.key({ name: "o", ctrl: false, meta: false, shift: false });
print("After 'o': searchIndex=" + ed.searchIndex + " cmd=" + ed.cmd + " cursor=" + ed.cursor);

// Press up
const r1 = ed.key({ name: "up", ctrl: false, meta: false, shift: false });
print("After up: result=" + JSON.stringify(r1) + " searchIndex=" + ed.searchIndex + " cmd=" + ed.cmd + " cursor=" + ed.cursor);

// Press down
const r2 = ed.key({ name: "down", ctrl: false, meta: false, shift: false });
print("After down: result=" + JSON.stringify(r2) + " searchIndex=" + ed.searchIndex + " cmd=" + ed.cmd + " cursor=" + ed.cursor);